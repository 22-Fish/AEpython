/*
 * launcher.c - AEpy 启动器（无控制台）
 *
 * 职责：
 *   1. 以自身所在目录为项目根目录，优先使用项目自带虚拟环境里的解释器
 *      （.venv\Scripts\pythonw.exe，其次 venv、env），找不到再回退到 PATH 上的 Python；
 *   2. 以分离（detached）方式启动 src\main.pyw：不继承句柄、不与本进程共享控制台、
 *      也不受本进程退出的影响；
 *   3. 启动成功后立即关闭句柄并退出，不等待被启动的进程结束。
 *
 * 编译方式见 build_launcher.bat：
 *   windres launcher.rc -O coff -o launcher_res.o
 *   gcc -O2 -s -mwindows -o AEpy.exe launcher.c launcher_res.o
 * 其中 -mwindows 指定 Windows 图形子系统，所以运行时不会弹出控制台窗口。
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <wchar.h>

/* 路径缓冲区容量（支持长路径） */
#define PATH_CAP 32768

/* 入口脚本相对于项目根目录的位置 */
static const wchar_t *ENTRY_SCRIPT = L"src\\main.pyw";

/* 候选解释器：虚拟环境内的 pythonw.exe 优先，其次虚拟环境内的 python.exe */
static const wchar_t *VENV_CANDIDATES[] = {
    L".venv\\Scripts\\pythonw.exe",
    L"venv\\Scripts\\pythonw.exe",
    L"env\\Scripts\\pythonw.exe",
    L".venv\\Scripts\\python.exe",
    L"venv\\Scripts\\python.exe",
    L"env\\Scripts\\python.exe",
};

/* 取启动器自身所在目录（结尾不带反斜杠），失败返回 FALSE */
static BOOL GetAppDir(wchar_t *buf, DWORD cch)
{
    DWORD len = GetModuleFileNameW(NULL, buf, cch);
    wchar_t *slash;

    if (len == 0 || len >= cch) {
        return FALSE;
    }
    slash = wcsrchr(buf, L'\\');
    if (slash == NULL) {
        return FALSE;
    }
    *slash = L'\0';
    return TRUE;
}

