#include <windows.h>
#include <windowsx.h>

#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "canvas.h"
#include "debug.h"
#include "dialog.h"
#include "dnd.h"
#include "fs.h"
#include "ids.h"
#include "preview.h"
#include "render.h"
#include "scrollbar.h"
#include "settings.h"
#include "snapshot.h"
#include "theme.h"

namespace {

App g_app;
bool g_isSnapshot = false;

int pxOf(double scale, double v) { return (int)(v * scale + 0.5); }

UINT g_dumpMsg = 0;

void dumpLayout(App& a) {
    wchar_t path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"moc-layout.txt");
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"w, ccs=UTF-8") != 0 || !f) return;
    fwprintf(f, L"client %dx%d scale %.2f\n", a.width, a.height, a.scale);
    auto rect = [f](const char* name, const RECT& r) {
        fwprintf(f, L"%hs %d,%d %dx%d\n", name, (int)r.left, (int)r.top, (int)(r.right - r.left),
                 (int)(r.bottom - r.top));
    };
    rect("title", a.lay.title);
    rect("menu", a.lay.menu);
    rect("tool", a.lay.tool);
    rect("addr", a.lay.addr);
    rect("status", a.lay.status);
    rect("sidebar", a.lay.sidebar);
    rect("splitter", a.lay.splitter);
    rect("content", a.lay.content);
    rect("treeViewport", a.lay.treeViewport);
    rect("listHeader", a.lay.listHeader);
    rect("listViewport", a.lay.listViewport);
    rect("gridViewport", a.lay.gridViewport);
    fwprintf(f, L"nameW %d\n", a.lay.nameW);
    for (int i = 0; i < 6; ++i) {
        RECT r = a.menuItemRect(i);
        fwprintf(f, L"menuItem %d %d,%d %dx%d\n", i, (int)r.left, (int)r.top,
                 (int)(r.right - r.left), (int)(r.bottom - r.top));
    }
    for (int i = 0; i < 6; ++i) {
        RECT r = a.toolbarItemRect(i);
        fwprintf(f, L"toolItem %d %d,%d %dx%d\n", i, (int)r.left, (int)r.top,
                 (int)(r.right - r.left), (int)(r.bottom - r.top));
    }
    fwprintf(f, L"current %ls\n", a.current.c_str());
    fwprintf(f, L"sysicons %d hidden %d\n", a.useSystemIcons ? 1 : 0, a.showHidden ? 1 : 0);
    fwprintf(f, L"scrollers tree %d list %d grid %d\n", a.treeScroll, a.listScroll, a.gridScroll);
    fwprintf(f, L"treeRows %d selected %d\n", (int)a.treeFlat.size(), a.treeSelectedRow());
    for (int i = 0; i < (int)a.treeFlat.size() && i < 60; ++i) {
        RECT r = a.treeRowRect(i);
        fwprintf(f, L"treeRow %d %d,%d depth %d title %ls\n", i, (int)r.left, (int)r.top,
                 a.treeFlat[i]->depth, a.treeFlat[i]->title.c_str());
    }
    fwprintf(f, L"listRows %d mode %d rowH %d\n", (int)a.entries.size(), (int)a.mode, a.m.rowH);
    for (int i = 0; i < 40 && i < (int)a.entries.size(); ++i) {
        RECT r = a.mode == ViewMode::Details ? a.listRowRect(i) : a.gridCellRect(i);
        fwprintf(f, L"row %d %d,%d %dx%d name %ls\n", i, (int)r.left, (int)r.top,
                 (int)(r.right - r.left), (int)(r.bottom - r.top), a.entries[i].name.c_str());
    }
    fwprintf(f, L"selection %d\n", (int)a.selection.size());
    fclose(f);
}

void repaint(HWND hwnd) { InvalidateRect(hwnd, nullptr, FALSE); }

void paintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Canvas canvas(mem, g_app.scale);
        renderApp(canvas, g_app);
    }
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ---------------------------------------------------------------- selection

int firstSelectedIndex(const App& a) {
    for (int i = 0; i < (int)a.entries.size(); ++i)
        if (a.selection.count(a.entries[i].path)) return i;
    return -1;
}

void selectOnly(App& a, int index) {
    a.selection.clear();
    if (index >= 0 && index < (int)a.entries.size()) a.selection.insert(a.entries[index].path);
}

void selectRange(App& a, int from, int to) {
    if (from < 0) from = 0;
    if (to < from) std::swap(from, to);
    for (int i = from; i <= to && i < (int)a.entries.size(); ++i) a.selection.insert(a.entries[i].path);
}

void ensureVisible(App& a, int index) {
    if (index < 0) return;
    if (a.mode == ViewMode::Details) {
        int top = index * a.m.rowH;
        int bottom = top + a.m.rowH;
        int view = a.lay.listViewport.bottom - a.lay.listViewport.top;
        if (top < a.listScroll) a.listScroll = top;
        else if (bottom > a.listScroll + view) a.listScroll = bottom - view;
    } else if (a.mode == ViewMode::List) {
        int ch = a.gridCellH();
        int cols = a.gridCols();
        int row = cols > 0 ? index / cols : 0;
        int top = row * ch;
        int view = a.lay.gridViewport.bottom - a.lay.gridViewport.top;
        if (top < a.gridScroll) a.gridScroll = top;
        else if (top + ch > a.gridScroll + view) a.gridScroll = top + ch - view;
    } else {
        RECT r = a.gridCellRect(index);
        int top = r.top - a.lay.gridViewport.top;
        int bottom = r.bottom - a.lay.gridViewport.top;
        int view = a.lay.gridViewport.bottom - a.lay.gridViewport.top;
        if (top < 0) a.gridScroll += top;
        else if (bottom > view) a.gridScroll += bottom - view;
    }
    a.clampScroll();
}

