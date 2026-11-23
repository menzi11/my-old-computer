#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Persisted state, kept in %APPDATA%\My Old Computer\settings.ini so the
// Windows build behaves like the macOS one (window frame, sidebar, favorites).
// Which entries the window's context menus contain.  The shell menu itself
// supplies its own items; these flags control the extras this app adds.
enum MenuOption : unsigned {
    kMenuShell = 1u << 0,  // host the native Windows shell menu for items
    kMenuItemNewWindow = 1u << 1,
    kMenuItemPreview = 1u << 2,
    kMenuItemReveal = 1u << 3,
    kMenuItemCopyPath = 1u << 4,
    kMenuItemTerminal = 1u << 5,
    kMenuItemRename = 1u << 6,
    kMenuBgView = 1u << 7,
    kMenuBgSort = 1u << 8,
    kMenuBgRefresh = 1u << 9,
    kMenuBgNew = 1u << 10,
    kMenuBgTerminal = 1u << 11,
    kMenuBgHidden = 1u << 12,
    kMenuBgSettings = 1u << 13,
};

constexpr unsigned kMenuDefaults =
    kMenuShell | kMenuItemNewWindow | kMenuItemPreview | kMenuItemReveal | kMenuItemCopyPath |
    kMenuItemTerminal | kMenuItemRename | kMenuBgView | kMenuBgSort | kMenuBgRefresh |
    kMenuBgNew | kMenuBgTerminal | kMenuBgHidden | kMenuBgSettings;

struct Settings {
    int x = -1, y = -1, w = 1000, h = 640;
    int sidebarWidth = 200;
    int viewMode = 0;  // 0 details, 1 icons, 2 list
    bool showHidden = false;
    bool useSystemIcons = true;
    unsigned menuMask = kMenuDefaults;
    std::vector<std::wstring> favorites;

    void load();
    void save() const;
};

std::wstring settingsPath();
