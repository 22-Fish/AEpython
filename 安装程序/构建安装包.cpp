/* ============================================================================
 *  构建安装包.cpp —— 构建器（把整个项目打成安装包）
 *
 *  运行「构建安装包.exe」（和 build.json 放在同一个文件夹里）会：
 *      1. 读取 build.json：安装包名、源目录、输出目录、排除打包的目录/文件；
 *      2. 读取 用户协议.JSON，取出协议正文；
 *      3. 按“排除打包”过滤后，把源目录里的所有文件打成 ZIP（存储方式，不压缩）；
 *      4. 生成资源脚本（图标 / 清单 / 版本信息 / ZIP / 协议 / 安装信息 / 卸载程序）；
 *      5. 用 g++ 编译出「卸载程序.exe」，再把它一起塞进安装程序；
 *      6. 输出 <安装包名>.exe 到 build.json 里的“输出目录”。
 *
 *  为什么要在临时目录里编译：
 *      MinGW 的链接器/资源编译器不能处理中文路径，所以所有编译输入输出
 *      都会放到 %TEMP% 下的英文目录里，编译完再把 exe 复制回目标位置。
 *
 *  命令行：
 *      --keep      保留临时目录（排查构建问题时用）
 *      --keep-zip  把打包好的 payload.zip 复制一份到输出目录旁边
 *      --help      显示帮助
 * ==========================================================================*/
#include "源码/安装包公共.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace aepy;

/* ============================================================ 控制台输出 === */

static void Out(const std::wstring& s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE && WriteConsoleW(h, s.c_str(), (DWORD)s.size(), NULL, NULL))
        return;
    /* 重定向到文件/管道时 WriteConsoleW 会失败，退回 UTF-8 字节输出 */
    std::string utf8 = WideToUtf8(s);
    fwrite(utf8.c_str(), 1, utf8.size(), stdout);
}
static void OutLine(const std::wstring& s) { Out(s + L"\r\n"); }

static bool PathIsAbsolute(const std::wstring& p)
{
    if (p.size() >= 2 && p[1] == L':') return true;
    if (p.size() >= 2 && IsSepW(p[0]) && IsSepW(p[1])) return true;
    return false;
}

static unsigned long long FileSizeW(const std::wstring& path)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 0;
    LARGE_INTEGER sz;
    sz.QuadPart = 0;
    GetFileSizeEx(h, &sz);
    CloseHandle(h);
    return (unsigned long long)sz.QuadPart;
}

/* ============================================================ 简易 JSON ==== */
namespace jx {

struct Value {
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool boolean = false;
    double number = 0;
    std::wstring str;
    std::vector<Value> arr;
    std::vector<std::pair<std::wstring, Value> > obj;

    const Value* find(const std::wstring& key) const
    {
        if (type != Object) return NULL;
        for (size_t i = 0; i < obj.size(); ++i)
            if (EqualsIgnoreCaseW(obj[i].first, key)) return &obj[i].second;
        return NULL;
    }
    std::wstring strOr(const std::wstring& def) const
    {
        return (type == String && !str.empty()) ? str : def;
    }
};

class Parser {
public:
    Parser(const std::wstring& text) : s_(text), i_(0) {}

    bool parse(Value& out)
    {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        if (i_ < s_.size()) return fail(L"多余的内容");
        return true;
    }
    const std::wstring& error() const { return err_; }

private:
    const std::wstring& s_;
    size_t i_;
    std::wstring err_;

    void skipWs()
    {
        while (i_ < s_.size()) {
            wchar_t c = s_[i_];
            if (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n') { ++i_; continue; }
            if (c == L'/' && i_ + 1 < s_.size() && s_[i_ + 1] == L'/') {     /* // 注释 */
                while (i_ < s_.size() && s_[i_] != L'\n') ++i_;
                continue;
            }
            break;
        }
    }

    bool fail(const std::wstring& msg)
    {
        err_ = FormatW(L"第 %d 个字符附近：%ls", (int)i_ + 1, msg.c_str());
        return false;
    }

    bool parseValue(Value& out)
    {
        if (i_ >= s_.size()) return fail(L"内容意外结束");
        wchar_t c = s_[i_];
        if (c == L'{') return parseObject(out);
        if (c == L'[') return parseArray(out);
        if (c == L'"') { out.type = Value::String; return parseString(out.str); }
        if (c == L't' || c == L'f') return parseBool(out);
        if (c == L'n') {
            if (s_.compare(i_, 4, L"null") == 0) { i_ += 4; out.type = Value::Null; return true; }
            return fail(L"无法识别的值");
        }
        return parseNumber(out);
    }

    bool parseBool(Value& out)
    {
        if (s_.compare(i_, 4, L"true") == 0)  { i_ += 4; out.type = Value::Bool; out.boolean = true;  return true; }
        if (s_.compare(i_, 5, L"false") == 0) { i_ += 5; out.type = Value::Bool; out.boolean = false; return true; }
        return fail(L"无法识别的值");
    }

    bool parseNumber(Value& out)
    {
        size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == L'-' || s_[i_] == L'+')) ++i_;
        bool any = false;
        while (i_ < s_.size() && ((s_[i_] >= L'0' && s_[i_] <= L'9') || s_[i_] == L'.' ||
                                  s_[i_] == L'e' || s_[i_] == L'E' || s_[i_] == L'-' || s_[i_] == L'+')) {
            any = true;
            ++i_;
        }
        if (!any) return fail(L"不是合法的数字");
        out.type = Value::Number;
        out.number = wcstod(s_.substr(start, i_ - start).c_str(), NULL);
        return true;
    }