void moveSelection(App& a, int dx, int dy) {
    int cur = firstSelectedIndex(a);
    int n = (int)a.entries.size();
    if (n == 0) return;
    if (cur < 0) {
        selectOnly(a, 0);
        a.selAnchor = 0;
        ensureVisible(a, 0);
        return;
    }
    int idx = cur;
    if (a.mode == ViewMode::Details) {
        idx = std::clamp(cur + dy, 0, n - 1);
    } else if (a.mode == ViewMode::List) {
        int cols = std::max(1, a.gridCols());
        idx = cur + dx + dy * cols;
        idx = std::clamp(idx, 0, n - 1);
    } else {
        int cols = std::max(1, a.gridCols());
        idx = cur + dx + dy * cols;
        idx = std::clamp(idx, 0, n - 1);
    }
    selectOnly(a, idx);
    a.selAnchor = idx;
    ensureVisible(a, idx);
}

void marqueeUpdate(App& a) {
    // +1 on the far edges: a perfectly vertical/horizontal drag has zero width
    // as a RECT and would otherwise select nothing.
    RECT r{std::min(a.marqueeStart.x, a.marqueeEnd.x), std::min(a.marqueeStart.y, a.marqueeEnd.y),
           std::max(a.marqueeStart.x, a.marqueeEnd.x) + 1,
           std::max(a.marqueeStart.y, a.marqueeEnd.y) + 1};
    bool additive = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    if (!additive) a.selection.clear();
    for (int i = 0; i < (int)a.entries.size(); ++i) {
        // Only items the band actually touches: the icon and the name text.
        // A band drawn in the blank part of the row must not pick anything.
        RECT hit{};
        if (a.mode == ViewMode::Details) {
            hit = a.listRowRect(i);
            hit.right = hit.left + std::min(a.lay.nameW, a.nameExtent(i));
        } else {
            hit = a.gridCellRect(i);
        }
        RECT inter{};
        if (IntersectRect(&inter, &hit, &r)) a.selection.insert(a.entries[i].path);
    }
}

void startMarquee(HWND hwnd, App& a, POINT p) {
    if (!(GetKeyState(VK_CONTROL) & 0x8000)) a.selection.clear();
    a.marqueeStart = p;
    a.marqueeEnd = p;
    a.marquee = true;
    a.dragArmed = false;
    a.dragPath.clear();
    SetCapture(hwnd);
    repaint(hwnd);
}

// ---------------------------------------------------------------- dragging

// The actual drag is OLE based (see dnd.cpp); the window's IDropTarget keeps
// app.dragTargetFolder up to date while the pointer is inside the window.

// ---------------------------------------------------------------- commands

void showAppMenu(HWND hwnd, App& a, int index, POINT p) {
    if (index < 0 || index >= 6) return;
    std::vector<MenuItem> items = a.menuFor(a.menuTitle(index));
    a.pressMenu = index;
    repaint(hwnd);
    UpdateWindow(hwnd);
    int x = a.menuItemRect(index).left, y = a.menuItemRect(index).bottom;
    POINT screen{x, y};
    ClientToScreen(hwnd, &screen);
    int id = showPopupMenu(hwnd, items, screen.x, screen.y, false);
    a.pressMenu = -1;
    if (id) a.performMenuAction(id);
    repaint(hwnd);
    (void)p;
}

void showContextMenu(HWND hwnd, App& a, POINT p) {
    std::vector<MenuItem> items;
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        int row = a.treeRowAt(p);
        std::wstring path;
        bool fav = false;
        if (row >= 0) {
            TreeNode* n = a.treeFlat[row];
            path = n->path;
            fav = n->kind == TreeNode::Favorite;
            a.selection.clear();
            a.hoverTree = row;
        }
        a.contextFolder = path;
        a.contextIsFavorite = fav;
        items = a.treeContextMenu(path, fav);
    } else {
        int row = -1;
        if (a.mode == ViewMode::Details) {
            row = a.listRowAt(p);
        } else {
            int idx = a.gridIndexAt(p);
            if (idx >= 0) row = idx;
        }
        if (row >= 0 && row < (int)a.entries.size()) {
            const Entry& e = a.entries[row];
            if (!a.selection.count(e.path)) selectOnly(a, row);
            a.contextFolder = e.isDir ? e.path : L"";
        } else {
            a.selection.clear();
            a.contextFolder.clear();
        }
        a.contextIsFavorite = false;
        items = a.contextMenu();
    }
    POINT screen = p;
    ClientToScreen(hwnd, &screen);
    int id = showPopupMenu(hwnd, items, screen.x, screen.y, false);
    if (id) a.performMenuAction(id);
    repaint(hwnd);
}

// ---------------------------------------------------------------- input

