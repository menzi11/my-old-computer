#include "settings.h"

#include <shlobj.h>

#include <algorithm>
#include <cstdio>

namespace {

std::wstring iniRead(const std::wstring& path, const wchar_t* section, const wchar_t* key,
                     const wchar_t* def) {
    wchar_t buf[4096];
    GetPrivateProfileStringW(section, key, def, buf, 4096, path.c_str());
    return buf;
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
    hiddenMenuItems.clear();
    for (int i = 0; i < 64; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"hide%d", i);
        std::wstring v = iniRead(path, L"menu", key, L"");
        if (!v.empty()) hiddenMenuItems.push_back(v);
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
    for (int i = 0; i < 64; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"hide%d", i);
        if (i < (int)hiddenMenuItems.size())
            WritePrivateProfileStringW(L"menu", key, hiddenMenuItems[i].c_str(), path.c_str());
        else
            WritePrivateProfileStringW(L"menu", key, nullptr, path.c_str());
    }
    WritePrivateProfileStringW(L"favorites", nullptr, nullptr, path.c_str());
    for (size_t i = 0; i < favorites.size() && i < 64; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"fav%zu", i);
        WritePrivateProfileStringW(L"favorites", key, favorites[i].c_str(), path.c_str());
    }
}