    bool parseString(std::wstring& out)
    {
        out.clear();
        if (i_ >= s_.size() || s_[i_] != L'"') return fail(L"缺少字符串引号");
        ++i_;
        while (i_ < s_.size()) {
            wchar_t c = s_[i_++];
            if (c == L'"') return true;
            if (c != L'\\') { out += c; continue; }
            if (i_ >= s_.size()) return fail(L"转义不完整");
            wchar_t e = s_[i_++];
            switch (e) {
                case L'"':  out += L'"';  break;
                case L'\\': out += L'\\'; break;
                case L'/':  out += L'/';  break;
                case L'b':  out += L'\b'; break;
                case L'f':  out += L'\f'; break;
                case L'n':  out += L'\n'; break;
                case L'r':  out += L'\r'; break;
                case L't':  out += L'\t'; break;
                case L'u': {
                    if (i_ + 4 > s_.size()) return fail(L"\\u 转义不完整");
                    unsigned code = (unsigned)wcstoul(s_.substr(i_, 4).c_str(), NULL, 16);
                    i_ += 4;
                    if (code >= 0xD800 && code <= 0xDBFF && i_ + 6 <= s_.size() &&
                        s_[i_] == L'\\' && s_[i_ + 1] == L'u') {
                        unsigned low = (unsigned)wcstoul(s_.substr(i_ + 2, 4).c_str(), NULL, 16);
                        if (low >= 0xDC00 && low <= 0xDFFF) {
                            i_ += 6;
                            unsigned cp = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                            if (cp <= 0x10FFFF) {
                                out += (wchar_t)(0xD800 + ((cp - 0x10000) >> 10));
                                out += (wchar_t)(0xDC00 + ((cp - 0x10000) & 0x3FF));
                                break;
                            }
                        }
                    }
                    out += (wchar_t)code;
                    break;
                }
                default: return fail(L"不支持的转义字符");
            }
        }
        return fail(L"字符串没有结束引号");
    }

    bool parseArray(Value& out)
    {
        out.type = Value::Array;
        ++i_;                                 /* [ */
        skipWs();
        if (i_ < s_.size() && s_[i_] == L']') { ++i_; return true; }
        for (;;) {
            skipWs();
            Value v;
            if (!parseValue(v)) return false;
            out.arr.push_back(v);
            skipWs();
            if (i_ < s_.size() && s_[i_] == L',') {
                ++i_;
                skipWs();
                if (i_ < s_.size() && s_[i_] == L']') { ++i_; return true; }  /* 容忍尾随逗号 */
                continue;
            }
            if (i_ < s_.size() && s_[i_] == L']') { ++i_; return true; }
            return fail(L"数组缺少 , 或 ]");
        }
    }

    bool parseObject(Value& out)
    {
        out.type = Value::Object;
        ++i_;                                 /* { */
        skipWs();
        if (i_ < s_.size() && s_[i_] == L'}') { ++i_; return true; }
        for (;;) {
            skipWs();
            std::wstring key;
            if (!parseString(key)) return false;
            skipWs();
            if (i_ >= s_.size() || s_[i_] != L':') return fail(L"缺少冒号");
            ++i_;
            skipWs();
            Value v;
            if (!parseValue(v)) return false;
            out.obj.push_back(std::make_pair(key, v));
            skipWs();
            if (i_ < s_.size() && s_[i_] == L',') {
                ++i_;
                skipWs();
                if (i_ < s_.size() && s_[i_] == L'}') { ++i_; return true; }  /* 容忍尾随逗号 */
                continue;
            }
            if (i_ < s_.size() && s_[i_] == L'}') { ++i_; return true; }
            return fail(L"对象缺少 , 或 }");
        }
    }
};

inline bool Parse(const std::wstring& text, Value& out, std::wstring* errDetail = NULL)
{
    Parser p(text);
    bool ok = p.parse(out);
    if (!ok && errDetail) *errDetail = p.error();
    return ok;
}

} /* namespace jx */

/* ============================================================ 通配符匹配 == */

/*  *  匹配任意多个字符（不含 /）      **  匹配任意多个字符（含 /）
 *  ?  匹配一个字符（不含 /）          其余字符按字面比较（忽略大小写）      */
static bool GlobMatchAt(const std::wstring& p, size_t pi, const std::wstring& t, size_t ti)
{
    while (pi < p.size()) {
        wchar_t c = p[pi];
        if (c == L'*') {
            bool dbl = (pi + 1 < p.size() && p[pi + 1] == L'*');
            size_t next = pi + (dbl ? 2 : 1);
            size_t candidates[2] = { next, 0 };
            int count = 1;
            if (dbl && next < p.size() && p[next] == L'/') { candidates[1] = next + 1; count = 2; }
            for (int ci = 0; ci < count; ++ci) {
                for (size_t k = ti; ; ++k) {
                    if (GlobMatchAt(p, candidates[ci], t, k)) return true;
                    if (k >= t.size()) break;
                    if (!dbl && t[k] == L'/') break;
                }
            }
            return false;
        }
        if (c == L'?') {
            if (ti >= t.size() || t[ti] == L'/') return false;
            ++pi; ++ti;
            continue;
        }
        if (ti >= t.size() || LowerCh(c) != LowerCh(t[ti])) return false;
        ++pi; ++ti;
    }
    return ti == t.size();
}

static std::wstring NormalizePattern(const std::wstring& raw)
{
    std::wstring s = SlashesToFwdW(TrimW(raw));
    while (s.size() >= 2 && s[0] == L'.' && s[1] == L'/') s.erase(0, 2);
    while (!s.empty() && s[0] == L'/') s.erase(0, 1);
    while (!s.empty() && s[s.size() - 1] == L'/') s.erase(s.size() - 1);
    return s;
}

/* rel 使用 / 分隔、相对于源目录；命中任何一个模式就排除 */
static bool IsExcluded(const std::wstring& rel, const std::vector<std::wstring>& patterns)
{
    if (rel.empty()) return false;
    for (size_t i = 0; i < patterns.size(); ++i) {
        const std::wstring& p = patterns[i];
        if (p.empty()) continue;
        bool hasWildcard = (p.find_first_of(L"*?") != std::wstring::npos);
        if (!hasWildcard) {
            if (EqualsIgnoreCaseW(rel, p)) return true;
            if (rel.size() > p.size() && rel[p.size()] == L'/' && StartsWithIgnoreCaseW(rel, p))
                return true;
            continue;
        }
        if (GlobMatchAt(p, 0, rel, 0)) return true;
        for (size_t k = 1; k < rel.size(); ++k) {           /* 模式也可以只命中某一级目录 */
            if (rel[k] != L'/') continue;
            if (GlobMatchAt(p, 0, rel.substr(0, k), 0)) return true;
        }
    }
    return false;
}

/* ============================================================ 打包 ======== */

struct ZipItem {
    std::wstring abs;
    std::wstring rel;        /* 使用 / 分隔 */
    bool isDir = false;
};

struct ScanStats {
    int files = 0;
    int dirs = 0;
    int skipped = 0;
    int skippedJunctions = 0;
    size_t bytes = 0;
};

