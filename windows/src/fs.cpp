#include "fs.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <cwchar>
#include <cwctype>

namespace fsx {
namespace {

std::wstring lowerInvariant(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

std::wstring trimTrailingSlash(std::wstring p) {
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    return p;
}

bool isHiddenEntry(const WIN32_FIND_DATAW& fd) {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) return true;
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) return true;
    return false;
}

void appendDoubleNullList(std::vector<wchar_t>& out, const std::wstring& s) {
    out.insert(out.end(), s.begin(), s.end());
    out.push_back(0);
}

}  // namespace

std::wstring homeDir() {
    static std::wstring cached = [] {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, KF_FLAG_DEFAULT, nullptr, &p)) && p) {
            std::wstring s(p);
            CoTaskMemFree(p);
            return s;
        }
        wchar_t buf[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH)) return std::wstring(buf);
        return std::wstring(L"C:\\");
    }();
    return cached;
}

std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p)) && p) {
        std::wstring s(p);
        CoTaskMemFree(p);
        return s;
    }
    return {};
}

std::wstring extensionLower(const std::wstring& name) {
    size_t dot = name.find_last_of(L'.');
    if (dot == std::wstring::npos || dot + 1 >= name.size()) return {};
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos && dot < slash) return {};
    return lowerInvariant(name.substr(dot + 1));
}

std::wstring typeName(const std::wstring& path, bool isDir) {
    if (isDir) {
        if (isRoot(path)) return L"本地磁盘";
        return L"文件夹";
    }
    std::wstring ext = extensionLower(fileName(path));
    struct Row {
        const wchar_t* ext;
        const wchar_t* name;
    };
    static const Row table[] = {
        {L"png", L"PNG 图像"},      {L"jpg", L"JPEG 图像"},     {L"jpeg", L"JPEG 图像"},
        {L"gif", L"GIF 图像"},      {L"bmp", L"位图图像"},      {L"webp", L"WebP 图像"},
        {L"svg", L"SVG 图像"},      {L"ico", L"图标"},          {L"tif", L"TIFF 图像"},
        {L"tiff", L"TIFF 图像"},    {L"heic", L"HEIC 图像"},    {L"psd", L"Photoshop 图像"},
        {L"pdf", L"PDF 文档"},      {L"txt", L"文本文档"},      {L"md", L"Markdown"},
        {L"log", L"日志文件"},      {L"rtf", L"RTF 文档"},      {L"doc", L"Word 文档"},
        {L"docx", L"Word 文档"},    {L"xls", L"工作表"},        {L"xlsx", L"工作表"},
        {L"csv", L"CSV 文件"},      {L"ppt", L"演示文稿"},      {L"pptx", L"演示文稿"},
        {L"html", L"HTML 文档"},    {L"htm", L"HTML 文档"},     {L"xml", L"XML 文档"},
        {L"json", L"JSON 文件"},    {L"ini", L"配置设置"},      {L"zip", L"压缩文件"},
        {L"7z", L"7Z 压缩"},        {L"rar", L"RAR 压缩"},      {L"gz", L"GZip 压缩"},
        {L"tar", L"TAR 压缩"},      {L"iso", L"光盘映像"},      {L"mp3", L"MP3 音频"},
        {L"wav", L"WAV 音频"},      {L"flac", L"FLAC 音频"},    {L"m4a", L"M4A 音频"},
        {L"ogg", L"OGG 音频"},      {L"mp4", L"MP4 视频"},      {L"mkv", L"MKV 视频"},
        {L"avi", L"AVI 视频"},      {L"mov", L"MOV 视频"},      {L"exe", L"应用程序"},
        {L"msi", L"安装程序"},      {L"dll", L"程序扩展"},      {L"bat", L"批处理文件"},
        {L"cmd", L"命令脚本"},      {L"lnk", L"快捷方式"},      {L"ttf", L"字体"},
        {L"otf", L"字体"},          {L"sys", L"系统文件"},      {L"swift", L"Swift 源文件"},
        {L"c", L"C 源文件"},        {L"cpp", L"C++ 源文件"},    {L"h", L"C 头文件"},
        {L"py", L"Python 源文件"},  {L"js", L"JavaScript"},     {L"ts", L"TypeScript"},
        {L"css", L"样式表"},        {L"sh", L"Shell 脚本"},
    };
    for (auto& row : table)
        if (ext == row.ext) return row.name;
    if (ext.empty()) return L"文件";
    std::wstring upper = ext;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](wchar_t c) { return (wchar_t)std::towupper(c); });
    return upper + L" 文件";
}

std::wstring formatSize(unsigned long long n) {
    wchar_t buf[64];
    if (n < 1024ull) {
        swprintf_s(buf, L"%llu 字节", n);
        return buf;
    }
    double kb = (double)n / 1024.0;
    if (kb < 1024.0) {
        swprintf_s(buf, L"%.0f KB", kb);
        return buf;
    }
    double mb = kb / 1024.0;
    if (mb < 1024.0) {
        swprintf_s(buf, L"%.1f MB", mb);
        return buf;
    }
    double gb = mb / 1024.0;
    swprintf_s(buf, L"%.2f GB", gb);
    return buf;
}

