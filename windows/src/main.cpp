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
#include "hmmenu.h"
#include "ids.h"
#include "options.h"
#include "preview.h"
#include "render.h"
#include "scrollbar.h"
#include "settings.h"
#include "shellmenu.h"
#include "snapshot.h"
#include "theme.h"

namespace {

App g_app;
bool g_isSnapshot = false;

int pxOf(double scale, double v) { return (int)(v * scale + 0.5); }

constexpr UINT_PTR kMarqueeTimer = 1;

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

void repaint(HWND hwnd) {
    g_app.paintDirty = true;
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

// Rectangle covered by the rubber band, clipped to the file view.
RECT marqueeBand(const App& a) {
    const RECT& vp = a.mode == ViewMode::Details ? a.lay.listViewport : a.lay.gridViewport;
    RECT r{std::min(a.marqueeStart.x, a.marqueeEnd.x), std::min(a.marqueeStart.y, a.marqueeEnd.y),
           std::max(a.marqueeStart.x, a.marqueeEnd.x), std::max(a.marqueeStart.y, a.marqueeEnd.y)};
    r.left = std::max(r.left, (LONG)vp.left);
    r.top = std::max(r.top, (LONG)vp.top);
    r.right = std::min(r.right, (LONG)vp.right);
    r.bottom = std::min(r.bottom, (LONG)vp.bottom);
    // A perfectly vertical/horizontal drag is zero-sized as a RECT.
    r.right = std::max(r.right, r.left + 1);
    r.bottom = std::max(r.bottom, r.top + 1);
    return r;
}

// One reusable back buffer for the whole window: creating a compatible bitmap
// for every mouse-move repaint made big folders drag like molasses.
struct PaintCache {
    HDC mem = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    HDC screen = nullptr;
    int w = 0;
    int h = 0;
};
PaintCache g_paint;

void freePaintCache() {
    if (g_paint.mem) {
        if (g_paint.old) SelectObject(g_paint.mem, g_paint.old);
        DeleteDC(g_paint.mem);
        g_paint.mem = nullptr;
        g_paint.old = nullptr;
    }
    if (g_paint.bmp) {
        DeleteObject(g_paint.bmp);
        g_paint.bmp = nullptr;
    }
    if (g_paint.screen) {
        ReleaseDC(nullptr, g_paint.screen);
        g_paint.screen = nullptr;
    }
    g_paint.w = g_paint.h = 0;
}

// The rubber band lives directly on the window surface: waiting for the next
// WM_PAINT made it lag behind a fast mouse, so it is drawn and erased in the
// mouse handler itself.
RECT g_band{};
bool g_bandVisible = false;
// Band rectangle the current selection was computed for.
RECT g_appliedBand{};

void drawBandDots(HDC dc, const RECT& r) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    for (int x = r.left; x < r.right; x += 2) {
        SetPixelV(dc, x, r.top, Theme::dark);
        SetPixelV(dc, x, r.bottom - 1, Theme::dark);
    }
    for (int y = r.top; y < r.bottom; y += 2) {
        SetPixelV(dc, r.left, y, Theme::dark);
        SetPixelV(dc, r.right - 1, y, Theme::dark);
    }
}

void eraseBand(HWND hwnd) {
    if (!g_bandVisible) return;
    g_bandVisible = false;
    if (!hwnd) return;
    if (!g_paint.mem || g_band.right <= g_band.left || g_band.bottom <= g_band.top) return;
    HDC dc = GetDC(hwnd);
    if (!dc) return;
    BitBlt(dc, g_band.left, g_band.top, g_band.right - g_band.left, g_band.bottom - g_band.top,
           g_paint.mem, g_band.left, g_band.top, SRCCOPY);
    ReleaseDC(hwnd, dc);
}

void drawBand(HWND hwnd, App& a) {
    g_band = marqueeBand(a);
    g_bandVisible = false;
    if (!hwnd) return;
    if (g_band.right <= g_band.left || g_band.bottom <= g_band.top) return;
    HDC dc = GetDC(hwnd);
    if (!dc) return;
    drawBandDots(dc, g_band);
    ReleaseDC(hwnd, dc);
    g_bandVisible = true;
}

void paintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }
    if (g_paint.w != rc.right || g_paint.h != rc.bottom || !g_paint.mem) {
        freePaintCache();
        g_paint.screen = GetDC(nullptr);
        g_paint.mem = CreateCompatibleDC(g_paint.screen);
        g_paint.bmp = CreateCompatibleBitmap(g_paint.screen, rc.right, rc.bottom);
        g_paint.old = SelectObject(g_paint.mem, g_paint.bmp);
        g_paint.w = rc.right;
        g_paint.h = rc.bottom;
    }
    if (g_paint.mem) {
        if (g_app.paintDirty) {
            Canvas canvas(g_paint.mem, g_app.scale);
            renderApp(canvas, g_app);
            g_app.paintDirty = false;
        }
        BitBlt(dc, 0, 0, rc.right, rc.bottom, g_paint.mem, 0, 0, SRCCOPY);
    }
    EndPaint(hwnd, &ps);
    // The band must stay complete: after any repaint, erase it as a whole and
    // draw it again from the current state (a partial redraw used to leave
    // fragments of the previous rectangle behind).
    if (g_app.marquee && g_paint.mem) drawBand(hwnd, g_app);
}

// Re-renders one rectangle of the cached frame from the current state and
// shows it at once: the general dirty-rect primitive.  Cheap because
// renderApp clipped to r only rasterizes r's pixels, and correct because
// renderApp is a pure function of App's state.
void repaintRegion(HWND hwnd, App& a, const RECT& r) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    if (!g_paint.mem) {
        repaint(hwnd);
        return;
    }
    const int saved = SaveDC(g_paint.mem);
    IntersectClipRect(g_paint.mem, r.left, r.top, r.right, r.bottom);
    {
        Canvas c(g_paint.mem, a.scale);
        renderApp(c, a);
    }
    RestoreDC(g_paint.mem, saved);
    if (hwnd) {
        // Blit immediately: while the mouse keeps moving, WM_PAINT is starved.
        HDC dc = GetDC(hwnd);
        if (dc) {
            BitBlt(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, g_paint.mem, r.left,
                   r.top, SRCCOPY);
            ReleaseDC(hwnd, dc);
        }
    }
}

// Repaints individual rows: update the cached frame for those rows and let the
// paint handler blit them, instead of re-rendering the whole window.
void repaintRows(HWND hwnd, App& a, const std::vector<int>& rows) {
    if (rows.empty()) return;
    const RECT& vp = a.mode == ViewMode::Details ? a.lay.listViewport : a.lay.gridViewport;
    for (int i : rows) {
        RECT r = a.mode == ViewMode::Details ? a.listRowRect(i) : a.gridCellRect(i);
        RECT clip{};
        if (!IntersectRect(&clip, &r, &vp)) continue;
        repaintRegion(hwnd, a, clip);
    }
}

// ---------------------------------------------------------------- scrolling

// The scroll fast path, kept free of window state so tests can verify it
// against a full render: slides the cached viewport content by dy pixels and
// re-renders only the exposed strip (plus extra rects such as the scrollbar,
// whose knob moved).  Rows are position-independent, so the slid band is
// pixel-identical to a full re-render.
void scrollFrame(HDC dc, App& a, const RECT& vp, const RECT& extra, int dy) {
    const int viewH = vp.bottom - vp.top;
    BitBlt(dc, vp.left, vp.top + std::max(0, -dy), vp.right - vp.left, viewH - std::abs(dy), dc,
           vp.left, vp.top + std::max(0, dy), SRCCOPY);
    const RECT strip = dy > 0 ? RECT{vp.left, vp.bottom - dy, vp.right, vp.bottom}
                              : RECT{vp.left, vp.top, vp.right, vp.top - dy};
    HRGN rgn = CreateRectRgn(strip.left, strip.top, strip.right, strip.bottom);
    HRGN more = CreateRectRgn(extra.left, extra.top, extra.right, extra.bottom);
    CombineRgn(rgn, rgn, more, RGN_OR);
    DeleteObject(more);
    const int saved = SaveDC(dc);
    SelectClipRgn(dc, rgn);
    DeleteObject(rgn);
    {
        Canvas c(dc, a.scale);
        renderApp(c, a);
    }
    RestoreDC(dc, saved);
}

// Scrolls one pane (1 list, 2 grid, 3 tree) by delta and repaints through the
// cached frame instead of re-rendering the whole window - the difference
// between a wheel notch costing one strip and costing a 4K frame.
void scrollPaneAndRepaint(HWND hwnd, App& a, int pane, int delta) {
    const int before = pane == 1 ? a.listScroll : pane == 2 ? a.gridScroll : a.treeScroll;
    if (pane == 1) a.scrollListBy(delta);
    else if (pane == 2) a.scrollGridBy(delta);
    else a.scrollTreeBy(delta);
    const int after = pane == 1 ? a.listScroll : pane == 2 ? a.gridScroll : a.treeScroll;
    const int dy = after - before;
    if (dy == 0) return;  // already at the end (or a zero-length wheel step)
    const RECT vp = pane == 1   ? a.lay.listViewport
                    : pane == 2 ? a.lay.gridViewport
                                : a.lay.treeViewport;
    const RECT bar = pane == 1   ? a.lay.listBar
                     : pane == 2 ? a.lay.gridBar
                                 : a.lay.treeBar;
    const int viewH = vp.bottom - vp.top;
    if (!g_paint.mem || a.paintDirty || std::abs(dy) >= viewH) {
        repaint(hwnd);
        return;
    }
    scrollFrame(g_paint.mem, a, vp, bar, dy);
    if (hwnd) {
        HDC dc = GetDC(hwnd);
        if (dc) {
            BitBlt(dc, vp.left, vp.top, vp.right - vp.left, viewH, g_paint.mem, vp.left, vp.top,
                   SRCCOPY);
            BitBlt(dc, bar.left, bar.top, bar.right - bar.left, bar.bottom - bar.top, g_paint.mem,
                   bar.left, bar.top, SRCCOPY);
            ReleaseDC(hwnd, dc);
        }
    }
}

// Repaints the rows whose selection state changed plus the status panel that
// counts them - a click must not cost a full-window render.
void repaintSelectionChange(HWND hwnd, App& a, const std::set<std::wstring>& before) {
    std::vector<int> rows;
    for (int i = 0; i < (int)a.entries.size(); ++i) {
        const bool was = before.count(a.entries[i].path) != 0;
        const bool now = a.selection.count(a.entries[i].path) != 0;
        if (was != now) rows.push_back(i);
    }
    if (rows.empty()) return;  // nothing visible changed
    repaintRows(hwnd, a, rows);
    repaintRegion(hwnd, a, a.lay.statusLeft);
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
    } else {
        // List and icons share the same grid geometry.
        int cols = std::max(1, a.gridCols());
        idx = std::clamp(cur + dx + dy * cols, 0, n - 1);
    }
    selectOnly(a, idx);
    a.selAnchor = idx;
    ensureVisible(a, idx);
}

bool bandHits(const RECT& hit, const RECT& band) {
    RECT inter{};
    return IntersectRect(&inter, &hit, &band) != 0;
}

// Rows/cells a band rectangle can reach, so a drag never walks the whole
// listing.
void collectBandRows(const App& a, const RECT& band, std::vector<int>& out) {
    const int count = (int)a.entries.size();
    if (count == 0 || band.right <= band.left || band.bottom <= band.top) return;
    if (a.mode == ViewMode::Details) {
        const RECT& vp = a.lay.listViewport;
        const int rowH = std::max(1, (int)a.m.rowH);
        int first = (band.top - vp.top + a.listScroll) / rowH - 1;
        int last = (band.bottom - vp.top + a.listScroll) / rowH + 1;
        first = std::clamp(first, 0, count - 1);
        last = std::clamp(last, 0, count - 1);
        for (int i = first; i <= last; ++i) out.push_back(i);
    } else {
        const int cols = std::max(1, a.gridCols());
        const int cw = std::max(1, a.gridCellW());
        const int ch = std::max(1, a.gridCellH());
        const int originX = a.lay.gridViewport.left + (int)(4 * a.scale + 0.5);
        const int originY = a.lay.gridViewport.top + (int)(4 * a.scale + 0.5);
        int col0 = std::clamp((int)((band.left - originX) / cw), 0, cols - 1);
        int col1 = std::clamp((int)((band.right - originX) / cw), 0, cols - 1);
        int row0 = std::max(0, (int)((band.top - originY + a.gridScroll) / ch));
        int row1 = std::max(row0 - 1, (int)((band.bottom - originY + a.gridScroll) / ch));
        for (int row = row0; row <= row1; ++row)
            for (int col = col0; col <= col1; ++col) {
                const int index = row * cols + col;
                if (index >= 0 && index < count) out.push_back(index);
            }
    }
}

// Recomputes the selection for the current band and returns the rows whose
// selected state changed, so only those rows need repainting.
std::vector<int> marqueeUpdate(App& a, const RECT& previousBand) {
    const RECT band = marqueeBand(a);
    std::vector<int> candidates;
    collectBandRows(a, previousBand, candidates);
    collectBandRows(a, band, candidates);
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    const bool additive = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    std::set<std::wstring> next;
    if (additive) next = a.selection;
    std::vector<int> changed;
    for (int i : candidates) {
        if (i < 0 || i >= (int)a.entries.size()) continue;
        const std::wstring& path = a.entries[i].path;
        const bool was = a.selection.count(path) != 0;
        bool now = was && additive;
        if (!now) {
            RECT hit = a.mode == ViewMode::Details ? a.listRowRect(i) : a.gridCellRect(i);
            if (a.mode == ViewMode::Details)
                hit.right = hit.left + std::min(a.lay.nameW, a.nameExtent(i));
            now = bandHits(hit, band);
        }
        if (now) next.insert(path);
        if (was != now) changed.push_back(i);
    }
    if (changed.empty()) return changed;
    a.selection.swap(next);
    return changed;
}

void startMarquee(HWND hwnd, App& a, POINT p) {
    if (!(GetKeyState(VK_CONTROL) & 0x8000)) a.selection.clear();
    a.marqueeStart = p;
    a.marqueeEnd = p;
    a.marquee = true;
    a.dragArmed = false;
    a.dragPath.clear();
    g_bandVisible = false;
    g_appliedBand = marqueeBand(a);
    SetCapture(hwnd);
    // Selection is recomputed on a timer: a fast mouse only moves the band,
    // so rendering rows can never slow the rectangle down.
    SetTimer(hwnd, kMarqueeTimer, 15, nullptr);
    repaint(hwnd);
}

// Cheap: erase the old rectangle from the cached frame and draw the new one.
void moveBand(HWND hwnd, App& a, POINT p) {
    eraseBand(hwnd);
    a.marqueeEnd = p;
    drawBand(hwnd, a);
}

