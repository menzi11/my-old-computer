#include "settings.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdio>
#include <cwctype>

namespace {

std::wstring iniRead(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                     const wchar_t* def) {
    wchar_t buf[4096];
    GetPrivateProfileStringW(section, key, def, buf, 4096, path.c_str());
    return buf;
}

bool startsWith(const std::wstring& s, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return s.size() >= n && s.compare(0, n, prefix) == 0;
}

std::wstring lowerAscii(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

std::vector<std::wstring> sectionNames(const std::wstring& path) {
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetPrivateProfileSectionNamesW(buf.data(), (DWORD)buf.size(), path.c_str());
    std::vector<std::wstring> out;
    DWORD i = 0;
    while (i < n) {
        std::wstring s(&buf[i]);
        i += (DWORD)s.size() + 1;
        out.push_back(std::move(s));
    }
    return out;
}

// Reads hide0..hide63 of one section into `out`.
void readHidden(const std::wstring& path, const wchar_t* section,
                std::vector<std::wstring>& out) {
    for (int i = 0; i < 64; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"hide%d", i);
        std::wstring v = iniRead(path, section, key, L"");
        if (!v.empty()) out.push_back(v);
    }
}

void writeHidden(const std::wstring& path, const wchar_t* section,
                 const std::vector<std::wstring>& list) {
    for (int i = 0; i < 64; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"hide%d", i);
        if (i < (int)list.size())
            WritePrivateProfileStringW(section, key, list[i].c_str(), path.c_str());
        else
            WritePrivateProfileStringW(section, key, nullptr, path.c_str());
    }
}

}  // namespace

std::wstring settingsPath() {
    std::wstring dir;
    PWSTR p = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &p)) && p) {
        dir = p;
        CoTaskMemFree(p);
    }
    if (dir.empty()) return L"settings.ini";
    dir += L"\\My Old Computer";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

void Settings::load() {
    std::wstring path = settingsPath();
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    x = _wtoi(iniRead(path, L"window", L"x", L"-1").c_str());
    y = _wtoi(iniRead(path, L"window", L"y", L"-1").c_str());
    w = std::max(700, _wtoi(iniRead(path, L"window", L"w", L"1000").c_str()));
    h = std::max(440, _wtoi(iniRead(path, L"window", L"h", L"640").c_str()));
    sidebarWidth = _wtoi(iniRead(path, L"window", L"sidebar", L"200").c_str());
    viewMode = _wtoi(iniRead(path, L"view", L"mode", L"0").c_str());
    showHidden = iniRead(path, L"view", L"hidden", L"0") == L"1";
    useSystemIcons = iniRead(path, L"view", L"sysicons", L"1") == L"1";
    menuMask = (unsigned)wcstoul(
        iniRead(path, L"menu", L"mask", L"").c_str(), nullptr, 0);
    if (menuMask == 0) menuMask = kMenuDefaults;

    // Per-context hidden lists.  The old flat [menu] hide* list applied to
    // every context at once, so it migrates into all three base lists.
    std::vector<std::wstring> legacy;
    readHidden(path, L"menu", legacy);
    hiddenBg = legacy;
    hiddenFolder = legacy;
    hiddenFile = legacy;
    readHidden(path, L"menu-bg", hiddenBg);
    readHidden(path, L"menu-folder", hiddenFolder);
    readHidden(path, L"menu-file", hiddenFile);
    hiddenExt.clear();
    const std::wstring kExtPrefix = L"menu-ext.";
    for (auto& s : sectionNames(path)) {
        if (!startsWith(s, kExtPrefix.c_str())) continue;
        const std::wstring ext = lowerAscii(s.substr(kExtPrefix.size()));
        if (ext.empty()) continue;
        readHidden(path, s.c_str(), hiddenExt[ext]);
    }

    favorites.clear();
    for (int i = 0; i < 64; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"fav%d", i);
        std::wstring v = iniRead(path, L"favorites", key, L"");
        if (!v.empty()) favorites.push_back(v);
    }
}

void Settings::save() const {
    std::wstring path = settingsPath();
    wchar_t buf[64];
    swprintf_s(buf, L"%d", x);
    WritePrivateProfileStringW(L"window", L"x", buf, path.c_str());
    swprintf_s(buf, L"%d", y);
    WritePrivateProfileStringW(L"window", L"y", buf, path.c_str());
    swprintf_s(buf, L"%d", w);
    WritePrivateProfileStringW(L"window", L"w", buf, path.c_str());
    swprintf_s(buf, L"%d", h);
    WritePrivateProfileStringW(L"window", L"h", buf, path.c_str());
    swprintf_s(buf, L"%d", sidebarWidth);
    WritePrivateProfileStringW(L"window", L"sidebar", buf, path.c_str());
    swprintf_s(buf, L"%d", viewMode);
    WritePrivateProfileStringW(L"view", L"mode", buf, path.c_str());
    WritePrivateProfileStringW(L"view", L"hidden", showHidden ? L"1" : L"0", path.c_str());
    WritePrivateProfileStringW(L"view", L"sysicons", useSystemIcons ? L"1" : L"0", path.c_str());
    swprintf_s(buf, L"0x%X", menuMask);
    WritePrivateProfileStringW(L"menu", L"mask", buf, path.c_str());
    // Retire the legacy flat list; the per-context sections replace it.
    writeHidden(path, L"menu", {});
    writeHidden(path, L"menu-bg", hiddenBg);
    writeHidden(path, L"menu-folder", hiddenFolder);
    writeHidden(path, L"menu-file", hiddenFile);

    // One section per extension that has anything hidden; stale sections
    // (extensions whose items are all visible again) are removed.
    std::vector<std::wstring> keep;
    for (auto& [ext, list] : hiddenExt) {
        if (list.empty()) continue;
        const std::wstring section = L"menu-ext." + ext;
        writeHidden(path, section.c_str(), list);
        keep.push_back(section);
    }
    const std::wstring kExtPrefix = L"menu-ext.";
    for (auto& s : sectionNames(path)) {
        if (!startsWith(s, kExtPrefix.c_str())) continue;
        if (std::find(keep.begin(), keep.end(), s) == keep.end())
            WritePrivateProfileStringW(s.c_str(), nullptr, nullptr, path.c_str());
    }

    WritePrivateProfileStringW(L"favorites", nullptr, nullptr, path.c_str());
    for (size_t i = 0; i < favorites.size() && i < 64; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"fav%zu", i);
        WritePrivateProfileStringW(L"favorites", key, favorites[i].c_str(), path.c_str());
    }
}