std::wstring formatDate(const FILETIME& ft) {
    FILETIME local{};
    if (!FileTimeToLocalFileTime(&ft, &local)) return {};
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&local, &st)) return {};
    wchar_t buf[64];
    swprintf_s(buf, L"%04d/%d/%d %02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    return buf;
}

std::vector<Entry> listDirectory(const std::wstring& dir, bool showHidden) {
    std::vector<Entry> out;
    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (!showHidden && isHiddenEntry(fd)) continue;
        Entry e;
        e.name = name;
        e.path = dir;
        if (!e.path.empty() && e.path.back() != L'\\') e.path += L'\\';
        e.path += name;
        e.isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!e.isDir) {
            e.size = ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
            e.hasSize = true;
        }
        e.mtime = fd.ftLastWriteTime;
        e.hasTime = true;
        e.type = typeName(e.path, e.isDir);
        out.push_back(std::move(e));
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

void sortEntries(std::vector<Entry>& entries, int column, bool ascending) {
    auto cmp = [column](const Entry& a, const Entry& b) -> int {
        if (a.isDir != b.isDir) return a.isDir ? -1 : 1;
        switch (column) {
        case 1:
            if (a.size != b.size) return a.size < b.size ? -1 : 1;
            break;
        case 2: {
            int c = StrCmpLogicalW(a.type.c_str(), b.type.c_str());
            if (c != 0) return c;
            break;
        }
        case 3: {
            long long da = ((long long)a.mtime.dwHighDateTime << 32) | a.mtime.dwLowDateTime;
            long long db = ((long long)b.mtime.dwHighDateTime << 32) | b.mtime.dwLowDateTime;
            if (da != db) return da < db ? -1 : 1;
            break;
        }
        default:
            break;
        }
        return StrCmpLogicalW(a.name.c_str(), b.name.c_str());
    };
    std::stable_sort(entries.begin(), entries.end(), [&](const Entry& a, const Entry& b) {
        int c = cmp(a, b);
        return ascending ? c < 0 : c > 0;
    });
}

std::wstring parentPath(const std::wstring& path) {
    if (isRoot(path)) return path;
    std::filesystem::path p(path);
    auto parent = p.parent_path();
    std::wstring s = parent.wstring();
    if (s.empty()) return rootOf(path);
    if (s.size() == 2 && s[1] == L':') s += L'\\';
    return trimTrailingSlash(s);
}

std::wstring joinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    std::wstring s = dir;
    if (s.back() != L'\\' && s.back() != L'/') s += L'\\';
    return s + name;
}