void onMouseMove(HWND hwnd, App& a, POINT p) {
    bool changed = false;

    if (a.splitterDrag) {
        int dx = (int)((p.x - a.splitterStartX) / std::max(0.01, a.scale));
        int minSw = 120;
        int maxSw = std::max(minSw, (int)((a.width - 2 * a.m.edge) / a.scale) - 260);
        a.sidebarWidth = std::clamp(a.splitterStartW + dx, minSw, maxSw);
        a.relayout(a.width, a.height);
        repaint(hwnd);
        return;
    }

    if (a.scrollDrag) {
        int delta = p.y - a.scrollDragStartY;
        if (a.scrollDrag == 1) {
            a.listScroll = scrollbar::positionFromDrag(a.lay.listBar, a.m, a.listContentHeight(),
                                                      a.lay.listViewport.bottom - a.lay.listViewport.top,
                                                      a.scrollDragStartPos, delta);
        } else if (a.scrollDrag == 2) {
            a.gridScroll = scrollbar::positionFromDrag(a.lay.gridBar, a.m, a.gridContentHeight(),
                                                      a.lay.gridViewport.bottom - a.lay.gridViewport.top,
                                                      a.scrollDragStartPos, delta);
        } else {
            a.treeScroll = scrollbar::positionFromDrag(a.lay.treeBar, a.m, a.treeContentHeight(),
                                                      a.lay.treeViewport.bottom - a.lay.treeViewport.top,
                                                      a.scrollDragStartPos, delta);
        }
        repaint(hwnd);
        return;
    }

    if (a.dragArmed && !a.dragging) {
        int dx = p.x - a.marqueeStart.x, dy = p.y - a.marqueeStart.y;
        const int tx = std::max(3, GetSystemMetrics(SM_CXDRAG) / 2);
        const int ty = std::max(3, GetSystemMetrics(SM_CYDRAG) / 2);
        if (std::abs(dx) > tx || std::abs(dy) > ty) {
            std::vector<std::wstring> paths;
            for (auto& e : a.entries)
                if (a.selection.count(e.path)) paths.push_back(e.path);
            if (paths.empty() && !a.dragPath.empty()) paths.push_back(a.dragPath);
            a.dragArmed = false;
            a.dragging = true;
            if (GetCapture() == hwnd) ReleaseCapture();
            DWORD effect = 0;
            diagLog(L"startDrag paths=%d first=%ls", (int)paths.size(),
                    paths.empty() ? L"" : paths.front().c_str());
            dnd::dragOut(hwnd, paths, &effect);
            a.dragging = false;
            a.dragPath.clear();
            a.dragTargetFolder.clear();
            a.dragTargetIsTree = false;
            a.dragTargetRow = -1;
            a.reload();
            repaint(hwnd);
            return;
        }
    }
    if (a.marquee) {
        // Auto-scroll when the pointer is dragged past the edge of the list.
        const RECT& vp = a.mode == ViewMode::Details ? a.lay.listViewport : a.lay.gridViewport;
        const int step = a.mode == ViewMode::Details ? a.m.rowH : a.gridCellH();
        if (p.y < vp.top) {
            if (a.mode == ViewMode::Details) a.scrollListBy(-step);
            else a.scrollGridBy(-step);
        } else if (p.y >= vp.bottom) {
            if (a.mode == ViewMode::Details) a.scrollListBy(step);
            else a.scrollGridBy(step);
        }
        a.marqueeEnd = p;
        marqueeUpdate(a);
        repaint(hwnd);
        return;
    }

    int hoverTree = a.sidebarVisible ? a.treeRowAt(p) : -1;
    int hoverTool = PtInRect(&a.lay.tool, p) ? a.toolbarIndexAt(p) : -1;
    int hoverMenu = PtInRect(&a.lay.menu, p) ? a.menuItemAt(p) : -1;
    if (hoverTree != a.hoverTree || hoverTool != a.hoverTool || hoverMenu != a.hoverMenu) {
        a.hoverTree = hoverTree;
        a.hoverTool = hoverTool;
        a.hoverMenu = hoverMenu;
        changed = true;
    }
    if (changed) repaint(hwnd);
}

