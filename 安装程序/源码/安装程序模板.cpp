/* ============================================================================
 *  安装程序模板.cpp —— AEpy 安装包（向导式安装程序）
 *
 *  这个文件本身不能直接编译，它由「构建安装包.exe」在构建时：
 *      1. 复制到临时目录（编译工具链不能处理中文路径）；
 *      2. 一起生成 .rc 资源脚本，把要安装的文件（ZIP）、用户协议、安装信息、
 *         卸载程序、图标、清单、版本信息作为资源编译进来；
 *      3. 用 g++ 编译成 <安装包名>.exe。
 *
 *  界面对应三步：
 *      第 1 步  选择安装位置（可浏览）+ 三个可勾选选项
 *      第 2 步  显示用户协议（可直接下一步）
 *      第 3 步  安装（需要管理员权限时自动提权）；完成后按钮变“完成”
 *
 *  命令行（供提权自身调用 / 静默安装使用）：
 *      --elevated            已提权启动：直接进入第 3 步并自动开始安装
 *      --silent              静默安装（无界面），配合下面的参数使用
 *      --dir <路径>          安装位置
 *      --shortcut 0|1        是否创建桌面快捷方式
 *      --folder 0|1          是否关联文件夹右键菜单
 *      --py 0|1              是否关联 .py 右键菜单
 *
 *  改动界面文字：见下面「界面文本」一节，全部集中在那里。
 * ==========================================================================*/
#include "安装包公共.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace aepy;

/* ============================== 界面文本（可自行修改） ==================== */
namespace Txt {

const wchar_t* const kTitleSuffix     = L" 安装程序";
const wchar_t* const kStep1Title      = L"第 1 步，共 3 步：选择安装位置";
const wchar_t* const kStep2Title      = L"第 2 步，共 3 步：阅读用户协议";
const wchar_t* const kStep3Title      = L"第 3 步，共 3 步：安装";
const wchar_t* const kStep3BusyTitle  = L"第 3 步，共 3 步：正在安装…";
const wchar_t* const kStep3DoneTitle  = L"第 3 步，共 3 步：安装完成";

const wchar_t* const kBtnCancel       = L"取消安装";
const wchar_t* const kBtnBack         = L"上一步";
const wchar_t* const kBtnNext         = L"下一步";
const wchar_t* const kBtnInstall      = L"安装";
const wchar_t* const kBtnFinish       = L"完成";

const wchar_t* const kCancelConfirm   = L"确定要取消安装吗？";
const wchar_t* const kCancelBusy      = L"安装正在进行，确定要取消吗？\n\n"
                                        L"已经写入的文件会被清理干净。";
const wchar_t* const kCancelledMsg    = L"安装已取消，残留文件已清理。";
const wchar_t* const kFinishMsg       = L"安装完成。点击“完成”结束安装程序。";
const wchar_t* const kElevateFailed   = L"安装到该位置需要管理员权限，提权被取消或失败。\n\n"
                                        L"可以改用其他目录（例如“桌面”或 D 盘），或重新安装并同意提权。";

const wchar_t* const kLabelSelectDir  = L"请选择安装位置（安装程序会在该目录下创建“%ls”文件夹）：";
const wchar_t* const kLabelFinalDir   = L"将安装到：%ls";
const wchar_t* const kBtnBrowse       = L"浏览…";
const wchar_t* const kLabelOptions    = L"附加选项：";
const wchar_t* const kOptShortcut     = L"创建桌面快捷方式";
const wchar_t* const kOptFolderMenu   = L"关联文件夹右键菜单（“使用 %ls 作为项目打开”）";
const wchar_t* const kOptPyMenu       = L"关联 .py 右键菜单（“使用 %ls 编辑”）";
const wchar_t* const kDirHint         = L"提示：安装到“C:\\Program Files”等系统目录需要管理员权限，"
                                        L"点击“安装”后会自动提权，并给安装目录授予普通用户可写权限"
                                        L"（程序需要在自己的目录里保存配置）。";

const wchar_t* const kLicenseIntro    = L"请阅读下面的用户协议，如无异议请点击“下一步”继续。";
const wchar_t* const kLicenseEmpty    = L"（暂无用户协议）";

const wchar_t* const kInstallAt       = L"安装位置：%ls";
const wchar_t* const kInstallOpts     = L"附加选项：%ls";
const wchar_t* const kOptShortName    = L"创建桌面快捷方式";
const wchar_t* const kOptFolderName   = L"文件夹右键菜单";
const wchar_t* const kOptPyName       = L".py 右键菜单";
const wchar_t* const kOptNone         = L"（未选择附加选项）";
const wchar_t* const kLabelProgress   = L"安装进度：";
const wchar_t* const kStatusReady     = L"点击“安装”开始安装。";
const wchar_t* const kStatusExtract   = L"正在解压：%ls";
const wchar_t* const kStatusShortcut  = L"正在创建桌面快捷方式…";
const wchar_t* const kStatusMenus     = L"正在注册右键菜单…";
const wchar_t* const kStatusFinal     = L"正在写入卸载信息…";
const wchar_t* const kStatusPerms     = L"正在设置安装目录权限…";
const wchar_t* const kRunCheck        = L"运行 %ls";

/* 右键菜单里的文字 */
const wchar_t* const kMenuFolderLabel = L"使用 %ls 作为项目打开";
const wchar_t* const kMenuFileLabel   = L"使用 %ls 编辑";

const wchar_t* const kErrNoDir        = L"请先选择安装位置。";
const wchar_t* const kErrWinDir       = L"不能把 Windows 系统目录作为安装位置。";
const wchar_t* const kErrFailPrefix   = L"安装失败：\n\n";

const wchar_t* const kErrNoPayload    = L"安装包内没有可用的文件数据（payload 缺失）。";
const wchar_t* const kErrNoMeta       = L"安装包资源不完整（缺少安装信息）。";

} /* namespace Txt */

/* ============================== 控件 ID ================================== */
enum ControlId {
    IDC_HINT          = 2000,
    IDC_PATH_EDIT     = 2001,
    IDC_BROWSE        = 2002,
    IDC_CHK_SHORTCUT  = 2003,
    IDC_CHK_FOLDER    = 2004,
    IDC_CHK_PY        = 2005,
    IDC_LICENSE       = 2006,
    IDC_PROGRESS      = 2007,
    IDC_STATUS        = 2008,
    IDC_RUN           = 2009,
    IDC_FINALDIR      = 2010,
    IDB_CANCEL        = 3001,
    IDB_BACK          = 3002,
    IDB_NEXT          = 3003,
    WM_AEPY_AUTOSTART = WM_APP + 1,
};

