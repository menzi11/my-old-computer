#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Persisted state, kept in %APPDATA%\My Old Computer\settings.ini so the
// Windows build behaves like the macOS one (window frame, sidebar, favorites).
struct Settings {
    int x = -1, y = -1, w = 1000, h = 640;
    int sidebarWidth = 200;
    int viewMode = 0;  // 0 details, 1 icons, 2 list
    bool showHidden = false;
    std::vector<std::wstring> favorites;

    void load();
    void save() const;
};

std::wstring settingsPath();
