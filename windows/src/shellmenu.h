// Hosts the real Windows shell context menu (IContextMenu), so every installed
// shell extension - 7-Zip, Git, editors, Send To, Previous Versions - appears
// exactly as it does in Explorer.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "settings.h"

namespace shellmenu {

enum class Command {
    None,
    Open,
    OpenNewWindow,
    ViewBig,
    ViewList,
    ViewDetails,
    SortName,
    SortSize,
    SortType,
    SortDate,
    Refresh,
    ToggleHidden,
    Terminal,
    Rename,
    NewFolder,
    NewTextFile,
    Settings,
    Preview,
    Reveal,
    CopyPath,
};

// State needed to draw our own working view/sort items in the background menu.
struct ViewState {
    int mode = 0;  // 0 details, 1 icons, 2 list
    int sortColumn = 0;
    bool showHidden = false;
    unsigned menuMask = kMenuDefaults;  // MenuOption flags from settings.h
    std::vector<std::wstring> hidden;   // top-level labels to leave out
};

struct Result {
    bool shown = false;      // the shell menu was displayed
    bool invokedShell = false;  // a shell verb ran
    Command command = Command::None;  // one of our own items was chosen
};

// Shows the shell menu for `paths` (empty = the folder background menu).
Result show(HWND owner, const std::wstring& folder, const std::vector<std::wstring>& paths,
            POINT screenPoint, const ViewState& state);

// Forwards WM_INITMENUPOPUP / WM_DRAWITEM / WM_MEASUREITEM to the open menu's
// shell extensions.  Returns true when it handled the message.
bool forwardMessage(UINT msg, WPARAM wp, LPARAM lp);

// Used by --uitest: the final top-level labels of the menu that would be shown.
std::vector<std::wstring> labels(const std::wstring& folder,
                                 const std::vector<std::wstring>& paths, const ViewState& state);

// Top-level labels the shell itself contributes, for the settings dialog.
std::vector<std::wstring> shellLabels(const std::wstring& folder,
                                      const std::vector<std::wstring>& paths, bool background);

// Used by --uitest: can the shell produce a menu for this path?
bool canBuildItemMenu(const std::wstring& path);
bool canBuildBackgroundMenu(const std::wstring& folder);

}  // namespace shellmenu
