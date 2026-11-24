// Explorer's breadcrumb dropdown, drawn in this app's Win98 style.
#pragma once

#include <windows.h>

#include <vector>

#include "app.h"

// How a list was left, for a caller that may open another in its place:
// whether a press somewhere else closed it, and where that press was (screen
// coordinates).
struct FolderListEnd {
    bool pressedElsewhere = false;
    POINT press{};
};

// A white list with an icon and a name per row, and a scrollbar when it is
// long.  It opens under `anchor` (screen coordinates) with row `current`
// selected when there is one.  Returns the row that was chosen, -1 when the
// list was left without choosing.
int showFolderList(HWND owner, const std::vector<FolderRow>& rows, int current,
                   const RECT& anchor, bool systemIcons, FolderListEnd* end = nullptr);

// The list that is up, if any (its window class is MyOldComputerFolderList).
HWND folderListWindow();
// ... and its rows, in order, while it is up.
const std::vector<FolderRow>* folderListRows(HWND list);

// What a list window shows: which row is first in view, how many are in view,
// and which one is highlighted.
struct FolderListInfo {
    int first = 0;
    int visible = 0;
    int highlighted = -1;
    int rowH = 0;
    RECT rows{};  // where the rows are, in its client coordinates
};
bool folderListInfo(HWND list, FolderListInfo* info);
