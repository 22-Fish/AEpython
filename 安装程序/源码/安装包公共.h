/* ============================================================================
 *  安装包公共.h —— AEpy 安装包体系的公共工具（只含 inline 实现，无额外 .cpp）
 *
 *  被下面三处代码共用：
 *      源码/安装程序模板.cpp     安装程序（向导界面 + 解压安装）
 *      源码/卸载程序模板.cpp     卸载程序
 *      构建安装包.cpp            构建器（打包 + 调用编译器生成安装包）
 *
 *  约定：
 *      * 界面、注册表、命令行、文件路径一律使用 UTF-16 的 std::wstring，
 *        避免中文在不同代码页下被破坏；
 *      * 只有 ZIP 条目名与资源文本（用户协议、安装信息）使用 UTF-8 的 std::string；
 *      * 编译安装包时必须打开 UNICODE/_UNICODE（构建器会自动加上）。
 * ==========================================================================*/
#ifndef AEPY_INSTALL_COMMON_H
#define AEPY_INSTALL_COMMON_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601          /* Windows 7 及以上 */
#endif

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <string>
#include <vector>

namespace aepy {

/* ---------------------------------------------------------------- 资源 ID
 * 构建器生成的 .rc 与这里的取值必须一致。 */
enum ResourceId {
    AEPY_RES_ICON        = 101,    /* 主图标（RT_GROUP_ICON）          */
    AEPY_RES_MANIFEST    = 1,      /* 应用程序清单（RT_MANIFEST 固定 1） */
    AEPY_RES_PAYLOAD     = 1000,   /* 打包后的文件内容（RCDATA，ZIP）    */
    AEPY_RES_AGREEMENT   = 1001,   /* 用户协议（RCDATA，UTF-8 文本）     */
    AEPY_RES_META        = 1002,   /* 安装信息（RCDATA，UTF-8 KEY=VALUE）*/
    AEPY_RES_UNINSTALLER = 1003,   /* 卸载程序字节（RCDATA，exe）        */
};

/* ---------------------------------------------------------------- ZIP 常量 */
enum ZipConst {
    ZIP_SIG_LOCAL   = 0x04034b50,
    ZIP_SIG_CENTRAL = 0x02014b50,
    ZIP_SIG_EOCD    = 0x06054b50,
    ZIP_METHOD_STORE = 0,
    ZIP_FLAG_UTF8   = 0x0800,
};

#pragma pack(push, 1)
struct ZipLocalHeader {
    uint32_t sig;
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t method;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compSize;
    uint32_t uncompSize;
    uint16_t nameLen;
    uint16_t extraLen;
};
struct ZipCentralHeader {
    uint32_t sig;
    uint16_t versionMadeBy;
    uint16_t versionNeeded;
    uint16_t flags;
    uint16_t method;
    uint16_t modTime;
    uint16_t modDate;
    uint32_t crc32;
    uint32_t compSize;
    uint32_t uncompSize;
    uint16_t nameLen;
    uint16_t extraLen;
    uint16_t commentLen;
    uint16_t diskStart;
    uint16_t internalAttr;
    uint32_t externalAttr;
    uint32_t localOffset;
};
struct ZipEndOfCentralDir {
    uint32_t sig;
    uint16_t diskNum;
    uint16_t cdDiskNum;
    uint16_t entriesThisDisk;
    uint16_t entriesTotal;
    uint32_t cdSize;
    uint32_t cdOffset;
    uint16_t commentLen;
};
#pragma pack(pop)

/* ------------------------------------------------------------------ 字符串 */
inline std::wstring Utf8ToWide(const char* s, size_t len)
{
    if (!s || len == 0) return std::wstring();
    int need = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s, (int)len, &out[0], need);
    return out;
}
inline std::wstring Utf8ToWide(const std::string& s) { return Utf8ToWide(s.data(), s.size()); }
inline std::wstring Utf8ToWide(const char* s)       { return s ? Utf8ToWide(s, strlen(s)) : std::wstring(); }

inline std::string WideToUtf8(const wchar_t* s, size_t len)
{
    if (!s || len == 0) return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, s, (int)len, NULL, 0, NULL, NULL);
    if (need <= 0) return std::string();
    std::string out((size_t)need, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, (int)len, &out[0], need, NULL, NULL);
    return out;
}
inline std::string WideToUtf8(const std::wstring& s) { return WideToUtf8(s.c_str(), s.size()); }