static std::wstring RelJoin(const std::wstring& prefix, const std::wstring& name)
{
    if (prefix.empty()) return name;
    return prefix + L"/" + name;
}

static bool ScanDir(const std::wstring& absDir, const std::wstring& relDir,
                    const std::vector<std::wstring>& patterns,
                    std::vector<ZipItem>& items, ScanStats& stats, std::wstring& err)
{
    std::vector<std::wstring> dirs, files;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(PathJoinW(absDir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return true;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        std::wstring rel = RelJoin(relDir, name);
        bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (IsExcluded(rel, patterns)) { ++stats.skipped; continue; }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) { ++stats.skippedJunctions; continue; }
        if (isDir) dirs.push_back(name);
        else       files.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);

    std::sort(dirs.begin(), dirs.end());
    std::sort(files.begin(), files.end());

    for (size_t i = 0; i < dirs.size(); ++i) {
        std::wstring absChild = PathJoinW(absDir, dirs[i]);
        std::wstring relChild = RelJoin(relDir, dirs[i]);
        size_t before = items.size();
        if (!ScanDir(absChild, relChild, patterns, items, stats, err)) return false;
        if (items.size() == before) {                 /* 空目录也要建出来 */
            ZipItem it;
            it.abs = absChild;
            it.rel = relChild;
            it.isDir = true;
            items.push_back(it);
            ++stats.dirs;
        }
    }
    for (size_t i = 0; i < files.size(); ++i) {
        std::wstring absChild = PathJoinW(absDir, files[i]);
        ZipItem it;
        it.abs = absChild;
        it.rel = RelJoin(relDir, files[i]);
        it.isDir = false;
        items.push_back(it);
        ++stats.files;
        LARGE_INTEGER sz;
        sz.QuadPart = 0;
        HANDLE fh = CreateFileW(absChild.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                                OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (fh != INVALID_HANDLE_VALUE) {
            GetFileSizeEx(fh, &sz);
            CloseHandle(fh);
        } else {
            err = L"无法读取文件：" + absChild;
            return false;
        }
        stats.bytes += (size_t)sz.QuadPart;
    }
    return true;
}

/* --------------------------------------------------------------- ZIP 写入 */
class ZipWriter {
public:
    explicit ZipWriter(const std::wstring& path) : path_(path) {}
    ~ZipWriter() { close(); }

    bool open(std::wstring& err)
    {
        h_ = CreateFileW(path_.c_str(), GENERIC_WRITE | GENERIC_READ, 0, NULL,
                         CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h_ == INVALID_HANDLE_VALUE) { err = L"无法创建 ZIP 文件：" + path_; return false; }
        return true;
    }

    bool addFile(const std::wstring& absPath, const std::wstring& relPath, std::wstring& err)
    {
        std::string name = WideToUtf8(SlashesToFwdW(relPath));
        if (name.size() > 0xFFFF) { err = L"路径太长：" + relPath; return false; }

        FILETIME ft = FileTimeOf(absPath);
        ZipLocalHeader lh;
        ZeroMemory(&lh, sizeof(lh));
        lh.sig           = ZIP_SIG_LOCAL;
        lh.versionNeeded = 20;
        lh.flags         = ZIP_FLAG_UTF8;
        lh.method        = ZIP_METHOD_STORE;
        FileTimeToDos(ft, lh.modDate, lh.modTime);
        lh.nameLen       = (uint16_t)name.size();
        lh.extraLen      = 0;

        uint64_t headerPos = pos_;
        if (!write(&lh, sizeof(lh), err)) return false;
        if (!write(name.data(), name.size(), err)) return false;

        HANDLE f = CreateFileW(absPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f == INVALID_HANDLE_VALUE) { err = L"无法打开文件：" + absPath; return false; }

        std::vector<char> buf(1 << 20);
        uint32_t crc = Crc32Begin();
        uint64_t total = 0;
        for (;;) {
            DWORD got = 0;
            if (!ReadFile(f, &buf[0], (DWORD)buf.size(), &got, NULL)) {
                CloseHandle(f);
                err = L"读取文件失败：" + absPath;
                return false;
            }
            if (got == 0) break;
            crc = Crc32Update(crc, &buf[0], got);
            if (!write(&buf[0], got, err)) { CloseHandle(f); return false; }
            total += got;
        }
        CloseHandle(f);
        crc = Crc32End(crc);

        if (total > 0xFFFFFFFFull) { err = L"文件超过 4GB，暂不支持：" + absPath; return false; }
        if (!patch32(headerPos + offsetof(ZipLocalHeader, crc32), crc, err)) return false;
        if (!patch32(headerPos + offsetof(ZipLocalHeader, compSize), (uint32_t)total, err)) return false;
        if (!patch32(headerPos + offsetof(ZipLocalHeader, uncompSize), (uint32_t)total, err)) return false;

        Central c;
        ZeroMemory(&c.h, sizeof(c.h));
        c.h.sig           = ZIP_SIG_CENTRAL;
        c.h.versionMadeBy = 0x0014;                 /* MS-DOS/FAT */
        c.h.versionNeeded = 20;
        c.h.flags         = ZIP_FLAG_UTF8;
        c.h.method        = ZIP_METHOD_STORE;
        c.h.modDate       = lh.modDate;
        c.h.modTime       = lh.modTime;
        c.h.crc32         = crc;
        c.h.compSize      = (uint32_t)total;
        c.h.uncompSize    = (uint32_t)total;
        c.h.nameLen       = (uint16_t)name.size();
        c.h.externalAttr  = 0x20;                   /* FILE_ATTRIBUTE_ARCHIVE */
        c.h.localOffset   = (uint32_t)headerPos;
        c.name            = name;
        centrals_.push_back(c);
        return true;
    }

    bool addDir(const std::wstring& absPath, const std::wstring& relPath, std::wstring& err)
    {
        std::string name = WideToUtf8(SlashesToFwdW(relPath) + L"/");
        if (name.size() > 0xFFFF) { err = L"路径太长：" + relPath; return false; }

        FILETIME ft = FileTimeOf(absPath);
        ZipLocalHeader lh;
        ZeroMemory(&lh, sizeof(lh));
        lh.sig           = ZIP_SIG_LOCAL;
        lh.versionNeeded = 20;
        lh.flags         = ZIP_FLAG_UTF8;
        lh.method        = ZIP_METHOD_STORE;
        FileTimeToDos(ft, lh.modDate, lh.modTime);
        lh.nameLen       = (uint16_t)name.size();

        uint64_t headerPos = pos_;
        if (!write(&lh, sizeof(lh), err)) return false;
        if (!write(name.data(), name.size(), err)) return false;

        Central c;
        ZeroMemory(&c.h, sizeof(c.h));
        c.h.sig           = ZIP_SIG_CENTRAL;
        c.h.versionMadeBy = 0x0014;
        c.h.versionNeeded = 20;
        c.h.flags         = ZIP_FLAG_UTF8;
        c.h.method        = ZIP_METHOD_STORE;
        c.h.modDate       = lh.modDate;
        c.h.modTime       = lh.modTime;
        c.h.nameLen       = (uint16_t)name.size();
        c.h.externalAttr  = 0x10;                   /* FILE_ATTRIBUTE_DIRECTORY */
        c.h.localOffset   = (uint32_t)headerPos;
        c.name            = name;
        centrals_.push_back(c);
        return true;
    }

    bool finish(std::wstring& err)
    {
        if (centrals_.size() > 0xFFFF) { err = L"文件数量超过 65535，暂不支持。"; return false; }
        uint64_t cdStart = pos_;
        for (size_t i = 0; i < centrals_.size(); ++i) {
            if (!write(&centrals_[i].h, sizeof(centrals_[i].h), err)) return false;
            if (!write(centrals_[i].name.data(), centrals_[i].name.size(), err)) return false;
        }
        uint64_t cdSize = pos_ - cdStart;
        if (cdStart > 0xFFFFFFFFull || cdSize > 0xFFFFFFFFull) {
            err = L"安装包超过 4GB，暂不支持。";
            return false;
        }
        ZipEndOfCentralDir e;
        ZeroMemory(&e, sizeof(e));
        e.sig           = ZIP_SIG_EOCD;
        e.entriesThisDisk = (uint16_t)centrals_.size();
        e.entriesTotal    = (uint16_t)centrals_.size();
        e.cdSize        = (uint32_t)cdSize;
        e.cdOffset      = (uint32_t)cdStart;
        if (!write(&e, sizeof(e), err)) return false;
        return true;
    }

private:
    struct Central {
        ZipCentralHeader h;
        std::string name;
    };

    std::wstring path_;
    HANDLE h_ = INVALID_HANDLE_VALUE;
    uint64_t pos_ = 0;
    std::vector<Central> centrals_;

    void close()
    {
        if (h_ != INVALID_HANDLE_VALUE) { CloseHandle(h_); h_ = INVALID_HANDLE_VALUE; }
    }

    bool write(const void* data, size_t size, std::wstring& err)
    {
        const char* p = (const char*)data;
        size_t done = 0;
        while (done < size) {
            DWORD chunk = (DWORD)std::min<size_t>(size - done, 1u << 20);
            DWORD wrote = 0;
            if (!WriteFile(h_, p + done, chunk, &wrote, NULL) || wrote == 0) {
                err = L"写入 ZIP 失败。";
                return false;
            }
            done += wrote;
            pos_ += wrote;
        }
        return true;
    }

    bool patch32(uint64_t offset, uint32_t value, std::wstring& err)
    {
        LARGE_INTEGER li;
        li.QuadPart = (LONGLONG)offset;
        if (!SetFilePointerEx(h_, li, NULL, FILE_BEGIN)) { err = L"定位 ZIP 失败。"; return false; }
        DWORD wrote = 0;
        if (!WriteFile(h_, &value, 4, &wrote, NULL) || wrote != 4) { err = L"回填 ZIP 失败。"; return false; }
        li.QuadPart = (LONGLONG)pos_;
        if (!SetFilePointerEx(h_, li, NULL, FILE_BEGIN)) { err = L"定位 ZIP 失败。"; return false; }
        return true;
    }

    static FILETIME FileTimeOf(const std::wstring& path)
    {
        FILETIME ft;
        ZeroMemory(&ft, sizeof(ft));
        WIN32_FILE_ATTRIBUTE_DATA fad;
        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
            ft = fad.ftLastWriteTime;
        return ft;
    }

    static void FileTimeToDos(const FILETIME& ft, uint16_t& outDate, uint16_t& outTime)
    {
        FILETIME local;
        WORD d = 0, t = 0;
        if ((ft.dwLowDateTime || ft.dwHighDateTime) &&
            FileTimeToLocalFileTime(&ft, &local) &&
            FileTimeToDosDateTime(&local, &d, &t)) {
            outDate = d;
            outTime = t;
        } else {
            outDate = (uint16_t)(((1980) << 9) | (1 << 5) | 1);
            outTime = 0;
        }
    }
};

/* ============================================================ 配置 ======== */

struct Config {
    std::wstring baseDir;          /* build.json 所在目录 */
    std::wstring packageName;
    std::wstring sourceDir;
    std::wstring outputDir;
    std::wstring templateDir;
    std::vector<std::wstring> excludes;
    Meta meta;
    std::wstring iconPath;         /* 空 = 不用图标 */
    std::wstring gpp, windres;
};

/* 按先后顺序取第一个存在的字符串字段（用 initializer_list，避免数组长度写错） */
static std::wstring FirstString(const jx::Value& obj, std::initializer_list<const wchar_t*> keys)
{
    for (const wchar_t* k : keys) {
        const jx::Value* v = obj.find(k);
        if (v && v->type == jx::Value::String && !v->str.empty()) return v->str;
    }
    return std::wstring();
}

static bool LoadConfig(const std::wstring& baseDir, const std::wstring& jsonPath,
                       Config& cfg, std::wstring& err)
{
    cfg.baseDir     = baseDir;
    cfg.packageName = L"AEpython安装包";
    cfg.sourceDir   = PathJoinW(baseDir, L"..\\AEpython");
    cfg.outputDir   = baseDir;
    cfg.templateDir = PathJoinW(baseDir, L"源码");

    std::string raw;
    bool hasJson = ReadWholeFileW(jsonPath, raw);
    if (hasJson) {
        jx::Value root;
        std::wstring detail;
        if (!jx::Parse(DecodeTextAuto(raw), root, &detail)) {
            err = L"build.json 不是合法的 JSON（" + detail + L"）：\n" + jsonPath;
            return false;
        }
        const jx::Value* arrObj = &root;

        std::wstring v;
        if (!(v = FirstString(*arrObj, { L"安装包名", L"packageName" })).empty()) cfg.packageName = v;
        if (!(v = FirstString(*arrObj, { L"源目录", L"source" })).empty())
            cfg.sourceDir = PathIsAbsolute(v) ? v : PathJoinW(baseDir, v);
        if (!(v = FirstString(*arrObj, { L"输出目录", L"output" })).empty())
            cfg.outputDir = PathIsAbsolute(v) ? v : PathJoinW(baseDir, v);
        if (!(v = FirstString(*arrObj, { L"模板目录", L"templateDir" })).empty())
            cfg.templateDir = PathIsAbsolute(v) ? v : PathJoinW(baseDir, v);
        if (!(v = FirstString(*arrObj, { L"应用名", L"appName" })).empty())      cfg.meta.appName    = v;
        if (!(v = FirstString(*arrObj, { L"版本", L"version" })).empty())        cfg.meta.version    = v;
        if (!(v = FirstString(*arrObj, { L"主程序", L"mainExe" })).empty())      cfg.meta.mainExe    = v;
        if (!(v = FirstString(*arrObj, { L"发布者", L"publisher" })).empty())    cfg.meta.publisher  = v;
        if (!(v = FirstString(*arrObj, { L"安装目录名", L"folderName" })).empty()) cfg.meta.folderName = v;
        if (!(v = FirstString(*arrObj, { L"图标", L"icon" })).empty())
            cfg.iconPath = PathIsAbsolute(v) ? v : PathJoinW(baseDir, v);
        if (!(v = FirstString(*arrObj, { L"g++", L"gpp" })).empty())     cfg.gpp     = v;
        if (!(v = FirstString(*arrObj, { L"windres" })).empty())         cfg.windres = v;

        const jx::Value* ex = arrObj->find(L"排除打包");
        if (!ex) ex = arrObj->find(L"exclude");
        if (ex && ex->type == jx::Value::Array) {
            for (size_t i = 0; i < ex->arr.size(); ++i) {
                if (ex->arr[i].type != jx::Value::String) continue;
                std::wstring p = NormalizePattern(ex->arr[i].str);
                if (!p.empty()) cfg.excludes.push_back(p);
            }
        }
    }

    cfg.meta.packageName = cfg.packageName;
    if (cfg.meta.appName.empty())   cfg.meta.appName = L"AEpy";
    if (cfg.meta.version.empty())   cfg.meta.version = L"1.0.0";
    if (cfg.meta.mainExe.empty())   cfg.meta.mainExe = cfg.meta.appName + L".exe";
    if (cfg.meta.publisher.empty()) cfg.meta.publisher = cfg.meta.appName;
    if (cfg.meta.folderName.empty()) cfg.meta.folderName = cfg.meta.appName;

    cfg.sourceDir   = TrimTrailingSepsW(FullPathW(cfg.sourceDir));
    cfg.outputDir   = TrimTrailingSepsW(FullPathW(cfg.outputDir));
    cfg.templateDir = TrimTrailingSepsW(FullPathW(cfg.templateDir));
    if (cfg.iconPath.empty()) {
        std::wstring guess = PathJoinW(cfg.sourceDir, L"assets\\AEpy.ico");
        if (FileExistsW(guess)) cfg.iconPath = guess;
    }
    return true;
}

/* ============================================================ 工具函数 ==== */

static std::wstring QuoteArg(const std::wstring& s)
{
    return L"\"" + s + L"\"";
}

static std::wstring FindInPath(const wchar_t* exeName)
{
    std::vector<wchar_t> buf(32768);
    DWORD n = SearchPathW(NULL, exeName, NULL, (DWORD)buf.size(), &buf[0], NULL);
    if (n == 0 || n >= buf.size()) return std::wstring();
    return std::wstring(&buf[0], n);
}

static bool RunTool(const std::wstring& cmdLine, const std::wstring& workDir,
                    const std::wstring& logPath, std::wstring& err)
{
    SECURITY_ATTRIBUTES sa;
    ZeroMemory(&sa, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE hLog = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hLog == INVALID_HANDLE_VALUE) { err = L"无法创建编译日志：" + logPath; return false; }

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput  = hLog;
    si.hStdError   = hLog;
    si.hStdInput   = NULL;

    std::wstring cmd = cmdLine;
    BOOL ok = CreateProcessW(NULL, &cmd[0], NULL, NULL, TRUE, CREATE_NO_WINDOW,
                             NULL, workDir.c_str(), &si, &pi);
    CloseHandle(hLog);
    if (!ok) {
        err = L"无法启动编译器：" + cmdLine;
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    if (code != 0) {
        std::string log;
        ReadWholeFileW(logPath, log);
        std::wstring text = DecodeTextAuto(log);
        if (text.size() > 4000) text = text.substr(text.size() - 4000);
        err = L"编译命令执行失败（退出码 " + FormatW(L"%lu", (unsigned long)code) + L"）：\n" +
              cmdLine + L"\n\n" + text;
        return false;
    }
    return true;
}

/* ============================================================ RC 生成 ===== */

/* 把中文写成 RC 的宽字符串转义（L"\x4E2D..."），生成的 .rc 保持纯 ASCII，
 * 避免资源编译器按错误代码页解释中文。 */
static std::wstring RcWide(const std::wstring& s)
{
    std::wstring out = L"L\"";
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\r') continue;
        if (c == L'\n') { out += L"\\r\\n"; continue; }
        if (c == L'"')  { out += L"\\\"";  continue; }
        if (c == L'\\') { out += L"\\\\";  continue; }
        wchar_t buf[16];
        swprintf(buf, 15, L"\\x%04X", (unsigned)c);
        out += buf;
    }
    out += L"\"";
    return out;
}

static void VersionQuad(const std::wstring& version, int out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0;
    int idx = 0;
    unsigned cur = 0;
    bool any = false;
    for (size_t i = 0; i <= version.size(); ++i) {
        wchar_t c = (i < version.size()) ? version[i] : L'.';
        if (c >= L'0' && c <= L'9') {
            cur = cur * 10 + (unsigned)(c - L'0');
            any = true;
        } else if (c == L'.') {
            if (idx < 4) out[idx] = (int)cur;
            ++idx;
            cur = 0;
            any = false;
            if (idx > 4) break;
        }
    }
    if (idx < 4 && any) out[idx] = (int)cur;
}

static std::wstring MakeVersionInfo(const std::wstring& product, const std::wstring& desc,
                                    const std::wstring& version, const std::wstring& publisher,
                                    const std::wstring& originalName)
{
    int v[4];
    VersionQuad(version, v);
    std::wstring s;
    s += L"VS_VERSION_INFO VERSIONINFO\r\n";
    s += FormatW(L" FILEVERSION %d,%d,%d,%d\r\n", v[0], v[1], v[2], v[3]);
    s += FormatW(L" PRODUCTVERSION %d,%d,%d,%d\r\n", v[0], v[1], v[2], v[3]);
    s += L" FILEFLAGSMASK 0x3fL\r\n";
    s += L" FILEFLAGS 0x0L\r\n";
    s += L" FILEOS 0x40004L\r\n";
    s += L" FILETYPE 0x1L\r\n";
    s += L" FILESUBTYPE 0x0L\r\n";
    s += L"BEGIN\r\n";
    s += L"    BLOCK \"StringFileInfo\"\r\n";
    s += L"    BEGIN\r\n";
    s += L"        BLOCK \"080404B0\"\r\n";
    s += L"        BEGIN\r\n";
    s += FormatW(L"            VALUE \"CompanyName\", %ls\r\n",     RcWide(publisher).c_str());
    s += FormatW(L"            VALUE \"FileDescription\", %ls\r\n", RcWide(desc).c_str());
    s += FormatW(L"            VALUE \"FileVersion\", \"%ls\"\r\n",  version.c_str());
    s += FormatW(L"            VALUE \"InternalName\", %ls\r\n",    RcWide(product).c_str());
    s += FormatW(L"            VALUE \"OriginalFilename\", %ls\r\n", RcWide(originalName).c_str());
    s += FormatW(L"            VALUE \"ProductName\", %ls\r\n",     RcWide(product).c_str());
    s += FormatW(L"            VALUE \"ProductVersion\", \"%ls\"\r\n", version.c_str());
    s += L"        END\r\n";
    s += L"    END\r\n";
    s += L"    BLOCK \"VarFileInfo\"\r\n";
    s += L"    BEGIN\r\n";
    s += L"        VALUE \"Translation\", 0x804, 1200\r\n";
    s += L"    END\r\n";
    s += L"END\r\n";
    return s;
}

static std::wstring MakeManifest(const std::wstring& identity, const std::wstring& desc)
{
    std::wstring s;
    s += L"<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n";
    s += L"<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n";
    s += L"  <assemblyIdentity type=\"win32\" name=\"" + identity +
         L"\" version=\"1.0.0.0\" processorArchitecture=\"*\"/>\r\n";
    s += L"  <description>" + desc + L"</description>\r\n";
    s += L"  <dependency>\r\n";
    s += L"    <dependentAssembly>\r\n";
    s += L"      <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\"\r\n";
    s += L"                        version=\"6.0.0.0\" processorArchitecture=\"*\"\r\n";
    s += L"                        publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\r\n";
    s += L"    </dependentAssembly>\r\n";
    s += L"  </dependency>\r\n";
    s += L"  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n";
    s += L"    <security>\r\n";
    s += L"      <requestedPrivileges>\r\n";
    s += L"        <requestedExecutionLevel level=\"asInvoker\" uiAccess=\"false\"/>\r\n";
    s += L"      </requestedPrivileges>\r\n";
    s += L"    </security>\r\n";
    s += L"  </trustInfo>\r\n";
    s += L"  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n";
    s += L"    <windowsSettings>\r\n";
    s += L"      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true</dpiAware>\r\n";
    s += L"      <longPathAware xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">true</longPathAware>\r\n";
    s += L"    </windowsSettings>\r\n";
    s += L"  </application>\r\n";
    s += L"  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">\r\n";
    s += L"    <application>\r\n";
    s += L"      <supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>\r\n";
    s += L"      <supportedOS Id=\"{1f676c76-80e1-4239-95bb-83d0f6d0da78}\"/>\r\n";
    s += L"      <supportedOS Id=\"{35138b9a-5d96-4fbd-8e2d-a2440225f93a}\"/>\r\n";
    s += L"    </application>\r\n";
    s += L"  </compatibility>\r\n";
    s += L"</assembly>\r\n";
    return s;
}

/* ============================================================ 主体 ======== */

static std::wstring ExeDir()
{
    return TrimTrailingSepsW(PathDirW(ModulePathW()));
}

/* 找一个纯英文（ASCII）的可写临时根目录，给编译工具链用 */
static bool IsAsciiPath(const std::wstring& p)
{
    for (size_t i = 0; i < p.size(); ++i) if ((unsigned)p[i] > 127) return false;
    return true;
}

static std::wstring PickBuildRoot()
{
    std::vector<std::wstring> candidates;
    candidates.push_back(PathJoinW(TempDirW(), L"AEpyInstallerBuild"));
    wchar_t win[MAX_PATH + 2] = {0};
    if (GetWindowsDirectoryW(win, MAX_PATH) > 0)
        candidates.push_back(PathJoinW(win, L"Temp\\AEpyInstallerBuild"));
    candidates.push_back(L"C:\\AEpyInstallerBuild");
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (!IsAsciiPath(candidates[i])) continue;
        if (EnsureDirW(candidates[i])) return candidates[i];
    }
    return candidates[0];
}

