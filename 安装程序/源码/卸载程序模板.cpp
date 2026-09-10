/* ============================================================================
 *  卸载程序模板.cpp —— 卸载程序（安装时由安装包释放到安装目录）
 *
 *  它由「构建安装包.exe」单独编译成一个很小的 exe，再作为资源塞进安装包里；
 *  安装时被写到「安装目录\卸载<应用名>.exe」，同时在
 *  HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\<安装包名>
 *  注册卸载入口（控制面板 / 设置 → 应用 里可以看到）。
 *
 *  做四件事：
 *      1. 删除桌面快捷方式；
 *      2. 删除文件夹 / .py 右键菜单注册项；
 *      3. 删除安装目录里的所有文件（安装目录本身若为空则一起删除）；
 *      4. 删除注册表里的卸载入口。
 *
 *  命令行：
 *      --silent    静默卸载，不弹任何确认框
 *      --elevated  内部使用：表示已经提权，避免重复提权
 * ==========================================================================*/
#include "安装包公共.h"

#include <shlobj.h>

#include <string>
#include <vector>

using namespace aepy;

namespace {

Meta G_meta;
std::wstring G_selfPath;
std::wstring G_installDir;
bool G_silent = false;
bool g_consoleAttached = false;
std::wstring G_title;

void LogLine(const std::wstring& s)
{
    if (!G_silent) return;
    if (!g_consoleAttached) { AttachConsole(ATTACH_PARENT_PROCESS); g_consoleAttached = true; }
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        std::wstring line = s + L"\r\n";
        WriteConsoleW(h, line.c_str(), (DWORD)line.size(), &written, NULL);
    }
}

bool NeedsElevation(const std::wstring& dir)
{
    if (IsUnderOrEqualW(dir, ProgramFilesDirW(false))) return true;
    if (IsUnderOrEqualW(dir, ProgramFilesDirW(true)))  return true;
    wchar_t win[MAX_PATH + 2] = {0};
    if (GetWindowsDirectoryW(win, MAX_PATH) > 0 && IsUnderOrEqualW(dir, win)) return true;
    return !CanWriteInDirW(dir);
}

void RemoveDesktopShortcut()
{
    wchar_t desktop[MAX_PATH + 2] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) return;
    std::wstring lnk = PathJoinW(desktop, G_meta.shortcutName());
    if (FileExistsW(lnk)) {
        SetFileAttributesW(lnk.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(lnk.c_str())) LogLine(L"已删除桌面快捷方式。");
    }
}

void RemoveMenus()
{
    RegRemoveTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\OpenWithAEpy");
    RegRemoveTreeW(HKEY_CURRENT_USER,
                   L"Software\\Classes\\SystemFileAssociations\\.py\\shell\\EditWithAEpy");
    LogLine(L"已删除右键菜单注册项。");
}

void RemoveUninstallEntry()
{
    RegRemoveTreeW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + G_meta.packageName);
    LogLine(L"已删除卸载入口。");
}