/* 文件是否存在（目录不算） */
static BOOL FileExistsW(const wchar_t *path)
{
    DWORD attr = GetFileAttributesW(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

/* 拼接“目录 + 相对路径”，结果始终以 NUL 结尾 */
static void JoinPath(wchar_t *out, size_t cch, const wchar_t *dir, const wchar_t *rel)
{
    size_t used;

    out[0] = L'\0';
    wcsncat(out, dir, cch - 1);
    used = wcslen(out);
    if (used + 1 < cch) {
        out[used++] = L'\\';
        out[used] = L'\0';
    }
    if (used < cch) {
        wcsncat(out, rel, cch - used - 1);
    }
}

/* 在 PATH 中查找可执行文件 */
static BOOL SearchOnPath(const wchar_t *name, wchar_t *out, DWORD cch)
{
    return SearchPathW(NULL, name, NULL, cch, out, NULL) != 0;
}

/* 取命令行里“程序名之后”的参数部分，保持原始写法直接转发给脚本 */
static const wchar_t *ArgsAfterProgram(void)
{
    const wchar_t *p = GetCommandLineW();

    if (*p == L'"') {
        p++;
        while (*p != L'\0' && *p != L'"') {
            p++;
        }
        if (*p == L'"') {
            p++;
        }
    } else {
        while (*p != L'\0' && *p != L' ' && *p != L'\t') {
            p++;
        }
    }
    while (*p == L' ' || *p == L'\t') {
        p++;
    }
    return p;
}

/* 弹出错误提示（启动器是图形程序，没有控制台可以打印） */
static void ShowError(const wchar_t *text)
{
    MessageBoxW(NULL, text, L"AEpy", MB_OK | MB_ICONERROR);
}

/*
 * 解析要使用的解释器。
 * 返回 TRUE 表示找到；is_pythonw 指出是否为 pythonw.exe（图形子系统，不需要控制台）。
 */
static BOOL ResolveInterpreter(const wchar_t *app_dir, wchar_t *python, DWORD cch,
                               BOOL *is_pythonw)
{
    wchar_t candidate[PATH_CAP];
    int i;
    int total = (int)(sizeof(VENV_CANDIDATES) / sizeof(VENV_CANDIDATES[0]));

    for (i = 0; i < total; i++) {
        JoinPath(candidate, PATH_CAP, app_dir, VENV_CANDIDATES[i]);
        if (FileExistsW(candidate)) {
            wcsncpy(python, candidate, cch - 1);
            python[cch - 1] = L'\0';
            *is_pythonw = (wcsstr(VENV_CANDIDATES[i], L"pythonw.exe") != NULL);
            return TRUE;
        }
    }

    /* 项目里没有虚拟环境：退回使用 PATH 上的 Python */
    if (SearchOnPath(L"pythonw.exe", python, cch)) {
        *is_pythonw = TRUE;
        return TRUE;
    }
    if (SearchOnPath(L"python.exe", python, cch)) {
        *is_pythonw = FALSE;
        return TRUE;
    }
    return FALSE;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    wchar_t app_dir[PATH_CAP];
    wchar_t script[PATH_CAP];
    wchar_t python[PATH_CAP];
    wchar_t cmdline[PATH_CAP];
    wchar_t message[PATH_CAP];
    const wchar_t *extra_args;
    BOOL is_pythonw = FALSE;
    DWORD flags;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    (void)hInstance;
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    if (!GetAppDir(app_dir, PATH_CAP)) {
        ShowError(L"无法确定 AEpy 启动器所在目录。");
        return 1;
    }

    JoinPath(script, PATH_CAP, app_dir, ENTRY_SCRIPT);
    if (!FileExistsW(script)) {
        _snwprintf(message, PATH_CAP,
                   L"找不到程序入口：\n%s\n\n"
                   L"请将 AEpy.exe 放在项目根目录（与 src 文件夹同级）。",
                   script);
        ShowError(message);
        return 1;
    }

    if (!ResolveInterpreter(app_dir, python, PATH_CAP, &is_pythonw)) {
        _snwprintf(message, PATH_CAP,
                   L"未找到可用的 Python 解释器。\n\n"
                   L"请先运行 setup_venv.bat 创建虚拟环境（%s\\.venv），"
                   L"或安装 Python 并加入 PATH。",
                   app_dir);
        ShowError(message);
        return 1;
    }

    /* 组装命令行："解释器" "入口脚本" <透传参数> */
    extra_args = ArgsAfterProgram();
    if (extra_args[0] != L'\0') {
        _snwprintf(cmdline, PATH_CAP, L"\"%ls\" \"%ls\" %ls", python, script, extra_args);
    } else {
        _snwprintf(cmdline, PATH_CAP, L"\"%ls\" \"%ls\"", python, script);
    }
    cmdline[PATH_CAP - 1] = L'\0';

    /*
     * 分离式启动：
     *   - pythonw.exe（图形子系统）：用 DETACHED_PROCESS，让它完全不依附任何控制台；
     *   - python.exe（控制台子系统）：用 CREATE_NO_WINDOW 给它一个隐藏控制台，
     *     因为 DETACHED_PROCESS 下控制台子系统的标准流不可用，Python 会报错。
     * CREATE_NEW_PROCESS_GROUP：独立进程组，不受本进程退出影响。
     * 注意 DETACHED_PROCESS 与 CREATE_NO_WINDOW 同时使用会有一个被忽略，故二选一。
     */
    flags = CREATE_NEW_PROCESS_GROUP;
    if (is_pythonw) {
        flags |= DETACHED_PROCESS;
    } else {
        flags |= CREATE_NO_WINDOW;
    }

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    /* bInheritHandles = FALSE：不把启动器的句柄交给子进程，确保彻底分离 */
    if (!CreateProcessW(python, cmdline, NULL, NULL, FALSE, flags, NULL, app_dir, &si, &pi)) {
        _snwprintf(message, PATH_CAP,
                   L"启动失败（错误码 %lu）。\n\n解释器：%s\n脚本：%s",
                   (unsigned long)GetLastError(), python, script);
        ShowError(message);
        return 1;
    }

    /* 已经成功启动，关闭句柄后立即退出，不等待子进程结束 */
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
