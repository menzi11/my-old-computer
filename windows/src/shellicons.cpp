#include "shellicons.h"

#include <commctrl.h>
#include <commoncontrols.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <cwctype>
#include <map>
#include <utility>
#include <vector>

#include "debug.h"
#include "foldercolours.h"

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

std::map<std::pair<int, int>, HICON> g_folders;  // by colour and image list

// The folder icon with its yellow replaced by `colour`: every yellow pixel
// keeps how light it is and takes the colour instead - so the flap stays
// lighter than the front and the shades stay shades.  The outline, the paper
// and whatever else in the icon is not yellow stay as they are.
HICON recolour(HICON yellow, COLORREF colour) {
    ICONINFO info{};
    if (!yellow || !GetIconInfo(yellow, &info)) return nullptr;
    HICON result = nullptr;
    BITMAP bm{};
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0 &&
        bm.bmHeight > 0 && bm.bmBitsPixel == 32) {
        const int w = bm.bmWidth, h = bm.bmHeight;
        BITMAPINFOHEADER header{};
        header.biSize = sizeof(header);
        header.biWidth = w;
        header.biHeight = -h;  // top-down
        header.biPlanes = 1;
        header.biBitCount = 32;
        // An icon's colour bitmap is not premultiplied: plain ARGB both ways.
        std::vector<unsigned char> pixels((size_t)w * h * 4, 0);
        HDC dc = CreateCompatibleDC(nullptr);
        GetDIBits(dc, info.hbmColor, 0, h, pixels.data(), (BITMAPINFO*)&header, DIB_RGB_COLORS);
        const int tr = GetRValue(colour), tg = GetGValue(colour), tb = GetBValue(colour);
        for (size_t i = 0; i < pixels.size(); i += 4) {
            const int b = pixels[i], g = pixels[i + 1], r = pixels[i + 2];
            const int mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b));
            if (mx == mn) continue;  // grey: the outline, the paper
            const int span = mx - mn;
            double hue = mx == r ? 60.0 * (g - b) / span
                       : mx == g ? 60.0 * (2.0 + (double)(b - r) / span)
                                 : 60.0 * (4.0 + (double)(r - g) / span);
            if (hue < 0) hue += 360.0;
            const bool yellowish = hue >= 25.0 && hue <= 75.0 && span >= 0.2 * mx;
            if (!yellowish) continue;
            // Against the light of the folder's front; brighter than that
            // (the flap) gets more of the colour, a shade less.
            const double light = (0.299 * r + 0.587 * g + 0.114 * b) / (255.0 * 0.75);
            pixels[i] = (unsigned char)std::min(255.0, tb * light);
            pixels[i + 1] = (unsigned char)std::min(255.0, tg * light);
            pixels[i + 2] = (unsigned char)std::min(255.0, tr * light);
        }
        DeleteDC(dc);
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        HBITMAP colourBitmap = CreateDIBSection(screen, (BITMAPINFO*)&header, DIB_RGB_COLORS, &bits,
                                                nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (colourBitmap && bits) {
            memcpy(bits, pixels.data(), pixels.size());
            ICONINFO coloured{};
            coloured.fIcon = TRUE;
            coloured.hbmColor = colourBitmap;
            coloured.hbmMask = CreateBitmap(w, h, 1, 1, nullptr);
            result = CreateIconIndirect(&coloured);
            // The icon made from them owns the bitmaps, when there is one.
            if (!result) {
                DeleteObject(colourBitmap);
                DeleteObject(coloured.hbmMask);
            }
        } else if (colourBitmap) {
            DeleteObject(colourBitmap);
        }
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return result;
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

HICON folder(int colour, int px) {
    if (colour < 0 || colour >= foldercolours::kCount) return nullptr;
    const int shil = listForSize(px);
    const auto key = std::make_pair(colour, shil);
    const auto it = g_folders.find(key);
    if (it != g_folders.end()) return it->second;
    const HICON yellow = icon(std::wstring(), true, false, px);
    const HICON result = recolour(yellow, foldercolours::colour(colour).rgb);
    g_folders[key] = result;
    return result;
}

void clear() {
    for (Cache* c : {&g_small, &g_large, &g_extra, &g_jumbo}) {
        for (auto& kv : c->icons)
            if (kv.second) DestroyIcon(kv.second);
        c->icons.clear();
    }
    for (auto& kv : g_folders)
        if (kv.second) DestroyIcon(kv.second);
    g_folders.clear();
}

}  // namespace shellicons