std::wstring fileName(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

std::wstring rootOf(const std::wstring& path) {
    std::filesystem::path p(path);
    std::wstring s = p.root_path().wstring();
    if (s.empty()) return L"C:\\";
    if (s.size() == 2 && s[1] == L':') s += L'\\';
    return s;
}

bool isRoot(const std::wstring& path) {
    std::wstring t = trimTrailingSlash(path);
    if (t.size() == 2 && t[1] == L':') return true;
    if (t.size() == 3 && t[1] == L':' && t[2] == L'\\') return true;
    return std::filesystem::path(path).parent_path() == std::filesystem::path(path);
}

bool isInside(const std::wstring& child, const std::wstring& parent) {
    std::wstring c = trimTrailingSlash(child), p = trimTrailingSlash(parent);
    if (c == p) return true;
    if (p.empty()) return false;
    std::wstring prefix = p + L"\\";
    return c.size() > prefix.size() && _wcsnicmp(c.c_str(), prefix.c_str(), prefix.size()) == 0;
}

bool exists(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES;
}

bool isDirectory(const std::wstring& path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool sameVolume(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(rootOf(a).c_str(), rootOf(b).c_str()) == 0;
}

std::wstring abbreviateHome(const std::wstring& path) {
    std::wstring home = homeDir();
    if (path.size() >= home.size() && _wcsnicmp(path.c_str(), home.c_str(), home.size()) == 0) {
        if (path.size() == home.size()) return L"~";
        if (path[home.size()] == L'\\') return L"~" + path.substr(home.size());
    }
    return path;
}

std::wstring expandHome(const std::wstring& text) {
    if (text == L"~") return homeDir();
    if (text.size() > 1 && text[0] == L'~' && (text[1] == L'\\' || text[1] == L'/'))
        return homeDir() + L"\\" + text.substr(2);
    return text;
}

std::wstring uniqueTarget(const std::wstring& src, const std::wstring& destDir) {
    std::wstring name = fileName(src);
    std::wstring candidate = joinPath(destDir, name);
    if (!exists(candidate)) return candidate;
    size_t dot = name.find_last_of(L'.');
    std::wstring base = (dot == std::wstring::npos) ? name : name.substr(0, dot);
    std::wstring ext = (dot == std::wstring::npos) ? L"" : name.substr(dot);
    for (int n = 2; n < 10000; ++n) {
        wchar_t buf[32];
        swprintf_s(buf, L" (%d)", n);
        candidate = joinPath(destDir, base + buf + ext);
        if (!exists(candidate)) return candidate;
    }
    return candidate;
}

namespace {
bool shellOp(const std::vector<std::wstring>& from, const std::wstring& to, UINT func,
             FILEOP_FLAGS flags) {
    if (from.empty()) return false;
    std::vector<wchar_t> data;
    for (auto& f : from) appendDoubleNullList(data, f);
    data.push_back(0);
    SHFILEOPSTRUCTW op{};
    op.wFunc = func;
    op.pFrom = data.data();
    std::wstring toz = to;
    if (!toz.empty()) {
        for (auto& ch : toz)
            if (ch == L'/') ch = L'\\';
        toz.push_back(0);
        toz.push_back(0);
        op.pTo = toz.c_str();
    }
    op.fFlags = flags;
    return SHFileOperationW(&op) == 0;
}

bool copyItemTo(const std::wstring& src, const std::wstring& target) {
    std::error_code ec;
    if (isDirectory(src)) {
        std::filesystem::copy(std::filesystem::path(src), std::filesystem::path(target),
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::copy_symlinks,
                              ec);
        return !ec;
    }
    return CopyFileW(src.c_str(), target.c_str(), TRUE) != 0;
}

bool moveItemTo(const std::wstring& src, const std::wstring& target) {
    std::error_code ec;
    std::filesystem::rename(std::filesystem::path(src), std::filesystem::path(target), ec);
    if (!ec) return true;
    // Cross-volume move: copy the tree, then delete the source.
    if (!copyItemTo(src, target)) return false;
    ec.clear();
    std::filesystem::remove_all(std::filesystem::path(src), ec);
    return !ec;
}
}  // namespace

bool recycle(const std::vector<std::wstring>& paths) {
    return shellOp(paths, L"", FO_DELETE, FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_NOERRORUI);
}

bool copyItems(const std::vector<std::wstring>& paths, const std::wstring& destDir) {
    bool any = false;
    bool ok = true;
    for (auto& src : paths) {
        if (isInside(destDir, src)) continue;
        std::wstring target = joinPath(destDir, fileName(src));
        if (exists(target)) target = uniqueTarget(src, destDir);
        if (copyItemTo(src, target)) {
            any = true;
        } else {
            ok = false;
        }
    }
    return any && ok;
}

bool moveItems(const std::vector<std::wstring>& paths, const std::wstring& destDir) {
    bool any = false;
    bool ok = true;
    for (auto& src : paths) {
        if (isInside(destDir, src)) continue;
        std::wstring target = joinPath(destDir, fileName(src));
        if (exists(target)) target = uniqueTarget(src, destDir);
        if (moveItemTo(src, target)) {
            any = true;
        } else {
            ok = false;
        }
    }
    return any && ok;
}

bool makeFolder(const std::wstring& dir, const std::wstring& name, std::wstring* created) {
    std::wstring base = joinPath(dir, name);
    std::wstring target = base;
    int n = 1;
    while (exists(target)) {
        ++n;
        wchar_t buf[32];
        swprintf_s(buf, L" (%d)", n);
        target = base + buf;
    }
    if (CreateDirectoryW(target.c_str(), nullptr)) {
        if (created) *created = target;
        return true;
    }
    return false;
}

bool renameItem(const std::wstring& path, const std::wstring& newName, std::wstring* error) {
    std::wstring dest = joinPath(parentPath(path), newName);
    if (_wcsicmp(dest.c_str(), path.c_str()) == 0) return true;
    if (exists(dest)) {
        if (error) *error = L"已有同名文件或文件夹。";
        return false;
    }
    if (!MoveFileExW(path.c_str(), dest.c_str(), 0)) {
        if (error) *error = lastErrorText();
        return false;
    }
    return true;
}

bool openWithShell(const std::wstring& path) {
    HINSTANCE r = ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

bool revealInExplorer(const std::wstring& path) {
    std::wstring args = L"/select,\"" + path + L"\"";
    HINSTANCE r =
        ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

bool openTerminalAt(const std::wstring& dir) {
    std::wstring args = L"/k cd /d \"" + dir + L"\"";
    HINSTANCE r = ShellExecuteW(nullptr, L"open", L"cmd.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    return (INT_PTR)r > 32;
}

bool hasAnySubdirectory(const std::wstring& dir, bool showHidden) {
    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\') pattern += L'\\';
    pattern += L'*';
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        std::wstring name = fd.cFileName;
        if (name == L"." || name == L"..") continue;
        if (!showHidden && isHiddenEntry(fd)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            found = true;
            break;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

std::wstring lastErrorText(DWORD code) {
    if (code == 0) code = GetLastError();
    wchar_t* msg = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, (LPWSTR)&msg, 0, nullptr);
    std::wstring s = n ? std::wstring(msg, n) : L"操作失败。";
    if (msg) LocalFree(msg);
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    return s;
}

}  // namespace fsx
