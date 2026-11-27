#include "takeover.h"

#include <shlobj.h>    // SHChangeNotify
#include <shellapi.h>  // ShellExecute

#include "fs.h"

namespace takeover {
namespace {

constexpr const wchar_t* kRealKey = L"Software\\Classes\\Folder\\shell\\open\\command";

// What makes Explorer open the folder, and not the command: when it is there
// the shell runs that instead.  A user's key and the machine's are read as
// one, value by value, so the machine's would show through from there if this
// one simply had none: it is written here, empty.
constexpr const wchar_t* kDelegate = L"DelegateExecute";

// The keys on the way down to the command (...\shell\open, ...\shell,
// ...\Folder) are made for it, and taken out again with it when nothing else
// is in them.
constexpr int kMadeOnTheWay = 3;

struct Key {
    HKEY h = nullptr;
    ~Key() {
        if (h) RegCloseKey(h);
    }
};

const wchar_t* g_key = kRealKey;

std::wstring exePath() {
    wchar_t buf[2048]{};
    const DWORD n = GetModuleFileNameW(nullptr, buf, 2048);
    return std::wstring(buf, n < 2048 ? n : 0);
}

std::wstring quoted(const std::wstring& s) { return L"\"" + s + L"\""; }

bool begins(const std::wstring& s, const std::wstring& start) {
    return s.size() >= start.size() && _wcsnicmp(s.c_str(), start.c_str(), start.size()) == 0;
}

// The program the command starts: its path, quoted.  Empty when the command
// does not start with a quoted path.
std::wstring commandProgram(const std::wstring& command) {
    if (command.size() < 2 || command.front() != L'"') return {};
    const size_t end = command.find(L'"', 1);
    if (end == std::wstring::npos) return {};
    return command.substr(1, end - 1);
}

// What is written under `key`, if anything.
bool commandAt(const wchar_t* key, std::wstring* command) {
    wchar_t buf[2048]{};
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, key, nullptr, RRF_RT_REG_SZ, nullptr, buf, &size) !=
        ERROR_SUCCESS)
        return false;
    command->assign(buf);
    return true;
}

State stateAt(const wchar_t* key) {
    std::wstring command;
    if (!commandAt(key, &command)) return State::explorer;
    if (begins(command, quoted(exePath()) + L" ")) return State::ours;
    // The same program from another place.  It was moved after it was written
    // when it is not there any more - and then a double-click on a folder
    // would start nothing.  Where it still is, it is another copy that opens
    // folders as well as this one would, and is left to it.
    const std::wstring program = commandProgram(command);
    if (!program.empty() &&
        _wcsicmp(fsx::fileName(program).c_str(), fsx::fileName(exePath()).c_str()) == 0)
        return fsx::exists(program) ? State::ours : State::moved;
    return State::other;
}

// Tells whoever caches associations (Explorer first) that they are not what
// they were.
void changed() {
    if (g_key == kRealKey) SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

bool writeAt(const wchar_t* key) {
    Key k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k.h,
                        nullptr) != ERROR_SUCCESS)
        return false;
    const std::wstring what = command();
    const wchar_t blank = 0;
    const bool written =
        RegSetValueExW(k.h, nullptr, 0, REG_SZ, (const LPBYTE)what.c_str(),
                       (DWORD)((what.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS &&
        RegSetValueExW(k.h, kDelegate, 0, REG_SZ, (const LPBYTE)&blank, sizeof(blank)) ==
            ERROR_SUCCESS;
    if (!written) return false;
    changed();
    return true;
}

bool eraseAt(const wchar_t* key) {
    const LSTATUS gone = RegDeleteKeyW(HKEY_CURRENT_USER, key);
    // (The command's key has no key under it; one that has is not ours to take out whole.)
    if (gone != ERROR_SUCCESS && gone != ERROR_FILE_NOT_FOUND) return false;
    std::wstring up = key;
    for (int i = 0; i < kMadeOnTheWay; ++i) {
        const size_t cut = up.find_last_of(L'\\');
        if (cut == std::wstring::npos) break;
        up.resize(cut);
        Key k;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, up.c_str(), 0, KEY_READ, &k.h) != ERROR_SUCCESS)
            break;
        DWORD under = 0, values = 0;
        if (RegQueryInfoKeyW(k.h, nullptr, nullptr, nullptr, &under, nullptr, nullptr, &values,
                             nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS ||
            under || values)
            break;  // something else's is in it: it stays
        RegCloseKey(k.h);
        k.h = nullptr;
        if (RegDeleteKeyW(HKEY_CURRENT_USER, up.c_str()) != ERROR_SUCCESS) break;
    }
    changed();
    return true;
}

}  // namespace

bool isFolder(const std::wstring& what) {
    return !what.empty() && fsx::isDirectory(what);
}

std::wstring command() { return quoted(exePath()) + L" --path \"%1\""; }

State state() { return stateAt(g_key); }

bool on() {
    const State now = state();
    return now == State::ours || now == State::moved;
}

bool set(bool what) {
    if (what) return writeAt(g_key);
    if (state() == State::other) return false;
    return eraseAt(g_key);
}

bool heal() {
    return state() == State::moved && set(true);
}

std::wstring explorerParameters(const std::wstring& what) { return quoted(what); }

bool handOver(const std::wstring& what) {
    const std::wstring parameters = explorerParameters(what);
    return (INT_PTR)ShellExecuteW(nullptr, L"open", L"explorer.exe", parameters.c_str(), nullptr,
                                  SW_SHOWNORMAL) > 32;
}

void useKey(const wchar_t* key) { g_key = key ? key : kRealKey; }

}  // namespace takeover
