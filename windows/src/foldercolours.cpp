#include "foldercolours.h"

#include <algorithm>
#include <cwctype>
#include <map>

#include "settings.h"

namespace foldercolours {
namespace {

const Colour kColours[kCount] = {
    {L"红色", RGB(0xD4, 0x00, 0x00)}, {L"橙色", RGB(0xEC, 0x82, 0x00)},
    {L"绿色", RGB(0x00, 0x96, 0x3C)}, {L"青色", RGB(0x00, 0xA0, 0xAA)},
    {L"蓝色", RGB(0x00, 0x5A, 0xDC)}, {L"紫色", RGB(0x8C, 0x3C, 0xC8)},
    {L"粉色", RGB(0xEB, 0x6E, 0xAA)}, {L"灰色", RGB(0x82, 0x82, 0x82)},
};

std::map<std::wstring, int> g_of;  // by path, in lower case
bool g_loaded = false;             // the file has been read
int g_written = 0;                 // entries in the file's list as last read or written

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

// The list is "f0" .. "fN", each "<colour>|<path>": the path is the value, so
// its length is not the key's limit, and no character of it can mean
// something to the file format.
void write() {
    const std::wstring file = foldercolours::path();
    // A file that is not there yet is made as Unicode: folder names are in
    // any language.
    if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0,
                                   nullptr);
            h != INVALID_HANDLE_VALUE) {
            const BYTE bom[] = {0xFF, 0xFE};
            DWORD wrote = 0;
            WriteFile(h, bom, sizeof(bom), &wrote, nullptr);
            CloseHandle(h);
        }
    }
    int n = 0;
    for (const auto& kv : g_of) {
        wchar_t key[16], value[8];
        swprintf_s(key, L"f%d", n);
        swprintf_s(value, L"%d", kv.second);
        WritePrivateProfileStringW(L"folders", key, (std::wstring(value) + L"|" + kv.first).c_str(),
                                   file.c_str());
        ++n;
    }
    // What was in the list and is not any more.
    for (int i = n; i < g_written; ++i) {
        wchar_t key[16];
        swprintf_s(key, L"f%d", i);
        WritePrivateProfileStringW(L"folders", key, nullptr, file.c_str());
    }
    g_written = n;
}

}  // namespace

const Colour& colour(int i) {
    static const Colour none{L"默认颜色", RGB(0, 0, 0)};
    if (i < 0 || i >= kCount) return none;
    return kColours[i];
}

std::wstring path() {
    // The tests' settings file is a scratch one; theirs is a file beside it.
    wchar_t scratch[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"MOC_SETTINGS_FILE", scratch, MAX_PATH) > 0) {
        std::wstring s = scratch;
        const size_t dot = s.rfind(L'.');
        if (dot != std::wstring::npos) s.resize(dot);
        return s + L"-colours.ini";
    }
    const std::wstring settings = settingsPath();
    const size_t slash = settings.rfind(L'\\');
    return (slash == std::wstring::npos ? std::wstring() : settings.substr(0, slash + 1)) +
           L"foldercolours.ini";
}

void reload() {
    g_loaded = true;
    g_of.clear();
    g_written = 0;
    // The whole list: the buffer grows until the list fits.
    std::vector<wchar_t> buf(65536);
    DWORD n = 0;
    for (;;) {
        n = GetPrivateProfileSectionW(L"folders", buf.data(), (DWORD)buf.size(),
                                      foldercolours::path().c_str());
        // Two characters short of the end: the list ran past the buffer.
        if (n + 2 < buf.size() || buf.size() >= (1u << 22)) break;
        buf.resize(buf.size() * 2);
    }
    for (DWORD i = 0; i < n;) {
        const std::wstring line = &buf[i];
        i += (DWORD)line.size() + 1;
        const size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        ++g_written;
        const std::wstring value = line.substr(eq + 1);
        const size_t bar = value.find(L'|');
        if (bar == std::wstring::npos) continue;
        const int colour = _wtoi(value.c_str());
        if (colour < 0 || colour >= kCount || bar + 1 >= value.size()) continue;
        g_of[lower(value.substr(bar + 1))] = colour;
    }
}

int of(const std::wstring& path) {
    if (!g_loaded) reload();
    const auto it = g_of.find(lower(path));
    return it == g_of.end() ? kDefault : it->second;
}

void set(const std::vector<std::wstring>& paths, int colour) {
    if (!g_loaded) reload();
    for (const std::wstring& path : paths) {
        const std::wstring key = lower(path);
        if (colour == kDefault) g_of.erase(key);
        else if (colour >= 0 && colour < kCount) g_of[key] = colour;
    }
    write();
}

}  // namespace foldercolours