static std::wstring MakeAgreementText(const std::wstring& jsonPath)
{
    std::string raw;
    if (!ReadWholeFileW(jsonPath, raw)) return std::wstring();
    std::wstring text = DecodeTextAuto(raw);
    std::wstring trimmed = TrimW(text);
    if (!trimmed.empty() && trimmed[0] == L'{') {
        jx::Value root;
        if (jx::Parse(trimmed, root) && root.type == jx::Value::Object) {
            const wchar_t* keys[] = { L"协议", L"用户协议", L"内容", L"文本",
                                      L"agreement", L"license", L"text", L"content" };
            for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
                const jx::Value* v = root.find(keys[i]);
                if (!v) continue;
                if (v->type == jx::Value::String) return v->str;
                if (v->type == jx::Value::Array) {
                    std::wstring joined;
                    for (size_t k = 0; k < v->arr.size(); ++k) {
                        if (v->arr[k].type != jx::Value::String) continue;
                        if (!joined.empty()) joined += L"\n";
                        joined += v->arr[k].str;
                    }
                    return joined;
                }
            }
        }
    }
    return text;      /* 不是 JSON 就整篇当协议正文 */
}

static std::wstring BuildCompileFlags()
{
    return L" -std=c++17 -O2 -s -mwindows -static -DUNICODE -D_UNICODE"
           L" -finput-charset=UTF-8 -fexec-charset=UTF-8 -fwide-exec-charset=UTF-16LE"
           L" -I. -Wno-unused-variable";
}