/* 按系统 ANSI 代码页解码（老编辑器保存的 GBK 文本） */
inline std::wstring AnsiToWide(const char* s, size_t len)
{
    if (!s || len == 0) return std::wstring();
    int need = MultiByteToWideChar(CP_ACP, 0, s, (int)len, NULL, 0);
    if (need <= 0) return std::wstring();
    std::wstring out((size_t)need, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, (int)len, &out[0], need);
    return out;
}

/* 自动识别 UTF-8 / GBK / UTF-16LE 文本（带 BOM 时按 BOM 判断） */
inline std::wstring DecodeTextAuto(const std::string& raw)
{
    if (raw.size() >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF)
        return Utf8ToWide(raw.data() + 3, raw.size() - 3);

    if (raw.size() >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE)
        return std::wstring((const wchar_t*)(raw.data() + 2), (raw.size() - 2) / sizeof(wchar_t));

    /* 判断是否为合法 UTF-8 */
    bool utf8 = true;
    for (size_t i = 0; i < raw.size() && utf8; ) {
        unsigned char c = (unsigned char)raw[i];
        int extra = 0;
        if (c < 0x80)       { extra = 0; }
        else if (c >= 0xC2 && c <= 0xDF) { extra = 1; }
        else if (c >= 0xE0 && c <= 0xEF) { extra = 2; }
        else if (c >= 0xF0 && c <= 0xF4) { extra = 3; }
        else { utf8 = false; break; }
        if (i + (size_t)extra >= raw.size()) { utf8 = false; break; }
        for (int k = 1; k <= extra; ++k)
            if (((unsigned char)raw[i + k] & 0xC0) != 0x80) { utf8 = false; break; }
        i += (size_t)extra + 1;
    }
    if (utf8) return Utf8ToWide(raw);
    return AnsiToWide(raw.data(), raw.size());
}

inline std::wstring FormatW(const wchar_t* fmt, ...)
{
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vswprintf(buf, 2047, fmt, ap);
    va_end(ap);
    buf[2047] = L'\0';
    return std::wstring(buf);
}

inline wchar_t LowerCh(wchar_t c) { return (wchar_t)towlower(c); }

inline std::wstring TrimW(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) ++b;
    while (e > b && (s[e-1] == L' ' || s[e-1] == L'\t' || s[e-1] == L'\r' || s[e-1] == L'\n')) --e;
    return s.substr(b, e - b);
}
inline bool StartsWithW(const std::wstring& s, const std::wstring& p)
{
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}
inline bool StartsWithIgnoreCaseW(const std::wstring& s, const std::wstring& p)
{
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
        if (LowerCh(s[i]) != LowerCh(p[i])) return false;
    return true;
}
inline bool EndsWithIgnoreCaseW(const std::wstring& s, const std::wstring& p)
{
    if (s.size() < p.size()) return false;
    return StartsWithIgnoreCaseW(s.substr(s.size() - p.size()), p);
}
inline bool EqualsIgnoreCaseW(const std::wstring& a, const std::wstring& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (LowerCh(a[i]) != LowerCh(b[i])) return false;
    return true;
}

/* ------------------------------------------------------------------ 路径 */
inline bool IsSepW(wchar_t c) { return c == L'\\' || c == L'/'; }

inline std::wstring TrimTrailingSepsW(std::wstring p)
{
    while (p.size() > 1 && IsSepW(p[p.size() - 1])) {
        if (p.size() == 3 && p[1] == L':') break;   /* 保留 "C:\" */
        p.erase(p.size() - 1);
    }
    return p;
}
inline std::wstring PathJoinW(const std::wstring& a, const std::wstring& b)
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    std::wstring r = a;
    if (!IsSepW(r[r.size() - 1])) r += L'\\';
    size_t i = 0;
    while (i < b.size() && IsSepW(b[i])) ++i;
    r.append(b, i, std::wstring::npos);
    return r;
}
inline std::wstring PathDirW(const std::wstring& path)
{
    std::wstring p = TrimTrailingSepsW(path);
    if (p.empty()) return std::wstring();
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring();
    if (pos == 2 && p.size() > 2 && p[1] == L':') return p.substr(0, 3);   /* "C:\" */
    if (pos == 0) return p.substr(0, 1);                                   /* 根 "/" */
    return p.substr(0, pos);
}
inline std::wstring PathNameW(const std::wstring& path)
{
    std::wstring p = TrimTrailingSepsW(path);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return p;
    return p.substr(pos + 1);
}
inline std::wstring PathExtLowerW(const std::wstring& path)
{
    std::wstring name = PathNameW(path);
    size_t pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos) return std::wstring();
    std::wstring ext = name.substr(pos);
    for (size_t i = 0; i < ext.size(); ++i) ext[i] = LowerCh(ext[i]);
    return ext;
}
inline std::wstring SlashesToBackW(std::wstring p)
{
    for (size_t i = 0; i < p.size(); ++i) if (p[i] == L'/') p[i] = L'\\';
    return p;
}
inline std::wstring SlashesToFwdW(std::wstring p)
{
    for (size_t i = 0; i < p.size(); ++i) if (p[i] == L'\\') p[i] = L'/';
    return p;
}
inline std::wstring FullPathW(const std::wstring& path)
{
    if (path.empty()) return path;
    DWORD need = GetFullPathNameW(path.c_str(), 0, NULL, NULL);
    if (need == 0) return path;
    std::wstring buf((size_t)need, L'\0');
    DWORD n = GetFullPathNameW(path.c_str(), need, &buf[0], NULL);
    if (n == 0) return path;
    buf.resize(n);
    return buf;
}
/* path 是否就是 root 或位于 root 之内（忽略大小写） */
inline bool IsUnderOrEqualW(const std::wstring& path, const std::wstring& root)
{
    if (path.empty() || root.empty()) return false;
    std::wstring a = TrimTrailingSepsW(FullPathW(path));
    std::wstring b = TrimTrailingSepsW(FullPathW(root));
    if (a.size() < b.size()) return false;
    if (_wcsnicmp(a.c_str(), b.c_str(), b.size()) != 0) return false;
    return a.size() == b.size() || IsSepW(a[b.size()]);
}