/* ============================== 全局状态 ================================= */
namespace {

struct Rect { int x, y, w, h; };

struct App {
    HINSTANCE      inst          = NULL;
    HWND           hwnd          = NULL;
    HFONT          fontText      = NULL;
    HFONT          fontBold      = NULL;
    HFONT          fontTitle     = NULL;
    HFONT          fontSmall     = NULL;
    int            dpi           = 96;
    int            clientW       = 0;
    int            clientH       = 0;

    HWND           hTitle        = NULL;
    HWND           hSep          = NULL;
    HWND           hBtnCancel    = NULL;
    HWND           hBtnBack      = NULL;
    HWND           hBtnNext      = NULL;
    std::vector<HWND> body;

    HWND           hProgress     = NULL;
    HWND           hStatus       = NULL;
    HWND           hRunCheck     = NULL;
    HWND           hPathEdit     = NULL;
    HWND           hFinalLabel   = NULL;

    int            step          = 0;
    bool           installing    = false;
    bool           finished      = false;
    bool           cancelRequested = false;
    bool           elevatedMode  = false;
    bool           stepLocked    = false;   /* 提权启动时不允许“上一步” */
    bool           silent        = false;

    std::wstring   parentDir;      /* 界面上选择的目录（父目录） */
    std::wstring   installDir;     /* 实际安装目录 = 父目录\安装目录名 */
    bool           optShortcut   = true;
    bool           optFolderMenu = true;
    bool           optPyMenu     = true;

    Meta           meta;
    std::wstring   agreement;
    const char*    payload       = NULL;
    size_t         payloadSize   = 0;
    const char*    uninstaller   = NULL;
    size_t         uninstallerSize = 0;
    std::wstring   exePath;

    /* 安装过程记录，用于取消时回滚 */
    std::vector<std::wstring> createdFiles;
    std::vector<std::wstring> createdDirs;
    bool           shortcutCreated = false;
    bool           folderMenuCreated = false;
    bool           pyMenuCreated = false;
    bool           uninstallEntryCreated = false;
    std::wstring   shortcutPath;
};

App G;
bool g_consoleAttached = false;

} /* anonymous namespace */

/* ============================== 小工具 =================================== */

static int S(int v) { return MulDiv(v, G.dpi, 96); }

static std::wstring GetWindowTextStr(HWND h)
{
    if (!h) return std::wstring();
    int len = GetWindowTextLengthW(h);
    std::wstring s((size_t)len + 1, L'\0');
    int got = GetWindowTextW(h, &s[0], len + 1);
    s.resize((size_t)(got < 0 ? 0 : got));
    return s;
}

static void LogLine(const std::wstring& s)
{
    if (!G.silent) return;
    if (!g_consoleAttached) { AttachConsole(ATTACH_PARENT_PROCESS); g_consoleAttached = true; }
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        std::wstring line = s + L"\r\n";
        WriteConsoleW(h, line.c_str(), (DWORD)line.size(), &written, NULL);
    }
}

static std::wstring SelfPath()
{
    if (!G.exePath.empty()) return G.exePath;
    G.exePath = ModulePathW();
    return G.exePath;
}

/* ============================== 字体与布局 =============================== */

static HFONT MakeFont(int pt, int weight)
{
    LOGFONTW lf;
    ZeroMemory(&lf, sizeof(lf));
    lf.lfHeight  = -MulDiv(pt, G.dpi, 72);
    lf.lfWeight  = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    lstrcpynW(lf.lfFaceName, L"Microsoft YaHei UI", LF_FACESIZE);
    return CreateFontIndirectW(&lf);
}

static Rect BodyRect()
{
    int m      = S(18);
    int top    = S(58);
    int bottom = G.clientH - S(62);
    if (bottom < top + S(60)) bottom = top + S(60);
    Rect r;
    r.x = m;
    r.y = top;
    r.w = std::max(0, G.clientW - 2 * m);
    r.h = bottom - top;
    return r;
}

/* ============================== 控件创建 ================================= */

static HWND BodyCtl(const wchar_t* cls, const std::wstring& text, DWORD style, DWORD exStyle,
                    int x, int y, int w, int h, int id, HFONT font)
{
    HWND hc = CreateWindowExW(exStyle, cls, text.c_str(), WS_CHILD | WS_VISIBLE | style,
                              x, y, w, h, G.hwnd, (HMENU)(INT_PTR)id, G.inst, NULL);
    if (hc && font) SendMessageW(hc, WM_SETFONT, (WPARAM)font, TRUE);
    if (hc) G.body.push_back(hc);      /* 全部记下来，切换步骤时才能清空 */
    return hc;
}

static HWND BodyStatic(const std::wstring& text, int x, int y, int w, int h,
                       HFONT font = NULL, int id = 0)
{
    return BodyCtl(L"STATIC", text, SS_LEFT, 0, x, y, w, h, id, font ? font : G.fontText);
}

static HWND BodyCheck(const std::wstring& text, int x, int y, int w, int h, int id, bool checked)
{
    HWND hc = BodyCtl(L"BUTTON", text, BS_AUTOCHECKBOX | WS_TABSTOP, 0, x, y, w, h, id, G.fontText);
    if (hc && checked) SendMessageW(hc, BM_SETCHECK, BST_CHECKED, 0);
    return hc;
}

static void ClearBody()
{
    for (size_t i = 0; i < G.body.size(); ++i)
        if (IsWindow(G.body[i])) DestroyWindow(G.body[i]);
    G.body.clear();
    G.hProgress  = NULL;
    G.hStatus    = NULL;
    G.hRunCheck  = NULL;
    G.hPathEdit  = NULL;
    G.hFinalLabel = NULL;
}

/* ============================== 三个步骤的内容 =========================== */

/* 界面上选择的是“父目录”，真正安装到 <父目录>\<安装目录名>；
 * 如果选的目录本身就叫这个名字（例如 C:\Program Files\AEpy），就不再重复套一层。 */