static int RunBuilder(int argc, const std::vector<std::wstring>& args)
{
    bool keepTemp = HasArgW(args, L"--keep");
    bool keepZip  = HasArgW(args, L"--keep-zip");
    (void)argc;

    const std::wstring baseDir   = ExeDir();
    const std::wstring buildJson = PathJoinW(baseDir, L"build.json");

    OutLine(L"==================== AEpy 安装包构建器 ====================");
    OutLine(L"工作目录：" + baseDir);

    /* 1. 配置 -------------------------------------------------------------- */
    Config cfg;
    std::wstring err;
    if (!LoadConfig(baseDir, buildJson, cfg, err)) {
        OutLine(L"[错误] " + err);
        return 1;
    }
    if (!DirExistsW(cfg.sourceDir)) {
        OutLine(L"[错误] 找不到源目录：" + cfg.sourceDir);
        return 1;
    }
    OutLine(FormatW(L"[1/6] 配置：安装包名“%ls”，源目录 %ls",
                    cfg.packageName.c_str(), cfg.sourceDir.c_str()));

    /* 2. 扫描文件 ---------------------------------------------------------- */
    std::vector<ZipItem> items;
    ScanStats stats;
    if (!ScanDir(cfg.sourceDir, L"", cfg.excludes, items, stats, err)) {
        OutLine(L"[错误] " + err);
        return 1;
    }
    OutLine(FormatW(L"[2/6] 收集文件：%d 个文件，%.2f MB；排除 %d 项",
                    stats.files, stats.bytes / 1048576.0, stats.skipped));
    for (size_t i = 0; i < cfg.excludes.size(); ++i)
        OutLine(L"      排除规则：" + cfg.excludes[i]);
    if (stats.skippedJunctions > 0)
        OutLine(FormatW(L"      已跳过 %d 个符号链接/联接目录", stats.skippedJunctions));

    /* 3. 准备临时目录 ------------------------------------------------------ */
    std::wstring buildRoot = PickBuildRoot();
    std::wstring tempDir = PathJoinW(buildRoot,
                                     FormatW(L"build_%lu", (unsigned long)GetCurrentProcessId()));
    EnsureDirW(tempDir);
    EnsureDirW(PathJoinW(tempDir, L"源码"));
    OutLine(L"[3/6] 临时构建目录：" + tempDir);

    const wchar_t* srcFiles[] = { L"安装程序模板.cpp", L"卸载程序模板.cpp", L"安装包公共.h" };
    for (size_t i = 0; i < 3; ++i) {
        std::wstring from = PathJoinW(cfg.templateDir, srcFiles[i]);
        std::wstring to   = PathJoinW(PathJoinW(tempDir, L"源码"), srcFiles[i]);
        if (!FileExistsW(from)) {
            OutLine(L"[错误] 缺少模板文件：" + from);
            return 1;
        }
        if (!CopyFileW(from.c_str(), to.c_str(), FALSE)) {
            OutLine(L"[错误] 复制模板文件失败：" + from);
            return 1;
        }
    }

    std::wstring iconInTemp;
    if (!cfg.iconPath.empty() && FileExistsW(cfg.iconPath)) {
        iconInTemp = PathJoinW(tempDir, L"icon.ico");
        CopyFileW(cfg.iconPath.c_str(), iconInTemp.c_str(), FALSE);
    }

    std::wstring metaText;
    metaText += L"应用名=" + cfg.meta.appName + L"\n";
    metaText += L"安装包名=" + cfg.meta.packageName + L"\n";
    metaText += L"版本=" + cfg.meta.version + L"\n";
    metaText += L"主程序=" + cfg.meta.mainExe + L"\n";
    metaText += L"发布者=" + cfg.meta.publisher + L"\n";
    metaText += L"安装目录名=" + cfg.meta.folderName + L"\n";
    {
        std::string bytes = WideToUtf8(metaText);
        WriteWholeFileW(PathJoinW(tempDir, L"meta.txt"), bytes.data(), bytes.size());
    }

    std::wstring agreement = MakeAgreementText(PathJoinW(baseDir, L"用户协议.JSON"));
    {
        std::string bytes = WideToUtf8(agreement);
        WriteWholeFileW(PathJoinW(tempDir, L"agreement.txt"), bytes.data(), bytes.size());
    }
    if (agreement.empty()) OutLine(L"      用户协议为空，安装界面会显示“（暂无用户协议）”");

    /* 4. 打包 ZIP ---------------------------------------------------------- */
    std::wstring zipPath = PathJoinW(tempDir, L"payload.zip");
    {
        ZipWriter zw(zipPath);
        if (!zw.open(err)) { OutLine(L"[错误] " + err); return 1; }
        size_t done = 0;
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].isDir) {
                if (!zw.addDir(items[i].abs, items[i].rel, err)) { OutLine(L"[错误] " + err); return 1; }
            } else {
                if (!zw.addFile(items[i].abs, items[i].rel, err)) { OutLine(L"[错误] " + err); return 1; }
                if (++done % 40 == 0)
                    OutLine(FormatW(L"      打包中… %d/%d", (int)done, stats.files));
            }
        }
        if (!zw.finish(err)) { OutLine(L"[错误] " + err); return 1; }
    }
    {
        OutLine(FormatW(L"[4/6] 打包完成：payload.zip（%.2f MB，存储方式）",
                        FileSizeW(zipPath) / 1048576.0));
    }

    /* 5. 生成资源脚本并编译 ------------------------------------------------ */
    std::wstring windresExe = cfg.windres.empty() ? FindInPath(L"windres.exe") : cfg.windres;
    std::wstring gppExe     = cfg.gpp.empty()     ? FindInPath(L"g++.exe")     : cfg.gpp;
    if (windresExe.empty() || gppExe.empty()) {
        OutLine(L"[错误] 未找到 MinGW 工具链（需要 windres.exe 与 g++.exe 在 PATH 中）。");
        return 1;
    }
    OutLine(L"[5/6] 编译：");
    OutLine(L"      windres = " + windresExe);
    OutLine(L"      g++     = " + gppExe);

    const std::wstring flags = BuildCompileFlags();
    const std::wstring libs  = L" -lole32 -loleaut32 -luuid -lshell32 -lcomctl32"
                               L" -ladvapi32 -luser32 -lgdi32";
    const std::wstring logPath = PathJoinW(tempDir, L"compile.log");

    /* 卸载程序（单独编译，再作为资源塞进安装程序） */
    {
        std::wstring s;
        s += L"#include <windows.h>\r\n\r\n";
        if (!iconInTemp.empty()) s += L"IDI_APPICON ICON \"icon.ico\"\r\n";
        s += FormatW(L"%d 24 \"uninstaller.manifest\"\r\n\r\n", (int)AEPY_RES_MANIFEST);
        s += FormatW(L"%d RCDATA \"meta.txt\"\r\n\r\n", (int)AEPY_RES_META);
        s += MakeVersionInfo(cfg.meta.packageName + L" 卸载程序",
                             cfg.meta.appName + L" 卸载程序",
                             cfg.meta.version, cfg.meta.publisher,
                             L"卸载" + cfg.meta.appName + L".exe");
        {
            std::string bytes = WideToUtf8(s);
            WriteWholeFileW(PathJoinW(tempDir, L"uninstaller.rc"), bytes.data(), bytes.size());
        }
        std::string man = WideToUtf8(MakeManifest(L"AEpy.Uninstaller", L"AEpy Uninstaller"));
        WriteWholeFileW(PathJoinW(tempDir, L"uninstaller.manifest"), man.data(), man.size());

        std::wstring cmd = QuoteArg(windresExe) + L" uninstaller.rc -O coff -o uninstaller_res.o";
        if (!RunTool(cmd, tempDir, logPath, err)) { OutLine(L"[错误] " + err); return 1; }

        cmd = QuoteArg(gppExe) + flags + L" -o uninstaller.exe \"源码/卸载程序模板.cpp\""
              L" uninstaller_res.o" + libs;
        if (!RunTool(cmd, tempDir, logPath, err)) { OutLine(L"[错误] " + err); return 1; }
        OutLine(L"      已生成：卸载程序.exe");
    }

    /* 安装程序 */
    {
        std::wstring s;
        s += L"#include <windows.h>\r\n\r\n";
        if (!iconInTemp.empty()) s += L"IDI_APPICON ICON \"icon.ico\"\r\n";
        s += FormatW(L"%d 24 \"installer.manifest\"\r\n\r\n", (int)AEPY_RES_MANIFEST);
        s += FormatW(L"%d RCDATA \"payload.zip\"\r\n",     (int)AEPY_RES_PAYLOAD);
        s += FormatW(L"%d RCDATA \"agreement.txt\"\r\n",   (int)AEPY_RES_AGREEMENT);
        s += FormatW(L"%d RCDATA \"meta.txt\"\r\n",        (int)AEPY_RES_META);
        s += FormatW(L"%d RCDATA \"uninstaller.exe\"\r\n\r\n", (int)AEPY_RES_UNINSTALLER);
        s += MakeVersionInfo(cfg.packageName, cfg.meta.appName + L" 安装程序",
                             cfg.meta.version, cfg.meta.publisher,
                             cfg.packageName + L".exe");
        {
            std::string bytes = WideToUtf8(s);
            WriteWholeFileW(PathJoinW(tempDir, L"installer.rc"), bytes.data(), bytes.size());
        }
        std::string man = WideToUtf8(MakeManifest(L"AEpy.Installer", L"AEpy Installer"));
        WriteWholeFileW(PathJoinW(tempDir, L"installer.manifest"), man.data(), man.size());

        std::wstring cmd = QuoteArg(windresExe) + L" installer.rc -O coff -o installer_res.o";
        if (!RunTool(cmd, tempDir, logPath, err)) { OutLine(L"[错误] " + err); return 1; }

        cmd = QuoteArg(gppExe) + flags + L" -o installer.exe \"源码/安装程序模板.cpp\""
              L" installer_res.o" + libs;
        if (!RunTool(cmd, tempDir, logPath, err)) { OutLine(L"[错误] " + err); return 1; }
        OutLine(L"      已生成：安装程序");
    }

    /* 6. 输出 -------------------------------------------------------------- */
    EnsureDirW(cfg.outputDir);
    std::wstring srcExe = PathJoinW(tempDir, L"installer.exe");
    std::wstring outExe = PathJoinW(cfg.outputDir, cfg.packageName + L".exe");
    if (!CopyFileW(srcExe.c_str(), outExe.c_str(), FALSE)) {
        OutLine(L"[错误] 复制安装包失败：" + outExe);
        return 1;
    }
    if (keepZip) {
        CopyFileW(zipPath.c_str(), PathJoinW(cfg.outputDir, L"payload.zip").c_str(), FALSE);
        OutLine(L"      已保留 payload.zip（便于检查打包内容）");
    }

    OutLine(FormatW(L"[6/6] 输出：%ls（%.2f MB）", outExe.c_str(), FileSizeW(outExe) / 1048576.0));

    if (!keepTemp) {
        DeleteDirTreeW(tempDir, true);
    } else {
        OutLine(L"      临时目录已保留：" + tempDir);
    }

    OutLine(L"");
    OutLine(L"构建完成。");
    return 0;
}

int main(int argc, char** argv)
{
    (void)argv;
    std::vector<std::wstring> args = CommandLineArgsW();
    if (HasArgW(args, L"--help") || HasArgW(args, L"-h")) {
        OutLine(L"用法：构建安装包.exe [选项]");
        OutLine(L"  --keep      保留临时构建目录，便于排查编译问题");
        OutLine(L"  --keep-zip  额外输出 payload.zip，便于检查打包内容");
        OutLine(L"  --help      显示这份帮助");
        OutLine(L"");
        OutLine(L"配置来自同目录下的 build.json 与 用户协议.JSON。");
        return 0;
    }
    SetConsoleOutputCP(CP_UTF8);
    return RunBuilder(argc, args);
}