void onLButtonDown(HWND hwnd, App& a, POINT p) {
    SetFocus(hwnd);
    a.pressTitle = -1;
    a.pressTool = -1;

    for (int i = 0; i < 3; ++i) {
        RECT r = a.titleButtonRect(i);
        if (PtInRect(&r, p)) {
            a.pressTitle = i;
            SetCapture(hwnd);
            repaint(hwnd);
            return;
        }
    }
    if (a.sidebarVisible && PtInRect(&a.lay.splitter, p)) {
        a.splitterDrag = true;
        a.splitterStartW = a.sidebarWidth;
        a.splitterStartX = p.x;
        SetCapture(hwnd);
        return;
    }
    int menuIdx = PtInRect(&a.lay.menu, p) ? a.menuItemAt(p) : -1;
    if (menuIdx >= 0) {
        showAppMenu(hwnd, a, menuIdx, p);
        return;
    }
    int toolIdx = PtInRect(&a.lay.tool, p) ? a.toolbarIndexAt(p) : -1;
    if (toolIdx >= 0) {
        a.pressTool = toolIdx;
        SetCapture(hwnd);
        repaint(hwnd);
        return;
    }
    if (PtInRect(&a.lay.addrField, p)) {
        a.beginAddressEdit();
        return;
    }
    if (PtInRect(&a.lay.addrArrow, p)) {
        std::vector<MenuItem> items = a.placesMenu();
        POINT screen{p.x, a.lay.addr.bottom};
        ClientToScreen(hwnd, &screen);
        int id = showPopupMenu(hwnd, items, screen.x, screen.y, false);
        if (id) a.performMenuAction(id);
        repaint(hwnd);
        return;
    }
    if (PtInRect(&a.lay.addrGo, p)) {
        if (a.addrEditing) a.endAddressEdit(true);
        else a.reload();
        repaint(hwnd);
        return;
    }

    if (a.sidebarVisible && PtInRect(&a.lay.treeBar, p)) {
        int content = a.treeContentHeight();
        int view = a.lay.treeViewport.bottom - a.lay.treeViewport.top;
        auto part = scrollbar::hitTest(a.lay.treeBar, p, a.m, content, view, a.treeScroll);
        int page = std::max(a.m.rowH, view - a.m.rowH);
        if (part == scrollbar::Part::Decrement) a.scrollTreeBy(-3 * a.m.rowH);
        else if (part == scrollbar::Part::Increment) a.scrollTreeBy(3 * a.m.rowH);
        else if (part == scrollbar::Part::PageUp) a.scrollTreeBy(-page);
        else if (part == scrollbar::Part::PageDown) a.scrollTreeBy(page);
        else if (part == scrollbar::Part::Knob) {
            a.scrollDrag = 3;
            a.scrollDragStartY = p.y;
            a.scrollDragStartPos = a.treeScroll;
            SetCapture(hwnd);
        }
        repaint(hwnd);
        return;
    }

    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        int row = a.treeRowAt(p);
        if (row >= 0) {
            TreeNode* n = a.treeFlat[row];
            RECT rr = a.treeRowRect(row);
            int indent = pxOf(a.scale, n->depth * 14 + 3);
            RECT box{rr.left + indent, rr.top + pxOf(a.scale, 5), rr.left + indent + pxOf(a.scale, 9),
                     rr.top + pxOf(a.scale, 5) + pxOf(a.scale, 9)};
            RECT boxHit{box.left - pxOf(a.scale, 2), box.top - pxOf(a.scale, 2),
                        box.right + pxOf(a.scale, 2), box.bottom + pxOf(a.scale, 2)};
            if (n->kind == TreeNode::Favorites) {
                a.toggleTreeNode(*n);
            } else if (n->hasChildren && PtInRect(&boxHit, p)) {
                a.toggleTreeNode(*n);
            } else if (!n->path.empty()) {
                // Tree clicks only select and update the right pane; expanding
                // and scrolling here made the tree jump under the pointer.
                a.treeClickRow = row;
                a.loadDirectory(n->path, true, true);
            }
            repaint(hwnd);
            return;
        }
        return;
    }

    if (a.mode == ViewMode::Details) {
        if (PtInRect(&a.lay.listBar, p)) {
            int content = a.listContentHeight();
            int view = a.lay.listViewport.bottom - a.lay.listViewport.top;
            auto part = scrollbar::hitTest(a.lay.listBar, p, a.m, content, view, a.listScroll);
            int page = std::max(a.m.rowH, view - a.m.rowH);
            if (part == scrollbar::Part::Decrement) a.scrollListBy(-3 * a.m.rowH);
            else if (part == scrollbar::Part::Increment) a.scrollListBy(3 * a.m.rowH);
            else if (part == scrollbar::Part::PageUp) a.scrollListBy(-page);
            else if (part == scrollbar::Part::PageDown) a.scrollListBy(page);
            else if (part == scrollbar::Part::Knob) {
                a.scrollDrag = 1;
                a.scrollDragStartY = p.y;
                a.scrollDragStartPos = a.listScroll;
                SetCapture(hwnd);
            }
            repaint(hwnd);
            return;
        }
        if (PtInRect(&a.lay.listHeader, p)) {
            for (int i = 0; i < 4; ++i) {
                if (p.x >= a.lay.colX[i] && p.x < a.lay.colX[i] + a.lay.colW[i]) {
                    if (a.sortColumn == i) a.sortAsc = !a.sortAsc;
                    else {
                        a.sortColumn = i;
                        a.sortAsc = true;
                    }
                    fsx::sortEntries(a.entries, a.sortColumn, a.sortAsc);
                    repaint(hwnd);
                    break;
                }
            }
            return;
        }
    } else if (PtInRect(&a.lay.gridBar, p)) {
        int content = a.gridContentHeight();
        int view = a.lay.gridViewport.bottom - a.lay.gridViewport.top;
        auto part = scrollbar::hitTest(a.lay.gridBar, p, a.m, content, view, a.gridScroll);
        int page = std::max(a.m.rowH, view - a.m.rowH);
        if (part == scrollbar::Part::Decrement) a.scrollGridBy(-3 * a.m.rowH);
        else if (part == scrollbar::Part::Increment) a.scrollGridBy(3 * a.m.rowH);
        else if (part == scrollbar::Part::PageUp) a.scrollGridBy(-page);
        else if (part == scrollbar::Part::PageDown) a.scrollGridBy(page);
        else if (part == scrollbar::Part::Knob) {
            a.scrollDrag = 2;
            a.scrollDragStartY = p.y;
            a.scrollDragStartPos = a.gridScroll;
            SetCapture(hwnd);
        }
        repaint(hwnd);
        return;
    }

    int itemIndex = -1;
    if (a.hitTestItem(p, &itemIndex)) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const std::wstring& path = a.entries[itemIndex].path;
        if (ctrl) {
            if (a.selection.count(path)) a.selection.erase(path);
            else a.selection.insert(path);
            a.selAnchor = itemIndex;
        } else if (shift) {
            a.selection.clear();
            selectRange(a, a.selAnchor >= 0 ? a.selAnchor : itemIndex, itemIndex);
        } else if (!a.selection.count(path)) {
            selectOnly(a, itemIndex);
            a.selAnchor = itemIndex;
        }
        a.dragPath = path;
        a.dragArmed = true;
        a.marqueeStart = p;
        SetCapture(hwnd);
        repaint(hwnd);
        return;
    }

    // Blank canvas inside the file view (the empty part of a row, the space
    // below the last row, the padding around an icon): start a rubber band.
    if (a.mode == ViewMode::Details) {
        if (PtInRect(&a.lay.listViewport, p)) {
            startMarquee(hwnd, a, p);
            return;
        }
    } else if (PtInRect(&a.lay.gridViewport, p)) {
        startMarquee(hwnd, a, p);
        return;
    }
}