/* ------------------------------------------------------------------ 文件 */
inline bool FileExistsW(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) == 0;
}
inline bool DirExistsW(const std::wstring& path)
{
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

inline bool ReadWholeFileW(const std::wstring& path, std::string& out)
{
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) { CloseHandle(h); return false; }
    out.resize((size_t)size.QuadPart);
    size_t done = 0;
    while (done < out.size()) {
        DWORD chunk = (DWORD)std::min<size_t>(out.size() - done, 1u << 20);
        DWORD got = 0;
        if (!ReadFile(h, &out[done], chunk, &got, NULL) || got == 0) { CloseHandle(h); return false; }
        done += got;
    }
    CloseHandle(h);
    return true;
}

inline bool WriteWholeFileW(const std::wstring& path, const void* data, size_t size)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    const char* p = (const char*)data;
    size_t done = 0;
    while (done < size) {
        DWORD chunk = (DWORD)std::min<size_t>(size - done, 1u << 20);
        DWORD wrote = 0;
        if (!WriteFile(h, p + done, chunk, &wrote, NULL) || wrote == 0) { CloseHandle(h); return false; }
        done += wrote;
    }
    CloseHandle(h);
    return true;
}

/* 逐级创建目录（已存在也算成功） */
inline bool EnsureDirW(const std::wstring& dir)
{
    if (dir.empty()) return false;
    if (DirExistsW(dir)) return true;
    std::wstring parent = PathDirW(dir);
    if (!parent.empty() && !DirExistsW(parent) && !EnsureDirW(parent)) return false;
    if (CreateDirectoryW(dir.c_str(), NULL)) return true;
    return DirExistsW(dir);
}