// Expensive: work out which rows the band now covers and repaint just those.
void applyMarqueeSelection(HWND hwnd, App& a) {
    const RECT current = marqueeBand(a);
    if (EqualRect(&current, &g_appliedBand)) return;
    const int scrollBefore = a.mode == ViewMode::Details ? a.listScroll : a.gridScroll;
    // Auto-scroll when the pointer is dragged past the edge of the list.
    const RECT& vp = a.mode == ViewMode::Details ? a.lay.listViewport : a.lay.gridViewport;
    const int step = a.mode == ViewMode::Details ? a.m.rowH : a.gridCellH();
    if (a.marqueeEnd.y < vp.top) {
        if (a.mode == ViewMode::Details) a.scrollListBy(-step);
        else a.scrollGridBy(-step);
    } else if (a.marqueeEnd.y >= vp.bottom) {
        if (a.mode == ViewMode::Details) a.scrollListBy(step);
        else a.scrollGridBy(step);
    }
    const std::vector<int> changed = marqueeUpdate(a, g_appliedBand);
    g_appliedBand = current;
    const int scrollAfter = a.mode == ViewMode::Details ? a.listScroll : a.gridScroll;
    if (scrollAfter != scrollBefore) {
        repaint(hwnd);  // everything moved: one full redraw
    } else {
        repaintRows(hwnd, a, changed);
    }
    // The row blits may have covered part of the rectangle; put it back.
    drawBand(hwnd, a);
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

bool g_menuPending = false;
POINT g_menuPendingPoint{};

void showContextMenu(HWND hwnd, App& a, POINT p) {
    diagLog(L"rclick %d,%d menuOpen=%d", p.x, p.y, (int)menuIsOpen());
    if (menuIsOpen()) {
        // A menu is already up (its message loop is running further down the
        // stack).  Remember where the user clicked, close it, and reopen there
        // once this call returns.
        g_menuPending = true;
        g_menuPendingPoint = p;
        // Take the foreground back first: the click that got here must not be
        // the one Windows uses for activation (see WM_MOUSEACTIVATE).
        SetForegroundWindow(hwnd);
        menuClose();
        diagLog(L"rclick pending close+reopen at %d,%d", p.x, p.y);
        return;
    }
    std::vector<std::wstring> paths;
    std::wstring folder = a.current;
    bool fromTree = false;
    bool selectionChanged = false;
    bool favoriteNode = false;  // the menu is on a favorite's own row in the tree
    // A menu of our own (not the shell's) right here, for tree rows that are
    // no folder the shell could build a menu for.
    const auto ownPopup = [&](const std::vector<MenuItem>& items) {
        POINT screen = p;
        ClientToScreen(hwnd, &screen);
        const int id = showPopupMenu(hwnd, items, screen.x, screen.y, false);
        if (id == ID_REFRESH) {
            a.refreshTree();  // re-enumerates drives too (USB plugged in, ...)
            a.reload();
        } else if (id) {
            a.performMenuAction(id);
        }
        a.contextFolder.clear();
        a.contextTreeRow = -1;
        repaint(hwnd);
    };
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        // The tree shows the very same shell menu as the file list does for
        // that folder, so the commands behave identically in both places.
        int row = a.treeRowAt(p);
        if (row >= 0) {
            TreeNode* n = a.treeFlat[row];
            a.hoverTree = row;
            a.contextTreeRow = row;  // the selected row of the tree while its menu is up
            selectionChanged = true;
            if (n->kind == TreeNode::Favorites) {
                // The Favorites' header: what can be done with the group.
                ownPopup({{n->expanded ? L"折叠" : L"展开", ID_TREE_TOGGLE, n->hasChildren},
                          {L"添加当前文件夹到收藏夹", ID_ADD_FAV_CURRENT,
                           fsx::isDirectory(a.current) && a.favoriteIndex(a.current) < 0}});
                return;
            }
            if (n->kind == TreeNode::Favorite && n->missing) {
                // A favorite whose folder is gone: no folder to build a menu
                // for, only what can still be done with the entry itself.
                a.contextFolder = n->path;
                ownPopup({{L"从收藏夹中移除", ID_REMOVE_FAV, true},
                          {L"重命名收藏项…", ID_FAV_RENAME, true}});
                return;
            }
            if (n->path.empty()) {
                // A header without a folder (我的电脑) gets a small menu of
                // its own instead of one borrowed from the current directory.
                ownPopup({{n->expanded ? L"折叠" : L"展开", ID_TREE_TOGGLE, n->hasChildren},
                          {L"", 0, false, false, true},
                          {L"刷新", ID_REFRESH, true}});
                return;
            }
            a.selection.clear();
            a.contextFolder = n->path;
            a.contextIsFavorite = (n->kind == TreeNode::Favorite);
            favoriteNode = a.contextIsFavorite;
            paths.push_back(n->path);
            folder = n->path;
        }
        fromTree = true;
    } else {
        // The same hit rule as clicking and double-clicking: only an item's
        // icon and name count.  The seam between two entries (the blank rest
        // of a cell) is the folder background, not a neighbouring file.
        int row = -1;
        if (a.hitTestItem(p, &row)) {
            if (!a.selection.count(a.entries[row].path)) {
                selectOnly(a, row);
                selectionChanged = true;
            }
            for (auto& e : a.entries)
                if (a.selection.count(e.path)) paths.push_back(e.path);
            a.contextFolder = a.entries[row].isDir ? a.entries[row].path : L"";
        } else {
            if (!a.selection.empty()) {
                a.selection.clear();
                selectionChanged = true;
            }
            a.contextFolder.clear();
        }
        a.contextIsFavorite = false;
    }

    if (selectionChanged) {
        // The highlight must be on screen *under* the menu while it is up:
        // the menu runs its own message loop, and without a fresh frame the
        // paint handler would keep blitting the stale cache.
        repaint(hwnd);
        if (hwnd) UpdateWindow(hwnd);
    }

    POINT screen = p;
    ClientToScreen(hwnd, &screen);

    diagLog(L"rclick build folder=%ls paths=%d sel=%d", folder.c_str(),
            (int)paths.size(), (int)a.selection.size());

    shellmenu::ViewState state;
    state.mode = (int)a.mode;
    state.sortColumn = a.sortColumn;
    state.showHidden = a.showHidden;
    state.menuMask = a.settings.menuMask;
    state.hidden = a.hiddenFor(paths);
    state.isFavorite = paths.size() == 1 && a.favoriteIndex(paths.front()) >= 0;
    state.favoriteNode = favoriteNode;
    shellmenu::Result result;
    if (a.settings.menuMask & kMenuShell)
        result = shellmenu::show(hwnd, folder, paths, screen, state);

    if (!result.shown) {
        // The shell could not build a menu (unusual path): fall back to the
        // hand-drawn one so the window always has a context menu.
        std::vector<MenuItem> items = a.contextMenu();
        int id = showPopupMenu(hwnd, items, screen.x, screen.y, false);
        if (id) a.performMenuAction(id);
    } else {
        switch (result.command) {
        case shellmenu::Command::Open: a.openSelection(); break;
        case shellmenu::Command::OpenNewWindow: a.openInNewWindow(); break;
        case shellmenu::Command::ViewBig: a.setViewMode(ViewMode::Icons); break;
        case shellmenu::Command::ViewList: a.setViewMode(ViewMode::List); break;
        case shellmenu::Command::ViewDetails: a.setViewMode(ViewMode::Details); break;
        case shellmenu::Command::SortName: a.sortBy(0); break;
        case shellmenu::Command::SortSize: a.sortBy(1); break;
        case shellmenu::Command::SortType: a.sortBy(2); break;
        case shellmenu::Command::SortDate: a.sortBy(3); break;
        case shellmenu::Command::Refresh: a.reload(); break;
        case shellmenu::Command::ToggleHidden: a.performMenuAction(ID_TOGGLE_HIDDEN); break;
        case shellmenu::Command::Terminal: a.openTerminalForSelection(); break;
        case shellmenu::Command::Rename: a.renameSelection(); break;
        case shellmenu::Command::AddFavorite:
            if (!paths.empty()) a.addFavorite(paths.front());
            break;
        case shellmenu::Command::RemoveFavorite:
            if (!paths.empty()) a.removeFavorite(paths.front());
            break;
        case shellmenu::Command::RenameFavorite:
            if (!paths.empty()) a.promptRenameFavorite(paths.front());
            break;
        case shellmenu::Command::NewFolder: a.newFolder(); break;
        case shellmenu::Command::NewTextFile: a.newTextFile(); break;
        case shellmenu::Command::Preview:
            if (std::wstring sel = a.firstSelectedPath(); !sel.empty()) preview::show(hwnd, sel);
            break;
        case shellmenu::Command::Reveal: a.revealSelection(); break;
        case shellmenu::Command::CopyPath: a.copyPaths(); break;
        case shellmenu::Command::Settings: a.openMenuSettings(); break;
        case shellmenu::Command::None: break;
        }
        if (result.invokedShell) a.refreshAfterChange();
    }

    if (fromTree) a.selection.clear();
    a.contextFolder.clear();
    a.contextIsFavorite = false;
    a.contextTreeRow = -1;
    repaint(hwnd);

    if (g_menuPending) {
        // The user right-clicked somewhere else while the menu was open: show
        // that menu now instead of losing the click.
        g_menuPending = false;
        showContextMenu(hwnd, a, g_menuPendingPoint);
    }
}

// ---------------------------------------------------------------- input

// --------------------------------------------------------- pressing a tree row

// Forgets the pressed tree row and the favorite being dragged; nothing in
// the tree changes.
void cancelTreePress(HWND hwnd, App& a) {
    const bool wasDragging = a.favDragging;
    a.treePressRow = -1;
    a.treePressPath.clear();
    a.favDragArmed = false;
    a.favDragging = false;
    a.favDragPath.clear();
    a.favInsert = -1;
    a.favLine = RECT{};
    if (GetCapture() == hwnd) ReleaseCapture();
    if (wasDragging) repaint(hwnd);
}

// The pointer moves while a tree row is pressed.  A favorite moves only up
// and down inside its group - that is how the group is reordered; any other
// folder is dragged off like an item of the list, to be dropped on a folder,
// on the Favorites or in another program.
void treePressMove(HWND hwnd, App& a, POINT p) {
    const int tx = std::max(3, GetSystemMetrics(SM_CXDRAG) / 2);
    const int ty = std::max(3, GetSystemMetrics(SM_CYDRAG) / 2);
    const bool moved =
        std::abs(p.x - a.treePressPoint.x) > tx || std::abs(p.y - a.treePressPoint.y) > ty;
    if (a.favDragArmed && !a.favDragging) {
        if (!moved) return;
        a.favDragArmed = false;
        a.favDragging = true;
        a.favDragPath = a.treePressPath;
        a.hoverTree = -1;
    }
    if (a.favDragging) {
        // Past either end of the tree the rows scroll past under the pointer.
        if (p.y < a.lay.treeViewport.top)
            scrollPaneAndRepaint(hwnd, a, 3, -a.m.rowH);
        else if (p.y >= a.lay.treeViewport.bottom)
            scrollPaneAndRepaint(hwnd, a, 3, a.m.rowH);
        int insert = -1;
        RECT line{};
        a.favoriteDropPosition(p, &insert, &line);
        if (insert != a.favInsert || !EqualRect(&line, &a.favLine)) {
            a.favInsert = insert;
            a.favLine = line;
            repaint(hwnd);
        }
        return;
    }
    if (!moved || a.treePressPath.empty() || !fsx::isDirectory(a.treePressPath)) return;
    DWORD effect = 0;
    a.dragging = true;  // before the capture goes: losing it must not read as a cancelled press
    if (GetCapture() == hwnd) ReleaseCapture();
    diagLog(L"startTreeDrag %ls", a.treePressPath.c_str());
    dnd::dragOut(hwnd, {a.treePressPath}, &effect);
    a.dragging = false;
    a.treePressRow = -1;
    a.treePressPath.clear();
    a.dragTargetFolder.clear();
    a.dragTargetIsTree = false;
    a.dragTargetRow = -1;
    a.dragTargetFavorites = false;
    a.reload();
    repaint(hwnd);
}

// The button comes back up on a pressed tree row: a drag would have started
// already, so this is a click - it navigates - or the end of a reorder.
void treePressRelease(HWND hwnd, App& a, POINT p) {
    const std::wstring path = a.treePressPath;
    const bool reordered = a.favDragging;
    const int insert = a.favInsert;
    const std::wstring dragged = a.favDragPath;
    cancelTreePress(hwnd, a);
    if (reordered) {
        if (insert >= 0) a.moveFavorite(a.favoriteIndex(dragged), insert);
        return;
    }
    int row = a.treeRowAt(p);
    if (row < 0) return;
    TreeNode* n = a.treeFlat[row];
    if (_wcsicmp(n->path.c_str(), path.c_str()) != 0) return;
    if (!fsx::isDirectory(path)) {
        MessageBeep(MB_ICONASTERISK);  // a favorite whose folder is gone
        return;
    }
    // Tree clicks only select and update the right pane; expanding and
    // scrolling here made the tree jump under the pointer.
    a.treeClickRow = row;
    a.loadDirectory(path, true, true);
    repaint(hwnd);
}

void onMouseMove(HWND hwnd, App& a, POINT p) {
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
            int pos = scrollbar::positionFromDrag(a.lay.listBar, a.m, a.listContentHeight(),
                                                  a.lay.listViewport.bottom - a.lay.listViewport.top,
                                                  a.scrollDragStartPos, delta);
            scrollPaneAndRepaint(hwnd, a, 1, pos - a.listScroll);
        } else if (a.scrollDrag == 2) {
            int pos = scrollbar::positionFromDrag(a.lay.gridBar, a.m, a.gridContentHeight(),
                                                  a.lay.gridViewport.bottom - a.lay.gridViewport.top,
                                                  a.scrollDragStartPos, delta);
            scrollPaneAndRepaint(hwnd, a, 2, pos - a.gridScroll);
        } else {
            int pos = scrollbar::positionFromDrag(a.lay.treeBar, a.m, a.treeContentHeight(),
                                                  a.lay.treeViewport.bottom - a.lay.treeViewport.top,
                                                  a.scrollDragStartPos, delta);
            scrollPaneAndRepaint(hwnd, a, 3, pos - a.treeScroll);
        }
        return;
    }

    if (a.treePressRow >= 0) {
        treePressMove(hwnd, a, p);
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
            a.pressSelectedRow = -1;  // a real drag started: keep the multi-selection
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
        moveBand(hwnd, a, p);
        // Apply the selection from the move handler as well, capped at ~66 Hz:
        // the band is already on screen, so this can never hold it back, and
        // selection keeps working even if the timer is starved.
        static ULONGLONG lastApply = 0;
        const ULONGLONG now = GetTickCount64();
        if (now - lastApply >= 15) {
            lastApply = now;
            applyMarqueeSelection(hwnd, a);
        }
        return;
    }

    int hoverTree = a.sidebarVisible ? a.treeRowAt(p) : -1;
    int hoverTool = PtInRect(&a.lay.tool, p) ? a.toolbarIndexAt(p) : -1;
    int hoverMenu = PtInRect(&a.lay.menu, p) ? a.menuItemAt(p) : -1;
    if (hoverTree != a.hoverTree || hoverTool != a.hoverTool || hoverMenu != a.hoverMenu) {
        // Only the two rows/buttons involved change their look: repaint those
        // rects, not the window.
        const int oldTree = a.hoverTree, oldTool = a.hoverTool, oldMenu = a.hoverMenu;
        a.hoverTree = hoverTree;
        a.hoverTool = hoverTool;
        a.hoverMenu = hoverMenu;
        if (oldTree >= 0) repaintRegion(hwnd, a, a.treeRowRect(oldTree));
        if (oldTool >= 0) repaintRegion(hwnd, a, a.toolbarItemRect(oldTool));
        if (oldMenu >= 0) repaintRegion(hwnd, a, a.menuItemRect(oldMenu));
        if (hoverTree >= 0) repaintRegion(hwnd, a, a.treeRowRect(hoverTree));
        if (hoverTool >= 0) repaintRegion(hwnd, a, a.toolbarItemRect(hoverTool));
        if (hoverMenu >= 0) repaintRegion(hwnd, a, a.menuItemRect(hoverMenu));
    }
}