static std::wstring ResolveInstallDir(const std::wstring& chosen)
{
    std::wstring p = TrimTrailingSepsW(FullPathW(TrimW(chosen)));
    if (p.empty()) return p;
    if (EqualsIgnoreCaseW(PathNameW(p), G.meta.folderName)) return p;
    return PathJoinW(p, G.meta.folderName);
}

static void BuildStepSelectDir()
{
    Rect b = BodyRect();
    int editH = S(28);
    int btnW  = S(110);

    BodyStatic(FormatW(Txt::kLabelSelectDir, G.meta.folderName.c_str()), b.x, b.y, b.w, S(22));
    G.hPathEdit = BodyCtl(L"EDIT", G.parentDir, WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP,
                          WS_EX_CLIENTEDGE, b.x, b.y + S(24), b.w - btnW - S(8), editH,
                          IDC_PATH_EDIT, G.fontText);
    BodyCtl(L"BUTTON", Txt::kBtnBrowse, BS_PUSHBUTTON | WS_TABSTOP, 0,
            b.x + b.w - btnW, b.y + S(24), btnW, editH, IDC_BROWSE, G.fontText);

    G.hFinalLabel = BodyStatic(FormatW(Txt::kLabelFinalDir, G.installDir.c_str()),
                               b.x, b.y + S(58), b.w, S(22), G.fontBold, IDC_FINALDIR);

    int y = b.y + S(24) + editH + S(42);
    BodyStatic(Txt::kLabelOptions, b.x, y, b.w, S(20), G.fontBold, 0);

    y += S(26);
    BodyCheck(Txt::kOptShortcut, b.x + S(10), y, b.w - S(10), S(24), IDC_CHK_SHORTCUT, G.optShortcut);
    y += S(28);
    BodyCheck(FormatW(Txt::kOptFolderMenu, G.meta.appName.c_str()), b.x + S(10), y, b.w - S(10), S(24),
              IDC_CHK_FOLDER, G.optFolderMenu);
    y += S(28);
    BodyCheck(FormatW(Txt::kOptPyMenu, G.meta.appName.c_str()), b.x + S(10), y, b.w - S(10), S(24),
              IDC_CHK_PY, G.optPyMenu);

    BodyStatic(Txt::kDirHint, b.x, b.y + b.h - S(40), b.w, S(38), G.fontSmall, IDC_HINT);
}

static void BuildStepLicense()
{
    Rect b = BodyRect();
    BodyStatic(Txt::kLicenseIntro, b.x, b.y, b.w, S(22));
    std::wstring text = G.agreement.empty() ? std::wstring(Txt::kLicenseEmpty) : G.agreement;
    HWND e = BodyCtl(L"EDIT", L"", WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_TABSTOP,
                     WS_EX_CLIENTEDGE, b.x, b.y + S(26), b.w, b.h - S(26), IDC_LICENSE, G.fontText);
    if (e) SetWindowTextW(e, text.c_str());
}

static void BuildStepInstall()
{
    Rect b = BodyRect();
    std::wstring opts;
    if (G.optShortcut)   opts += Txt::kOptShortName;
    if (G.optFolderMenu) { if (!opts.empty()) opts += L"、"; opts += Txt::kOptFolderName; }
    if (G.optPyMenu)     { if (!opts.empty()) opts += L"、"; opts += Txt::kOptPyName; }
    if (opts.empty())    opts = Txt::kOptNone;
    BodyStatic(FormatW(Txt::kInstallAt, G.installDir.c_str()), b.x, b.y, b.w, S(22));
    BodyStatic(FormatW(Txt::kInstallOpts, opts.c_str()), b.x, b.y + S(24), b.w, S(22));
    BodyStatic(Txt::kLabelProgress, b.x, b.y + S(64), b.w, S(20), G.fontBold, 0);

    G.hProgress = BodyCtl(PROGRESS_CLASSW, L"", 0, 0, b.x, b.y + S(88), b.w, S(20),
                          IDC_PROGRESS, G.fontText);
    if (G.hProgress) {
        SendMessageW(G.hProgress, PBM_SETRANGE32, 0, 1000);
        SendMessageW(G.hProgress, PBM_SETPOS, (WPARAM)(G.finished ? 1000 : 0), 0);
    }
    G.hStatus = BodyStatic(G.finished ? Txt::kFinishMsg : Txt::kStatusReady,
                           b.x, b.y + S(118), b.w, S(22), G.fontText, IDC_STATUS);

    if (G.finished) {
        G.hRunCheck = BodyCheck(FormatW(Txt::kRunCheck, G.meta.appName.c_str()),
                                b.x, b.y + S(152), b.w, S(26), IDC_RUN, true);
    }
}

static void BuildStep()
{
    ClearBody();
    switch (G.step) {
        case 0: BuildStepSelectDir(); break;
        case 1: BuildStepLicense();   break;
        default: BuildStepInstall();  break;
    }
}

/* ============================== 按钮与标题 =============================== */

static void UpdateTitle()
{
    const wchar_t* t = Txt::kStep1Title;
    if (G.step == 1) t = Txt::kStep2Title;
    else if (G.step == 2) {
        if (G.finished)        t = Txt::kStep3DoneTitle;
        else if (G.installing) t = Txt::kStep3BusyTitle;
        else                   t = Txt::kStep3Title;
    }
    SetWindowTextW(G.hTitle, t);
}

static void UpdateButtons()
{
    if (G.step == 0) {
        SetWindowTextW(G.hBtnNext, Txt::kBtnNext);
        EnableWindow(G.hBtnBack, FALSE);
        EnableWindow(G.hBtnNext, TRUE);
        EnableWindow(G.hBtnCancel, TRUE);
    } else if (G.step == 1) {
        SetWindowTextW(G.hBtnNext, Txt::kBtnNext);
        EnableWindow(G.hBtnBack, TRUE);
        EnableWindow(G.hBtnNext, TRUE);
        EnableWindow(G.hBtnCancel, TRUE);
    } else if (G.finished) {
        SetWindowTextW(G.hBtnNext, Txt::kBtnFinish);
        EnableWindow(G.hBtnNext, TRUE);
        EnableWindow(G.hBtnBack, FALSE);
        EnableWindow(G.hBtnCancel, FALSE);
    } else if (G.installing) {
        SetWindowTextW(G.hBtnNext, Txt::kBtnInstall);
        EnableWindow(G.hBtnNext, FALSE);
        EnableWindow(G.hBtnBack, FALSE);      /* 安装中不能上一步 */
        EnableWindow(G.hBtnCancel, TRUE);
    } else {
        SetWindowTextW(G.hBtnNext, Txt::kBtnInstall);
        EnableWindow(G.hBtnNext, TRUE);
        EnableWindow(G.hBtnBack, !G.stepLocked);
        EnableWindow(G.hBtnCancel, TRUE);
    }
}