void onLButtonUp(HWND hwnd, App& a, POINT p) {
    if (GetCapture() == hwnd) ReleaseCapture();
    if (a.pressTitle >= 0) {
        int i = a.pressTitle;
        a.pressTitle = -1;
        RECT r = a.titleButtonRect(i);
        if (PtInRect(&r, p)) {
            if (i == 0) ShowWindow(hwnd, SW_MINIMIZE);
            else if (i == 1) ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
            else PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
        repaint(hwnd);
        return;
    }
    if (a.pressTool >= 0) {
        int i = a.pressTool;
        a.pressTool = -1;
        RECT r = a.toolbarItemRect(i);
        if (PtInRect(&r, p)) {
            switch (i) {
            case 0: a.goBack(); break;
            case 1: a.goForward(); break;
            case 2: a.goUp(); break;
            case 3:
                a.sidebarVisible = !a.sidebarVisible;
                a.relayout(a.width, a.height);
                break;
            case 4: a.cycleViewMode(); break;
            case 6: a.performMenuAction(ID_TOGGLE_HIDDEN); break;
            default: a.reload(); break;
            }
        }
        repaint(hwnd);
        return;
    }
    if (a.splitterDrag) {
        a.splitterDrag = false;
        a.saveSettings();
        return;
    }
    if (a.scrollDrag) {
        a.scrollDrag = 0;
        return;
    }
    if (a.marquee) {
        a.marquee = false;
        repaint(hwnd);
        return;
    }
    if (a.dragArmed || a.dragging) {
        a.dragArmed = false;
        a.dragging = false;
        a.dragPath.clear();
        repaint(hwnd);
    }
}

void onDoubleClick(HWND hwnd, App& a, POINT p) {
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        int row = a.treeRowAt(p);
        diagLog(L"DBLCLK tree %d,%d row=%d", p.x, p.y, row);
        if (row >= 0) {
            TreeNode* n = a.treeFlat[row];
            if (n->hasChildren) a.toggleTreeNode(*n);
            repaint(hwnd);
        }
        return;
    }
    if (a.mode == ViewMode::Details) {
        int row = a.listRowAt(p);
        diagLog(L"DBLCLK %d,%d row=%d", p.x, p.y, row);
        if (row >= 0) {
            a.openEntry(a.entries[row]);
            repaint(hwnd);
        }
    } else {
        int idx = a.gridIndexAt(p);
        diagLog(L"DBLCLK %d,%d idx=%d", p.x, p.y, idx);
        if (idx >= 0) {
            a.openEntry(a.entries[idx]);
            repaint(hwnd);
        }
    }
}

void onKeyDown(HWND hwnd, App& a, WPARAM key) {
    diagLog(L"KEYDOWN %u sel=%d", (unsigned)key, (int)a.selection.size());
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    (void)shift;
    switch (key) {
    case VK_RETURN: a.openSelection(); break;
    case VK_BACK: a.goUp(); break;
    case VK_DELETE: a.deleteSelection(); break;
    case VK_F2: a.renameSelection(); break;
    case VK_F5: a.reload(); break;
    case VK_SPACE:
        if (preview::visible()) preview::hide();
        else if (const Entry* e = a.firstSelected()) preview::show(hwnd, e->path);
        break;
    case VK_ESCAPE:
        if (a.marquee || a.dragging) {
            a.marquee = false;
            a.dragging = false;
            a.dragArmed = false;
        } else {
            a.clearSelection();
        }
        break;
    case VK_LEFT:
        if (alt) a.goBack();
        else moveSelection(a, -1, 0);
        break;
    case VK_RIGHT:
        if (alt) a.goForward();
        else moveSelection(a, 1, 0);
        break;
    case VK_UP:
        if (alt) a.goUp();
        else moveSelection(a, 0, -1);
        break;
    case VK_DOWN: moveSelection(a, 0, 1); break;
    case 'A': if (ctrl) a.selectAll(); break;
    case 'C': if (ctrl) a.copySelection(false); break;
    case 'X': if (ctrl) a.copySelection(true); break;
    case 'V': if (ctrl) a.paste(); break;
    case '1': if (ctrl) a.setViewMode(ViewMode::Details); break;
    case '2': if (ctrl) a.setViewMode(ViewMode::List); break;
    case '3': if (ctrl) a.setViewMode(ViewMode::Icons); break;
    default: break;
    }
    a.clampScroll();
    repaint(hwnd);
}

void onMouseWheel(HWND hwnd, App& a, POINT p, int delta) {
    POINT client = p;
    ScreenToClient(hwnd, &client);
    int steps = delta / WHEEL_DELTA;
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, client)) {
        a.scrollTreeBy(-steps * 3 * a.m.rowH);
    } else if (a.mode == ViewMode::Details) {
        a.scrollListBy(-steps * 3 * a.m.rowH);
    } else {
        int rows = a.mode == ViewMode::Icons ? 2 : 3;
        a.scrollGridBy(-steps * rows * a.gridCellH());
    }
    repaint(hwnd);
}