void onLButtonDown(HWND hwnd, App& a, POINT p) {
    SetFocus(hwnd);
    a.pressTitle = -1;
    a.pressTool = -1;
    a.pressSelectedRow = -1;

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
        // The Go button behaves like a toolbar button: press feedback first,
        // the action on release inside the button.
        a.pressTool = 100;
        SetCapture(hwnd);
        repaint(hwnd);
        return;
    }

    if (a.sidebarVisible && PtInRect(&a.lay.treeBar, p)) {
        int content = a.treeContentHeight();
        int view = a.lay.treeViewport.bottom - a.lay.treeViewport.top;
        auto part = scrollbar::hitTest(a.lay.treeBar, p, a.m, content, view, a.treeScroll);
        int page = std::max(a.m.rowH, view - a.m.rowH);
        if (part == scrollbar::Part::Decrement) scrollPaneAndRepaint(hwnd, a, 3, -3 * a.m.rowH);
        else if (part == scrollbar::Part::Increment) scrollPaneAndRepaint(hwnd, a, 3, 3 * a.m.rowH);
        else if (part == scrollbar::Part::PageUp) scrollPaneAndRepaint(hwnd, a, 3, -page);
        else if (part == scrollbar::Part::PageDown) scrollPaneAndRepaint(hwnd, a, 3, page);
        else if (part == scrollbar::Part::Knob) {
            a.scrollDrag = 3;
            a.scrollDragStartY = p.y;
            a.scrollDragStartPos = a.treeScroll;
            SetCapture(hwnd);
        }
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
                // Pressed, not clicked yet: the row is navigated to when the
                // button comes back up on it, and dragged if the pointer
                // moves first - the way Explorer's tree works.  (Tree clicks
                // only ever select and update the right pane; expanding and
                // scrolling here made the tree jump under the pointer.)
                a.treePressRow = row;
                a.treePressPath = n->path;
                a.treePressPoint = p;
                a.favDragArmed = n->kind == TreeNode::Favorite;
                SetCapture(hwnd);
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
            if (part == scrollbar::Part::Decrement) scrollPaneAndRepaint(hwnd, a, 1, -3 * a.m.rowH);
            else if (part == scrollbar::Part::Increment)
                scrollPaneAndRepaint(hwnd, a, 1, 3 * a.m.rowH);
            else if (part == scrollbar::Part::PageUp) scrollPaneAndRepaint(hwnd, a, 1, -page);
            else if (part == scrollbar::Part::PageDown) scrollPaneAndRepaint(hwnd, a, 1, page);
            else if (part == scrollbar::Part::Knob) {
                a.scrollDrag = 1;
                a.scrollDragStartY = p.y;
                a.scrollDragStartPos = a.listScroll;
                SetCapture(hwnd);
            }
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
                    a.rebuildNameWidths();
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
        if (part == scrollbar::Part::Decrement) scrollPaneAndRepaint(hwnd, a, 2, -3 * a.m.rowH);
        else if (part == scrollbar::Part::Increment) scrollPaneAndRepaint(hwnd, a, 2, 3 * a.m.rowH);
        else if (part == scrollbar::Part::PageUp) scrollPaneAndRepaint(hwnd, a, 2, -page);
        else if (part == scrollbar::Part::PageDown) scrollPaneAndRepaint(hwnd, a, 2, page);
        else if (part == scrollbar::Part::Knob) {
            a.scrollDrag = 2;
            a.scrollDragStartY = p.y;
            a.scrollDragStartPos = a.gridScroll;
            SetCapture(hwnd);
        }
        return;
    }

    int itemIndex = -1;
    if (a.hitTestItem(p, &itemIndex)) {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const std::wstring& path = a.entries[itemIndex].path;
        const std::set<std::wstring> selBefore = a.selection;
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
        } else if (a.selection.size() > 1) {
            // Plain click on one of several selected items: keep the selection
            // for a possible drag; collapse it on release if no drag follows.
            a.pressSelectedRow = itemIndex;
        }
        a.dragPath = path;
        a.dragArmed = true;
        a.marqueeStart = p;
        SetCapture(hwnd);
        repaintSelectionChange(hwnd, a, selBefore);
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
    // Before the capture goes back: what the press was is still needed.
    if (a.treePressRow >= 0) {
        treePressRelease(hwnd, a, p);
        return;
    }
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
        if (i == 100) {
            // The address bar's Go button.
            if (PtInRect(&a.lay.addrGo, p)) {
                if (a.addrEditing) a.endAddressEdit(true);
                else a.reload();
            }
            repaint(hwnd);
            return;
        }
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
        applyMarqueeSelection(hwnd, a);
        KillTimer(hwnd, kMarqueeTimer);
        eraseBand(hwnd);
        a.marquee = false;
        repaint(hwnd);
        return;
    }
    if (a.dragArmed || a.dragging) {
        const std::set<std::wstring> selBefore = a.selection;
        if (a.pressSelectedRow >= 0) {
            // No drag followed the press: collapse to the clicked item, the
            // way Explorer does.
            selectOnly(a, a.pressSelectedRow);
            a.selAnchor = a.pressSelectedRow;
            a.pressSelectedRow = -1;
        }
        a.dragArmed = false;
        a.dragging = false;
        a.dragPath.clear();
        repaintSelectionChange(hwnd, a, selBefore);
    }
}

// A second click that comes fast enough arrives as WM_LBUTTONDBLCLK instead
// of a button-down.  An item and a tree row mean something special by it;
// everywhere else - the toolbar, the menu bar, a title button, a scrollbar, a
// column header, blank canvas - it is just another click, and the first
// click's action has already run.  Dropping it there made a quick second
// click on the toolbar's View button do nothing.
void onDoubleClick(HWND hwnd, App& a, POINT p) {
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        int row = a.treeRowAt(p);
        diagLog(L"DBLCLK tree %d,%d row=%d", p.x, p.y, row);
        if (row >= 0) {
            TreeNode* n = a.treeFlat[row];
            if (n->hasChildren) a.toggleTreeNode(*n);
            repaint(hwnd);
            return;
        }
    }
    // Only the icon and the name open an item - the same hit rule as a
    // single click.  Double-clicking a row's blank part (size/type/date)
    // must not open anything, exactly like Explorer.
    int idx = -1;
    if (a.hitTestItem(p, &idx)) {
        diagLog(L"DBLCLK %d,%d idx=%d", p.x, p.y, idx);
        a.openEntry(a.entries[idx]);
        repaint(hwnd);
        return;
    }
    onLButtonDown(hwnd, a, p);
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
        else if (std::wstring p = a.firstSelectedPath(); !p.empty()) preview::show(hwnd, p);
        break;
    case VK_ESCAPE:
        if (a.marquee || a.dragging) {
            eraseBand(hwnd);
            a.marquee = false;
            a.dragging = false;
            a.dragArmed = false;
        } else if (a.treePressRow >= 0) {
            cancelTreePress(hwnd, a);  // a favorite being dragged to a new place stays where it is
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
        scrollPaneAndRepaint(hwnd, a, 3, -steps * 3 * a.m.rowH);
    } else if (a.mode == ViewMode::Details) {
        scrollPaneAndRepaint(hwnd, a, 1, -steps * 3 * a.m.rowH);
    } else {
        int rows = a.mode == ViewMode::Icons ? 2 : 3;
        scrollPaneAndRepaint(hwnd, a, 2, -steps * rows * a.gridCellH());
    }
}

void onDropFiles(HWND hwnd, App& a, HDROP drop) {
    std::vector<std::wstring> paths = fsx::pathsFromHdrop(drop);
    POINT p;
    DragQueryPoint(drop, &p);
    DragFinish(drop);
    std::wstring target;
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        int row = a.treeRowAt(p);
        if (row >= 0) target = a.treeFlat[row]->path;
    } else {
        // A drop needs the icon or the name too; the seam between entries
        // drops into the current folder, like Explorer.
        int idx = -1;
        if (a.hitTestItem(p, &idx) && a.entries[idx].isDir) target = a.entries[idx].path;
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
    case WM_CTLCOLOREDIT:
        // The rename field is white with black text like the list it sits in,
        // whatever the system's window colours are.
        if ((HWND)lp == a.renameEdit) {
            SetTextColor((HDC)wp, Theme::text);
            SetBkColor((HDC)wp, Theme::white);
            return (LRESULT)GetStockObject(WHITE_BRUSH);
        }
        break;
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
        // Nothing in the chrome depends on focus (the flag is kept for
        // parity with the macOS build), so no repaint is needed.
        a.windowActive = true;
        return 0;
    case WM_KILLFOCUS:
        a.windowActive = false;
        return 0;
    case WM_CAPTURECHANGED:
        // Something stole the mouse: never leave a rubber band, a pending drag
        // or a half-finished knob/splitter drag armed.
        if (a.marquee) {
            KillTimer(hwnd, kMarqueeTimer);
            eraseBand(hwnd);
            a.marquee = false;
        }
        if (a.dragArmed && !a.dragging) {
            a.dragArmed = false;
            a.dragPath.clear();
        }
        if (a.treePressRow >= 0 && !a.dragging) cancelTreePress(hwnd, a);
        a.scrollDrag = 0;
        a.splitterDrag = false;
        a.pressSelectedRow = -1;
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
    case WM_MOUSELEAVE: {
        // Same region treatment as a hover change: only the highlighted
        // rows/buttons need redrawing.
        const RECT tree = a.hoverTree >= 0 ? a.treeRowRect(a.hoverTree) : RECT{};
        const RECT tool = a.hoverTool >= 0 ? a.toolbarItemRect(a.hoverTool) : RECT{};
        const RECT menu = a.hoverMenu >= 0 ? a.menuItemRect(a.hoverMenu) : RECT{};
        if (a.hoverTree != -1 || a.hoverTool != -1 || a.hoverMenu != -1) {
            a.hoverTree = a.hoverTool = a.hoverMenu = -1;
            if (tree.right > tree.left) repaintRegion(hwnd, a, tree);
            if (tool.right > tool.left) repaintRegion(hwnd, a, tool);
            if (menu.right > menu.left) repaintRegion(hwnd, a, menu);
        }
        return 0;
    }
    case WM_ACTIVATE:
        diagLog(L"main activate %04X", (unsigned)LOWORD(wp));
        break;
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (menuIsOpen()) {
            diagLog(L"main lbutton while a menu is open at %d,%d", p.x, p.y);
            menuClose();
            return 0;
        }
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
    case WM_MOUSEACTIVATE:
        diagLog(L"main mouseactivate menu=%d", (int)menuIsOpen());
        // A click while one of our shell menus is up must not run the normal
        // activation dance: activating the main window makes Windows destroy
        // the foreground menu window mid-activation, and the click that
        // started it is lost entirely.  Take the click without activating;
        // the context-menu path closes and reopens the menu itself.
        if (menuIsOpen()) return MA_NOACTIVATE;
        break;
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        onMouseWheel(hwnd, a, p, GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }
    case WM_TIMER:
        if (wp == kMarqueeTimer && a.marquee) {
            // Follow the real cursor: even if a fast mouse outruns the move
            // messages, the rectangle stays under the pointer.
            POINT p{};
            if (GetCursorPos(&p)) {
                ScreenToClient(hwnd, &p);
                if (p.x != a.marqueeEnd.x || p.y != a.marqueeEnd.y) moveBand(hwnd, a, p);
            }
            applyMarqueeSelection(hwnd, a);
        }
        return 0;
    case WM_KEYDOWN:
        onKeyDown(hwnd, a, wp);
        return 0;
    case WM_SYSKEYDOWN:
        // With Alt held, arrow keys arrive here instead of WM_KEYDOWN; other
        // system keys (Alt+F4, F10) still go to DefWindowProc.
        if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP) {
            onKeyDown(hwnd, a, wp);
            return 0;
        }
        break;
    case WM_DROPFILES:
        onDropFiles(hwnd, a, (HDROP)wp);
        return 0;
    case WM_INITMENUPOPUP:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
        // A shell context menu may be open and its extensions want owner-draw
        // notifications (Send To, Open With, 7-Zip ...).
        if (shellmenu::forwardMessage(msg, wp, lp)) return 0;
        break;
    case WM_CLOSE:
        a.saveSettings();
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        dnd::revokeTarget(hwnd);
        preview::hide();
        freePaintCache();
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

// ------------------------------------------------------------- text prompt test
// Drives a text prompt while its own message loop runs: a timer on a window
// of this thread fires inside that loop, types into the prompt's edit box
// and presses the key.
constexpr UINT_PTR kPromptTestTimer = 0x4D45;
constexpr wchar_t kPromptTestClass[] = L"MyOldComputerPromptTest";
UINT g_promptKey = VK_RETURN;  // the key pressed after typing

BOOL CALLBACK findPromptDialog(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t cls[64]{};
    if (pid == GetCurrentProcessId() && GetClassNameW(h, cls, 64) &&
        lstrcmpW(cls, L"MyOldComputerDialog") == 0) {
        *(HWND*)lp = h;
        return FALSE;
    }
    return TRUE;
}