static void SetStatus(const std::wstring& s)
{
    if (G.hStatus && IsWindow(G.hStatus)) SetWindowTextW(G.hStatus, s.c_str());
    LogLine(s);
}

static void SetProgress(size_t done, size_t total)
{
    if (!G.hProgress || !IsWindow(G.hProgress)) return;
    int permille = (total > 0) ? (int)((done * 1000) / total) : 0;
    if (permille > 1000) permille = 1000;
    SendMessageW(G.hProgress, PBM_SETPOS, (WPARAM)permille, 0);
}

static void PumpMessages()
{
    if (G.silent) return;
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) { G.cancelRequested = true; return; }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

/* ============================== 目录与权限 =============================== */

static bool NeedsElevation(const std::wstring& dir)
{
    if (IsUnderOrEqualW(dir, ProgramFilesDirW(false))) return true;
    if (IsUnderOrEqualW(dir, ProgramFilesDirW(true)))  return true;
    wchar_t win[MAX_PATH + 2] = {0};
    if (GetWindowsDirectoryW(win, MAX_PATH) > 0 && IsUnderOrEqualW(dir, win)) return true;
    return !CanWriteInDirW(dir);
}

static bool ValidateDirInput(std::wstring& err)
{
    std::wstring p = TrimW(GetWindowTextStr(G.hPathEdit));
    if (p.empty()) { err = Txt::kErrNoDir; return false; }
    p = TrimTrailingSepsW(FullPathW(p));
    std::wstring finalDir = ResolveInstallDir(p);
    if (finalDir.empty()) { err = Txt::kErrNoDir; return false; }
    wchar_t win[MAX_PATH + 2] = {0};
    if (GetWindowsDirectoryW(win, MAX_PATH) > 0 && IsUnderOrEqualW(finalDir, win)) {
        err = Txt::kErrWinDir;
        return false;
    }
    G.parentDir  = p;
    G.installDir = finalDir;
    return true;
}

static void BrowseFolder()
{
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = G.hwnd;
    bi.lpszTitle = L"请选择安装位置";
    bi.ulFlags   = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;
    LPITEMIDLIST idl = SHBrowseForFolderW(&bi);
    if (!idl) return;
    wchar_t path[MAX_PATH + 2] = {0};
    if (SHGetPathFromIDListW(idl, path)) {
        std::wstring p = TrimTrailingSepsW(std::wstring(path));
        if (!p.empty() && G.hPathEdit) SetWindowTextW(G.hPathEdit, p.c_str());
    }
    CoTaskMemFree(idl);
}

/* ============================== 安装包数据 =============================== */

struct ZipEntry {
    std::wstring   rel;      /* 相对安装目录的路径，反斜杠分隔 */
    bool           isDir;
    const uint8_t* data;
    size_t         size;
};

static bool IsUnsafeEntryName(const std::wstring& name)
{
    if (name.empty()) return true;
    if (name.size() >= 2 && name[1] == L':') return true;           /* C:... */
    if (IsSepW(name[0]) || IsSepW(name[1])) return true;            /* 绝对路径 */
    /* 检查 ".." 路径片段 */
    size_t pos = 0;
    while (pos <= name.size()) {
        size_t sep = name.find_first_of(L"\\/", pos);
        std::wstring part = name.substr(pos, sep == std::wstring::npos ? std::wstring::npos : sep - pos);
        if (part == L"..") return true;
        if (sep == std::wstring::npos) break;
        pos = sep + 1;
    }
    return false;
}

static bool ParsePayload(std::vector<ZipEntry>& out, std::wstring& err)
{
    const uint8_t* buf = (const uint8_t*)G.payload;
    size_t n = G.payloadSize;
    if (!buf || n < sizeof(ZipEndOfCentralDir)) { err = Txt::kErrNoPayload; return false; }

    size_t lowest = (n > 65535 + sizeof(ZipEndOfCentralDir))
                  ? n - (65535 + sizeof(ZipEndOfCentralDir)) : 0;
    size_t eocdPos = (size_t)-1;
    ZipEndOfCentralDir eocd;
    for (size_t i = n - sizeof(ZipEndOfCentralDir); ; --i) {
        ZipEndOfCentralDir e;
        memcpy(&e, buf + i, sizeof(e));
        if (e.sig == ZIP_SIG_EOCD && e.entriesTotal > 0 &&
            (size_t)e.cdOffset + (size_t)e.cdSize <= n) {
            eocdPos = i;
            eocd = e;
            break;
        }
        if (i == lowest) break;
    }
    if (eocdPos == (size_t)-1) { err = Txt::kErrNoPayload; return false; }

    size_t pos = eocd.cdOffset;
    for (unsigned k = 0; k < eocd.entriesTotal; ++k) {
        if (pos + sizeof(ZipCentralHeader) > n) { err = L"安装包数据损坏（中央目录越界）。"; return false; }
        ZipCentralHeader c;
        memcpy(&c, buf + pos, sizeof(c));
        if (c.sig != ZIP_SIG_CENTRAL) { err = L"安装包数据损坏（条目签名错误）。"; return false; }
        if (pos + sizeof(c) + c.nameLen > n) { err = L"安装包数据损坏（条目名越界）。"; return false; }

        std::wstring name = Utf8ToWide((const char*)(buf + pos + sizeof(c)), c.nameLen);
        bool isDir = (!name.empty() && IsSepW(name[name.size() - 1]));
        name = SlashesToBackW(name);
        while (!name.empty() && IsSepW(name[name.size() - 1])) name.erase(name.size() - 1);

        if (c.method != ZIP_METHOD_STORE) {
            err = L"安装包使用了不支持的压缩方式（只支持“存储”）。";
            return false;
        }
        if (IsUnsafeEntryName(name)) {
            err = L"安装包内的路径不合法：" + name;
            return false;
        }

        size_t dataStart = c.localOffset + sizeof(ZipLocalHeader);
        if (c.localOffset + sizeof(ZipLocalHeader) <= n) {
            ZipLocalHeader lh;
            memcpy(&lh, buf + c.localOffset, sizeof(lh));
            if (lh.sig == ZIP_SIG_LOCAL)
                dataStart = c.localOffset + sizeof(ZipLocalHeader) + lh.nameLen + lh.extraLen;
        }
        if (dataStart + (size_t)c.compSize > n) {
            err = L"安装包数据损坏（文件内容越界）：" + name;
            return false;
        }

        ZipEntry e;
        e.rel   = name;
        e.isDir = isDir;
        e.data  = isDir ? NULL : (buf + dataStart);
        e.size  = isDir ? 0 : (size_t)c.compSize;
        out.push_back(e);

        pos += sizeof(c) + c.nameLen + c.extraLen + c.commentLen;
    }
    return true;
}

