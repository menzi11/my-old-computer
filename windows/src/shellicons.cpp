#include "shellicons.h"

#include <commctrl.h>
#include <commoncontrols.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cwctype>
#include <map>

#include "debug.h"

namespace shellicons {
namespace {

struct Cache {
    std::map<std::wstring, HICON> icons;
};

Cache g_small;   // 16 px
Cache g_large;   // 32 px
Cache g_extra;   // 48 px
Cache g_jumbo;   // 256 px, used when the interface is scaled up

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return (wchar_t)std::towlower(c); });
    return s;
}

// A key shared by all items that show the same icon.  Drives keep their own
// path as part of the key: HDD, USB, DVD and network drives all draw
// differently, a single "<drive>" key used to give every volume the first
// drive's icon.
std::wstring keyFor(const std::wstring& path, bool isDir, bool isDrive) {
    if (isDrive) return L"drv:" + path;
    if (isDir) return L"<dir>";
    std::wstring ext = PathFindExtensionW(path.c_str());
    if (ext.empty()) return L"<file>";
    ext = lower(ext);
    // These carry their own icon, so they are cached per file.
    if (ext == L".exe" || ext == L".lnk" || ext == L".ico" || ext == L".msi" ||
        ext == L".url")
        return path;
    return ext;
}

Cache& cacheFor(int shil) {
    if (shil == SHIL_SMALL) return g_small;
    if (shil == SHIL_LARGE) return g_large;
    if (shil == SHIL_JUMBO) return g_jumbo;
    return g_extra;
}

int listForSize(int px) {
    if (px <= 16) return SHIL_SMALL;
    if (px <= 32) return SHIL_LARGE;
    if (px <= 48) return SHIL_EXTRALARGE;
    return SHIL_JUMBO;
}

HICON load(const std::wstring& path, bool isDir, bool isDrive, int shil) {
    std::wstring key = keyFor(path, isDir, isDrive);
    const bool perFile = (key == path);

    SHFILEINFOW sfi{};
    DWORD flags = SHGFI_SYSICONINDEX;
    std::wstring fake;
    const wchar_t* target = nullptr;
    DWORD attrs = FILE_ATTRIBUTE_NORMAL;
    if (isDrive) {
        target = path.c_str();
        attrs = 0;
    } else if (isDir) {
        fake = L"folder";
        target = fake.c_str();
        attrs = FILE_ATTRIBUTE_DIRECTORY;
        flags |= SHGFI_USEFILEATTRIBUTES;
    } else if (perFile) {
        target = path.c_str();
        attrs = 0;
    } else {
        fake = L"file" + key;
        target = fake.c_str();
        flags |= SHGFI_USEFILEATTRIBUTES;
    }
    if (!SHGetFileInfoW(target, attrs, &sfi, sizeof(sfi), flags)) {
        diagLog(L"shell icon: SHGetFileInfo failed for %ls (key %ls)", target, key.c_str());
        return nullptr;
    }

    IImageList* list = nullptr;
    HRESULT hr = SHGetImageList(shil, IID_PPV_ARGS(&list));
    if (FAILED(hr) || !list) {
        diagLog(L"shell icon: SHGetImageList failed 0x%08lX (key %ls)", (unsigned long)hr,
                key.c_str());
        return nullptr;
    }
    HICON icon = nullptr;
    list->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &icon);
    list->Release();
    return icon;
}

}  // namespace

HICON icon(const std::wstring& path, bool isDir, bool isDrive, int px) {
    const int shil = listForSize(px);
    Cache& cache = cacheFor(shil);
    std::wstring key = keyFor(path, isDir, isDrive);
    auto it = cache.icons.find(key);
    if (it != cache.icons.end()) return it->second;
    // .exe/.lnk/.ico items are cached per file; keep the cache bounded so a
    // stroll through a directory of executables cannot accumulate GDI
    // handles forever.
    if (cache.icons.size() > 400) clear();
    HICON result = load(path, isDir, isDrive, shil);
    cache.icons[key] = result;
    return result;
}

void clear() {
    for (Cache* c : {&g_small, &g_large, &g_extra, &g_jumbo}) {
        for (auto& kv : c->icons)
            if (kv.second) DestroyIcon(kv.second);
        c->icons.clear();
    }
}

}  // namespace shellicons