void onDropFiles(HWND hwnd, App& a, HDROP drop) {
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    std::vector<std::wstring> paths;
    for (UINT i = 0; i < count; ++i) {
        wchar_t buf[32768];
        if (DragQueryFileW(drop, i, buf, 32768)) paths.push_back(buf);
    }
    POINT p;
    DragQueryPoint(drop, &p);
    DragFinish(drop);
    std::wstring target;
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        int row = a.treeRowAt(p);
        if (row >= 0) target = a.treeFlat[row]->path;
    } else if (a.mode == ViewMode::Details) {
        int row = a.listRowAt(p);
        if (row >= 0 && a.entries[row].isDir) target = a.entries[row].path;
    } else {
        int idx = a.gridIndexAt(p);
        if (idx >= 0 && a.entries[idx].isDir) target = a.entries[idx].path;
    }
    a.performDrop(paths, target);
    repaint(hwnd);
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    App& a = g_app;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paintWindow(hwnd);
        return 0;
    case WM_PRINTCLIENT: {
        Canvas canvas((HDC)wp, g_app.scale);
        renderApp(canvas, g_app);
        return 0;
    }
    case WM_SETCURSOR: {
        if (LOWORD(lp) == HTCLIENT) {
            POINT p;
            GetCursorPos(&p);
            ScreenToClient(hwnd, &p);
            if (g_app.sidebarVisible && PtInRect(&g_app.lay.splitter, p)) {
                SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
                return TRUE;
            }
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
            return TRUE;
        }
        break;
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        const int grip = pxOf(a.scale, 4);
        if (!IsZoomed(hwnd)) {
            bool left = p.x < grip, right = p.x >= a.width - grip;
            bool top = p.y < grip, bottom = p.y >= a.height - grip;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
        }
        if (p.y >= a.m.edge && p.y < a.m.edge + a.m.titleH) {
            for (int i = 0; i < 3; ++i) {
                RECT r = a.titleButtonRect(i);
                if (PtInRect(&r, p)) return HTCLIENT;
            }
            return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        auto* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = pxOf(a.scale, 700);
        mmi->ptMinTrackSize.y = pxOf(a.scale, 440);
        return 0;
    }
    case WM_NCCALCSIZE:
        if (wp == TRUE) {
            if (IsZoomed(hwnd)) {
                // A custom frame has no non-client area, but Windows still
                // maximises us with an invisible resize border.  Pin the client
                // to the monitor work area so nothing is clipped off screen.
                HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO mi{sizeof(mi)};
                if (GetMonitorInfoW(mon, &mi)) {
                    ((NCCALCSIZE_PARAMS*)lp)->rgrc[0] = mi.rcWork;
                }
            }
            return 0;
        }
        break;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            int w = LOWORD(lp), h = HIWORD(lp);
            diagLog(L"WM_SIZE %dx%d scale=%.2f", w, h, a.scale);
            if (!IsZoomed(hwnd) && a.scale > 0) {
                a.logicalW = (int)std::lround(w / a.scale);
                a.logicalH = (int)std::lround(h / a.scale);
            }
            a.relayout(w, h);
            repaint(hwnd);
        }
        return 0;
    case WM_DPICHANGED: {
        RECT* r = (RECT*)lp;
        double newScale = HIWORD(wp) / 96.0;
        a.setScale(newScale);
        if (IsZoomed(hwnd)) {
            // Windows already resized the maximized window; only move it.
            SetWindowPos(hwnd, nullptr, r->left, r->top, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            int w = (int)std::lround(a.logicalW * newScale);
            int h = (int)std::lround(a.logicalH * newScale);
            SetWindowPos(hwnd, nullptr, r->left, r->top, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_SETFOCUS:
        a.windowActive = true;
        repaint(hwnd);
        return 0;
    case WM_KILLFOCUS:
        a.windowActive = false;
        repaint(hwnd);
        return 0;
    case WM_CAPTURECHANGED:
        // Something stole the mouse: never leave a rubber band or a pending
        // drag armed.
        if (a.marquee) a.marquee = false;
        if (a.dragArmed && !a.dragging) {
            a.dragArmed = false;
            a.dragPath.clear();
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!a.splitterDrag && !a.scrollDrag && !a.marquee && !a.dragging && !a.dragArmed) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        onMouseMove(hwnd, a, p);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (a.hoverTree != -1 || a.hoverTool != -1 || a.hoverMenu != -1) {
            a.hoverTree = a.hoverTool = a.hoverMenu = -1;
            repaint(hwnd);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        onLButtonDown(hwnd, a, p);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        onLButtonUp(hwnd, a, p);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        onDoubleClick(hwnd, a, p);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        showContextMenu(hwnd, a, p);
        return 0;
    }
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        onMouseWheel(hwnd, a, p, GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }
    case WM_KEYDOWN:
        onKeyDown(hwnd, a, wp);
        return 0;
    case WM_DROPFILES:
        onDropFiles(hwnd, a, (HDROP)wp);
        return 0;
    case WM_CLOSE:
        a.saveSettings();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        dnd::revokeTarget(hwnd);
        preview::hide();
        PostQuitMessage(0);
        return 0;
    default:
        if (g_dumpMsg && msg == g_dumpMsg) {
            dumpLayout(a);
            return 0;
        }
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void enableDpiAwareness() {
    typedef BOOL(WINAPI * SetCtxFn)(DPI_AWARENESS_CONTEXT);
    static auto setCtx = (SetCtxFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                  "SetProcessDpiAwarenessContext");
    if (setCtx) {
        setCtx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    } else {
        SetProcessDPIAware();
    }
}

double dpiScaleFor(HWND hwnd) {
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static auto getDpi = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                           "GetDpiForWindow");
    if (getDpi) {
        UINT dpi = getDpi(hwnd);
        if (dpi >= 96) return dpi / 96.0;
    }
    return 1.0;
}

std::wstring argValue(const std::vector<std::wstring>& args, const wchar_t* name) {
    for (size_t i = 0; i + 1 < args.size(); ++i)
        if (args[i] == name) return args[i + 1];
    return {};
}

bool hasArg(const std::vector<std::wstring>& args, const wchar_t* name) {
    for (auto& a : args)
        if (a == name) return true;
    return false;
}

// Drives the same input handlers the window procedure uses, with synthesized
// points, so click/selection behaviour can be checked without touching the
// mouse or a real window.
int runUiTest(App& app) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::wstring base = std::wstring(temp) + L"moc-uitest-" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(base.c_str(), nullptr);
    for (int i = 0; i < 12; ++i) {
        std::wstring f = base + L"\\item" + std::to_wstring(i) + L".txt";
        HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }

    std::vector<std::pair<std::wstring, bool>> results;
    auto writeOut = [&results, &temp]() {
        std::wstring out;
        for (auto& r : results) {
            if (!out.empty()) out += L" | ";
            out += r.first + (r.second ? L" OK" : L" FAIL");
        }
        FILE* f = nullptr;
        if (_wfopen_s(&f, (std::wstring(temp) + L"moc-uitest.txt").c_str(), L"w, ccs=UTF-8") == 0 &&
            f) {
            fwprintf(f, L"%s\n", (L"UITEST " + out).c_str());
            fclose(f);
        }
    };
    auto check = [&results, &writeOut](const wchar_t* name, bool ok) {
        results.push_back({name, ok});
        writeOut();
    };

    app.init(nullptr);
    app.setViewMode(ViewMode::Details);
    app.relayout(1100, 760);
    app.loadDirectory(base, false);

    RECT r0 = app.listRowRect(0);
    RECT r2 = app.listRowRect(2);
    POINT namePt{r0.left + 40, (r0.top + r0.bottom) / 2};
    POINT blankPt{app.lay.listViewport.left + app.lay.nameW + 40, (r0.top + r0.bottom) / 2};
    check(L"名称可命中", app.hitTestItem(namePt, nullptr));
    check(L"列空白不命中", !app.hitTestItem(blankPt, nullptr));

    app.selection.clear();
    onLButtonDown(nullptr, app, namePt);
    onLButtonUp(nullptr, app, namePt);
    check(L"单击名称选中1项", app.selection.size() == 1);

    POINT to{blankPt.x, (r2.top + r2.bottom) / 2};
    onLButtonDown(nullptr, app, blankPt);
    onMouseMove(nullptr, app, to);
    onLButtonUp(nullptr, app, to);
    check(L"列空白框选不选中", app.selection.empty());

    POINT bandTo{namePt.x, (r2.top + r2.bottom) / 2};
    onLButtonDown(nullptr, app, blankPt);
    onMouseMove(nullptr, app, bandTo);
    onLButtonUp(nullptr, app, bandTo);
    check(L"拖入名称列框选3项", app.selection.size() == 3);

    POINT below{blankPt.x, app.listRowRect(11).bottom + 20};
    onLButtonDown(nullptr, app, below);
    onLButtonUp(nullptr, app, below);
    check(L"点击空白清空选择", app.selection.empty());

    app.setViewMode(ViewMode::Icons);
    app.relayout(1100, 760);
    app.clampScroll();
    RECT cell = app.gridCellRect(0);
    POINT iconPt{(cell.left + cell.right) / 2, cell.top + 30};
    POINT padPt{cell.left + 2, cell.top + 2};
    POINT labelPt{(cell.left + cell.right) / 2, cell.top + 60};
    check(L"图标可命中", app.hitTestItem(iconPt, nullptr));
    check(L"格子留白不命中", !app.hitTestItem(padPt, nullptr));
    check(L"图标名可命中", app.hitTestItem(labelPt, nullptr));
    RECT cell2 = app.gridCellRect(2);
    onLButtonDown(nullptr, app, padPt);
    onMouseMove(nullptr, app, POINT{(cell2.left + cell2.right) / 2, cell2.top + 30});
    onLButtonUp(nullptr, app, POINT{(cell2.left + cell2.right) / 2, cell2.top + 30});
    check(L"大图标框选3项", app.selection.size() == 3);
    return 0;
}

int runSelfTest(App& app) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::wstring base = std::wstring(temp) + L"moc-selftest-" + std::to_wstring(GetTickCount64());
    CreateDirectoryW(base.c_str(), nullptr);
    std::wstring dir = fsx::joinPath(base, L"case");
    CreateDirectoryW(dir.c_str(), nullptr);
    for (int i = 0; i < 3; ++i) {
        std::wstring f = dir + L"\\f" + std::to_wstring(i) + L".txt";
        HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD wrote = 0;
            WriteFile(h, "x", 1, &wrote, nullptr);
            CloseHandle(h);
        }
    }
    CreateDirectoryW((dir + L"\\sub").c_str(), nullptr);
    std::vector<std::wstring> results;
    app.loadDirectory(dir, false);
    results.push_back(std::wstring(L"列目录 ") + (app.entries.size() == 4 ? L"OK" : L"FAIL"));
    app.setViewMode(ViewMode::Icons);
    results.push_back(std::wstring(L"大图标 ") + (app.entries.size() == 4 ? L"OK" : L"FAIL"));
    app.setViewMode(ViewMode::Details);
    app.sortColumn = 1;
    app.sortAsc = true;
    fsx::sortEntries(app.entries, 1, true);
    results.push_back(std::wstring(L"排序 ") +
                      (!app.entries.empty() && app.entries.front().isDir ? L"OK" : L"FAIL"));

    std::wstring created;
    bool made = fsx::makeFolder(dir, L"新建文件夹", &created);
    results.push_back(std::wstring(L"新建文件夹 ") + (made ? L"OK" : L"FAIL"));

    std::wstring err;
    bool renamed = fsx::renameItem(dir + L"\\f0.txt", L"renamed.txt", &err);
    results.push_back(std::wstring(L"重命名 ") + (renamed ? L"OK" : L"FAIL"));

    app.reload();
    std::vector<std::wstring> toDelete{dir + L"\\renamed.txt"};
    bool deleted = fsx::recycle(toDelete);
    results.push_back(std::wstring(L"回收站 ") + (deleted ? L"OK" : L"FAIL"));

    bool copied = fsx::copyItems({dir + L"\\f1.txt"}, dir + L"\\sub");
    results.push_back(std::wstring(L"复制 ") + (copied ? L"OK" : L"FAIL"));

    bool collide = fsx::copyItems({dir + L"\\f1.txt"}, dir);
    results.push_back(std::wstring(L"同名副本 ") +
                      (collide && fsx::exists(dir + L"\\f1 (2).txt") ? L"OK" : L"FAIL"));

    bool moved = fsx::moveItems({dir + L"\\f2.txt"}, dir + L"\\sub");
    results.push_back(std::wstring(L"移动 ") + (moved ? L"OK" : L"FAIL"));

    bool noopDrop = app.performDrop({dir + L"\\f1.txt"}, L"");
    results.push_back(std::wstring(L"拖放同目录 ") +
                      (!noopDrop && fsx::exists(dir + L"\\f1.txt") ? L"OK" : L"FAIL"));

    std::wstring out;
    for (auto& r : results) {
        if (!out.empty()) out += L" | ";
        out += r;
    }
    std::wstring line = L"SELFTEST " + out;
    OutputDebugStringW((line + L"\n").c_str());
    FILE* f = nullptr;
    if (_wfopen_s(&f, (base + L"\\selftest.txt").c_str(), L"w, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"%s\n", line.c_str());
        fclose(f);
    }
    app.loadDirectory(base, false);
    return 0;
}

int runSnapshot(App& app, const std::vector<std::wstring>& args) {
    app.demo = hasArg(args, L"--demo");
    std::wstring path = argValue(args, L"--path");
    if (path.empty()) path = fsx::homeDir();
    path = fsx::expandHome(path);
    app.current = path;
    app.init(nullptr);
    app.scale = 1.0;
    if (auto s = argValue(args, L"--scale"); !s.empty()) app.scale = _wtof(s.c_str());

    int w = 1000, h = 640;
    if (auto size = argValue(args, L"--size"); !size.empty()) {
        wchar_t* end = nullptr;
        w = (int)wcstol(size.c_str(), &end, 10);
        if (end && *end == L'x') h = (int)wcstol(end + 1, nullptr, 10);
    }
    app.relayout((int)(w * app.scale), (int)(h * app.scale));
    app.loadDirectory(path, false);

    if (auto view = argValue(args, L"--view"); !view.empty())
        app.setViewMode(view == L"icons" ? ViewMode::Icons
                                         : (view == L"list" ? ViewMode::List : ViewMode::Details));

    if (auto sort = argValue(args, L"--sort"); !sort.empty()) {
        wchar_t* end = nullptr;
        int col = (int)wcstol(sort.c_str(), &end, 10);
        bool asc = true;
        if (end && *end == L',') asc = wcstol(end + 1, nullptr, 10) != 0;
        app.sortColumn = col;
        app.sortAsc = asc;
        fsx::sortEntries(app.entries, col, asc);
    }

    if (auto sel = argValue(args, L"--select"); !sel.empty()) {
        std::wstring cur;
        for (wchar_t ch : sel) {
            if (ch == L',') {
                if (!cur.empty()) {
                    int idx = _wtoi(cur.c_str());
                    if (idx >= 0 && idx < (int)app.entries.size())
                        app.selection.insert(app.entries[idx].path);
                    cur.clear();
                }
            } else {
                cur.push_back(ch);
            }
        }
        if (!cur.empty()) {
            int idx = _wtoi(cur.c_str());
            if (idx >= 0 && idx < (int)app.entries.size())
                app.selection.insert(app.entries[idx].path);
        }
    }

    std::wstring out = argValue(args, L"--snapshot");
    if (out.empty()) out = L"snapshot.png";
    app.addrEditing = false;
    bool ok = renderSnapshot(app, out, (int)(w * app.scale), (int)(h * app.scale));
    if (!ok) return 2;
    if (hasArg(args, L"--dump")) {
        FILE* f = nullptr;
        if (_wfopen_s(&f, (out + L".txt").c_str(), L"w, ccs=UTF-8") == 0 && f) {
            fwprintf(f, L"window %dx%d scale %.2f\n", w, h, app.scale);
            fwprintf(f, L"current %ls\n", app.current.c_str());
            fwprintf(f, L"entries %d selected %d\n", (int)app.entries.size(),
                     (int)app.selection.size());
            for (int i = 0; i < 8 && i < (int)app.entries.size(); ++i)
                fwprintf(f, L"  row %d: %ls | %ls | %ls\n", i, app.entries[i].name.c_str(),
                         app.entries[i].type.c_str(), app.entries[i].isDir ? L"dir" : L"file");
            fwprintf(f, L"listHeader %d,%d %dx%d nameW %d\n", (int)app.lay.listHeader.left,
                     (int)app.lay.listHeader.top,
                     (int)(app.lay.listHeader.right - app.lay.listHeader.left),
                     (int)(app.lay.listHeader.bottom - app.lay.listHeader.top), app.lay.nameW);
            fwprintf(f, L"tree rows %d\n", (int)app.treeFlat.size());
            fclose(f);
        }
    }
    return 0;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCmd) {
    enableDpiAwareness();
    if (FAILED(OleInitialize(nullptr))) CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    g_dumpMsg = RegisterWindowMessageW(L"MyOldComputerDump");

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    for (int i = 0; i < argc; ++i) args.push_back(argv[i]);
    LocalFree(argv);

    bool snapshot = hasArg(args, L"--snapshot");
    if (snapshot) {
        int code = runSnapshot(g_app, args);
        OleUninitialize();
        return code;
    }
    if (hasArg(args, L"--selftest")) {
        App app;
        app.init(nullptr);
        app.relayout(1000, 640);
        int code = runSelfTest(app);
        OleUninitialize();
        return code;
    }
    if (hasArg(args, L"--uitest")) {
        App app;
        int code = runUiTest(app);
        OleUninitialize();
        return code;
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hIconSm =
        (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"MyOldComputerWindow";
    RegisterClassExW(&wc);

    Settings settings;
    settings.load();
    int w = settings.w, h = settings.h;
    int x = settings.x, y = settings.y;
    if (x < 0 || y < 0) {
        x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"MyOldComputerWindow", L"My Old Computer",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, nullptr,
                                instance, nullptr);
    if (!hwnd) return 1;

    g_app.hwnd = hwnd;
    g_app.scale = dpiScaleFor(hwnd);
    g_app.init(hwnd);
    g_app.logicalW = w;
    g_app.logicalH = h;
    diagLog(L"startup dpi=%.0f scale=%.2f requested=%dx%d", 0.0, g_app.scale, w, h);
    int physicalW = (int)std::lround(w * g_app.scale);
    int physicalH = (int)std::lround(h * g_app.scale);
    if (settings.x < 0 || settings.y < 0) {
        x = (GetSystemMetrics(SM_CXSCREEN) - physicalW) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - physicalH) / 2;
    }
    SetWindowPos(hwnd, nullptr, x, y, physicalW, physicalH, SWP_NOZORDER | SWP_NOACTIVATE);
    g_app.relayout(physicalW, physicalH);
    g_app.demo = hasArg(args, L"--demo");
    std::wstring startPath = argValue(args, L"--path");
    if (startPath.empty()) startPath = fsx::homeDir();
    startPath = fsx::expandHome(startPath);
    if (g_app.demo) {
        g_app.current = startPath;
        g_app.buildTree();
    }
    g_app.loadDirectory(startPath, false);
    dnd::registerTarget(hwnd, &g_app);
    DragAcceptFiles(hwnd, TRUE);
    ShowWindow(hwnd, showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    g_app.saveSettings();
    OleUninitialize();
    return 0;
}