/* ============================== 安装动作 ================================= */

static bool EnsureDirRecording(const std::wstring& dir)
{
    if (dir.empty()) return true;
    if (DirExistsW(dir)) return true;
    std::wstring parent = PathDirW(dir);
    if (!parent.empty() && !DirExistsW(parent)) {
        if (!EnsureDirRecording(parent)) return false;
    }
    if (CreateDirectoryW(dir.c_str(), NULL)) {
        G.createdDirs.push_back(dir);
        return true;
    }
    return DirExistsW(dir);
}

static bool CreateDesktopShortcut(std::wstring& outPath)
{
    wchar_t desktop[MAX_PATH + 2] = {0};
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) return false;
    std::wstring target = PathJoinW(G.installDir, G.meta.mainExe);
    if (!FileExistsW(target)) return false;

    IShellLinkW* link = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER,
                                IID_IShellLinkW, (void**)&link)))
        return false;

    link->SetPath(target.c_str());
    link->SetWorkingDirectory(G.installDir.c_str());
    link->SetIconLocation(target.c_str(), 0);
    link->SetDescription(G.meta.appName.c_str());

    bool ok = false;
    IPersistFile* pf = NULL;
    if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void**)&pf))) {
        outPath = PathJoinW(desktop, G.meta.shortcutName());
        ok = SUCCEEDED(pf->Save(outPath.c_str(), TRUE));
        pf->Release();
    }
    link->Release();
    return ok;
}

static void RegisterFolderMenu()
{
    std::wstring exe  = PathJoinW(G.installDir, G.meta.mainExe);
    std::wstring base = L"Software\\Classes\\Directory\\shell\\OpenWithAEpy";
    RegWriteStringW(HKEY_CURRENT_USER, base, L"",
                    FormatW(Txt::kMenuFolderLabel, G.meta.appName.c_str()));
    RegWriteStringW(HKEY_CURRENT_USER, base, L"Icon", L"\"" + exe + L"\",0");
    RegWriteStringW(HKEY_CURRENT_USER, base + L"\\command", L"",
                    L"\"" + exe + L"\" --folder \"%1\"");
    G.folderMenuCreated = true;
}

static void RegisterPyMenu()
{
    std::wstring exe  = PathJoinW(G.installDir, G.meta.mainExe);
    std::wstring base = L"Software\\Classes\\SystemFileAssociations\\.py\\shell\\EditWithAEpy";
    RegWriteStringW(HKEY_CURRENT_USER, base, L"",
                    FormatW(Txt::kMenuFileLabel, G.meta.appName.c_str()));
    RegWriteStringW(HKEY_CURRENT_USER, base, L"Icon", L"\"" + exe + L"\",0");
    RegWriteStringW(HKEY_CURRENT_USER, base + L"\\command", L"",
                    L"\"" + exe + L"\" --file \"%1\"");
    G.pyMenuCreated = true;
}

static void WriteUninstallEntry(size_t installedBytes)
{
    std::wstring key    = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + G.meta.packageName;
    std::wstring exe    = PathJoinW(G.installDir, G.meta.mainExe);
    std::wstring uninst = PathJoinW(G.installDir, G.meta.uninstallerName());
    RegWriteStringW(HKEY_CURRENT_USER, key, L"DisplayName",     G.meta.packageName);
    RegWriteStringW(HKEY_CURRENT_USER, key, L"DisplayVersion",  G.meta.version);
    RegWriteStringW(HKEY_CURRENT_USER, key, L"Publisher",       G.meta.publisher);
    RegWriteStringW(HKEY_CURRENT_USER, key, L"InstallLocation", G.installDir);
    RegWriteStringW(HKEY_CURRENT_USER, key, L"DisplayIcon",     exe);
    RegWriteStringW(HKEY_CURRENT_USER, key, L"UninstallString", L"\"" + uninst + L"\"");
    RegWriteStringW(HKEY_CURRENT_USER, key, L"QuietUninstallString", L"\"" + uninst + L"\" --silent");
    RegWriteDwordW (HKEY_CURRENT_USER, key, L"NoModify", 1);
    RegWriteDwordW (HKEY_CURRENT_USER, key, L"NoRepair", 1);
    RegWriteDwordW (HKEY_CURRENT_USER, key, L"EstimatedSize",
                    (DWORD)(installedBytes / 1024));
    G.uninstallEntryCreated = true;
}

/* 提权安装到 Program Files 之类的目录后，管理员写进去的文件默认只有管理员能改，
 * 而 AEpy 要在自己的目录里写 config\*.json（Python 还会写 __pycache__），
 * 普通用户运行就会失败。这里用系统自带的 icacls 给安装目录（含子目录/文件）
 * 授予 BUILTIN\Users 修改权限：*S-1-5-32-545 是 Users 组的 SID（与系统语言无关），
 * (OI)(CI) 表示子目录与文件都继承，M = 修改。
 * 用 CREATE_NO_WINDOW 启动，不会闪出控制台窗口。 */