inline bool IsDirEmptyW(const std::wstring& dir)
{
    std::wstring pat = PathJoinW(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    bool empty = true;
    do {
        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0) { empty = false; break; }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return empty;
}

/* 删除目录内容；includeSelf = true 时连目录本身一起删 */
inline bool DeleteDirTreeW(const std::wstring& dir, bool includeSelf)
{
    std::wstring pat = PathJoinW(dir, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            std::wstring child = PathJoinW(dir, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                DeleteDirTreeW(child, true);
            } else {
                SetFileAttributesW(child.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(child.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    if (includeSelf) {
        SetFileAttributesW(dir.c_str(), FILE_ATTRIBUTE_NORMAL);
        return RemoveDirectoryW(dir.c_str()) != 0;
    }
    return true;
}

/* 目标目录（或它最近的已存在上级）是否可写 —— 用来判断要不要提权 */
inline bool CanWriteInDirW(const std::wstring& dir)
{
    std::wstring probe = TrimTrailingSepsW(dir);
    int guard = 0;
    while (!probe.empty() && !DirExistsW(probe) && guard++ < 64) {
        std::wstring parent = PathDirW(probe);
        if (parent.empty() || parent == probe) return false;
        probe = parent;
    }
    if (probe.empty()) return false;
    std::wstring test = PathJoinW(probe, L".aepy_write_test.tmp");
    HANDLE h = CreateFileW(test.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

/* ---------------------------------------------------------------- CRC32 */
inline const uint32_t* Crc32Table()
{
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    return table;
}
inline uint32_t Crc32Begin() { return 0xFFFFFFFFu; }
inline uint32_t Crc32Update(uint32_t crc, const void* data, size_t size)
{
    const uint8_t* p = (const uint8_t*)data;
    const uint32_t* t = Crc32Table();
    for (size_t i = 0; i < size; ++i) crc = t[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}
inline uint32_t Crc32End(uint32_t crc) { return crc ^ 0xFFFFFFFFu; }
inline uint32_t Crc32(const void* data, size_t size) { return Crc32End(Crc32Update(Crc32Begin(), data, size)); }

/* ------------------------------------------------------------ 模块与资源 */
inline std::wstring ModulePathW()
{
    std::wstring buf(1024, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(NULL, &buf[0], (DWORD)buf.size());
        if (n == 0) return std::wstring();
        if (n < buf.size() - 1) { buf.resize(n); return buf; }
        if (buf.size() >= 32768) { buf.resize(n); return buf; }
        buf.resize(buf.size() * 2);
    }
}

inline bool LoadResourceBytes(HMODULE mod, int id, const void** data, size_t* size)
{
    HRSRC r = FindResourceW(mod, MAKEINTRESOURCEW((WORD)id), MAKEINTRESOURCEW(10 /*RT_RCDATA*/));
    if (!r) return false;
    HGLOBAL g = LoadResource(mod, r);
    if (!g) return false;
    void* p = LockResource(g);
    DWORD n = SizeofResource(mod, r);
    if (!p || n == 0) return false;
    *data = p;
    *size = (size_t)n;
    return true;
}

/* -------------------------------------------------------------- 安装信息 */
struct Meta {
    std::wstring appName;        /* 应用名，如 AEpy                    */
    std::wstring packageName;    /* 安装包名，如 AEpython安装包         */
    std::wstring version;        /* 版本号，如 1.0.0                   */
    std::wstring mainExe;        /* 主程序（相对安装目录），如 AEpy.exe  */
    std::wstring publisher;      /* 发布者                             */
    std::wstring folderName;     /* 默认安装子目录名，如 AEpy           */

    std::wstring shortcutName() const { return appName + L".lnk"; }
    std::wstring uninstallerName() const { return L"卸载" + appName + L".exe"; }
    Meta() : appName(L"AEpy"), packageName(L"AEpython安装包"), version(L"1.0.0"),
             mainExe(L"AEpy.exe"), publisher(L"AEpy"), folderName(L"AEpy") {}
};

/* 解析 "键=值" 形式的安装信息文本 */
inline Meta ParseMetaText(const char* data, size_t size)
{
    Meta m;
    std::wstring text = DecodeTextAuto(std::string(data, size));
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        std::wstring line = TrimW(text.substr(pos, nl == std::wstring::npos ? std::wstring::npos : nl - pos));
        if (!line.empty() && line[0] != L'#') {
            size_t eq = line.find(L'=');
            if (eq != std::wstring::npos) {
                std::wstring key = TrimW(line.substr(0, eq));
                std::wstring val = TrimW(line.substr(eq + 1));
                if (key == L"应用名")          m.appName = val;
                else if (key == L"安装包名")   m.packageName = val;
                else if (key == L"版本")       m.version = val;
                else if (key == L"主程序")     m.mainExe = val;
                else if (key == L"发布者")     m.publisher = val;
                else if (key == L"安装目录名") m.folderName = val;
            }
        }
        if (nl == std::wstring::npos) break;
        pos = nl + 1;
    }
    if (m.appName.empty())     m.appName = L"AEpy";
    if (m.packageName.empty()) m.packageName = m.appName + L"安装包";
    if (m.publisher.empty())   m.publisher = m.appName;
    if (m.folderName.empty())  m.folderName = m.appName;
    if (m.mainExe.empty())     m.mainExe = m.appName + L".exe";
    return m;
}

/* ------------------------------------------------------------------ 注册表 */
inline bool RegWriteStringW(HKEY root, const std::wstring& sub, const std::wstring& name,
                            const std::wstring& value)
{
    HKEY key = NULL;
    if (RegCreateKeyExW(root, sub.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;
    LSTATUS st = RegSetValueExW(key, name.c_str(), 0, REG_SZ,
                                (const BYTE*)value.c_str(),
                                (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}
inline bool RegWriteDwordW(HKEY root, const std::wstring& sub, const std::wstring& name, DWORD value)
{
    HKEY key = NULL;
    if (RegCreateKeyExW(root, sub.c_str(), 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;
    LSTATUS st = RegSetValueExW(key, name.c_str(), 0, REG_DWORD, (const BYTE*)&value, sizeof(value));
    RegCloseKey(key);
    return st == ERROR_SUCCESS;
}
inline bool RegRemoveTreeW(HKEY root, const std::wstring& sub)
{
    return RegDeleteTreeW(root, sub.c_str()) == ERROR_SUCCESS;
}

/* --------------------------------------------------------------- 权限相关 */
inline bool IsProcessElevated()
{
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION info;
    DWORD size = 0;
    bool elevated = false;
    if (GetTokenInformation(token, TokenElevation, &info, sizeof(info), &size))
        elevated = info.TokenIsElevated != 0;
    CloseHandle(token);
    return elevated;
}

/* 用管理员权限重新启动自己；成功返回 true（调用方应立即退出） */
inline bool RelaunchElevatedW(const std::wstring& exePath, const std::wstring& params,
                              bool waitForExit = false)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb       = L"runas";
    sei.lpFile       = exePath.c_str();
    sei.lpParameters = params.empty() ? NULL : params.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) return false;
    if (waitForExit && sei.hProcess) WaitForSingleObject(sei.hProcess, INFINITE);
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return true;
}

/* --------------------------------------------------------------- 命令行/启动 */
inline std::vector<std::wstring> CommandLineArgsW()
{
    std::vector<std::wstring> args;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    LocalFree(argv);
    return args;
}
inline bool HasArgW(const std::vector<std::wstring>& args, const std::wstring& flag)
{
    for (size_t i = 0; i < args.size(); ++i) if (EqualsIgnoreCaseW(args[i], flag)) return true;
    return false;
}
inline std::wstring ArgValueW(const std::vector<std::wstring>& args, const std::wstring& flag)
{
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (EqualsIgnoreCaseW(args[i], flag)) return args[i + 1];
    return std::wstring();
}
/* 0/1/不填 解析成布尔 */
inline bool ArgBoolW(const std::vector<std::wstring>& args, const std::wstring& flag, bool def)
{
    std::wstring v = ArgValueW(args, flag);
    if (v.empty()) return def;
    return !(v == L"0" || EqualsIgnoreCaseW(v, L"false") || EqualsIgnoreCaseW(v, L"no"));
}

inline bool ShellOpenW(const std::wstring& file, const std::wstring& params, const std::wstring& workDir)
{
    SHELLEXECUTEINFOW sei;
    ZeroMemory(&sei, sizeof(sei));
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOASYNC;
    sei.lpVerb       = L"open";
    sei.lpFile       = file.c_str();
    sei.lpParameters = params.empty() ? NULL : params.c_str();
    sei.lpDirectory  = workDir.empty() ? NULL : workDir.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

/* 借用 explorer.exe 以“普通用户权限”启动程序（安装器提权后仍以普通权限打开主程序） */
inline bool ShellOpenUnelevatedW(const std::wstring& file, const std::wstring& workDir)
{
    wchar_t winDir[MAX_PATH] = {0};
    GetWindowsDirectoryW(winDir, MAX_PATH);
    std::wstring explorer = PathJoinW(winDir, L"explorer.exe");
    std::wstring full = FullPathW(file);
    std::wstring params = L"\"" + full + L"\"";
    (void)workDir;
    return ShellOpenW(explorer, params, std::wstring());
}

inline std::wstring TempDirW()
{
    wchar_t buf[MAX_PATH + 2] = {0};
    DWORD n = GetTempPathW(MAX_PATH, buf);
    if (n == 0) return L"C:\\Windows\\Temp\\";
    return std::wstring(buf, n);
}

inline std::wstring ProgramFilesDirW(bool x86)
{
    wchar_t buf[MAX_PATH + 2] = {0};
    int csidl = x86 ? CSIDL_PROGRAM_FILESX86 : CSIDL_PROGRAM_FILES;
    if (SUCCEEDED(SHGetFolderPathW(NULL, csidl | CSIDL_FLAG_CREATE, NULL, 0, buf)))
        return TrimTrailingSepsW(buf);
    std::wstring env = x86 ? L"ProgramFiles(x86)" : L"ProgramFiles";
    wchar_t val[MAX_PATH + 2] = {0};
    if (GetEnvironmentVariableW(env.c_str(), val, MAX_PATH) > 0) return TrimTrailingSepsW(val);
    return x86 ? L"C:\\Program Files (x86)" : L"C:\\Program Files";
}

} /* namespace aepy */

#endif /* AEPY_INSTALL_COMMON_H */