LRESULT CALLBACK promptTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kPromptTestTimer) {
        KillTimer(hwnd, kPromptTestTimer);
        HWND dialog = nullptr;
        EnumWindows(findPromptDialog, (LPARAM)&dialog);
        const HWND edit = dialog ? FindWindowExW(dialog, nullptr, L"Edit", nullptr) : nullptr;
        if (edit) {
            SetWindowTextW(edit, L"typed in the box");
            PostMessageW(edit, WM_KEYDOWN, g_promptKey, 0);
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- menu test
// Drives a real popup menu - real windows, real posted mouse moves - and reads
// back what is on screen: which submenu window exists, where it sits, and
// which row is painted highlighted.  (Log lines and window counts once hid a
// submenu that never switched to another row's; this cannot.)

constexpr UINT_PTR kMenuTestTimer = 0x4D43;
constexpr UINT_PTR kPopupTestTimer = 0x4D44;
constexpr wchar_t kMenuTestClass[] = L"MyOldComputerMenuTest";

struct MenuDrive {
    struct Seen {
        bool hasSub = false;
        int subTop = 0, subHeight = 0;  // submenu top relative to the root's top
        bool row0Hot = false, row1Hot = false;  // painted highlight at the row centres
        bool row3Hot = false;                  // ... and at the fourth row's
    };
    HWND root = nullptr;
    int phase = 0;        // which drive this is (see menuTestStep)
    int step = 0;
    int chosen = 0;       // what the menu returned when a submenu row was clicked
    bool subHot = false;    // the submenu's own row, painted highlighted
    bool activates = false;  // the submenu asked the system to activate it on a click
    int x0 = 10, yc0 = 0, yc1 = 0, yc2 = 0, yc3 = 0;  // probe points, client coords of the root
    int itemH = 0, margin = 0;
    Seen afterA, afterB, backA, diagonal, downToB, curved, vertical, plain, returned;
    Seen tall, belowTall;  // a submenu far taller than the menu beside it
    Seen ownActivation;    // after one of the menu's own windows took the foreground
};

MenuDrive g_drive;
HWND g_popup = nullptr;       // the menu bar's popup, while its own test drives it
bool g_popupClicked = false;  // the double-click was posted to it
bool g_popupEscaped = false;  // ... and did not close it: Escape had to

BOOL CALLBACK collectMenuWindows(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t cls[64]{};
    if (pid != GetCurrentProcessId() || !GetClassNameW(h, cls, 64)) return TRUE;
    if (lstrcmpW(cls, L"MyOldComputerHMenu") == 0)
        ((std::vector<HWND>*)lp)->push_back(h);
    else if (lstrcmpW(cls, L"MyOldComputerMenu") == 0)
        g_popup = h;
    return TRUE;
}

// The menu windows of this process; the root is the one on the left, since a
// submenu opens to the right of its parent.
std::vector<HWND> menuWindows(HWND* root) {
    std::vector<HWND> wins;
    g_popup = nullptr;
    EnumWindows(collectMenuWindows, (LPARAM)&wins);
    *root = nullptr;
    for (HWND w : wins) {
        if (!*root) {
            *root = w;
            continue;
        }
        RECT a{}, b{};
        GetWindowRect(w, &a);
        GetWindowRect(*root, &b);
        if (a.left < b.left) *root = w;
    }
    return wins;
}

// True when the pixel the menu window shows at client (x, y) is on screen as
// its highlight colour.  Read from the display itself: the menu is a topmost
// popup, so this is literally what the user sees.
bool menuRowHot(HWND menu, int x, int y) {
    POINT p{x, y};
    ClientToScreen(menu, &p);
    HDC screen = GetDC(nullptr);
    const COLORREF px = GetPixel(screen, p.x, p.y);
    ReleaseDC(nullptr, screen);
    diagLog(L"menutest probe %d,%d = 0x%08lX (select 0x%08lX)", x, y, (unsigned long)px,
            (unsigned long)Theme::select);
    return (px & 0x00FFFFFF) == Theme::select;
}

MenuDrive::Seen menuTestLook() {
    MenuDrive::Seen seen;
    HWND root = nullptr;
    const std::vector<HWND> wins = menuWindows(&root);
    if (!root) return seen;  // the menu is gone
    RECT rr{};
    GetWindowRect(root, &rr);
    for (HWND w : wins) {
        if (w == root) continue;
        RECT wr{};
        GetWindowRect(w, &wr);
        seen.hasSub = true;
        seen.subTop = wr.top - rr.top;
        seen.subHeight = wr.bottom - wr.top;
    }
    seen.row0Hot = menuRowHot(root, g_drive.x0, g_drive.yc0);
    seen.row1Hot = menuRowHot(root, g_drive.x0, g_drive.yc1);
    return seen;
}

// One timer tick = one posted input, so every move is processed like a real
// one before the next step looks at the screen.
void menuTestStep(HWND owner) {
    MenuDrive& d = g_drive;
    auto post = [](HWND w, UINT msg, int x, int y) {
        PostMessageW(w, msg, 0, MAKELPARAM((short)x, (short)y));
    };
    auto next = [owner](int delay) { SetTimer(owner, kMenuTestTimer, delay, nullptr); };
    if (!d.root) {
        HWND root = nullptr;
        menuWindows(&root);
        d.root = root;
        if (!d.root) return;  // menu already closed
    }
    RECT rc{};
    GetClientRect(d.root, &rc);
    const int xe = rc.right - d.margin - 2;  // the root's right edge, just inside
    // Leaves the row at about 25 degrees for 40 px, then goes flat: the way a
    // hand really travels, and not a straight line to anything.
    auto curved = [&](int yFlat) {
        for (int k = 1; k <= 5; ++k) post(d.root, WM_MOUSEMOVE, d.x0 + 8 * k, d.yc0 + 4 * k);
        for (int x = d.x0 + 48; x <= xe; x += 8) post(d.root, WM_MOUSEMOVE, x, yFlat);
    };
    // The submenu window, and where its top left corner lies in the root's
    // client coordinates.
    auto submenu = [&](POINT* at) {
        HWND root = nullptr;
        for (HWND w : menuWindows(&root)) {
            if (w == root) continue;
            RECT a{}, b{};
            GetWindowRect(w, &a);
            GetWindowRect(root, &b);
            *at = POINT{a.left - b.left, a.top - b.top};
            return w;
        }
        return (HWND)nullptr;
    };
    switch (d.phase) {
    case 1:  // switching rows, with the path to the submenu crossing them
        switch (d.step++) {
        case 0:
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc0);  // hover A
            next(80);
            break;
        case 1:
            d.afterA = menuTestLook();
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc1);  // straight down to B
            next(80);
            break;
        case 2:
            d.afterB = menuTestLook();
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc0);  // back up to A
            next(80);
            break;
        case 3:
            d.backA = menuTestLook();
            // Diagonal from A toward the lower part of its own submenu: the
            // path crosses B's band on its way and ends in it.
            for (int k = 1; k <= 4; ++k)
                post(d.root, WM_MOUSEMOVE, d.x0 + (xe - d.x0) * k / 4,
                     d.yc0 + (d.itemH / 2 + 8) * k / 4);
            next(120);
            break;
        case 4:
            d.diagonal = menuTestLook();
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc1);  // back to the labels, at B
            next(80);
            break;
        case 5:
            d.downToB = menuTestLook();
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc0);  // back up to A, along the labels
            next(80);
            break;
        case 6:
            curved(d.yc0 + 22);
            next(120);
            break;
        case 7:
            d.curved = menuTestLook();
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc1);  // back to the labels, at B
            next(80);
            break;
        case 8:
            d.vertical = menuTestLook();
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc2);  // down to the plain item
            next(80);
            break;
        case 9:
            d.plain = menuTestLook();
            PostMessageW(d.root, WM_KEYDOWN, VK_ESCAPE, 0);
            next(150);
            break;
        default:
            menuClose();  // Escape did not close it: end this now, the check fails
            break;
        }
        break;
    case 2:  // all the way into the submenu, and a click in it
        switch (d.step++) {
        case 0:
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc0);
            next(80);
            break;
        case 1: {
            POINT at{};
            const HWND sub = submenu(&at);
            if (!sub) {
                menuClose();
                break;
            }
            curved(d.yc0 + 22);
            // On into the submenu, to its third row.
            const int y3 = d.margin + 2 * d.itemH + d.itemH / 2;
            for (int k = 1; k <= 5; ++k) {
                const int x = xe + (at.x + 36 - xe) * k / 5;
                const int y = d.yc0 + 22 + (at.y + y3 - (d.yc0 + 22)) * k / 5;
                if (x < at.x)
                    post(d.root, WM_MOUSEMOVE, x, y);
                else
                    post(sub, WM_MOUSEMOVE, x - at.x, y - at.y);
            }
            next(120);
            break;
        }
        case 2: {
            POINT at{};
            const HWND sub = submenu(&at);
            if (!sub) {
                menuClose();
                break;
            }
            const int y3 = d.margin + 2 * d.itemH + d.itemH / 2;
            d.subHot = menuRowHot(sub, 12, y3);
            // What Windows does around a real click in the submenu (a posted
            // message skips all of it): it asks the window whether to
            // activate, and on "yes" takes activation away from the window
            // that has it - the root menu.
            d.activates =
                SendMessageW(sub, WM_MOUSEACTIVATE, (WPARAM)sub,
                             MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)) != MA_NOACTIVATE;
            if (d.activates)
                SendMessageW(d.root, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), (LPARAM)sub);
            if (IsWindow(sub)) post(sub, WM_LBUTTONUP, 36, y3);  // click a3
            next(150);
            break;
        }
        default:
            menuClose();
            break;
        }
        break;
    case 3:  // into the submenu and back out into another row
        switch (d.step++) {
        case 0:
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc0);
            next(80);
            break;
        case 1: {
            POINT at{};
            const HWND sub = submenu(&at);
            if (!sub) {
                menuClose();
                break;
            }
            const int y1 = d.margin + d.itemH / 2;
            post(d.root, WM_MOUSEMOVE, xe - 4, d.yc0);
            post(sub, WM_MOUSEMOVE, 6, y1);
            post(sub, WM_MOUSEMOVE, 40, y1);
            next(120);
            break;
        }
        case 2: {
            POINT at{};
            const HWND sub = submenu(&at);
            if (sub) d.subHot = menuRowHot(sub, 12, d.margin + d.itemH / 2);
            // Back out into the root, in B's band and to the right of where
            // the pointer left A: inside the path to A's submenu.
            post(d.root, WM_MOUSEMOVE, xe - 2, d.yc1);
            next(100);
            break;
        }
        case 3:
            d.returned = menuTestLook();
            PostMessageW(d.root, WM_KEYDOWN, VK_ESCAPE, 0);
            next(150);
            break;
        default:
            menuClose();
            break;
        }
        break;
    case 4:  // a submenu far taller than the menu must not hold its row against the rows beside it
        switch (d.step++) {
        case 0:
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc3);  // hover D
            next(80);
            break;
        case 1:
            d.tall = menuTestLook();
            d.tall.row3Hot = menuRowHot(d.root, d.x0, d.yc3);
            // Down and a little to the right, out of D: along the menu.
            post(d.root, WM_MOUSEMOVE, d.x0 + 6, d.yc3 + d.itemH / 2 + 12);
            next(100);
            break;
        case 2:
            d.belowTall = menuTestLook();
            d.belowTall.row3Hot = menuRowHot(d.root, d.x0, d.yc3);
            PostMessageW(d.root, WM_KEYDOWN, VK_ESCAPE, 0);
            next(150);
            break;
        default:
            menuClose();
            break;
        }
        break;
    case 5:  // one of the menu's own windows takes the foreground from the root
        switch (d.step++) {
        case 0:
            post(d.root, WM_MOUSEMOVE, d.x0, d.yc0);
            next(80);
            break;
        case 1: {
            POINT at{};
            const HWND sub = submenu(&at);
            if (!sub) {
                menuClose();
                break;
            }
            // What Windows tells a window that is losing the foreground to
            // another one.
            SendMessageW(d.root, WM_ACTIVATE, MAKEWPARAM(WA_INACTIVE, 0), (LPARAM)sub);
            next(100);
            break;
        }
        case 2:
            d.ownActivation = menuTestLook();
            PostMessageW(d.root, WM_KEYDOWN, VK_ESCAPE, 0);
            next(150);
            break;
        default:
            menuClose();
            break;
        }
        break;
    }
}

LRESULT CALLBACK menuTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kMenuTestTimer) {
        KillTimer(hwnd, kMenuTestTimer);
        menuTestStep(hwnd);
        return 0;
    }
    if (msg == WM_TIMER && wp == kPopupTestTimer) {
        // The menu bar's popup is up.  Click outside it the way a quick second
        // click arrives: as a double-click.  If that does not close it, the
        // next tick does, so the test cannot hang on a menu left open.
        KillTimer(hwnd, kPopupTestTimer);
        if (!g_popupClicked) {
            HWND ignored = nullptr;
            menuWindows(&ignored);  // also finds g_popup
            if (g_popup) {
                PostMessageW(g_popup, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(-20, -20));
                g_popupClicked = true;
            }
            SetTimer(hwnd, kPopupTestTimer, 150, nullptr);
        } else if (g_popup && IsWindow(g_popup)) {
            g_popupEscaped = true;  // the double-click did not close it
            PostMessageW(g_popup, WM_KEYDOWN, VK_ESCAPE, 0);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// --------------------------------------------------------- rename field test
// A row painted into a bitmap, the row's origin at (0, 0): what the renderer
// really drew, readable pixel by pixel.
constexpr wchar_t kRenameTestClass[] = L"MyOldComputerRenameTest";
constexpr COLORREF kSeen = RGB(255, 0, 255);  // the test window's background

// The edit control's parent in this test: one flat colour, so that colour on
// screen proves nothing is covering the window.
LRESULT CALLBACK renameTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        HBRUSH brush = CreateSolidBrush(kSeen);
        FillRect((HDC)wp, &rc, brush);
        DeleteObject(brush);
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

struct RowBitmap {
    HDC dc = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ old = nullptr;
    RECT rect{};  // the row's rect in window coordinates, 1 px larger all round
    int width = 0, height = 0;

    RowBitmap(const App& app, int row) {
        RECT r = app.mode == ViewMode::Details ? app.listRowRect(row) : app.gridCellRect(row);
        // One pixel of the neighbourhood all round: anything the row paints
        // out there is painting over another row.
        rect = RECT{r.left - 1, r.top - 1, r.right + 1, r.bottom + 1};
        width = (int)(rect.right - rect.left);
        height = (int)(rect.bottom - rect.top);
        BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER)};
        info.biWidth = width;
        info.biHeight = -height;  // top-down
        info.biPlanes = 1;
        info.biBitCount = 32;
        void* bits = nullptr;
        bmp = CreateDIBSection(nullptr, (BITMAPINFO*)&info, DIB_RGB_COLORS, &bits, nullptr, 0);
        dc = CreateCompatibleDC(nullptr);
        old = SelectObject(dc, bmp);
    }
    RowBitmap(const RowBitmap&) = delete;
    RowBitmap& operator=(const RowBitmap&) = delete;
    ~RowBitmap() {
        if (old) SelectObject(dc, old);
        if (bmp) DeleteObject(bmp);
        if (dc) DeleteDC(dc);
    }

    // The row as it is painted right now.
    void paint(App& app, int row) {
        RECT all{0, 0, width, height};
        FillRect(dc, &all, (HBRUSH)GetStockObject(WHITE_BRUSH));
        SetViewportOrgEx(dc, -rect.left, -rect.top, nullptr);
        {
            Canvas c(dc, app.scale);
            renderItemRow(c, app, row);
        }
        SetViewportOrgEx(dc, 0, 0, nullptr);
    }

    // What the screen shows in the window rect `at`, copied over the row at
    // that place.  The edit control is read this way: without visual styles a
    // classic EDIT paints nothing on WM_PRINTCLIENT, so the display is the
    // only honest copy of it.
    void capture(const RECT& at, const POINT& screen) {
        HDC display = GetDC(nullptr);
        BitBlt(dc, at.left - rect.left, at.top - rect.top, (int)(at.right - at.left),
               (int)(at.bottom - at.top), display, screen.x, screen.y, SRCCOPY);
        ReleaseDC(nullptr, display);
    }
};

struct PixelBox {
    int left = 0, top = 0, right = 0, bottom = 0;  // right and bottom exclusive
    int count = 0;                                  // matching pixels; 0 = none
};

bool sameBox(const PixelBox& a, const PixelBox& b) {
    return a.count && b.count && a.left == b.left && a.top == b.top && a.right == b.right &&
           a.bottom == b.bottom;
}

bool isNavy(COLORREF px) { return (px & 0xFFFFFF) == (Theme::select & 0xFFFFFF); }
bool isBlack(COLORREF px) { return (px & 0xFFFFFF) == (Theme::dark & 0xFFFFFF); }

// Ink of a name: a pixel at least 78% covered on every subpixel - the same
// for the row the renderer painted and for the edit control read from the
// screen, both black on white.
bool blackInk(COLORREF px) {
    return GetRValue(px) <= 56 && GetGValue(px) <= 56 && GetBValue(px) <= 56;
}
bool notWhite(COLORREF px) { return (px & 0xFFFFFF) != 0xFFFFFF; }

// A pixel of an icon: anything but the selection blue, white and black, the
// only colours a selection box that has swallowed its icon can be made of.
bool iconInk(COLORREF px) {
    return !isNavy(px) && !isBlack(px) && (px & 0xFFFFFF) != 0xFFFFFF;
}