static bool GrantUsersModifyW(const std::wstring& dir)
{
    wchar_t sysDir[MAX_PATH + 2] = {0};
    if (GetSystemDirectoryW(sysDir, MAX_PATH) == 0) return false;
    std::wstring icacls = PathJoinW(sysDir, L"icacls.exe");
    if (!FileExistsW(icacls)) return false;

    std::wstring cmd = L"\"" + icacls + L"\" \"" + dir +
                       L"\" /grant *S-1-5-32-545:(OI)(CI)M /T /C /Q";
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::wstring cmdLine = cmd;
    if (!CreateProcessW(NULL, &cmdLine[0], NULL, NULL, FALSE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return code == 0;
}

/* 卸载程序没法删除“正在运行的自己”，它会把自己挪到 %TEMP% 留到下次。
 * 这里在安装成功时顺手把上次的副本清掉（卸载程序里也有同样的清理）。 */
static void CleanupStaleUninstallCopies()
{
    std::wstring tempDir = TempDirW();
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(PathJoinW(tempDir, L"AEpyUninstall_*.exe").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    int removed = 0;
    do {
        std::wstring p = PathJoinW(tempDir, fd.cFileName);
        if (EqualsIgnoreCaseW(p, G.exePath)) continue;
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(p.c_str())) ++removed;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    if (removed > 0) LogLine(FormatW(L"已清理 %d 个上次卸载留下的临时文件。", removed));
}

/* 真正干活：解压 + 快捷方式 + 右键菜单 + 卸载信息。失败/取消时由调用方回滚。 */
static bool InstallAll(std::wstring& err)
{
    if (!EnsureDirRecording(G.installDir)) {
        err = L"无法创建安装目录：\n" + G.installDir;
        return false;
    }

    std::vector<ZipEntry> entries;
    if (!ParsePayload(entries, err)) return false;

    size_t total = 0;
    for (size_t i = 0; i < entries.size(); ++i) if (!entries[i].isDir) total += entries[i].size;
    size_t done = 0;

    for (size_t i = 0; i < entries.size(); ++i) {
        const ZipEntry& e = entries[i];
        if (G.cancelRequested) return false;

        std::wstring target = PathJoinW(G.installDir, e.rel);
        if (e.isDir) {
            if (!EnsureDirRecording(target)) { err = L"无法创建目录：\n" + target; return false; }
        } else {
            if (!EnsureDirRecording(PathDirW(target))) { err = L"无法创建目录：\n" + PathDirW(target); return false; }
            SetStatus(FormatW(Txt::kStatusExtract, e.rel.c_str()));
            if (!WriteWholeFileW(target, e.data, e.size)) {
                err = L"写入文件失败：\n" + target;
                return false;
            }
            G.createdFiles.push_back(target);
            done += e.size;
            SetProgress(done, total);
        }
        PumpMessages();
        if (G.cancelRequested) return false;
    }

    /* 卸载程序（内嵌在安装包里的独立 exe） */
    if (G.uninstaller && G.uninstallerSize > 0) {
        std::wstring un = PathJoinW(G.installDir, G.meta.uninstallerName());
        if (WriteWholeFileW(un, G.uninstaller, G.uninstallerSize))
            G.createdFiles.push_back(un);
    }

    /* 提权安装时，给安装目录补上普通用户的写权限 */
    if (IsProcessElevated() || G.elevatedMode) {
        SetStatus(Txt::kStatusPerms);
        PumpMessages();
        if (!GrantUsersModifyW(G.installDir))
            LogLine(L"警告：未能设置安装目录权限，程序可能无法保存配置。");
    }

    if (G.optShortcut) {
        SetStatus(Txt::kStatusShortcut);
        PumpMessages();
        std::wstring sp;
        if (CreateDesktopShortcut(sp)) { G.shortcutCreated = true; G.shortcutPath = sp; }
    }

    if (G.optFolderMenu || G.optPyMenu) {
        SetStatus(Txt::kStatusMenus);
        PumpMessages();
        if (G.optFolderMenu) RegisterFolderMenu();
        if (G.optPyMenu)     RegisterPyMenu();
    }

    SetStatus(Txt::kStatusFinal);
    PumpMessages();
    size_t installedBytes = total + G.uninstallerSize;
    WriteUninstallEntry(installedBytes);
    CleanupStaleUninstallCopies();

    SetProgress(1, 1);
    return true;
}

static void RollbackInstall()
{
    for (size_t i = G.createdFiles.size(); i-- > 0; ) {
        SetFileAttributesW(G.createdFiles[i].c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(G.createdFiles[i].c_str());
    }
    G.createdFiles.clear();
    for (size_t i = G.createdDirs.size(); i-- > 0; ) RemoveDirectoryW(G.createdDirs[i].c_str());
    G.createdDirs.clear();

    if (G.shortcutCreated && !G.shortcutPath.empty()) {
        DeleteFileW(G.shortcutPath.c_str());
        G.shortcutCreated = false;
    }
    if (G.folderMenuCreated) {
        RegRemoveTreeW(HKEY_CURRENT_USER, L"Software\\Classes\\Directory\\shell\\OpenWithAEpy");
        G.folderMenuCreated = false;
    }
    if (G.pyMenuCreated) {
        RegRemoveTreeW(HKEY_CURRENT_USER,
            L"Software\\Classes\\SystemFileAssociations\\.py\\shell\\EditWithAEpy");
        G.pyMenuCreated = false;
    }
    if (G.uninstallEntryCreated) {
        RegRemoveTreeW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + G.meta.packageName);
        G.uninstallEntryCreated = false;
    }
}

/* ============================== 界面流程 ================================= */

static void GoToStep(int step)
{
    if (G.step == 0) {
        std::wstring p = TrimW(GetWindowTextStr(G.hPathEdit));
        if (!p.empty()) {
            G.parentDir  = TrimTrailingSepsW(FullPathW(p));
            G.installDir = ResolveInstallDir(G.parentDir);
        }
        HWND c1 = GetDlgItem(G.hwnd, IDC_CHK_SHORTCUT);
        HWND c2 = GetDlgItem(G.hwnd, IDC_CHK_FOLDER);
        HWND c3 = GetDlgItem(G.hwnd, IDC_CHK_PY);
        if (c1) G.optShortcut   = SendMessageW(c1, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (c2) G.optFolderMenu = SendMessageW(c2, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (c3) G.optPyMenu     = SendMessageW(c3, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    G.step = step;
    BuildStep();
    UpdateTitle();
    UpdateButtons();
}

static void StartInstallUI()
{
    G.installing      = true;
    G.cancelRequested = false;
    UpdateTitle();
    UpdateButtons();

    std::wstring err;
    bool ok = InstallAll(err);

    if (G.cancelRequested) {
        RollbackInstall();
        G.installing = false;
        if (IsWindow(G.hwnd))
            MessageBoxW(G.hwnd, Txt::kCancelledMsg, G.meta.packageName.c_str(),
                        MB_OK | MB_ICONINFORMATION);
        DestroyWindow(G.hwnd);
        return;
    }
    if (!ok) {
        RollbackInstall();
        G.installing = false;
        UpdateTitle();
        UpdateButtons();
        if (IsWindow(G.hwnd)) {
            SetStatus(L"安装失败。");
            std::wstring msg = std::wstring(Txt::kErrFailPrefix) + err;
            MessageBoxW(G.hwnd, msg.c_str(), G.meta.packageName.c_str(), MB_OK | MB_ICONERROR);
        }
        return;
    }

    G.installing = false;
    G.finished   = true;
    BuildStep();               /* 重建第 3 步：出现“运行 …”复选框 */
    UpdateTitle();
    UpdateButtons();
}

static void FinishClick()
{
    bool run = G.hRunCheck && SendMessageW(G.hRunCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (run) {
        std::wstring target = PathJoinW(G.installDir, G.meta.mainExe);
        if (FileExistsW(target)) {
            if (IsProcessElevated()) ShellOpenUnelevatedW(target, G.installDir);
            else                     ShellOpenW(target, L"", G.installDir);
        }
    }
    DestroyWindow(G.hwnd);
}

static void OnMainButton()
{
    if (G.installing) return;
    std::wstring err;
    switch (G.step) {
        case 0:
            if (!ValidateDirInput(err)) {
                MessageBoxW(G.hwnd, err.c_str(), G.meta.packageName.c_str(), MB_OK | MB_ICONWARNING);
                return;
            }
            GoToStep(1);
            break;
        case 1:
            GoToStep(2);
            break;
        default:
            if (G.finished) {
                FinishClick();
                return;
            }
            if (G.hPathEdit) ValidateDirInput(err);
            if (NeedsElevation(G.installDir) && !IsProcessElevated()) {
                /* 需要管理员权限：以管理员身份重新启动自己，本进程退出 */
                std::wstring params = FormatW(L"--elevated --dir \"%ls\" --shortcut %d --folder %d --py %d",
                                              G.installDir.c_str(),
                                              G.optShortcut ? 1 : 0,
                                              G.optFolderMenu ? 1 : 0,
                                              G.optPyMenu ? 1 : 0);
                if (!RelaunchElevatedW(SelfPath(), params, false)) {
                    MessageBoxW(G.hwnd, Txt::kElevateFailed, G.meta.packageName.c_str(),
                                MB_OK | MB_ICONERROR);
                    return;
                }
                DestroyWindow(G.hwnd);
                return;
            }
            StartInstallUI();
            break;
    }
}

static void OnCancelClick()
{
    if (G.finished) { DestroyWindow(G.hwnd); return; }

    std::wstring msg = G.installing ? std::wstring(Txt::kCancelBusy) : std::wstring(Txt::kCancelConfirm);
    if (MessageBoxW(G.hwnd, msg.c_str(), G.meta.packageName.c_str(),
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
        return;

    if (G.installing) {
        G.cancelRequested = true;
        EnableWindow(G.hBtnCancel, FALSE);
        SetStatus(L"正在取消并清理残留文件…");
        return;
    }
    DestroyWindow(G.hwnd);
}

/* ============================== 窗口过程 ================================= */

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CREATE:
            return 0;

        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == IDC_PATH_EDIT && HIWORD(wp) == EN_CHANGE) {
                if (G.hFinalLabel && IsWindow(G.hFinalLabel)) {
                    std::wstring finalDir = ResolveInstallDir(GetWindowTextStr(G.hPathEdit));
                    SetWindowTextW(G.hFinalLabel,
                                   FormatW(Txt::kLabelFinalDir, finalDir.c_str()).c_str());
                }
                return 0;
            }
            if (HIWORD(wp) == BN_CLICKED || HIWORD(wp) == 0) {
                switch (id) {
                    case IDB_NEXT:   OnMainButton();  return 0;
                    case IDB_BACK:   if (G.step > 0 && !G.installing) GoToStep(G.step - 1); return 0;
                    case IDB_CANCEL: OnCancelClick(); return 0;
                    case IDC_BROWSE: BrowseFolder();  return 0;
                }
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC dc = (HDC)wp;
            SetBkMode(dc, TRANSPARENT);
            HWND ctl = (HWND)lp;
            if (ctl && GetDlgCtrlID(ctl) == IDC_HINT) SetTextColor(dc, RGB(0x70, 0x70, 0x70));
            else                                       SetTextColor(dc, GetSysColor(COLOR_BTNTEXT));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }

        case WM_CLOSE:
            OnCancelClick();
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_AEPY_AUTOSTART:
            StartInstallUI();
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ============================== 界面搭建 ================================= */

static void CreateChrome()
{
    G.fontText  = MakeFont(9,  FW_NORMAL);
    G.fontBold  = MakeFont(9,  FW_BOLD);
    G.fontTitle = MakeFont(12, FW_BOLD);
    G.fontSmall = MakeFont(8,  FW_NORMAL);

    int m = S(18);
    int btnW = S(104), btnH = S(32), gap = S(8);
    int btnY = G.clientH - S(18) - btnH;

    G.hTitle = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                               m, S(18), G.clientW - 2 * m, S(28),
                               G.hwnd, NULL, G.inst, NULL);
    SendMessageW(G.hTitle, WM_SETFONT, (WPARAM)G.fontTitle, TRUE);

    G.hSep = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                             m, btnY - S(12), G.clientW - 2 * m, S(2),
                             G.hwnd, NULL, G.inst, NULL);

    int x = G.clientW - m - btnW;
    G.hBtnNext = CreateWindowExW(0, L"BUTTON", Txt::kBtnNext,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 x, btnY, btnW, btnH, G.hwnd, (HMENU)(INT_PTR)IDB_NEXT, G.inst, NULL);
    x -= gap + btnW;
    G.hBtnBack = CreateWindowExW(0, L"BUTTON", Txt::kBtnBack,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                 x, btnY, btnW, btnH, G.hwnd, (HMENU)(INT_PTR)IDB_BACK, G.inst, NULL);
    x -= gap + btnW;
    G.hBtnCancel = CreateWindowExW(0, L"BUTTON", Txt::kBtnCancel,
                                   WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   x, btnY, btnW, btnH, G.hwnd, (HMENU)(INT_PTR)IDB_CANCEL, G.inst, NULL);

    SendMessageW(G.hBtnNext,   WM_SETFONT, (WPARAM)G.fontText, TRUE);
    SendMessageW(G.hBtnBack,   WM_SETFONT, (WPARAM)G.fontText, TRUE);
    SendMessageW(G.hBtnCancel, WM_SETFONT, (WPARAM)G.fontText, TRUE);

    HICON icon = (HICON)LoadImageW(G.inst, MAKEINTRESOURCEW(AEPY_RES_ICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON iconSmall = (HICON)LoadImageW(G.inst, MAKEINTRESOURCEW(AEPY_RES_ICON), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (icon)      SendMessageW(G.hwnd, WM_SETICON, ICON_BIG,   (LPARAM)icon);
    if (iconSmall) SendMessageW(G.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall);
}

static bool LoadResourcesFromSelf()
{
    const void* data = NULL;
    size_t size = 0;

    if (!LoadResourceBytes(G.inst, AEPY_RES_META, &data, &size)) {
        MessageBoxW(NULL, Txt::kErrNoMeta, L"安装程序", MB_OK | MB_ICONERROR);
        return false;
    }
    G.meta = ParseMetaText((const char*)data, size);

    if (LoadResourceBytes(G.inst, AEPY_RES_PAYLOAD, &data, &size)) {
        G.payload = (const char*)data;
        G.payloadSize = size;
    }
    if (LoadResourceBytes(G.inst, AEPY_RES_AGREEMENT, &data, &size)) {
        std::string raw((const char*)data, size);
        G.agreement = TrimW(DecodeTextAuto(raw));
    }
    if (LoadResourceBytes(G.inst, AEPY_RES_UNINSTALLER, &data, &size)) {
        G.uninstaller = (const char*)data;
        G.uninstallerSize = size;
    }
    return true;
}

/* ============================== 静默安装 ================================= */

static int RunSilent()
{
    if (NeedsElevation(G.installDir) && !IsProcessElevated()) {
        std::wstring params = FormatW(L"--silent --dir \"%ls\" --shortcut %d --folder %d --py %d",
                                      G.installDir.c_str(),
                                      G.optShortcut ? 1 : 0,
                                      G.optFolderMenu ? 1 : 0,
                                      G.optPyMenu ? 1 : 0);
        if (!RelaunchElevatedW(SelfPath(), params, true)) {
            LogLine(L"提权失败（需要管理员权限），安装未开始。");
            return 3;
        }
        return 0;
    }
    std::wstring err;
    if (!InstallAll(err)) {
        RollbackInstall();                 /* 失败也要清掉这次已经写进去的文件 */
        LogLine(L"安装失败：" + err);
        return 1;
    }
    LogLine(L"安装完成：" + G.installDir);
    return 0;
}

/* ============================== 入口 ===================================== */

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmdLine, int nCmdShow)
{
    (void)hPrev; (void)lpCmdLine;
    G.inst = hInst;
    G.exePath = ModulePathW();

    std::vector<std::wstring> args = CommandLineArgsW();
    G.silent       = HasArgW(args, L"--silent");
    G.elevatedMode = HasArgW(args, L"--elevated");

    if (!LoadResourcesFromSelf()) return 2;

    /* 安装位置：界面上选/命令行给的都是“父目录”，实际安装目录 = 父目录\安装目录名。
     * 默认父目录 = C:\Program Files（于是默认装到 C:\Program Files\AEpy）。
     * 如果给的目录本身就叫这个名字（例如 --elevated 回传的最终目录），不再重复套一层。 */
    G.parentDir = ArgValueW(args, L"--dir");
    if (G.parentDir.empty()) G.parentDir = ProgramFilesDirW(false);
    G.parentDir  = TrimTrailingSepsW(FullPathW(G.parentDir));
    G.installDir = ResolveInstallDir(G.parentDir);

    G.optShortcut   = ArgBoolW(args, L"--shortcut", true);
    G.optFolderMenu = ArgBoolW(args, L"--folder",   true);
    G.optPyMenu     = ArgBoolW(args, L"--py",       true);

    if (G.silent) {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        int rc = RunSilent();
        CoUninitialize();
        return rc;
    }

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    G.dpi = 96;
    {
        HDC screen = GetDC(NULL);
        if (screen) {
            int d = GetDeviceCaps(screen, LOGPIXELSX);
            if (d > 0) G.dpi = d;
            ReleaseDC(NULL, screen);
        }
    }

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = GetSysColorBrush(COLOR_BTNFACE);
    wc.lpszClassName = L"AEpyInstallerWizard";
    wc.hIcon         = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(AEPY_RES_ICON), IMAGE_ICON,
                                         GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm       = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(AEPY_RES_ICON), IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    if (!RegisterClassExW(&wc)) return 4;

    G.clientW = MulDiv(660, G.dpi, 96);
    G.clientH = MulDiv(470, G.dpi, 96);

    DWORD style   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    DWORD exStyle = WS_EX_APPWINDOW;
    RECT rc = { 0, 0, G.clientW, G.clientH };
    AdjustWindowRectEx(&rc, style, FALSE, exStyle);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;

    RECT work;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int winX = work.left + ((work.right - work.left) - winW) / 2;
    int winY = work.top + ((work.bottom - work.top) - winH) / 2;

    std::wstring title = G.meta.packageName + Txt::kTitleSuffix;
    HWND hwnd = CreateWindowExW(exStyle, L"AEpyInstallerWizard", title.c_str(),
                                style, winX, winY, winW, winH,
                                NULL, NULL, hInst, NULL);
    if (!hwnd) return 5;
    G.hwnd = hwnd;

    CreateChrome();
    G.stepLocked = G.elevatedMode;
    G.step       = G.elevatedMode ? 2 : 0;
    BuildStep();
    UpdateTitle();
    UpdateButtons();

    ShowWindow(hwnd, nCmdShow ? nCmdShow : SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    /* 提权启动时：直接进入第 3 步并自动开始安装 */
    if (G.elevatedMode) PostMessageW(hwnd, WM_AEPY_AUTOSTART, 0, 0);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    CoUninitialize();
    return 0;
}