/* 删除目录里的所有内容，跳过正在运行的自己 */
void DeleteContentsExceptSelf(const std::wstring& dir, int& files, int& dirs)
{
    std::wstring pattern = PathJoinW(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        std::wstring child = PathJoinW(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            DeleteContentsExceptSelf(child, files, dirs);
            if (RemoveDirectoryW(child.c_str())) ++dirs;
        } else {
            if (EqualsIgnoreCaseW(child, G_selfPath)) continue;
            SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
            if (DeleteFileW(child.c_str())) ++files;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* 顺手清理以前卸载时留在 %TEMP% 的自身副本（旧版本/被杀掉的进程留下的） */
void CleanupOldSelfCopies()
{
    std::wstring tempDir = TempDirW();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(PathJoinW(tempDir, L"AEpyUninstall_*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring p = PathJoinW(tempDir, fd.cFileName);
        if (EqualsIgnoreCaseW(p, G_selfPath)) continue;
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(p.c_str());          /* 还开着的删不掉，下次卸载再试 */
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* FileDispositionInfoEx 只有在 NTDDI_VERSION >= 0x0A000002 时才会出现在头文件里，
 * 这里直接按它的枚举值使用（旧系统调用会失败，随后会自动退回 FileDispositionInfo）。 */
const FILE_INFO_BY_HANDLE_CLASS kFileDispositionInfoEx = (FILE_INFO_BY_HANDLE_CLASS)20;

/* 卸载程序要删掉整个安装目录，就得先让自己离开那里：
 *   1. 把正在运行的自已改名搬到 %TEMP%（运行中的 exe 允许改名）；
 *   2. 用 FILE_DISPOSITION_INFO 把它标记成“最后一个句柄关闭时删除”，
 *      进程一退出这个临时 exe 就自动消失。
 * 全程只用 Win32 API，不启动任何辅助进程（不会弹控制台窗口，也不会去 ping 任何地址）。 */
bool MoveSelfAsideAndMarkDelete(const std::wstring& selfPath, std::wstring& movedTo)
{
    std::wstring tempSelf = PathJoinW(TempDirW(),
                                      FormatW(L"AEpyUninstall_%lu.exe", (unsigned long)GetCurrentProcessId()));
    if (!MoveFileExW(selfPath.c_str(), tempSelf.c_str(), MOVEFILE_REPLACE_EXISTING))
        return false;
    movedTo = tempSelf;

    HANDLE h = CreateFileW(tempSelf.c_str(), DELETE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    /* 首选 Win10 1903+ 的写法：允许删除“正在作为映像运行”的 exe，文件名立刻消失；
     * 老系统退回“句柄关闭后删除”。两种都不需要外部进程。 */
    FILE_DISPOSITION_INFO_EX infoEx;
    ZeroMemory(&infoEx, sizeof(infoEx));
    infoEx.Flags = FILE_DISPOSITION_FLAG_DELETE |
                   FILE_DISPOSITION_FLAG_POSIX_SEMANTICS |
                   FILE_DISPOSITION_FLAG_FORCE_IMAGE_SECTION_CHECK;
    BOOL ok = SetFileInformationByHandle(h, kFileDispositionInfoEx, &infoEx, sizeof(infoEx));
    if (!ok) {
        FILE_DISPOSITION_INFO info;
        ZeroMemory(&info, sizeof(info));
        info.DeleteFile = TRUE;
        ok = SetFileInformationByHandle(h, FileDispositionInfo, &info, sizeof(info));
    }
    CloseHandle(h);
    if (!ok) {
        /* 再兜底一次：让系统在重启后清理（需要管理员权限，失败也无所谓） */
        MoveFileExW(tempSelf.c_str(), NULL, MOVEFILE_DELAY_UNTIL_REBOOT);
    }
    return ok != FALSE;
}

} /* anonymous namespace */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrev; (void)lpCmdLine; (void)nCmdShow;

    std::vector<std::wstring> args = CommandLineArgsW();
    G_silent = HasArgW(args, L"--silent");
    bool elevated = HasArgW(args, L"--elevated");

    const void* data = NULL;
    size_t size = 0;
    if (LoadResourceBytes(hInst, AEPY_RES_META, &data, &size))
        G_meta = ParseMetaText((const char*)data, size);
    G_title = G_meta.packageName + L" 卸载";

    G_selfPath   = ModulePathW();
    G_installDir = TrimTrailingSepsW(PathDirW(G_selfPath));

    /* 安装到 Program Files 时卸载同样需要管理员权限 */
    if (!elevated && NeedsElevation(G_installDir) && !IsProcessElevated()) {
        std::wstring params = G_silent ? L"--elevated --silent" : L"--elevated";
        if (!RelaunchElevatedW(G_selfPath, params, true)) {
            LogLine(L"提权失败（卸载需要管理员权限）。");
            if (!G_silent)
                MessageBoxW(NULL, L"卸载该程序需要管理员权限，提权被取消或失败。",
                            G_title.c_str(), MB_OK | MB_ICONERROR);
            return 3;
        }
        return 0;
    }

    if (!G_silent) {
        std::wstring msg = L"确定要卸载 " + G_meta.appName + L" 吗？\n\n"
                           L"安装位置：" + G_installDir + L"\n"
                           L"（桌面快捷方式、右键菜单和安装目录里的文件都会被删除）";
        if (MessageBoxW(NULL, msg.c_str(), G_title.c_str(),
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
            return 0;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    RemoveDesktopShortcut();
    RemoveMenus();
    RemoveUninstallEntry();
    CleanupOldSelfCopies();

    int files = 0, dirs = 0;
    DeleteContentsExceptSelf(G_installDir, files, dirs);
    LogLine(FormatW(L"已删除 %d 个文件、%d 个子目录。", files, dirs));

    std::wstring movedTo;
    bool selfRemoved = MoveSelfAsideAndMarkDelete(G_selfPath, movedTo);
    bool dirRemoved  = RemoveDirectoryW(G_installDir.c_str()) != 0;
    bool leftover    = !dirRemoved && !IsDirEmptyW(G_installDir);
    if (!selfRemoved)
        LogLine(L"提示：卸载程序自身无法删除（它会在下次重启后被系统清理）。");
    if (!dirRemoved) {
        LogLine(leftover ? L"提示：安装目录里还有非安装程序写入的文件，目录已保留。"
                         : L"提示：安装目录未能删除。");
    }

    if (!G_silent) {
        std::wstring msg = L"卸载完成。";
        if (leftover)
            msg += L"\n\n注意：安装目录里还有不是安装程序写入的文件，只删掉了本程序的文件，"
                   L"目录已保留：\n" + G_installDir;
        else if (!dirRemoved)
            msg += L"\n\n注意：安装目录未能删除（可能仍有文件被占用）：\n" + G_installDir;
        MessageBoxW(NULL, msg.c_str(), G_title.c_str(), MB_OK | MB_ICONINFORMATION);
    }

    CoUninitialize();
    return 0;
}