// Bounding box of the pixels of `area` (bitmap coordinates) that `take` takes.
PixelBox pixels(HDC dc, const RECT& area, bool (*take)(COLORREF)) {
    PixelBox box;
    for (int y = area.top; y < area.bottom; ++y) {
        for (int x = area.left; x < area.right; ++x) {
            const COLORREF px = GetPixel(dc, x, y);
            if (px == CLR_INVALID || !take(px)) continue;
            if (box.count++ == 0) {
                box.left = x;
                box.top = y;
            } else {
                if (x < box.left) box.left = x;
                if (y < box.top) box.top = y;
            }
            if (x + 1 > box.right) box.right = x + 1;
            if (y + 1 > box.bottom) box.bottom = y + 1;
        }
    }
    return box;
}

// The same for a painted row, with `area` given in window coordinates; the
// box comes back in window coordinates too.
PixelBox find(const RowBitmap& bmp, const RECT& area, bool (*take)(COLORREF)) {
    PixelBox box = pixels(bmp.dc, RECT{area.left - bmp.rect.left, area.top - bmp.rect.top,
                                       area.right - bmp.rect.left, area.bottom - bmp.rect.top},
                          take);
    box.left += bmp.rect.left;
    box.right += bmp.rect.left;
    box.top += bmp.rect.top;
    box.bottom += bmp.rect.top;
    return box;
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

    // The tree has to offer exactly the menu the file list offers.
    CreateDirectoryW((base + L"\\sub").c_str(), nullptr);
    app.reload();
    const std::wstring sub = base + L"\\sub";
    app.selection.clear();
    app.selection.insert(sub);
    app.contextFolder = sub;
    auto listMenu = app.contextMenu();
    app.selection.clear();
    app.contextFolder = sub;  // what a tree right-click sets up
    auto treeMenu = app.contextMenu();
    bool sameMenu = listMenu.size() == treeMenu.size();
    for (size_t i = 0; sameMenu && i < listMenu.size(); ++i)
        sameMenu = listMenu[i].id == treeMenu[i].id && listMenu[i].title == treeMenu[i].title;
    check(L"树与列表右键菜单一致", sameMenu);

    // A right-click in the tree makes that row the selected one for as long
    // as its menu is up, like Explorer's tree: painted in the selection
    // colour, and the current folder's row not.
    {
        const bool sidebarWas = app.sidebarVisible;
        app.sidebarVisible = true;
        app.relayout(1100, 760);
        const int selected = app.treeSelectedRow();
        int row = -1;
        const RECT vp = app.lay.treeViewport;
        for (int i = 0; i < (int)app.treeFlat.size() && row < 0; ++i) {
            const RECT r = app.treeRowRect(i);
            if (i != selected && r.top >= vp.top && r.bottom <= vp.bottom && r.right > r.left) row = i;
        }
        // The window as it would be painted now, read at one point.
        const auto selectAt = [&app](POINT p) {
            HDC screen = GetDC(nullptr);
            HDC mem = CreateCompatibleDC(screen);
            HBITMAP bmp = CreateCompatibleBitmap(screen, app.width, app.height);
            HGDIOBJ old = SelectObject(mem, bmp);
            {
                Canvas c(mem, app.scale);
                renderApp(c, app);
            }
            const COLORREF px = GetPixel(mem, p.x, p.y);
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            ReleaseDC(nullptr, screen);
            return (px & 0x00FFFFFF) == Theme::select;
        };
        // Just inside the row's right end, past where a clipped title may reach.
        const auto probe = [&app](int i) {
            const RECT r = app.treeRowRect(i);
            return POINT{r.right - 2, (r.top + r.bottom) / 2};
        };
        app.contextTreeRow = row;
        const bool rowBlue = row >= 0 && selectAt(probe(row));
        const bool currentPlain = selected < 0 || !selectAt(probe(selected));
        app.contextTreeRow = -1;
        const bool rowPlainAgain = row >= 0 && !selectAt(probe(row));
        const bool currentBlueAgain = selected < 0 || selectAt(probe(selected));
        check(L"文件树里右键点的那一行在菜单开着时是选中的蓝色", rowBlue && currentPlain);
        check(L"菜单关掉后文件树的选中高亮回到当前文件夹", rowPlainAgain && currentBlueAgain);
        app.sidebarVisible = sidebarWas;
        app.relayout(1100, 760);
    }

    bool hasProps = false, hasNewFolder = false, hasOpenWith = false, hasShortcut = false;
    for (auto& it : listMenu) {
        if (it.id == ID_PROPERTIES) hasProps = true;
        if (it.id == ID_CREATE_SHORTCUT) hasShortcut = true;
    }
    check(L"文件夹菜单有属性和快捷方式", hasProps && hasShortcut);
    app.selection.clear();
    app.selection.insert(base + L"\\item0.txt");
    app.contextFolder.clear();
    auto fileMenu = app.contextMenu();
    for (auto& it : fileMenu)
        if (it.id == ID_OPEN_WITH) hasOpenWith = true;
    check(L"文件菜单有打开方式", hasOpenWith);
    app.selection.clear();
    app.contextFolder.clear();
    auto background = app.contextMenu();
    for (auto& it : background)
        if (it.id == ID_NEW_FOLDER) hasNewFolder = true;
    check(L"空白右键有新建文件夹", hasNewFolder);
    // The real background menu, in Explorer's order, with New as a submenu.
    {
        shellmenu::ViewState state;
        state.menuMask = kMenuDefaults;
        std::vector<std::wstring> subs;
        const std::vector<std::wstring> top = shellmenu::labels(app.current, {}, state, &subs);
        const bool flat = std::find(top.begin(), top.end(), L"新建文件夹") != top.end() ||
                          std::find(top.begin(), top.end(), L"新建文本文档") != top.end();
        check(L"空白菜单依次为 查看/排序方式/刷新/新建",
              top.size() >= 4 && top[0] == L"查看" && top[1] == L"排序方式" &&
                  top[2] == L"刷新" && top[3] == L"新建");
        check(L"新建是子菜单，里面是 文件夹 和 文本文档",
              !flat && std::find(subs.begin(), subs.end(), L"新建/文件夹") != subs.end() &&
                  std::find(subs.begin(), subs.end(), L"新建/文本文档") != subs.end());
    }
    app.selection.clear();

    // List view: the seam to the right of a short name is blank canvas, the
    // same seam a right-click must not turn into a neighbouring item.
    app.setViewMode(ViewMode::List);
    app.relayout(1100, 760);
    app.clampScroll();
    {
        RECT cellL = app.gridCellRect(0);
        POINT seam{cellL.left + (int)(150 * app.scale + 0.5), (cellL.top + cellL.bottom) / 2};
        check(L"列表缝隙不命中", !app.hitTestItem(seam, nullptr));
    }

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
    check(L"原生文件菜单可用", shellmenu::canBuildItemMenu(base + L"\\item0.txt"));
    check(L"原生背景菜单可用", shellmenu::canBuildBackgroundMenu(base));

    auto fileLabels =
        shellmenu::labels(base, {base + L"\\item0.txt"}, shellmenu::ViewState{});
    bool menuHasOpen = false, menuHasProps = false;
    for (auto& text : fileLabels) {
        if (text.find(L"打开") != std::wstring::npos) menuHasOpen = true;
        if (text.find(L"属性") != std::wstring::npos) menuHasProps = true;
    }
    check(L"系统菜单含打开和属性", fileLabels.size() >= 6 && menuHasOpen && menuHasProps);
    auto folderLabels = shellmenu::labels(base, {}, shellmenu::ViewState{});
    bool menuHasView = false;
    for (auto& text : folderLabels)
        if (text.find(L"查看") != std::wstring::npos) menuHasView = true;
    check(L"背景菜单含查看", folderLabels.size() >= 4 && menuHasView);

    auto folderItemLabels = shellmenu::labels(base, {sub}, shellmenu::ViewState{});
    bool hasNewWindow = false, hasRename = false;
    for (auto& text : folderItemLabels) {
        if (text.find(L"在新窗口中打开") != std::wstring::npos) hasNewWindow = true;
        if (text.find(L"重命名") != std::wstring::npos) hasRename = true;
    }
    check(L"文件夹菜单有新窗口和重命名", hasNewWindow && hasRename);

    // Double-click opens only through the icon or the name; the blank rest of
    // a row must stay inert, like Explorer.
    {
        app.loadDirectory(base, false);
        app.setViewMode(ViewMode::Details);
        app.relayout(1100, 760);
        int subRow = -1;
        for (int i = 0; i < (int)app.entries.size(); ++i)
            if (app.entries[i].isDir) { subRow = i; break; }
        RECT r = app.listRowRect(subRow);
        const int midY = (r.top + r.bottom) / 2;
        // No seam between two items: the last pixel of a row, the boundary
        // itself and the first pixel of the next row must all hit an item.
        int hitRow = -2;
        check(L"行内边缘命中",
              app.hitTestItem(POINT{r.left + 40, r.bottom - 1}, &hitRow) && hitRow == subRow);
        check(L"行交界命中下一行",
              app.hitTestItem(POINT{r.left + 40, r.bottom}, &hitRow) && hitRow == subRow + 1);
        onDoubleClick(nullptr, app, POINT{r.left + 10, midY});
        check(L"双击名称进入文件夹", app.current == sub);
        app.loadDirectory(base, false);
        onDoubleClick(nullptr, app, POINT{r.left + app.lay.nameW + 60, midY});
        check(L"双击行空白不打开", app.current == base);
        // Explorer-style rename starts in place and cancels cleanly.
        app.selection.clear();
        app.selection.insert(app.entries[subRow].path);
        app.renameSelection();
        check(L"就地重命名启动", app.renaming && app.renameRow == subRow);
        app.endRename(false);
        check(L"就地重命名取消", !app.renaming && fsx::exists(sub));
    }

    // Per-context hiding: background, folder and per-extension lists are
    // independent, and a file gets its generic list plus its extension's.
    app.settings.hiddenBg = {L"查看"};
    app.settings.hiddenFolder = {L"在新窗口中打开"};
    app.settings.hiddenFile = {L"打印"};
    app.settings.hiddenExt[L"txt"] = {L"编辑"};
    check(L"空白处隐藏独立", app.hiddenFor({}) == std::vector<std::wstring>{L"查看"});
    check(L"文件夹隐藏独立",
          app.hiddenFor({sub}) == std::vector<std::wstring>{L"在新窗口中打开"});
    auto fileHidden = app.hiddenFor({base + L"\\item0.txt"});
    bool hasPrint = false, hasEdit = false;
    for (auto& h : fileHidden) {
        if (h == L"打印") hasPrint = true;
        if (h == L"编辑") hasEdit = true;
    }
    check(L"文件隐藏=通用+扩展名", hasPrint && hasEdit && fileHidden.size() == 2);
    check(L"扩展名菜单可枚举", !shellmenu::probeFileLabels(L"txt").empty());

    // The settings dialog renders its sectioned list offscreen.
    {
        std::vector<MenuSection> secs;
        MenuSection s1;
        s1.key = L"bg";
        s1.title = L"空白处（当前文件夹）";
        s1.items = {L"粘贴", L"属性", L"显示选项"};
        s1.hidden = {L"显示选项"};
        MenuSection s2;
        s2.key = L"dir";
        s2.title = L"文件夹";
        s2.items = {L"打开", L"在新窗口中打开", L"属性"};
        MenuSection s3;
        s3.key = L"txt";
        s3.title = L".txt 文件";
        s3.items = {L"打开", L"编辑", L"打印", L"属性"};
        s3.hidden = {L"打印"};
        secs = {s1, s2, s3};
        HDC pScreen = GetDC(nullptr);
        HDC pMem = CreateCompatibleDC(pScreen);
        HBITMAP pBmp = CreateCompatibleBitmap(pScreen, 760, 520);
        HGDIOBJ pOld = SelectObject(pMem, pBmp);
        {
            Canvas c(pMem, 1.0);
            drawOptionsPreview(c, RECT{0, 0, 760, 520}, 1.0, kMenuDefaults, secs);
        }
        int inked = 0;
        for (int y = 0; y < 520; y += 2)
            for (int x = 380; x < 760; x += 2)
                if (GetPixel(pMem, x, y) != Theme::face) ++inked;
        check(L"设置对话框分组列表渲染", inked > 200);
        // Save the frame as a BMP so the layout can be inspected by hand.
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 760;
        bmi.bmiHeader.biHeight = -520;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        std::vector<BYTE> bits((size_t)760 * 520 * 4);
        GetDIBits(pMem, pBmp, 0, 520, bits.data(), &bmi, DIB_RGB_COLORS);
        wchar_t out[MAX_PATH]{};
        GetTempPathW(MAX_PATH, out);
        wcscat_s(out, L"moc-opts-preview.bmp");
        FILE* f = nullptr;
        if (_wfopen_s(&f, out, L"wb") == 0 && f) {
            BITMAPFILEHEADER fh{};
            fh.bfType = 0x4D42;
            fh.bfOffBits = sizeof(fh) + sizeof(BITMAPINFOHEADER);
            fh.bfSize = fh.bfOffBits + (DWORD)bits.size();
            fwrite(&fh, sizeof(fh), 1, f);
            fwrite(&bmi.bmiHeader, sizeof(bmi.bmiHeader), 1, f);
            fwrite(bits.data(), bits.size(), 1, f);
            fclose(f);
        }
        SelectObject(pMem, pOld);
        DeleteObject(pBmp);
        DeleteDC(pMem);
        ReleaseDC(nullptr, pScreen);
    }
    app.settings.hiddenBg.clear();
    app.settings.hiddenFolder.clear();
    app.settings.hiddenFile.clear();
    app.settings.hiddenExt.clear();

    // A deleted folder must disappear from the tree model.
    const std::wstring live = base + L"\\tree-live";
    CreateDirectoryW(live.c_str(), nullptr);
    TreeNode keepNode;
    keepNode.kind = TreeNode::Folder;
    keepNode.path = live;
    keepNode.title = L"tree-live";
    TreeNode goneNode;
    goneNode.kind = TreeNode::Folder;
    goneNode.path = base + L"\\tree-gone";
    goneNode.title = L"tree-gone";
    app.treeRoots.push_back(keepNode);
    app.treeRoots.push_back(goneNode);
    app.flattenTree();
    auto hasTreePath = [&app](const std::wstring& p) {
        for (auto* node : app.treeFlat)
            if (_wcsicmp(node->path.c_str(), p.c_str()) == 0) return true;
        return false;
    };
    const bool hadGone = hasTreePath(goneNode.path);
    app.pruneTree();
    check(L"删除的树节点消失", hadGone && !hasTreePath(goneNode.path) && hasTreePath(live));

    // The whole refresh path: delete a folder, then refresh like the menu does.
    const std::wstring victim = base + L"\\tree-victim";
    CreateDirectoryW(victim.c_str(), nullptr);
    TreeNode victimNode;
    victimNode.kind = TreeNode::Folder;
    victimNode.path = victim;
    victimNode.title = L"tree-victim";
    app.treeRoots.push_back(victimNode);
    app.flattenTree();
    RemoveDirectoryW(victim.c_str());
    app.refreshAfterChange();
    check(L"刷新后删除的文件夹离开树", !hasTreePath(victim));

    app.contextFolder = base;
    app.newTextFile();
    check(L"新建文本文档", fsx::exists(base + L"\\新建文本文档.txt"));

    // Rubber band geometry: both corners must really be drawn.
    app.setViewMode(ViewMode::Details);
    app.relayout(1100, 760);
    app.marquee = true;
    app.marqueeStart = POINT{app.lay.listViewport.left + 50, app.lay.listViewport.top + 20};
    app.marqueeEnd = POINT{app.lay.listViewport.left + 300, app.lay.listViewport.top + 120};
    HDC bandScreen = GetDC(nullptr);
    HDC bandDc = CreateCompatibleDC(bandScreen);
    HBITMAP bandBmp = CreateCompatibleBitmap(bandScreen, app.width, app.height);
    HGDIOBJ bandOld = SelectObject(bandDc, bandBmp);
    {
        Canvas c(bandDc, app.scale);
        renderApp(c, app);
        renderMarquee(c, app);
    }
    int darkTop = 0, firstTop = -1, lastTop = -1;
    for (int x = app.marqueeStart.x; x < app.marqueeEnd.x; ++x) {
        if (GetPixel(bandDc, x, app.marqueeStart.y) == Theme::dark) {
            ++darkTop;
            if (firstTop < 0) firstTop = x;
            lastTop = x;
        }
    }
    check(L"框选框横跨整段", darkTop > 10 && firstTop == app.marqueeStart.x &&
                                 lastTop >= app.marqueeEnd.x - 3);
    bool leftEdge = GetPixel(bandDc, app.marqueeStart.x, app.marqueeStart.y + 20) == Theme::dark;
    bool rightEdge = GetPixel(bandDc, app.marqueeEnd.x - 1, app.marqueeStart.y + 20) == Theme::dark;
    check(L"框选框左右边都在", leftEdge && rightEdge);
    SelectObject(bandDc, bandOld);
    DeleteObject(bandBmp);
    DeleteDC(bandDc);
    ReleaseDC(nullptr, bandScreen);
    app.marquee = false;
    app.marqueeEnd = app.marqueeStart;

    // A large listing must not make the rubber band re-measure every name.
    const std::wstring bigDir = base + L"\\big";
    CreateDirectoryW(bigDir.c_str(), nullptr);
    for (int i = 0; i < 1200; ++i) {
        const std::wstring file = bigDir + L"\\f" + std::to_wstring(i) + L".bin";
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
    }
    app.loadDirectory(bigDir, false);
    app.setViewMode(ViewMode::Details);
    app.relayout(1100, 760);

    // The scroll fast path (slide the cached frame, re-render the exposed
    // strip) must land on exactly the pixels a full render makes.
    {
        HDC screen2 = GetDC(nullptr);
        HDC full = CreateCompatibleDC(screen2);
        HDC slid = CreateCompatibleDC(screen2);
        HBITMAP fullBmp = CreateCompatibleBitmap(screen2, app.width, app.height);
        HBITMAP slidBmp = CreateCompatibleBitmap(screen2, app.width, app.height);
        HGDIOBJ fullOld = SelectObject(full, fullBmp);
        HGDIOBJ slidOld = SelectObject(slid, slidBmp);
        app.listScroll = 0;
        app.clampScroll();
        { Canvas c(slid, app.scale); renderApp(c, app); }  // the cached frame at top
        const int jump = 7 * app.m.rowH + 5;               // deliberately not row-aligned
        app.listScroll = jump;
        app.clampScroll();
        { Canvas c(full, app.scale); renderApp(c, app); }  // reference render
        scrollFrame(slid, app, app.lay.listViewport, app.lay.listBar, jump);
        // Measured values go into the label of a failed check.
        int differing = 0, firstX = -1, firstY = -1;
        COLORREF fullAt = 0, slidAt = 0;
        for (int y = 0; y < app.height; y += 2)
            for (int x = 0; x < app.width; x += 2) {
                const COLORREF a = GetPixel(full, x, y), b = GetPixel(slid, x, y);
                if (a == b) continue;
                if (firstX < 0) {
                    firstX = x;
                    firstY = y;
                    fullAt = a;
                    slidAt = b;
                }
                ++differing;
            }
        const wchar_t* scrollFastName = L"滚动快路径与整帧一致";
        const std::wstring scrollFastLabel =
            differing
                ? scrollFastName + std::wstring(L"：") + std::to_wstring(differing) +
                      L" 处不同，首处在 " + std::to_wstring(firstX) + L"," +
                      std::to_wstring(firstY) + L" 整帧 " +
                      std::to_wstring(fullAt & 0xFFFFFF) + L" 滑动 " +
                      std::to_wstring(slidAt & 0xFFFFFF)
                : std::wstring(scrollFastName);
        check(scrollFastLabel.c_str(), !differing);
        SelectObject(full, fullOld);
        SelectObject(slid, slidOld);
        DeleteObject(fullBmp);
        DeleteObject(slidBmp);
        DeleteDC(full);
        DeleteDC(slid);
        ReleaseDC(nullptr, screen2);
        app.listScroll = 0;
    }

    app.marquee = true;
    app.marqueeStart = POINT{app.lay.listViewport.left + 30, app.lay.listViewport.top + 10};
    const ULONGLONG dragStart = GetTickCount64();
    for (int k = 0; k < 40; ++k) {
        const RECT previousBand = marqueeBand(app);
        app.marqueeEnd =
            POINT{app.lay.listViewport.left + 240, app.lay.listViewport.top + 10 + k * 12};
        marqueeUpdate(app, previousBand);
    }
    const ULONGLONG dragMs = GetTickCount64() - dragStart;
    app.marquee = false;
    std::wstring dragLabel = L"1200 项框选40次 " + std::to_wstring(dragMs) + L"ms";
    check(dragLabel.c_str(), dragMs < 400);

    // The popup menu, with real windows.  Menu: A(a1,a2,a3) / B(b1) / C.
    // What matters is the submenu that is actually on screen (A's has three
    // rows, B's one) and the row actually painted.
    {
        WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
        wc.lpfnWndProc = menuTestProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kMenuTestClass;
        RegisterClassExW(&wc);
        HWND owner = CreateWindowExW(0, kMenuTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100,
                                     nullptr, nullptr, wc.hInstance, nullptr);
        HMENU menu = CreatePopupMenu();
        HMENU subA = CreatePopupMenu();
        AppendMenuW(subA, MF_STRING, 201, L"a1");
        AppendMenuW(subA, MF_STRING, 202, L"a2");
        AppendMenuW(subA, MF_STRING, 203, L"a3");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)subA, L"A");
        HMENU subB = CreatePopupMenu();
        AppendMenuW(subB, MF_STRING, 211, L"b1");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)subB, L"B");
        AppendMenuW(menu, MF_STRING, 221, L"C");
        HMENU subD = CreatePopupMenu();  // far taller than the menu it belongs to
        for (int i = 0; i < 8; ++i) AppendMenuW(subD, MF_STRING, 240 + i, L"d");
        AppendMenuW(menu, MF_POPUP, (UINT_PTR)subD, L"D");

        MenuDrive& d = g_drive;
        const double s = dpiScaleOfWindow(owner);
        const int margin = (int)(3 * s + 0.5);
        const int itemH = (int)(22 * s + 0.5);
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        // Shows the menu and lets the timer drive it.  A click anywhere else
        // closes it early; then the drive is simply repeated.
        const auto drive = [&](int phase, int steps) {
            for (int attempt = 0; attempt < 4; ++attempt) {
                d = MenuDrive{};
                d.phase = phase;
                d.margin = margin;
                d.itemH = itemH;
                d.yc0 = margin + itemH / 2;
                d.yc1 = d.yc0 + itemH;
                d.yc2 = d.yc1 + itemH;
                d.yc3 = d.yc2 + itemH;
                const HWND foreground = GetForegroundWindow();
                SetTimer(owner, kMenuTestTimer, 150, nullptr);
                const int chosen = showHMenu(owner, menu, POINT{work.left + 40, work.top + 40});
                if (foreground) SetForegroundWindow(foreground);
                KillTimer(owner, kMenuTestTimer);
                if (d.step < steps) continue;  // closed from outside before the drive ended
                if (phase == 2) d.chosen = chosen;
                break;
            }
        };
        // Measured values go into the label of a failed check.
        auto look = [](const MenuDrive::Seen& v) {
            return L"（实测 top=" + std::to_wstring(v.subTop) + L" h=" +
                   std::to_wstring(v.subHeight) + L" 行0=" + std::to_wstring((int)v.row0Hot) +
                   L" 行1=" + std::to_wstring((int)v.row1Hot) + L"）";
        };
        auto expect = [&check, &look](const wchar_t* name, const MenuDrive::Seen& v, int top,
                                      int height, bool row0, bool row1) {
            std::wstring label = name;
            if (!(v.hasSub && v.subTop == top && v.subHeight == height && v.row0Hot == row0 &&
                  v.row1Hot == row1))
                label += look(v);
            check(label.c_str(), label == name);
        };
        const int topA = 0, heightA = 2 * margin + 3 * itemH;
        const int topB = itemH, heightB = 2 * margin + itemH;
        const int topD = 3 * itemH, heightD = 2 * margin + 8 * itemH;

        // 1. The row under the pointer is the selected row - except while the
        //    pointer is on its way from A to A's submenu, which lies beside
        //    B: that path crosses B, and the submenu has to still be there
        //    when the pointer arrives.
        drive(1, 10);
        expect(L"悬停A弹出A的子菜单", d.afterA, topA, heightA, true, false);
        expect(L"垂直移到B切换为B的子菜单", d.afterB, topB, heightB, false, true);
        expect(L"回到A切回A的子菜单", d.backA, topA, heightA, true, false);
        expect(L"斜线穿过B行去A的子菜单：A保持", d.diagonal, topA, heightA, true, false);
        expect(L"从去子菜单的路上回到标签处：立刻到B", d.downToB, topB, heightB, false, true);
        expect(L"先陡后平去A的子菜单：A保持", d.curved, topA, heightA, true, false);
        expect(L"再回到标签处：立刻到B", d.vertical, topB, heightB, false, true);
        check(L"移到普通项关闭子菜单", !d.plain.hasSub && !d.plain.row0Hot && !d.plain.row1Hot);

        // 4. A submenu eight rows tall beside a four-row menu: its path must
        //    not be so wide that D keeps the selection while the pointer
        //    moves along the menu.
        drive(4, 3);
        check(L"很高的子菜单照样弹出", d.tall.hasSub && d.tall.subTop == topD &&
                                    d.tall.subHeight == heightD && d.tall.row3Hot);
        check(L"很高的子菜单不拦着离开它那一行",
              !d.belowTall.hasSub && !d.belowTall.row3Hot);

        // 2. All the way in, and the click lands: the menu returns a3.
        drive(2, 3);
        check(L"点击菜单窗口时不要求激活（否则根菜单会被取消激活而关闭）", !d.activates);
        check(L"沿路走进子菜单，点的那一项就是要执行的那一项", d.chosen == 203 && d.subHot);

        // 3. Back out of the submenu into another row: that row is selected.
        drive(3, 4);
        expect(L"从子菜单回到B行就是B", d.returned, topB, heightB, false, true);

        // 5. The foreground moving to one of the menu's own windows is not
        //    the user leaving the menu.
        drive(5, 3);
        expect(L"前台转给菜单自己的窗口时菜单不关", d.ownActivation, topA, heightA, true, false);
        DestroyMenu(menu);

        // The menu bar's popup (another window class) closes on a quick
        // second click too, which arrives as a double-click.
        {
            const std::vector<MenuItem> items{{L"a", 301, true}, {L"b", 302, true}};
            // Another window taking the foreground closes the popup before the
            // click (someone is using the machine); then it is simply repeated.
            for (int attempt = 0; attempt < 3; ++attempt) {
                g_popupClicked = false;
                g_popupEscaped = false;
                SetTimer(owner, kPopupTestTimer, 200, nullptr);
                showPopupMenu(owner, items, work.left + 40, work.top + 40, false);
                KillTimer(owner, kPopupTestTimer);
                if (g_popupClicked) break;
            }
            check(L"菜单栏弹出的菜单被快速的第二下点击关掉", g_popupClicked && !g_popupEscaped);
        }
        DestroyWindow(owner);
    }

    // A second click that comes fast enough arrives as a double-click.  On
    // the toolbar it is just another click: the View button is pressed by it.
    {
        app.setViewMode(ViewMode::Details);
        app.relayout(1100, 760);
        const RECT tool = app.toolbarItemRect(4);
        const POINT at{(tool.left + tool.right) / 2, (tool.top + tool.bottom) / 2};
        const ViewMode before = app.mode;
        app.pressTool = -1;
        onDoubleClick(nullptr, app, at);
        const bool pressed = app.pressTool == 4;
        onLButtonUp(nullptr, app, at);
        check(L"快速的第二下点击（双击消息）照样按下工具栏的查看按钮",
              pressed && app.mode != before && app.pressTool == -1);
        app.setViewMode(ViewMode::Details);
    }

    // The Favorites in the sidebar - this app's own, not Explorer's: add,
    // remove, rename, reorder, drop on, and what the tree and the menus show.
    {
        const std::wstring favA = base + L"\\favA", favB = base + L"\\favB", favC = base + L"\\favC";
        const std::wstring favD = base + L"\\favD", favGone = base + L"\\favGone";
        for (const std::wstring& d : {favA, favB, favC, favD, favGone})
            CreateDirectoryW(d.c_str(), nullptr);
        // The favorites as a set: paths only, order ignored.
        const auto sameSet = [](const std::vector<Favorite>& favs,
                                std::initializer_list<const std::wstring*> want) {
            std::vector<std::wstring> a, b;
            for (auto& f : favs) a.push_back(f.path);
            for (auto* w : want) b.push_back(*w);
            auto less = [](const std::wstring& x, const std::wstring& y) {
                return _wcsicmp(x.c_str(), y.c_str()) < 0;
            };
            std::sort(a.begin(), a.end(), less);
            std::sort(b.begin(), b.end(), less);
            return a == b;
        };
        // The favorites in their order.
        const auto inOrder = [](const std::vector<Favorite>& favs,
                                std::initializer_list<const std::wstring*> want) {
            if (favs.size() != want.size()) return false;
            size_t i = 0;
            for (auto* w : want)
                if (_wcsicmp(favs[i++].path.c_str(), w->c_str()) != 0) return false;
            return true;
        };
        const auto groupRow = [&app]() {
            for (int i = 0; i < (int)app.treeFlat.size(); ++i)
                if (app.treeFlat[i]->kind == TreeNode::Favorites) return i;
            return -1;
        };
        const auto favRows = [&app]() {
            std::vector<int> rows;
            for (int i = 0; i < (int)app.treeFlat.size(); ++i)
                if (app.treeFlat[i]->kind == TreeNode::Favorite) rows.push_back(i);
            return rows;
        };
        const auto titleOf = [&app](int row) { return app.treeFlat[row]->title; };
        const auto center = [&app](int row) {
            const RECT r = app.treeRowRect(row);
            return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
        };

        // Add
        app.settings.favorites.clear();
        app.refreshTree();
        check(L"收藏夹一组始终在树的最上面，空的时候也在", groupRow() == 0 && favRows().empty());
        app.addFavorite(favA);
        app.addFavorite(favB);
        app.addFavorite(favC);
        app.addFavorite(favA);
        app.addFavorite(base + L"\\item0.txt");
        check(L"加入收藏夹：文件夹收下，重复的和文件不收",
              sameSet(app.settings.favorites, {&favA, &favB, &favC}));
        check(L"树里的收藏夹一组按顺序列出它们",
              favRows().size() == 3 && titleOf(favRows()[0]) == L"favA" &&
                  titleOf(favRows()[1]) == L"favB" && titleOf(favRows()[2]) == L"favC");

        // Rename
        app.renameFavorite(favB, L"  工作  ");
        check(L"收藏项可以起自己的名字（两端的空格不算）",
              app.settings.favorites[1].name == L"工作" && titleOf(favRows()[1]) == L"工作");
        app.renameFavorite(favB, L"favB");
        check(L"改回文件夹自己的名字，就不再带别名",
              app.settings.favorites[1].name.empty() && titleOf(favRows()[1]) == L"favB");

        // A favorite whose folder is gone
        app.addFavorite(favGone);
        RemoveDirectoryW(favGone.c_str());
        app.refreshTree();
        check(L"目标没了的收藏项照样列出，并标成失效",
              favRows().size() == 4 && app.treeFlat[favRows()[3]]->missing &&
                  titleOf(favRows()[3]) == L"favGone");

        // The Favorites menu
        const std::vector<MenuItem> favMenu = app.menuFor(L"收藏");
        const MenuItem* itemA = nullptr;
        const MenuItem* itemGone = nullptr;
        for (auto& it : favMenu) {
            if (it.title == L"favA") itemA = &it;
            if (it.title == L"favGone（已失效）") itemGone = &it;
        }
        check(L"“收藏”菜单按名字列出收藏项，失效的那项变灰并注明",
              itemA && itemA->enabled && itemGone && !itemGone->enabled);
        app.performMenuAction(itemA->id);
        check(L"“收藏”菜单里点一项就跳过去", app.current == favA);
        app.performMenuAction(itemGone->id);
        check(L"失效的那项点了不跳", app.current == favA);
        check(L"当前文件夹已在收藏夹里：“添加当前文件夹”变灰", !favMenu.empty() &&
              !app.menuFor(L"收藏")[0].enabled);

        // Remove
        app.removeFavorite(favGone);
        app.removeFavorite(favC);
        check(L"从收藏夹移除", sameSet(app.settings.favorites, {&favA, &favB}));

        // Move
        app.moveFavorite(0, 1);
        const bool moved = inOrder(app.settings.favorites, {&favB, &favA});
        app.moveFavorite(1, 0);
        app.moveFavorite(0, 7);  // no such place: nothing happens
        check(L"收藏项能调整位置，调到不存在的地方则不动",
              moved && inOrder(app.settings.favorites, {&favA, &favB}));

        // Dropped on the Favorites
        const bool dropped = app.performDrop({favA, favC, base + L"\\item0.txt"}, L"", true);
        check(L"拖到收藏夹上：文件夹加入，已有的和文件不算，文件夹本身不动",
              dropped && sameSet(app.settings.favorites, {&favA, &favB, &favC}) &&
                  fsx::isDirectory(favC));
        check(L"只拖着文件拖到收藏夹上：不算放下",
              !app.performDrop({base + L"\\item0.txt"}, L"", true) &&
                  sameSet(app.settings.favorites, {&favA, &favB, &favC}));

        // The context menus
        const auto has = [](const std::vector<std::wstring>& labels, const wchar_t* text) {
            return std::find(labels.begin(), labels.end(), text) != labels.end();
        };
        shellmenu::ViewState view;
        const auto notYet = shellmenu::labels(base, {favD}, view);
        check(L"文件夹的右键菜单：还没收藏的是“添加到收藏夹”",
              has(notYet, L"添加到收藏夹") && !has(notYet, L"从收藏夹中移除"));
        view.isFavorite = true;
        const auto already = shellmenu::labels(base, {favC}, view);
        check(L"已收藏的文件夹：右键菜单是“从收藏夹中移除”",
              has(already, L"从收藏夹中移除") && !has(already, L"添加到收藏夹") &&
                  !has(already, L"重命名收藏项…"));
        view.favoriteNode = true;
        check(L"树里收藏项那一行：还能“重命名收藏项…”",
              has(shellmenu::labels(base, {favC}, view), L"重命名收藏项…"));

        // Saved and read back
        app.renameFavorite(favB, L"工作");
        app.settings.save();
        Settings again;
        again.load();
        check(L"收藏夹存盘再读回：路径、名字、顺序都不变",
              inOrder(again.favorites, {&favA, &favB, &favC}) && again.favorites[1].name == L"工作");
        // A settings file from before favorites had names of their own.
        const std::wstring ini = settingsPath();
        WritePrivateProfileStringW(L"favorites", nullptr, nullptr, ini.c_str());
        WritePrivateProfileStringW(L"favorites", L"fav0", favA.c_str(), ini.c_str());
        WritePrivateProfileStringW(L"favorites", L"fav1", favB.c_str(), ini.c_str());
        Settings old;
        old.load();
        check(L"旧版 settings.ini（只有路径）照常读入",
              old.favorites.size() == 2 && old.favorites[0].path == favA &&
                  old.favorites[0].name.empty() && old.favorites[1].path == favB);

        // Reordered by dragging in the tree, through the input handlers
        app.loadDirectory(base, false);
        app.treeScroll = 0;
        app.clampScroll();
        std::vector<int> rows = favRows();
        check(L"拖动排序的前提：收藏项在树里按顺序排着", rows.size() == 3 && rows[0] == 1);
        POINT press = center(rows[0]);
        onLButtonDown(nullptr, app, press);
        check(L"按下收藏项那一行：先不跳转", app.treePressRow == rows[0] && app.current == base);
        const RECT rowB = app.treeRowRect(rows[1]);
        POINT onto{press.x, rowB.bottom - 2};  // the lower half of favB's row
        onMouseMove(nullptr, app, onto);
        check(L"拖到另一项的下半格：出现插入线，位置还没变",
              app.favDragging && app.favLine.top == rowB.bottom - 1 &&
                  inOrder(app.settings.favorites, {&favA, &favB, &favC}));
        onLButtonUp(nullptr, app, onto);
        check(L"松开：拖的那项落到插入线的位置",
              inOrder(app.settings.favorites, {&favB, &favA, &favC}) && !app.favDragging);
        // Onto the header: the top.  Esc: nothing moves.
        onLButtonDown(nullptr, app, center(favRows()[2]));
        onMouseMove(nullptr, app, center(groupRow()));
        check(L"拖到表头上：插入线在第一项上面",
              app.favDragging && app.favLine.top == app.treeRowRect(groupRow() + 1).top - 1);
        onKeyDown(nullptr, app, VK_ESCAPE);
        check(L"拖动中按 Esc：位置不变",
              !app.favDragging && inOrder(app.settings.favorites, {&favB, &favA, &favC}));
        // A click: navigate when the button comes back up on the row.
        press = center(favRows()[1]);
        onLButtonDown(nullptr, app, press);
        check(L"按下收藏项：等松开才跳转", app.current == base);
        onLButtonUp(nullptr, app, press);
        check(L"松开在原来那一行：跳到那个文件夹", app.current == favA && app.treePressRow < 0);
        // Released somewhere else: not a click.
        app.loadDirectory(base, false);
        app.treeScroll = 0;
        app.clampScroll();
        onLButtonDown(nullptr, app, center(favRows()[0]));
        onLButtonUp(nullptr, app, POINT{app.lay.listViewport.left + 50, app.lay.listViewport.top + 50});
        check(L"按下后挪到别处松开：不算点击", app.current == base && app.treePressRow < 0);

        // The receiving side of a drag, driven the way Windows drives it.
        app.loadDirectory(base, false);
        app.treeScroll = 0;
        app.listScroll = 0;
        app.clampScroll();
        HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 1100, 760, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);  // never shown; client = screen
        dnd::registerTarget(owner, &app);
        IDropTarget* target = dnd::dropTarget();
        IDataObject* data = dnd::makeData({favD, base + L"\\item0.txt"});
        const RECT group = app.treeRowRect(groupRow());
        const POINTL overGroup{group.left + 40, (group.top + group.bottom) / 2};
        // Windows hands the target the effects the source allows.
        DWORD effect = DROPEFFECT_COPY | DROPEFFECT_MOVE | DROPEFFECT_LINK;
        const HRESULT entered = target ? target->DragEnter(data, 0, overGroup, &effect) : E_FAIL;
        check(L"拖着文件夹经过“收藏夹”那一组：高亮它，光标是链接",
              SUCCEEDED(entered) && app.dragTargetFavorites && app.dragTargetRow == groupRow() &&
                  effect == DROPEFFECT_LINK);
        const RECT listRow = app.listRowRect(app.rowOfPath(favD));
        const POINTL overFolder{listRow.left + 40, (listRow.top + listRow.bottom) / 2};
        effect = 0;
        if (target) target->DragOver(0, overFolder, &effect);
        check(L"拖着文件夹经过列表里的文件夹：是移动或复制，不是收藏",
              !app.dragTargetFavorites && app.dragTargetFolder == favD && effect != DROPEFFECT_LINK &&
                  effect != DROPEFFECT_NONE);
        effect = 0;
        if (target) target->Drop(data, 0, overGroup, &effect);
        check(L"在“收藏夹”上松手：文件夹进了收藏夹，文件没进，什么都没被移动",
              inOrder(app.settings.favorites, {&favB, &favA, &favC, &favD}) &&
                  effect == DROPEFFECT_LINK && fsx::isDirectory(favD));
        check(L"松手后高亮消失", !app.dragTargetFavorites && !app.dragTargetIsTree);
        IDataObject* files = dnd::makeData({base + L"\\item0.txt"});
        effect = 1;
        if (target) target->DragEnter(files, 0, overGroup, &effect);
        check(L"只拖着文件经过“收藏夹”：不能放", effect == DROPEFFECT_NONE);
        if (target) target->DragLeave();
        files->Release();
        data->Release();
        dnd::revokeTarget(owner);
        DestroyWindow(owner);

        app.settings.favorites.clear();
        app.settings.save();
        app.refreshTree();
        app.loadDirectory(base, false);
    }

    // The text prompt returns what was typed.  Its edit box is destroyed with
    // the dialog window, so the text has to be read before that; it used to
    // be read after, and every prompt came back empty.
    {
        WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
        wc.lpfnWndProc = promptTestProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kPromptTestClass;
        RegisterClassExW(&wc);
        HWND owner = CreateWindowExW(0, kPromptTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100,
                                     nullptr, nullptr, wc.hInstance, nullptr);  // never shown
        std::wstring typed = L"(untouched)";
        g_promptKey = VK_RETURN;
        SetTimer(owner, kPromptTestTimer, 200, nullptr);
        const bool accepted = promptText(owner, L"标题", L"提示：", L"初始", &typed);
        check(L"文字输入对话框：按回车，输入的文字原样返回",
              accepted && typed == L"typed in the box");
        std::wstring cancelled = L"(untouched)";
        g_promptKey = VK_ESCAPE;
        SetTimer(owner, kPromptTestTimer, 200, nullptr);
        const bool ok = promptText(owner, L"标题", L"提示：", L"初始", &cancelled);
        check(L"文字输入对话框：按 Esc 是取消", !ok);
        // The whole of "重命名收藏项…": the name typed is the name shown.
        app.settings.favorites.clear();
        const std::wstring favC = base + L"\\favC";
        app.addFavorite(favC);
        app.hwnd = owner;
        g_promptKey = VK_RETURN;
        SetTimer(owner, kPromptTestTimer, 200, nullptr);
        app.promptRenameFavorite(favC);
        app.hwnd = nullptr;
        check(L"“重命名收藏项…”：输入的名字成为收藏项显示的名字",
              app.settings.favorites.size() == 1 &&
                  app.settings.favorites[0].name == L"typed in the box" &&
                  app.treeFlat[1]->title == L"typed in the box");
        app.settings.favorites.clear();
        app.settings.save();
        app.refreshTree();
        DestroyWindow(owner);
    }

    // New Folder / New Text Document, the way Explorer does it: the new item
    // is scrolled into view, selected, and its name is being typed over;
    // Esc leaves it as it was created, Enter renames it to what was typed.
    {
        // A listing long enough that a new folder, which sorts after every
        // ASCII name, starts out below the window.
        for (int i = 0; i < 80; ++i)
            CreateDirectoryW((base + L"\\folder" + std::to_wstring(i)).c_str(), nullptr);
        for (int i = 0; i < 40; ++i) {
            const std::wstring f = base + L"\\file" + std::to_wstring(i) + L".txt";
            HANDLE h = CreateFileW(f.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        app.loadDirectory(base, false);
        app.setViewMode(ViewMode::Details);
        app.relayout(1100, 760);
        app.listScroll = 0;
        app.clampScroll();
        // The edit control needs a parent; never shown.  (A stock class: the
        // rename-field test further on registers a class of its own.)
        HWND owner = CreateWindowExW(0, L"STATIC", L"", WS_OVERLAPPED, 0, 0, 400, 300, nullptr,
                                     nullptr, GetModuleHandleW(nullptr), nullptr);
        app.hwnd = owner;
        const auto wholeInView = [&app](int row) {
            const RECT r = app.mode == ViewMode::Details ? app.listRowRect(row)
                                                         : app.gridCellRect(row);
            const RECT vp = app.mode == ViewMode::Details ? app.lay.listViewport
                                                          : app.lay.gridViewport;
            return row >= 0 && r.top >= vp.top && r.bottom <= vp.bottom && r.left >= vp.left &&
                   r.right <= vp.right;
        };
        const auto onlySelected = [&app](const std::wstring& path) {
            return app.selection.size() == 1 && app.selection.count(path) != 0;
        };

        app.newFolder();
        const std::wstring folder = app.selection.empty() ? L"" : *app.selection.begin();
        check(L"新建文件夹：滚到可见位置、选中、进入改名",
              fsx::isDirectory(folder) && wholeInView(app.renameRow) && app.renaming &&
              app.renameEdit != nullptr && onlySelected(folder));
        SendMessageW(app.renameEdit, WM_KEYDOWN, VK_ESCAPE, 0);
        check(L"改名时按 Esc：退出改名，文件夹保持新建时的名字",
              !app.renaming && app.renameEdit == nullptr && fsx::isDirectory(folder) &&
                  onlySelected(folder));

        // Esc throws away what was typed.
        app.selection.clear();
        app.selection.insert(folder);
        app.renameSelection();
        SendMessageW(app.renameEdit, WM_SETTEXT, 0, (LPARAM)L"typed then cancelled");
        SendMessageW(app.renameEdit, WM_KEYDOWN, VK_ESCAPE, 0);
        check(L"打了一半按 Esc：输入的作废，名字不变",
              !app.renaming && fsx::isDirectory(folder) &&
                  !fsx::exists(base + L"\typed then cancelled"));

        app.selection.clear();
        app.selection.insert(folder);
        app.renameSelection();
        SendMessageW(app.renameEdit, WM_SETTEXT, 0, (LPARAM)L"renamed by the test");
        SendMessageW(app.renameEdit, WM_KEYDOWN, VK_RETURN, 0);
        check(L"改名时按 Enter：按输入的名字改名",
              !app.renaming && fsx::isDirectory(base + L"\\renamed by the test") &&
                  !fsx::exists(folder) &&
                  onlySelected(fsx::joinPath(base, L"renamed by the test")));

        // Icons view, and a file: in view, editing, extension not selected.
        app.setViewMode(ViewMode::Icons);
        app.relayout(1100, 760);
        app.gridScroll = 0;
        app.clampScroll();
        app.newTextFile();
        const std::wstring text = app.selection.empty() ? L"" : *app.selection.begin();
        DWORD selStart = 0, selEnd = 0;
        if (app.renameEdit) SendMessageW(app.renameEdit, EM_GETSEL, (WPARAM)&selStart, (LPARAM)&selEnd);
        const std::wstring textName = fsx::fileName(text);
        const bool nameOnly = selStart == 0 && selEnd == textName.size() - 4;
        check(L"新建文本文档：滚到可见位置、选中、进入改名（扩展名不在改名范围里）",
              fsx::exists(text) && wholeInView(app.renameRow) && app.renaming &&
              onlySelected(text) && nameOnly);
        SendMessageW(app.renameEdit, WM_KEYDOWN, VK_ESCAPE, 0);
        check(L"新文件上按 Esc：退出改名，文件保持新建时的名字",
              !app.renaming && fsx::exists(text) && onlySelected(text));

        app.hwnd = nullptr;
        DestroyWindow(owner);
        app.setViewMode(ViewMode::Details);
        app.relayout(1100, 760);
        app.listScroll = 0;
        app.gridScroll = 0;
        app.clampScroll();
    }

    // The rename field, against what the row really paints.  Every view draws
    // its row into a bitmap twice - selected, and while the name is edited -
    // and the real edit control is read back from the screen into the second
    // one.  The field must sit exactly on the name's selection box, and the
    // name must leave the same ink as before F2, pixel for pixel.
    {
        // An Asian name as well: its glyphs come from a linked font.
        HANDLE rf = CreateFileW((base + L"\\rename.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, 0, nullptr);
        if (rf != INVALID_HANDLE_VALUE) CloseHandle(rf);
        CreateDirectoryW((base + L"\\重命名测试").c_str(), nullptr);
        app.loadDirectory(base, false);  // earlier checks may have navigated away
        WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
        wc.lpfnWndProc = renameTestProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kRenameTestClass;
        RegisterClassExW(&wc);
        // The edit control's parent: a small topmost window, put on screen
        // for each capture and hidden again, its background the "not covered"
        // colour.
        HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kRenameTestClass, L"",
                                     WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, wc.hInstance,
                                     nullptr);
        app.hwnd = owner;
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const POINT spot{work.left + 16, work.top + 16};  // where the edit is shown
        auto editRect = [owner, &app]() {
            RECT e{};
            GetWindowRect(app.renameEdit, &e);
            MapWindowPoints(nullptr, owner, (LPPOINT)&e, 2);
            return e;
        };
        // Shows the edit in the owner near the screen corner and copies what
        // the screen shows there into `into`, at the edit's own place `e`.
        // (Where the edit sits on screen is no part of what it paints.)
        // False when another window covers it: the ring around the edit is
        // then not the owner's colour.
        auto readEdit = [&](RowBitmap& into, const RECT& e) {
            const int w = (int)(e.right - e.left), h = (int)(e.bottom - e.top);
            const int pad = 20;
            for (int attempt = 0; attempt < 5; ++attempt) {
                SetWindowPos(owner, HWND_TOPMOST, spot.x - pad, spot.y - pad, w + 2 * pad,
                             h + 2 * pad, SWP_NOACTIVATE | SWP_SHOWWINDOW);
                SetWindowPos(app.renameEdit, nullptr, pad, pad, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
                HideCaret(app.renameEdit);  // a black bar is not part of the name
                RedrawWindow(owner, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                HDC screen = GetDC(nullptr);
                const bool seen = GetPixel(screen, spot.x - 2, spot.y - 2) == kSeen &&
                                  GetPixel(screen, spot.x + w + 1, spot.y - 2) == kSeen &&
                                  GetPixel(screen, spot.x - 2, spot.y + h + 1) == kSeen &&
                                  GetPixel(screen, spot.x + w + 1, spot.y + h + 1) == kSeen;
                if (seen) into.capture(e, spot);
                ReleaseDC(nullptr, screen);
                if (seen) return true;
                Sleep(40);
            }
            return false;
        };
        auto boxOf = [](const PixelBox& b) {
            return std::to_wstring(b.left) + L"," + std::to_wstring(b.top) + L"-" +
                   std::to_wstring(b.right) + L"," + std::to_wstring(b.bottom);
        };
        auto rectOf = [](const RECT& r) {
            return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L"-" +
                   std::to_wstring(r.right) + L"," + std::to_wstring(r.bottom);
        };
        std::wstring onBox, band, ink, line, kept, outside, icon;  // first failure of each
        auto note = [](std::wstring& into, const std::wstring& what) {
            if (into.empty()) into = what;
        };
        for (double scale : {1.0, 1.5}) {
            app.setScale(scale);
            app.relayout((int)(1100 * scale), (int)(760 * scale));
            int lineHeight = 0;
            {
                HDC dc = CreateCompatibleDC(nullptr);
                Canvas c(dc, scale);
                lineHeight = c.lineHeight();
                DeleteDC(dc);
            }
            const auto px = [scale](double v) { return (int)(v * scale + 0.5); };
            const ViewMode views[] = {ViewMode::Details, ViewMode::List, ViewMode::Icons};
            for (ViewMode view : views) {
                app.setViewMode(view);
                const bool icons = view == ViewMode::Icons;
                // A details or list row is 19 px for an 18 px line at 100%
                // scale: no pixel above and below the name for the frame's
                // top and bottom lines, so they are left out there.
                const bool framed = icons || app.m.rowH - lineHeight >= 2;
                for (const wchar_t* name : {L"rename.txt", L"重命名测试"}) {
                    const std::wstring path = base + L"\\" + name;
                    int row = -1;
                    for (int i = 0; i < (int)app.entries.size(); ++i)
                        if (app.entries[i].path == path) row = i;
                    if (row < 0) {
                        note(onBox, L"missing " + path);
                        continue;
                    }
                    const std::wstring where = std::to_wstring((int)(scale * 100)) + L"% 视图" +
                                              std::to_wstring((int)view) + L" " + name + L"：";
                    app.selection.clear();
                    app.selection.insert(path);

                    RowBitmap a(app, row);
                    a.paint(app, row);
                    const RECT r{a.rect.left + 1, a.rect.top + 1, a.rect.right - 1, a.rect.bottom - 1};
                    // Where the name starts, by the renderer's own numbers;
                    // the icon-view label is the only selection blue there.
                    int nameX = r.left, scanTop = r.top;
                    if (view == ViewMode::Details) nameX = r.left + px(22);
                    else if (view == ViewMode::List) nameX = r.left + px(2) + px(16) + px(4);
                    else scanTop = r.top + px(53);
                    const RECT blueArea{r.left, scanTop, r.right, r.bottom};
                    const PixelBox box = find(a, blueArea, isNavy);  // the selection box
                    const int tw = row < (int)app.textWidths.size() ? app.textWidths[row] : 0;
                    const RECT inkArea = icons ? RECT{box.left, box.top, box.right, box.bottom}
                                               : RECT{nameX - 1, r.top, nameX + tw + 1, r.bottom};
                    // The name as an unselected row paints it: black on white,
                    // the edit's own colours.  (ClearType's fringes depend on
                    // the colours, so the white-on-blue ink of the selected
                    // row is not what the edit can be held to.)
                    app.selection.clear();
                    RowBitmap p(app, row);
                    p.paint(app, row);
                    const PixelBox inkP = find(p, inkArea, blackInk);

                    app.beginRename(row);
                    RowBitmap b(app, row);
                    b.paint(app, row);
                    // 1. The field is the selection box: 1 px of white and the
                    //    frame before the name, the frame right after the box.
                    //    (The scan stops at the box: the other columns' text
                    //    is black too.)
                    const RECT frameArea =
                        icons ? blueArea
                              : RECT{nameX - 3, r.top, std::min((int)r.right, box.right + 2), r.bottom};
                    const PixelBox frame = find(b, frameArea, isBlack);
                    const int wantLeft = icons ? box.left - 1 : nameX - 2;
                    if (!(box.count && frame.count && frame.left == wantLeft &&
                          frame.right == box.right + 1))
                        note(onBox, where + L"选中框 " + boxOf(box) + L" 输入框 " + boxOf(frame) +
                                        L"（应在 " + std::to_wstring(wantLeft) + L"-" +
                                        std::to_wstring(box.right + 1) + L"）");
                    // 2. Top and bottom lines exactly when the row has room,
                    //    and nothing else between the two sides.
                    const RECT e = editRect();
                    if (frame.count) {
                        // From scanTop: above an icon-view label sits the icon,
                        // which has black of its own.
                        const PixelBox inside =
                            find(b, RECT{frame.left + 1, scanTop, frame.right - 1, r.bottom}, isBlack);
                        const int lineW = frame.right - frame.left - 2;
                        const bool right = framed ? inside.count == 2 * lineW &&
                                                     inside.top == frame.top &&
                                                     inside.bottom == frame.bottom
                                                   : inside.count == 0;
                        if (!(right && (frame.top < e.top) == framed))
                            note(band, where + L"框 " + boxOf(frame) + L" 内部黑像素 " +
                                            std::to_wstring(inside.count) + L" 文字顶=" +
                                            std::to_wstring(e.top) + L" 上下边线应为" +
                                            (framed ? L"有" : L"无"));
                    }
                    // 6. Nothing of it is painted over the neighbouring rows:
                    //    the pixel ring around the row stays white.
                    int spilled = 0;
                    for (const RECT& strip :
                         {RECT{r.left - 1, r.top - 1, r.right + 1, r.top},
                          RECT{r.left - 1, r.bottom, r.right + 1, r.bottom + 1},
                          RECT{r.left - 1, r.top, r.left, r.bottom},
                          RECT{r.right, r.top, r.right + 1, r.bottom}})
                        spilled += pixels(b.dc, RECT{strip.left - r.left + 1, strip.top - r.top + 1,
                                                      strip.right - r.left + 1, strip.bottom - r.top + 1},
                                          notWhite).count;
                    if (spilled)
                        note(outside, where + L"行外画了 " + std::to_wstring(spilled) + L" 像素");
                    // 4. The edit fills the frame from side to side and is
                    //    one text line tall, never more.
                    if (!(frame.count && e.left == frame.left + 1 && e.right == frame.right - 1 &&
                          e.bottom - e.top == lineHeight))
                        note(line, where + L"输入框 " + rectOf(e) + L" 高 " +
                                       std::to_wstring((int)(e.bottom - e.top)) + L"，框 " +
                                       boxOf(frame) + L" 一行 " + std::to_wstring(lineHeight));
                    // 5. The icon keeps its selection blue, and only the
                    //    icon: none of it is left under or beside the field.
                    //    An icon-view label is the field now.
                    const PixelBox blue = find(b, blueArea, isNavy);
                    const bool iconKept =
                        icons ? blue.count == 0
                              : blue.count && blue.left == box.left && blue.right == frame.left;
                    if (!iconKept)
                        note(kept, where + L"选中蓝 " + boxOf(blue) + L"（图标到 " +
                                        std::to_wstring(frame.left) + L" 为止）");

                    // 7. The icon is still there: the selection box goes
                    //    under it, so a selected row is not a bare blue box.
                    if (!icons) {
                        const int x = r.left + px(view == ViewMode::Details ? 3 : 2);
                        const RECT iconRect{x, r.top + px(2), x + px(16), r.top + px(2) + px(16)};
                        if (!find(b, iconRect, iconInk).count)
                            note(icon, where + L"选中行的图标被选中框盖住了");
                    }

                    // 3. The name stays on its pixels.  No selection
                    //    highlight: black on white, like the reference.
                    SendMessageW(app.renameEdit, EM_SETSEL, 0, 0);
                    const bool shown = readEdit(b, e);
                    const PixelBox inkB = find(b, e, blackInk);
                    app.endRename(false);
                    if (!shown)
                        note(ink, where + L"测试窗口被别的窗口挡住");
                    else if (!sameBox(inkP, inkB))
                        note(ink, where + L"原名墨迹 " + boxOf(inkP) + L" 输入框 " + rectOf(e) +
                                       L" 内 " + boxOf(inkB));
                }
            }
        }
        ShowWindow(owner, SW_HIDE);
        auto verdict = [&check](const wchar_t* name, const std::wstring& failure) {
            std::wstring label = name;
            if (!failure.empty()) label += L"：" + failure;
            check(label.c_str(), label == name);
        };
        verdict(L"重命名框正好盖住名称的选中框", onBox);
        verdict(L"重命名框的上下边线只在行里有空像素时画", band);
        verdict(L"输入框里的名字和原名逐像素重合", ink);
        verdict(L"输入框填满框内，恰好一行高", line);
        verdict(L"图标的选中高亮保留", kept);
        verdict(L"重命名框不画到自己那一行以外", outside);
        verdict(L"选中行的图标还在", icon);

        // Scrolling moves the field with its row, and clips it like the row.
        app.setScale(1.0);
        app.relayout(1100, 300);  // a few rows fit
        app.setViewMode(ViewMode::Details);
        auto regionType = [&app]() {
            HRGN rgn = CreateRectRgn(0, 0, 0, 0);
            const int type = GetWindowRgn(app.renameEdit, rgn);
            DeleteObject(rgn);
            return type;
        };
        app.beginRename(2);
        const RECT before = editRect();
        app.scrollListBy(app.m.rowH * 2);  // the row is cut at the top of the list
        const RECT cut = editRect();
        const int cutType = regionType();
        app.scrollListBy(app.m.rowH * 2);  // and gone
        const int goneType = regionType();
        app.scrollListBy(-app.m.rowH * 4);
        const RECT back = editRect();
        const int backType = regionType();
        app.endRename(false);
        const bool follows = before.top - cut.top == 2 * app.m.rowH;
        const bool clips = cutType == SIMPLEREGION && goneType == NULLREGION &&
                           backType == SIMPLEREGION && EqualRect(&back, &before);
        const wchar_t* scrollName = L"重命名框跟着行滚动，行滚出列表它就不见，滚回来复原";
        const std::wstring scrollLabel =
            follows && clips
                ? std::wstring(scrollName)
                : scrollName + std::wstring(L"：位移 ") +
                      std::to_wstring(before.top - cut.top) + L" 区域 " +
                      std::to_wstring(cutType) + L"/" + std::to_wstring(goneType) + L"/" +
                      std::to_wstring(backType) + L" 复位=" +
                      std::to_wstring(EqualRect(&back, &before));
        check(scrollLabel.c_str(), follows && clips);

        app.hwnd = nullptr;
        DestroyWindow(owner);
    }

    // Paint benchmark: catches accidental full-window repaint storms.
    app.setViewMode(ViewMode::Details);
    app.relayout(1100, 760);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, app.width, app.height);
    HGDIOBJ old = SelectObject(mem, bmp);
    const ULONGLONG t0 = GetTickCount64();
    for (int i = 0; i < 20; ++i) {
        Canvas c(mem, app.scale);
        renderApp(c, app);
    }
    const ULONGLONG frameMs = GetTickCount64() - t0;
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    std::wstring frameLabel = L"渲染20帧 " + std::to_wstring(frameMs) + L"ms";
    check(frameLabel.c_str(), frameMs < 2000);
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
        app.rebuildNameWidths();
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
        // The tests start from default settings and save to a scratch file:
        // whatever they do must never reach the user's settings.ini.
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        const std::wstring scratch = std::wstring(temp) + L"moc-uitest-settings.ini";
        DeleteFileW(scratch.c_str());
        SetEnvironmentVariableW(L"MOC_SETTINGS_FILE", scratch.c_str());
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
    if (x >= 0 && y >= 0 && !MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONULL)) {
        x = y = -1;  // saved position is no longer on any monitor (unplugged display)
    }
    if (x < 0 || y < 0) {
        x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
    }

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"MyOldComputerWindow", L"My Old Computer",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, w, h, nullptr, nullptr,
                                instance, nullptr);
    if (!hwnd) return 1;

    g_app.hwnd = hwnd;
    g_app.scale = dpiScaleOfWindow(hwnd);
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
