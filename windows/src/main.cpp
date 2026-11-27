#include <windows.h>
#include <windowsx.h>

#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <winreg.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "app.h"
#include "canvas.h"
#include "debug.h"
#include "dialog.h"
#include "dnd.h"
#include "folderlist.h"
#include "foldercolours.h"
#include "fs.h"
#include "hmmenu.h"
#include "ids.h"
#include "options.h"
#include "preview.h"
#include "render.h"
#include "scrollbar.h"
#include "settings.h"
#include "shellmenu.h"
#include "shellicons.h"
#include "snapshot.h"
#include "theme.h"
#include "thumbs.h"
#include "takeover.h"

int measureUiText(const std::wstring& s, int fontPx);  // the UI font, as the rows are measured with

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
    rect("addrField", a.lay.addrField);
    rect("addrMore", a.lay.addrMore);
    rect("addrBlank", a.lay.addrBlank);
    // The address bar's buttons, with the folder each one goes to: a picture
    // of the bar is held against this.
    fwprintf(f, L"crumbs %d first %d\n", (int)a.crumbs.size(), a.crumbFirst);
    for (int i = 0; i < (int)a.crumbs.size(); ++i) {
        const Crumb& c = a.crumbs[i];
        fwprintf(f, L"crumb %d %d,%d %dx%d more %d,%d %dx%d path %ls title %ls\n", i,
                 (int)c.box.left, (int)c.box.top, (int)(c.box.right - c.box.left),
                 (int)(c.box.bottom - c.box.top), (int)c.more.left, (int)c.more.top,
                 (int)(c.more.right - c.more.left), (int)(c.more.bottom - c.more.top),
                 c.path.c_str(), c.title.c_str());
        if (c.text != c.title)  // cut with an ellipsis, or the machine as an icon alone
            fwprintf(f, L"crumbText %d [%ls]\n", i, c.text.c_str());
    }
    // The list that is up under one of them, if any: where it is on the
    // screen, what is in view, and every row it holds.
    if (HWND list = folderListWindow(); list && folderListRows(list)) {
        FolderListInfo info;
        RECT wr{};
        GetWindowRect(list, &wr);
        if (folderListInfo(list, &info)) {
            fwprintf(f, L"crumbList %d,%d %dx%d first %d visible %d hot %d rowH %d rows %d\n",
                     (int)wr.left, (int)wr.top, (int)(wr.right - wr.left),
                     (int)(wr.bottom - wr.top), info.first, info.visible, info.highlighted,
                     info.rowH, (int)folderListRows(list)->size());
            fwprintf(f, L"crumbListRows %d,%d %dx%d\n", (int)info.rows.left, (int)info.rows.top,
                     (int)(info.rows.right - info.rows.left), (int)(info.rows.bottom - info.rows.top));
            const auto& rows = *folderListRows(list);
            for (int i = 0; i < (int)rows.size() && i < 4000; ++i)
                fwprintf(f, L"listRow %d %ls path %ls\n", i, rows[i].title.c_str(),
                         rows[i].path.c_str());
        }
    }
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
    // Every row with its +/- and whether that was ever asked for, to hold a
    // picture of the tree against the disk.  Read, not asked: what is written
    // here is what a picture taken at this moment was painted from.
    for (int i = 0; i < (int)a.treeFlat.size(); ++i) {
        const TreeNode* n = a.treeFlat[i];
        fwprintf(f, L"treePlus %d %d %d depth %d path %ls\n", i, n->hasChildren ? 1 : 0,
                 n->hasChildrenKnown ? 1 : 0, n->depth, n->path.c_str());
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
// Ctrl and a press on a folder in the list: the folder opens in a new window
// when the button comes back up on it (onLButtonUp) - not if it was dragged.
bool g_pressNewWindow = false;

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

// The cells in view (the big-icon view) whose thumbnail is in - each with
// whether it is drawn with it in the window's frame.
std::vector<int> thumbsInView(App& a, bool drawn) {
    std::vector<int> out;
    if (!a.thumbnails || a.mode != ViewMode::Icons) return out;
    const int cellH = std::max(1, a.gridCellH()), cols = a.gridCols();
    const RECT vp = a.lay.gridViewport;
    const int first = std::min((int)a.entries.size(), (a.gridScroll / cellH) * cols);
    const int past = std::min((int)a.entries.size(),
                              ((a.gridScroll + (int)(vp.bottom - vp.top)) / cellH + 1) * cols);
    const int box = a.iconBox();
    for (int i = first; i < past; ++i) {
        if (a.entries[i].isDir || i >= (int)a.thumbDrawn.size()) continue;
        if (a.thumbDrawn[i] != drawn) continue;
        if (thumbs::peek(a.entries[i].path, box)) out.push_back(i);
    }
    return out;
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
            const double t0 = diagMillis();
            Canvas canvas(g_paint.mem, g_app.scale);
            // Which thumbnails this frame draws it says itself (a thumbnail
            // can come in while the frame is being drawn).
            g_app.thumbRendered.clear();
            renderApp(canvas, g_app);
            g_app.paintDirty = false;
            for (int i : g_app.thumbRendered)
                if (i < (int)g_app.thumbDrawn.size()) g_app.thumbDrawn[i] = true;
            diagLog(L"frame rendered in %.1f ms (%d rows)", diagMillis() - t0,
                    (int)g_app.entries.size());
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

// The list under an address bar button: the subfolders of the crumb it follows
// (crumb), or the crumbs the ... stands for (crumb = -1).  The machine's
// button opens the first kind of list too - it has no folder to go to, only
// its drives.  The row that is chosen is gone to.  A press on another of the
// bar's arrows while the list is up opens that one's list in its place, as it
// does between menu titles.
void showCrumbList(HWND hwnd, App& a, int crumb, bool onButton = false) {
    for (;;) {
        if (crumb >= (int)a.crumbs.size()) return;
        const bool hidden = crumb < 0;
        const RECT anchor = hidden ? a.lay.addrMore
                                   : onButton ? a.crumbs[crumb].box : a.crumbs[crumb].more;
        std::vector<FolderRow> rows;
        int current = -1;
        if (hidden) rows = a.hiddenCrumbRows();
        else rows = a.crumbRows(crumb, &current);
        if (rows.empty()) {
            MessageBeep(MB_ICONASTERISK);  // nothing to list (a folder with no subfolder in view)
            return;
        }
        // The arrow stays pressed while its list is up, as an open menu title does.
        if (hidden) a.pressAddrMore = true;
        else a.pressCrumbMore = crumb;
        repaintRegion(hwnd, a, a.lay.addrField);
        if (hwnd) UpdateWindow(hwnd);
        RECT screen = anchor;
        MapWindowPoints(hwnd, nullptr, (POINT*)&screen, 2);
        FolderListEnd end;
        const int chosen = showFolderList(hwnd, rows, current, screen, a.useSystemIcons, &end);
        a.pressAddrMore = false;
        a.pressCrumbMore = -1;
        if (chosen >= 0 && chosen < (int)rows.size() && !rows[chosen].path.empty()) {
            a.loadDirectory(rows[chosen].path);
            break;
        }
        onButton = false;  // only the machine's own button opens a list
        if (end.pressedElsewhere) {
            ScreenToClient(hwnd, &end.press);
            int other = -1;
            const AddressHit hit = a.addressAt(end.press, &other);
            if (hit == AddressHit::CrumbMore) {
                crumb = other;
                continue;
            }
            if (hit == AddressHit::More) {
                crumb = -1;
                continue;
            }
        }
        break;
    }
    repaint(hwnd);
}

bool g_menuPending = false;
POINT g_menuPendingPoint{};

// ------------------------------------------- the shell's menu, built ahead of the click
//
// The shell and its extensions take a good part of a second to build a menu.
// It is built on a thread of this program's own, before the right-click
// (shellmenu.h), and the right-click shows it.  This is what a right-click
// is for, from the click to the menu and from the menu to the shell command
// picked in it.
struct MenuFor {
    shellmenu::Request what;     // the things - none: the folder's background
    shellmenu::ViewState state;  // the view as it was, for this program's own items
    bool fromTree = false;
    POINT client{};              // where it was clicked
    POINT screen{};              // where its menu goes
    ULONGLONG serial = 0;        // what the menu thread was asked under (0: nothing pending)
    DWORD tick = 0;              // what the user had done by then (g_inputTick)
};
MenuFor g_menuWait;    // asked for, not up yet
MenuFor g_menuShown;   // the one just closed with a shell command picked in it
MenuFor g_menuOpen;    // the one on screen (while showHMenu runs)
// What is on screen of the menu that is up: the menu, and of each of the
// shell's items in it where it lay and what it said (a command picked is
// looked up by that in the menu built for the things themselves).  It can
// change while the menu is up, when the things' own menu comes in and its
// rows are made to say that (menuRestate).
struct MenuDisplay {
    HMENU menu = nullptr;
    std::vector<shellmenu::Where> wheres;
    bool model = false;  // it stands for the things: it was built for another of their kind
};
MenuDisplay* g_menuDisplay = nullptr;  // the open menu's, while showHMenu runs
ULONGLONG g_menuSerial = 0;
bool g_menuShownModel = false;  // the menu shown stood for another of the kind (the tests ask)
ULONGLONG g_menuOpenEntry = 0;  // ... which one (a menu built); else the things' own
ULONGLONG g_menuReplaceWith = 0;  // the things' own menu is in and has other rows than the stand-in: put up anew
ULONGLONG g_menuRestatePending = 0;  // ... and is to be put on the rows that are there, once no submenu is open
constexpr UINT_PTR kMenuRestateTimer = 0x4D4D;
int g_menuSyncDepth = 0;        // a menu is being built and shown on the window's thread, the old way
DWORD g_inputTick = 0;  // up by one on every click, key and scroll: what was waited for is not wanted after
// A menu built longer ago than this is built again as it is shown - shown all
// the same, at once - and what is on screen is made to say the new one's say
// when that is in.  (The tests set it.)
ULONGLONG g_menuAgainMs = shellmenu::kAgainMs;
// The pointer rests this long on something before its menu is built.
constexpr UINT kHoverMenuMs = 70;
constexpr UINT_PTR kHoverMenuTimer = 0x4D4F;
// More things selected than this: their menu is not built ahead (it is a slow one to build, and rarely wanted).
constexpr size_t kMenuMostPaths = 16;
int g_hoverMenuRow = -1;  // the entry the pointer is resting on, if any
bool menusAhead(const App& a);

void cancelHover(HWND hwnd) {
    if (g_hoverMenuRow == -1) return;
    g_hoverMenuRow = -1;
    KillTimer(hwnd, kHoverMenuTimer);
}

// The pointer came to rest on something a right-click would be on: after a
// moment there, the menu of it - or of the selection it is in - is built.
void noteHover(HWND hwnd, App& a, POINT p) {
    int row = -1;
    const bool onEntry = menusAhead(a) && !menuIsOpen() && !a.splitterDrag && !a.scrollDrag &&
                         !a.marquee && !a.dragging && !a.dragArmed && !a.renaming &&
                         a.hitTestItem(p, &row);
    if (!onEntry) {
        cancelHover(hwnd);
        return;
    }
    if (row == g_hoverMenuRow) return;
    g_hoverMenuRow = row;
    SetTimer(hwnd, kHoverMenuTimer, kHoverMenuMs, nullptr);
}

// The menus a right-click in the folder is most likely to want, built while
// nobody waits for one: the folder's background, a folder in it, and a file of
// each of the four commonest kinds in it.  What was asked for for the folder
// shown before is not wanted any more.
void prefetchMenus(const App& a) {
    if (!menusAhead(a)) return;
    shellmenu::newBatch();
    shellmenu::prefetch(shellmenu::Request{a.current, {}, a.hiddenFor({})});
    std::wstring folder;
    struct Kind {
        size_t count = 0;
        std::wstring first;
    };
    std::map<std::wstring, Kind> kinds;
    for (const auto& e : a.entries) {
        if (e.isDir) {
            if (folder.empty()) folder = e.path;
            continue;
        }
        Kind& kind = kinds[fsx::extensionLower(e.name)];
        if (kind.count++ == 0) kind.first = e.path;
    }
    if (!folder.empty())
        shellmenu::prefetch(shellmenu::Request{a.current, {folder}, a.hiddenFor({folder})});
    std::vector<std::pair<size_t, std::wstring>> common;
    for (const auto& [ext, kind] : kinds) common.emplace_back(kind.count, kind.first);
    std::sort(common.begin(), common.end(), [](const auto& l, const auto& r) {
        return l.first != r.first ? l.first > r.first : l.second < r.second;
    });
    for (int i = 0; i < 4 && i < (int)common.size(); ++i) {
        const std::vector<std::wstring> paths{common[i].second};
        shellmenu::prefetch(shellmenu::Request{a.current, paths, a.hiddenFor(paths)});
    }
}

void contextMenuOver(HWND hwnd, App& a, bool fromTree);
void applyMenuChoice(HWND hwnd, App& a, const std::vector<std::wstring>& paths,
                     const shellmenu::Result& result);
void showContextMenu(HWND hwnd, App& a, POINT p);

bool menusAhead(const App& a) {
    return (a.settings.menuMask & kMenuShell) && (a.settings.menuMask & kMenuAhead) &&
           shellmenu::running();
}

// The menu of a right-click is over - shown and closed, or not shown after all.
void contextMenuOver(HWND hwnd, App& a, bool fromTree) {
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

// Whether what was right-clicked is still what a click there would be on:
// the same things in the list are selected, or the tree's same row is there.
bool targetStillThere(const App& a, const MenuFor& m) {
    if (m.fromTree) {
        const int row = a.treeRowAt(m.client);
        if (row < 0 || row >= (int)a.treeFlat.size() || m.what.paths.size() != 1) return false;
        return _wcsicmp(a.treeFlat[row]->path.c_str(), m.what.paths.front().c_str()) == 0;
    }
    if (_wcsicmp(a.current.c_str(), m.what.folder.c_str()) != 0) return false;
    if (m.what.paths.empty()) return !a.hitTestItem(m.client, nullptr);
    int row = -1;
    if (!a.hitTestItem(m.client, &row)) return false;
    if (a.selection.size() != m.what.paths.size()) return false;
    for (const std::wstring& path : m.what.paths)
        if (!a.selection.count(path)) return false;
    return a.selection.count(a.entries[row].path) != 0;
}

// The menu built and shown on the window's own thread, as it was before
// menus were built ahead - and the way when that is switched off.
void showMenuNow(HWND hwnd, App& a, const std::wstring& folder,
                 const std::vector<std::wstring>& paths, POINT screen,
                 const shellmenu::ViewState& state, bool fromTree) {
    shellmenu::Result result;
    if (a.settings.menuMask & kMenuShell) {
        ++g_menuSyncDepth;
        result = shellmenu::show(hwnd, folder, paths, screen, state);
        --g_menuSyncDepth;
    }

    if (!result.shown) {
        // The shell could not build a menu (unusual path): fall back to the
        // hand-drawn one so the window always has a context menu.
        std::vector<MenuItem> items = a.contextMenu();
        int id = showPopupMenu(hwnd, items, screen.x, screen.y, false);
        if (id) a.performMenuAction(id);
    } else {
        applyMenuChoice(hwnd, a, paths, result);
    }
    contextMenuOver(hwnd, a, fromTree);
}

// The menu on screen is made to say what the menu built as g_menuOpenEntry
// says now - the same rows along the top, saying what that says where they
// differ, and with its submenus as they are now (they are filled in further a
// moment after the menu was built).  The menu stays up as it is, put up once;
// it is not done while a submenu is open (that one shows what it showed).
// False when there was nothing to do.
bool restateOpenMenu() {
    if (!g_menuDisplay || !menuIsOpen() || menuSubmenuUp()) return false;
    std::vector<shellmenu::Item> items;
    if (!shellmenu::itemsOf(g_menuOpenEntry, &items) || items.empty()) return false;
    MenuDisplay restated;
    restated.model = g_menuDisplay->model;
    restated.menu = shellmenu::materialize(items, restated.model, &restated.wheres);
    shellmenu::addOwnItems(restated.menu, g_menuOpen.what.paths, g_menuOpen.state);
    // What is picked from now on lies where the menu has it, and says what it says there.
    if (HMENU was = menuRestate(restated.menu)) {
        DestroyMenu(was);  // the one that was on screen: not any more
        g_menuDisplay->menu = restated.menu;
        g_menuDisplay->wheres.swap(restated.wheres);
        return true;
    }
    DestroyMenu(restated.menu);
    return false;
}

// The menu on screen is made to say what the menu built as `entry` says: from
// then on that is what is on screen.  Its submenus are filled in once more a
// moment after it was built: what they say then is to be on screen too.
void restateOpenMenuTo(ULONGLONG entry) {
    const ULONGLONG shownId = g_menuOpenEntry;
    g_menuOpenEntry = entry;
    if (!restateOpenMenu()) g_menuOpenEntry = shownId;
    if (g_menuOpenEntry == entry) shellmenu::showing(entry);
}

// ... not while a submenu is open: its rows are the old menu's, and would be
// taken from under the pointer.  It is done as soon as none is (kMenuRestateTimer).
void restateWhenQuiet(HWND hwnd, ULONGLONG entry) {
    if (menuSubmenuUp()) {
        g_menuRestatePending = entry;
        SetTimer(hwnd, kMenuRestateTimer, 30, nullptr);
        return;
    }
    restateOpenMenuTo(entry);
}

// Shows the menu built as `entry` says: a copy of the shell's part of it, with
// this program's own items for the things as they are now.  The copy was
// built for the very things or (`model`) for another of their kind: a
// stand-in until the menu of these is in.  That is asked for here - and the
// thing's own menu, built a while ago (`again`), is asked for again - and
// when it is in it is what is on screen from then on: the rows are made to
// say its say where they differ (kMenuReadyMessage: a few items are the
// file's own - an archive's name, say), the menu staying up as it is.  A
// command picked meanwhile is run from the thing's own menu all the same.
void showBuiltMenu(HWND hwnd, App& a, const MenuFor& m, ULONGLONG entry, bool model, bool again) {
    std::vector<shellmenu::Item> items;
    if (!shellmenu::itemsOf(entry, &items) || items.empty()) {
        showMenuNow(hwnd, a, m.what.folder, m.what.paths, m.screen, m.state, m.fromTree);
        return;
    }
    MenuDisplay display;
    display.model = model;
    display.menu = shellmenu::materialize(items, model, &display.wheres);
    shellmenu::addOwnItems(display.menu, m.what.paths, m.state);

    g_menuShownModel = model;
    g_menuOpen = m;
    g_menuOpenEntry = entry;
    g_menuOpen.serial = 0;
    g_menuRestatePending = 0;  // what was to be put on the menu before is not on this one
    KillTimer(hwnd, kMenuRestateTimer);
    if (model || again) {
        g_menuOpen.serial = ++g_menuSerial;
        shellmenu::ask(m.what, hwnd, g_menuOpen.serial, g_menuAgainMs);
    }
    g_menuDisplay = &display;
    shellmenu::showing(entry);  // says something new: this menu is to say it too
    const int chosen = showHMenu(hwnd, display.menu, m.screen);
    shellmenu::showing(0);
    g_menuDisplay = nullptr;
    g_menuOpen.serial = 0;
    // (What is on screen after a menuRestate is the restated one.)
    DestroyMenu(display.menu);
    display.menu = nullptr;
    if (chosen == 0 && g_menuReplaceWith) {
        // The stand-in left the screen for the menu of the things themselves:
        // that one has other rows.
        const ULONGLONG own = g_menuReplaceWith;
        g_menuReplaceWith = 0;
        showBuiltMenu(hwnd, a, m, own, false, false);
        return;
    }
    g_menuReplaceWith = 0;

    const shellmenu::Result picked = shellmenu::decodeChoice(chosen);
    if (picked.command != shellmenu::Command::None) {
        applyMenuChoice(hwnd, a, m.what.paths, picked);
    } else if (chosen >= 1 && chosen < (int)display.wheres.size() + 1 &&
               !display.wheres[chosen - 1].text.empty()) {
        // A command of the shell's.  It is run on the thread that builds the
        // menus, from the menu built for the things themselves, which is
        // looked the item up in by where it lay and what it said;
        // kMenuDoneMessage says how that went.
        MenuFor& shown = g_menuShown;
        shown = MenuFor{};
        shown.what = m.what;
        shown.state = m.state;
        shown.fromTree = m.fromTree;
        shown.screen = m.screen;
        shown.client = m.screen;
        ScreenToClient(hwnd, &shown.client);
        shown.tick = g_inputTick;
        shown.serial = ++g_menuSerial;
        shellmenu::invoke(m.what, display.wheres[chosen - 1].path,
                          display.wheres[chosen - 1].text, hwnd, shown.serial);
    }
    contextMenuOver(hwnd, a, m.fromTree);
}

// The menu of a right-click, from what was built ahead - at once, from
// whatever there is.  From the menu built for the very things: one built a
// while ago is asked for again, and what is on screen is made to say the new
// one's say when it is in (kMenuReadyMessage).  For what has none of its own,
// from the menu of another of their kind until that one is in (which is asked
// for then: a command picked in it is run from the menu of the things
// themselves).  With nothing to show it from, it is asked for and false comes
// back: kMenuReadyMessage brings it - `askedAndDone` then says that moment
// came, and there is still nothing.
bool showMenuAhead(HWND hwnd, App& a, const MenuFor& m, bool askedAndDone) {
    const shellmenu::Request& what = m.what;
    const bool background = what.paths.empty();
    const shellmenu::Found own = shellmenu::find(what);
    ULONGLONG source = 0;
    bool model = false, again = false;
    if (own.entry && own.ageMs < shellmenu::kModelAgeMs) {
        // Its own menu, as it was last built - never waited for.
        source = own.entry;
        again = own.ageMs >= g_menuAgainMs;
    } else if (!background) {
        // Not built yet: the menu of one of its kind stands for it, until its own is in.
        const shellmenu::Found alike = shellmenu::findKind(shellmenu::kindOf(what));
        if (alike.entry && alike.ageMs < shellmenu::kModelAgeMs) {
            source = alike.entry;
            model = true;
        }
    }
    if (askedAndDone) {
        // The menu asked for is what was found - or the shell could not build it.
        if (!source) return false;
        showBuiltMenu(hwnd, a, m, source, model, false);
        return true;
    }
    diagLog(L"menu ahead: own %llu (%d ms), source %llu model %d again %d", own.entry, own.ageMs, source,
            (int)model, (int)again);
    if (!source) {
        // Nothing to show it from yet: asked for, and shown when it is in
        // (kMenuReadyMessage) - unless anything is done until then.
        g_menuWait = m;
        g_menuWait.serial = ++g_menuSerial;
        g_menuWait.tick = g_inputTick;
        shellmenu::ask(what, hwnd, g_menuWait.serial, shellmenu::kAgainMs);
        return false;
    }
    showBuiltMenu(hwnd, a, m, source, model, again);
    return true;
}

// 文件夹颜色: the folders the menu was for - not a file among them, not a
// drive - are shown in that colour from now on: in this window, in every
// other one of this program, and after the next start.
void colourFolders(const std::vector<std::wstring>& paths, int colour) {
    std::vector<std::wstring> folders;
    for (const std::wstring& path : paths)
        if (fsx::isDirectory(path) && !fsx::isRoot(path)) folders.push_back(path);
    foldercolours::set(folders, colour);
}

// What the context menu asked for is done: one of our own items (the
// shell ran its own verb itself) - a folder opened in a new window, a view
// chosen, a colour given...
void applyMenuChoice(HWND hwnd, App& a, const std::vector<std::wstring>& paths,
                      const shellmenu::Result& result) {
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
    case shellmenu::Command::FolderColour: colourFolders(paths, result.colour); break;
    case shellmenu::Command::Settings: a.openMenuSettings(); break;
    case shellmenu::Command::None: break;
    }
    if (result.invokedShell) a.refreshAfterChange();
}

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
                ownPopup({{n->expanded ? L"折叠" : L"展开", ID_TREE_TOGGLE,
                           a.treeRowHasChildren(*n)},
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
                ownPopup({{n->expanded ? L"折叠" : L"展开", ID_TREE_TOGGLE,
                           a.treeRowHasChildren(*n)},
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
    // Another window may have coloured the folder since this one last looked.
    foldercolours::reload();
    state.folderColour = paths.size() == 1 && fsx::isDirectory(paths.front()) &&
                                 !fsx::isRoot(paths.front())
                             ? foldercolours::of(paths.front())
                             : foldercolours::kDefault;
    if ((a.settings.menuMask & kMenuShell) && menusAhead(a)) {
        // Built ahead on a thread of its own: shown now from what is there, or
        // when what is asked for is in (the window is free until then).
        MenuFor menu;
        menu.what = shellmenu::Request{folder, paths, state.hidden};
        menu.state = state;
        menu.fromTree = fromTree;
        menu.client = p;
        menu.screen = screen;
        showMenuAhead(hwnd, a, menu, false);
        return;
    }
    showMenuNow(hwnd, a, folder, paths, screen, state, fromTree);
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
    // Read before the capture goes back (cancelTreePress), as the rest is.
    const bool inNewWindow = g_pressNewWindow;
    g_pressNewWindow = false;
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
    if (inNewWindow) {
        // Ctrl and a click on the row: the folder opens in a new window, as it
        // does from the list; this window stays where it is.
        a.openPathInNewWindow(path);
    } else {
        // Tree clicks only select and update the right pane; expanding and
        // scrolling here made the tree jump under the pointer.
        a.treeClickRow = row;
        a.loadDirectory(path, true, true);
    }
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
    // The address bar's buttons light up under the pointer.
    int hoverCrumb = -1, hoverCrumbMore = -1;
    bool hoverAddrMore = false;
    if (!a.addrEditing && PtInRect(&a.lay.addrField, p)) {
        int crumb = -1;
        const AddressHit hit = a.addressAt(p, &crumb);
        hoverCrumb = hit == AddressHit::Crumb ? crumb : -1;
        hoverCrumbMore = hit == AddressHit::CrumbMore ? crumb : -1;
        hoverAddrMore = hit == AddressHit::More;
    }
    if (hoverCrumb != a.hoverCrumb || hoverCrumbMore != a.hoverCrumbMore ||
        hoverAddrMore != a.hoverAddrMore) {
        a.hoverCrumb = hoverCrumb;
        a.hoverCrumbMore = hoverCrumbMore;
        a.hoverAddrMore = hoverAddrMore;
        repaintRegion(hwnd, a, a.lay.addrField);
    }
}

// The button went down.  `ctrl` is whether it was down with Ctrl held (the
// window procedure reads the keyboard; the tests pass their own).
void onLButtonDown(HWND hwnd, App& a, POINT p, bool ctrl) {
    SetFocus(hwnd);
    a.pressTitle = -1;
    a.pressTool = -1;
    a.pressSelectedRow = -1;
    g_pressNewWindow = false;

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
        // Every folder on the way to the current one is a button, the v after
        // it opens that folder's subfolders, and the blank part of the field
        // turns the whole path into an edit box.
        int crumb = -1;
        const AddressHit hit = a.addrEditing ? AddressHit::Blank : a.addressAt(p, &crumb);
        if (hit == AddressHit::CrumbMore || hit == AddressHit::More) {
            showCrumbList(hwnd, a, hit == AddressHit::More ? -1 : crumb);
        } else if (hit == AddressHit::Crumb) {
            // Pressed, not gone to yet: that happens when the button comes
            // back up on it, like the toolbar's buttons.
            a.pressCrumb = crumb;
            SetCapture(hwnd);
            repaintRegion(hwnd, a, a.lay.addrField);
        } else {
            a.beginAddressEdit();
        }
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
            } else if (a.treeRowHasChildren(*n) && PtInRect(&boxHit, p)) {
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
                // With Ctrl down, the click is to open the folder in a new
                // window (treePressRelease), not to go there in this one.
                g_pressNewWindow = ctrl;
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
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const std::wstring& path = a.entries[itemIndex].path;
        const std::set<std::wstring> selBefore = a.selection;
        if (ctrl && a.entries[itemIndex].isDir) {
            // Ctrl and a click on a folder opens it in a new window, as asked
            // for - the click itself goes with it: the folder is selected as a
            // plain click selects it, and it can still be dragged from here.
            g_pressNewWindow = true;
            selectOnly(a, itemIndex);
            a.selAnchor = itemIndex;
        } else if (ctrl) {
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

void onLButtonDown(HWND hwnd, App& a, POINT p) {
    onLButtonDown(hwnd, a, p, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
}

void onLButtonUp(HWND hwnd, App& a, POINT p) {
    // Before the capture goes back: what the press was is still needed.
    if (a.treePressRow >= 0) {
        treePressRelease(hwnd, a, p);
        return;
    }
    // Ctrl and a click on a folder in the list, let go on it with nothing
    // dragged: it opens in a new window.  Asked here, before the capture goes
    // back: that ends the press as far as the window is concerned.
    if (a.dragArmed && !a.dragging && g_pressNewWindow) {
        int under = -1;
        if (fsx::isDirectory(a.dragPath) && a.hitTestItem(p, &under) && under >= 0 &&
            under < (int)a.entries.size() && a.entries[under].path == a.dragPath)
            a.openPathInNewWindow(a.dragPath);
        g_pressNewWindow = false;
    }
    if (a.pressCrumb >= 0) {
        // An address bar button comes up: gone to when it comes up on itself.
        const int i = a.pressCrumb;
        const bool onIt = i < (int)a.crumbs.size() && PtInRect(&a.crumbs[i].box, p);
        if (onIt && a.crumbs[i].path.empty()) {
            // The machine's button has nowhere to go: its list of drives
            // opens instead, and the button stays pressed while it is up.
            showCrumbList(hwnd, a, i, true);  // the list takes the mouse
            a.pressCrumb = -1;
            repaint(hwnd);
            return;
        }
        a.pressCrumb = -1;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (onIt) a.loadDirectory(a.crumbs[i].path);
        repaint(hwnd);
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
void onDoubleClick(HWND hwnd, App& a, POINT p, bool ctrl) {
    if (a.sidebarVisible && PtInRect(&a.lay.sidebar, p)) {
        int row = a.treeRowAt(p);
        diagLog(L"DBLCLK tree %d,%d row=%d", p.x, p.y, row);
        if (row >= 0) {
            TreeNode* n = a.treeFlat[row];
            if (a.treeRowHasChildren(*n)) a.toggleTreeNode(*n);
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
        // Ctrl and a double click on a folder: the first click of it already
        // opened the folder in a new window (onLButtonUp); this one opens
        // nothing.  On a file Ctrl changes nothing.
        if (ctrl && a.entries[idx].isDir) return;
        a.openEntry(a.entries[idx]);
        repaint(hwnd);
        return;
    }
    // The address bar's buttons act on the first click of a double one.  After
    // it the crumbs are laid out anew (the way got shorter), and the second
    // click would land on whatever is in that place now.
    if (PtInRect(&a.lay.addrField, p)) return;
    onLButtonDown(hwnd, a, p);
}

void onDoubleClick(HWND hwnd, App& a, POINT p) {
    onDoubleClick(hwnd, a, p, (GetKeyState(VK_CONTROL) & 0x8000) != 0);
}

// The key with the modifiers it was pressed with (onKeyDown reads them from
// the keyboard; the tests pass their own).
void handleKey(HWND hwnd, App& a, WPARAM key, bool ctrl, bool shift, bool alt) {
    (void)shift;
    switch (key) {
    case VK_RETURN: {
        // Ctrl and Enter on one folder: it opens in a new window, as in
        // Explorer.  On anything else Ctrl changes nothing.
        const bool oneFolder =
            a.selection.size() == 1 && fsx::isDirectory(*a.selection.begin());
        if (ctrl && oneFolder) a.openInNewWindow();
        else a.openSelection();
        break;
    }
    case VK_BACK: a.goUp(); break;
    case VK_DELETE: a.deleteSelection(); break;
    case VK_F2: a.renameSelection(); break;
    case VK_F5: a.reload(); break;
    // Explorer's keys for the address bar: the whole path, selected, to type over.
    case VK_F4:
        if (!a.addrEditing) a.beginAddressEdit();
        break;
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
    case 'L': if (ctrl && !a.addrEditing) a.beginAddressEdit(); break;
    // Ctrl+W closes this window, as it does in Explorer.
    case 'W': if (ctrl) PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
    case '1': if (ctrl) a.setViewMode(ViewMode::Details); break;
    case '2': if (ctrl) a.setViewMode(ViewMode::List); break;
    case '3': if (ctrl) a.setViewMode(ViewMode::Icons); break;
    default: break;
    }
    a.clampScroll();
    repaint(hwnd);
}

void onKeyDown(HWND hwnd, App& a, WPARAM key) {
    diagLog(L"KEYDOWN %u sel=%d", (unsigned)key, (int)a.selection.size());
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    handleKey(hwnd, a, key, ctrl, shift, alt);
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

// ----------------------------------------------------- a new window in front

// The main window a process is showing, if it is showing one.
struct ProcessWindow {
    DWORD pid = 0;
    HWND window = nullptr;
};

BOOL CALLBACK findProcessWindow(HWND h, LPARAM lp) {
    ProcessWindow& s = *(ProcessWindow*)lp;
    wchar_t cls[64]{};
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid == s.pid && !s.window && IsWindowVisible(h) && GetClassNameW(h, cls, 64) &&
        lstrcmpW(cls, L"MyOldComputerWindow") == 0)
        s.window = h;
    return TRUE;
}

HWND windowOfProcess(DWORD pid) {
    ProcessWindow scan;
    scan.pid = pid;
    EnumWindows(findProcessWindow, (LPARAM)&scan);
    return scan.window;
}

// The window of an instance started for a new folder is to be in front.  Left
// to come up by itself, it can end up behind the window it was opened from:
// the system lets only some processes take the foreground, and whether the
// new one may is not this program's to know.  This process is the one the
// user just clicked in, and a foreground process may hand the foreground to
// any window: as soon as the new window is showing, it is made the foreground
// window.  (Started by openPathInNewWindow; an instance that stays in the
// background, a test's, never does this.)
void raiseNewWindows(HWND hwnd, App& a) {
    std::vector<DWORD> stillWaiting;
    for (DWORD pid : a.newWindowPids) {
        const HWND found = windowOfProcess(pid);
        if (!found) {
            stillWaiting.push_back(pid);
            continue;
        }
        const BOOL inFront = SetForegroundWindow(found);
        diagLog(L"new window: brought to the front %d", (int)inFront);
    }
    a.newWindowPids.swap(stillWaiting);
    // Three seconds: an instance that shows no window by then is not waited for any longer.
    if (a.newWindowPids.empty() || ++a.newWindowTicks > 200) {
        a.newWindowPids.clear();
        KillTimer(hwnd, kNewWindowTimer);
    }
}

// A thumbnail came in (thumbs.h).  The cells in view that show a picture and
// are still showing its icon are drawn again - those and nothing else.
void repaintThumbs(HWND hwnd, App& a) {
    const std::vector<int> rows = thumbsInView(a, false);
    if (rows.empty()) return;
    repaintRows(hwnd, a, rows);
    for (int i : rows) a.thumbDrawn[i] = true;
}

// WM_LBUTTONUP's whole handling.
void onLeftReleased(HWND hwnd, App& a, POINT p) {
    onLButtonUp(hwnd, a, p);
    // What a right-click there would be on may have changed (it was selected).
    cancelHover(hwnd);
    noteHover(hwnd, a, p);
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
        ++g_inputTick;
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
        ++g_inputTick;
        return 0;
    case WM_CLIPBOARDUPDATE:
        // Whether there is something to paste is what a folder's background
        // menu says: its menus are not kept past a change of the clipboard.
        // The one of the folder shown is built anew, ahead of the next click.
        if (menusAhead(a)) {
            shellmenu::forgetBackgrounds();
            shellmenu::prefetch(shellmenu::Request{a.current, {}, a.hiddenFor({})});
        }
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
        // An address bar button that was held when the mouse was taken is no
        // longer held - unless it is the machine's, pressed while its own list is up.
        if (a.pressCrumb >= 0 && !folderListWindow()) {
            a.pressCrumb = -1;
            repaintRegion(hwnd, a, a.lay.addrField);
        }
        return 0;
    case WM_MOUSEMOVE: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        if (!a.splitterDrag && !a.scrollDrag && !a.marquee && !a.dragging && !a.dragArmed) {
            TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
        }
        onMouseMove(hwnd, a, p);
        noteHover(hwnd, a, p);
        return 0;
    }
    case WM_MOUSELEAVE: {
        // (Not the menu the pointer's resting on an entry would have built:
        // it is made all the same if the entry was rested on long enough.)
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
        if (a.hoverCrumb >= 0 || a.hoverCrumbMore >= 0 || a.hoverAddrMore) {
            a.hoverCrumb = a.hoverCrumbMore = -1;
            a.hoverAddrMore = false;
            repaintRegion(hwnd, a, a.lay.addrField);
        }
        return 0;
    }
    case WM_ACTIVATE:
        diagLog(L"main activate %04X", (unsigned)LOWORD(wp));
        // Another window of this program may have coloured folders since:
        // what it did, this window shows from now on.
        if (LOWORD(wp) != WA_INACTIVE) {
            foldercolours::reload();
            repaint(hwnd);
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ++g_inputTick;
        // A press kept while a menu was up is the window's own now, as any
        // other (hmmenu.cpp); it may carry an up along, let go before it came.
        POINT letGo{};
        if (menuPressGiven(hwnd, &letGo)) {
            onLButtonDown(hwnd, a, p);
            onLeftReleased(hwnd, a, letGo);
            return 0;
        }
        if (menuIsOpen()) {
            // (Only a press sent straight to the window, not one put in its
            // queue: that is kept by the menu's loop and is the window's once
            // the menu is down.)
            diagLog(L"main lbutton while a menu is open at %d,%d", p.x, p.y);
            menuClose();
            return 0;
        }
        onLButtonDown(hwnd, a, p);
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        // An up before the press that is on its way (kept while a menu was
        // up): kept with it, and given right after it.
        if (menuPressLetGo(hwnd, p)) return 0;
        onLeftReleased(hwnd, a, p);
        return 0;
    }
    case WM_LBUTTONDBLCLK: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        onDoubleClick(hwnd, a, p);
        return 0;
    }
    case WM_RBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ++g_inputTick;
        showContextMenu(hwnd, a, p);
        return 0;
    }
    case WM_MOUSEACTIVATE:
        diagLog(L"main mouseactivate menu=%d", (int)menuIsOpen());
        // A click while one of our shell menus is up must not run the normal
        // activation dance: activating the main window makes Windows destroy
        // the foreground menu window mid-activation, and the click that
        // started it is lost entirely.  Take the click without activating:
        // it is the window's own once the menu is down for it (hmmenu.cpp).
        if (menuIsOpen()) return MA_NOACTIVATE;
        break;
    case WM_MOUSEWHEEL: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ++g_inputTick;
        cancelHover(hwnd);
        onMouseWheel(hwnd, a, p, GET_WHEEL_DELTA_WPARAM(wp));
        return 0;
    }
    case WM_TIMER:
        if (wp == kNewWindowTimer) {
            raiseNewWindows(hwnd, a);
            return 0;
        }
        if (wp == kMenuRestateTimer) {
            // A submenu was open as the things' own menu came in: the menu on
            // screen says its say as soon as none is.
            KillTimer(hwnd, kMenuRestateTimer);
            const ULONGLONG entry = g_menuRestatePending;
            g_menuRestatePending = 0;
            if (entry && menuIsOpen()) restateWhenQuiet(hwnd, entry);
            return 0;
        }
        if (wp == kHoverMenuTimer) {
            // The pointer has rested on an entry: the menu of a right-click
            // there is built now, while nobody waits for it.
            KillTimer(hwnd, kHoverMenuTimer);
            const int row = g_hoverMenuRow;
            g_hoverMenuRow = -1;
            if (row >= 0 && row < (int)a.entries.size()) {
                const std::wstring& hit = a.entries[row].path;
                std::vector<std::wstring> paths;
                if (a.selection.count(hit) && a.selection.size() <= kMenuMostPaths)
                    for (const auto& e : a.entries)
                        if (a.selection.count(e.path)) paths.push_back(e.path);
                if (paths.empty()) paths.push_back(hit);
                shellmenu::hover(shellmenu::Request{a.current, paths, a.hiddenFor(paths)});
            }
            return 0;
        }
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
    case thumbs::kReadyMessage:
        repaintThumbs(hwnd, a);
        return 0;
    case shellmenu::kMenuReadyMessage:
        // The menu asked for on a right-click is built.  The click is long
        // gone if anything was done since, or what it was on is other now.
        if (g_menuWait.serial && wp == g_menuWait.serial) {
            const MenuFor wait = g_menuWait;
            g_menuWait.serial = 0;
            if (wait.tick != g_inputTick || !targetStillThere(a, wait)) {
                contextMenuOver(hwnd, a, wait.fromTree);
            } else if (!showMenuAhead(hwnd, a, wait, true)) {
                // The shell could not build it ahead: as it was done before
                // menus were, which ends in this program's own menu.
                showMenuNow(hwnd, a, wait.what.folder, wait.what.paths, wait.screen, wait.state,
                            wait.fromTree);
            }
        } else if (g_menuOpen.serial && wp == g_menuOpen.serial && menuIsOpen() &&
                   g_menuDisplay) {
            // A menu built anew for the very things is in while they are shown
            // from one built before - their own of a while ago, or one of
            // their kind.  Its say is put on the rows that are there, where
            // they differ (a few items are the thing's own: an archive's name,
            // say) - the menu stays up as it is, put up once.  Only when it has
            // other rows than the one on screen is that taken down for it
            // (showBuiltMenu goes on from there).
            std::vector<shellmenu::Item> shown, own;
            const shellmenu::Found built = shellmenu::find(g_menuOpen.what);
            if (built.entry && built.entry != g_menuOpenEntry &&
                shellmenu::itemsOf(built.entry, &own)) {
                // What the one on screen said is not had any more (let go of
                // since: a menu built again takes the place of the one it was
                // built from): it is taken to have the same rows - menuRestate
                // says so too, and then nothing is done.
                const bool known = shellmenu::itemsOf(g_menuOpenEntry, &shown);
                if (!known || shellmenu::sameRows(shown, own)) {
                    restateWhenQuiet(hwnd, built.entry);
                } else {
                    g_menuReplaceWith = built.entry;
                    menuClose();
                }
            }
        }
        return 0;
    case shellmenu::kMenuChangedMessage:
        // The menu on screen was built from the menu that says something new
        // (its submenus were filled in once more): it says that from now on.
        if (wp == g_menuOpenEntry) restateOpenMenu();
        return 0;
    case shellmenu::kMenuDoneMessage:
        if (g_menuShown.serial && wp == g_menuShown.serial) {
            const MenuFor shown = g_menuShown;
            g_menuShown.serial = 0;
            if (lp == shellmenu::kVerbRan) {
                shellmenu::Result result;
                result.invokedShell = true;
                applyMenuChoice(hwnd, a, shown.what.paths, result);
            } else if (lp == shellmenu::kVerbNotFound && shown.tick == g_inputTick &&
                       targetStillThere(a, shown)) {
                // The menu of the thing itself has no item where the one
                // picked lay, saying what it said (what was shown stood for
                // another of its kind): that menu is shown now, as it is.
                showContextMenu(hwnd, a, shown.client);
            }
        }
        return 0;
    case WM_KEYDOWN:
        ++g_inputTick;
        onKeyDown(hwnd, a, wp);
        return 0;
    case WM_SYSKEYDOWN:
        ++g_inputTick;
        // With Alt held, arrow keys arrive here instead of WM_KEYDOWN; other
        // system keys (Alt+F4, F10) still go to DefWindowProc.
        if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP) {
            onKeyDown(hwnd, a, wp);
            return 0;
        }
        // Alt+D, as the bar's own label says: the address bar (label "地址(D)").
        if ((wp == 'D' || wp == 'd') && !a.addrEditing) {
            a.beginAddressEdit();
            return 0;
        }
        break;
    case WM_SYSCHAR:
        // The character of that Alt+D follows into whichever window has the
        // focus by then: it is no mnemonic of ours.
        if (wp == 'D' || wp == 'd') return 0;
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
        diagLog(L"WM_DESTROY of %p", hwnd);
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
// The system's highlight: what an edit box puts behind the selected part of
// its text - and so, while a name is being edited, that row's selection box.
bool isHighlight(COLORREF px) {
    return (px & 0xFFFFFF) == (GetSysColor(COLOR_HIGHLIGHT) & 0xFFFFFF);
}
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

// The same on a row being renamed, whose selection box is the highlight.
bool iconInkOnHighlight(COLORREF px) {
    return !isHighlight(px) && !isBlack(px) && (px & 0xFFFFFF) != 0xFFFFFF;
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

// -------------------------------------------------------------- frame test
// The window keeps its last frame: hovering and scrolling draw into it, and
// it is rendered again only when the state says it changed.  So a change that
// nobody announced stayed on screen as it was - after Enter in the address bar
// the listing had changed and every check of the state passed, while the old
// folder was still showing.  This drives the real window (its class, its
// procedure, posted input) through the things that end inside App itself, and
// holds what the desktop shows against a frame rendered fresh from the state.

constexpr UINT_PTR kFrameTestTimer = 0x4D46;
constexpr wchar_t kFrameTestClass[] = L"MyOldComputerFrameTest";

// The main window's class, once per process.
void registerMainClass(HINSTANCE instance) {
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon =
        (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    wc.hIconSm =
        (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"MyOldComputerWindow";
    RegisterClassExW(&wc);
}

// Puts the main window at its size and place, without activating it.
void placeWindow(HWND hwnd, int x, int y, int w, int h) {
    // SWP_FRAMECHANGED: the window is asked where its client area goes even
    // when its size does not change here.  Without it a window created at the
    // size it keeps (at 100 % scaling, where the saved size is the physical
    // one) was never asked, and kept the system's title bar above its own.
    SetWindowPos(hwnd, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// Where a window of this program is about to open: a step down and to the
// right of any other instance's window that is at that place already, as
// often as it takes - so that a folder opened in a new window, or the program
// started a second time, does not open exactly over the first.  The step is
// a title bar high: the title bars of the windows behind stay theirs to
// click.  The title bar stays on the monitor: when the next step would take
// it off, the window opens where the first window did.
struct SiblingScan {
    std::wstring exe;
    std::vector<POINT> at;  // where the windows of the other instances open
};

BOOL CALLBACK collectSiblings(HWND h, LPARAM lp) {
    SiblingScan& s = *(SiblingScan*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t cls[64]{};
    if (!pid || pid == GetCurrentProcessId()) return TRUE;
    if (!GetClassNameW(h, cls, 64) || lstrcmpW(cls, L"MyOldComputerWindow") != 0) return TRUE;
    // Another instance of this program, not a test build somewhere else.
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return TRUE;
    wchar_t path[MAX_PATH * 2]{};
    DWORD n = (DWORD)(sizeof(path) / sizeof(path[0]));
    const bool same = QueryFullProcessImageNameW(process, 0, path, &n) &&
                      _wcsicmp(path, s.exe.c_str()) == 0;
    CloseHandle(process);
    if (!same) return TRUE;
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(h, &wp))
        s.at.push_back(POINT{wp.rcNormalPosition.left, wp.rcNormalPosition.top});
    return TRUE;
}

POINT cascadePosition(POINT at, int w, int titleH) {
    wchar_t exe[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, exe, (DWORD)(sizeof(exe) / sizeof(exe[0])));
    SiblingScan scan;
    scan.exe = exe;
    EnumWindows(collectSiblings, (LPARAM)&scan);
    const int step = GetSystemMetrics(SM_CYCAPTION);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    const RECT bounds{at.x, at.y, at.x + w, at.y + titleH};
    GetMonitorInfoW(MonitorFromRect(&bounds, MONITOR_DEFAULTTONEAREST), &mi);
    POINT firstFree = at;
    bool found = false;
    // One place more than there are windows: one of them is free.
    for (int i = 0; i <= (int)scan.at.size(); ++i) {
        const POINT p{at.x + i * step, at.y + i * step};
        bool taken = false;
        for (const POINT q : scan.at) taken = taken || (q.x == p.x && q.y == p.y);
        if (taken) continue;
        if (!found) {
            firstFree = p;  // if none of them keeps the title bar on the monitor
            found = true;
        }
        const bool onMonitor = p.x >= mi.rcWork.left && p.y >= mi.rcWork.top &&
                               p.x + std::min(w, 4 * step) <= mi.rcWork.right &&
                               p.y + titleH <= mi.rcWork.bottom;
        if (onMonitor) return p;
    }
    return firstFree;
}

// One frame of pixels, top-down.
struct Frame {
    int w = 0, h = 0;
    std::vector<COLORREF> px;
};

// Reads a compatible bitmap back into pixels.
void readFrame(HDC dc, HBITMAP bmp, Frame& f) {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = f.w;
    bi.bmiHeader.biHeight = -f.h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    f.px.assign((size_t)f.w * f.h, 0);
    GetDIBits(dc, bmp, 0, f.h, f.px.data(), &bi, DIB_RGB_COLORS);
}

// A fresh full frame, rendered into the kind of surface the window renders
// its own into.
Frame renderedFrame(App& a) {
    Frame f;
    f.w = a.width;
    f.h = a.height;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, f.w, f.h);
    HGDIOBJ old = SelectObject(dc, bmp);
    {
        Canvas canvas(dc, a.scale);
        renderApp(canvas, a);
    }
    GdiFlush();
    SelectObject(dc, old);
    if (bmp) readFrame(dc, bmp, f);
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return f;
}

// What the desktop shows of the window, read back from the compositor: the
// pixels the user sees, whatever lies on top of the window.  (Asked through
// WM_PRINTCLIENT instead, the window would render a fresh frame, and so could
// never show a stale one.)
Frame shownFrame(HWND hwnd) {
    Frame f;
    RECT rc{};
    GetClientRect(hwnd, &rc);
    f.w = rc.right;
    f.h = rc.bottom;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, f.w, f.h);
    HGDIOBJ old = SelectObject(dc, bmp);
    PrintWindow(hwnd, dc, PW_RENDERFULLCONTENT);
    GdiFlush();
    SelectObject(dc, old);
    if (bmp) readFrame(dc, bmp, f);
    if (bmp) DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(nullptr, screen);
    return f;
}

// How many pixels the two frames disagree on, and the box those lie in.
int frameMismatch(const Frame& a, const Frame& b, RECT* box) {
    *box = RECT{0, 0, 0, 0};
    if (a.w != b.w || a.h != b.h || a.px.size() != b.px.size() || a.px.empty()) return 1;
    int n = 0;
    for (int y = 0; y < a.h; ++y) {
        for (int x = 0; x < a.w; ++x) {
            const size_t i = (size_t)y * a.w + x;
            if ((a.px[i] & 0xFFFFFF) == (b.px[i] & 0xFFFFFF)) continue;
            if (!n) {
                *box = RECT{x, y, x + 1, y + 1};
            } else {
                if (x < box->left) box->left = x;
                if (x + 1 > box->right) box->right = x + 1;
                if (y + 1 > box->bottom) box->bottom = y + 1;
            }
            ++n;
        }
    }
    return n;
}

// How many pixels of `area` (window coordinates) the frame has that `take`
// takes.
int frameCount(const Frame& f, const RECT& area, bool (*take)(COLORREF)) {
    const RECT c{std::max<LONG>(0, area.left), std::max<LONG>(0, area.top),
                 std::min<LONG>(f.w, area.right), std::min<LONG>(f.h, area.bottom)};
    int n = 0;
    for (int y = c.top; y < c.bottom; ++y) {
        for (int x = c.left; x < c.right; ++x) {
            // GetDIBits gives blue, green, red in memory; a COLORREF, which
            // is what the palette and GetSysColor are, is red, green, blue.
            // (Frame against frame needs no such care: both are the same way.)
            const COLORREF raw = f.px[(size_t)y * f.w + x];
            if (take(RGB((raw >> 16) & 0xFF, (raw >> 8) & 0xFF, raw & 0xFF))) ++n;
        }
    }
    return n;
}

// Under MOC_DEBUG both frames of a comparison go to %TEMP%\moc-shots, to be
// looked at.
void keepFrames(const wchar_t* name, const Frame& shown, const Frame& fresh) {
    wchar_t debug[8]{};
    if (!GetEnvironmentVariableW(L"MOC_DEBUG", debug, 8)) return;
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"moc-shots";
    CreateDirectoryW(dir.c_str(), nullptr);
    const std::wstring stem = dir + L"\\frame-";
    writePng(stem + name + L"-shown.png", shown.px.data(), shown.w, shown.h);
    writePng(stem + name + L"-fresh.png", fresh.px.data(), fresh.w, fresh.h);
}

constexpr int kFrameTestEnd = 10;  // the step past the last one

struct FrameDrive {
    HWND main = nullptr;
    int step = 0;   // what happens next; kFrameTestEnd is past the last
    int wait = 0;   // ticks to let what was posted get through first
    int ticks = 0;  // the watchdog's count
    std::wstring to;  // the folder typed into the address bar
    bool baseline = false;  // a clean window: what is shown is what the state renders
    bool editCame = false, editCameAgain = false;
    bool fieldCame = false, fieldCameAgain = false;
    bool navigated = false, leftOk = false, kept = false, renamed = false;  // the state after each key
    int blockOff = 0;          // pixels beside the icon, while renaming, that are not the highlight
    int nameHighlighted = 0;   // ... and highlight pixels inside the edit box (the selected name)
    int stale[4] = {0, 0, 0, 0};  // pixels on screen that the state does not have
    RECT where[4] = {};
};
FrameDrive g_frame;

// One timer tick = one action; timers fire only when nothing else is queued,
// so what the step before posted has been dealt with (and painted).
void frameTestStep(HWND driver) {
    FrameDrive& d = g_frame;
    KillTimer(driver, kFrameTestTimer);
    if (d.step >= kFrameTestEnd) return;
    if (++d.ticks > 240) {  // six seconds: something did not answer - give up, do not hang
        d.step = kFrameTestEnd;
        return;
    }
    const auto later = [driver] { SetTimer(driver, kFrameTestTimer, 25, nullptr); };
    if (d.wait-- > 0) {
        later();
        return;
    }
    d.wait = 0;
    const auto post = [](HWND w, UINT msg, WPARAM wp, int x, int y) {
        PostMessageW(w, msg, wp, MAKELPARAM((short)x, (short)y));
    };
    const auto click = [&](int x, int y) {
        post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, x, y);
        post(d.main, WM_LBUTTONUP, 0, x, y);
    };
    // Whatever is waiting to be painted is painted now.
    const auto settle = [&] { UpdateWindow(d.main); };
    // The desktop against the state; both frames kept for looking at.
    const auto compare = [&](int which, const wchar_t* name) {
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(name, shown, fresh);
        d.stale[which] = frameMismatch(shown, fresh, &d.where[which]);
    };
    // On the name of a row, where a click selects it.
    const auto clickName = [&](int row) {
        const RECT r = g_app.listRowRect(row);
        click(r.left + pxOf(g_app.scale, 30), (r.top + r.bottom) / 2);
    };
    // The blank part of the address bar: a click there turns the whole path
    // into an edit box (the rest of the bar is buttons, see the crumb test).
    const auto clickAddress = [&] {
        const RECT b = g_app.lay.addrBlank;
        click((b.left + b.right) / 2, (b.top + b.bottom) / 2);
    };
    switch (d.step) {
    case 0:
        settle();
        d.baseline = frameMismatch(shownFrame(d.main), renderedFrame(g_app), &d.where[0]) == 0;
        d.wait = 3;
        clickAddress();
        break;
    case 1:  // the edit is up: type the other folder and press Enter
        d.editCame = g_app.addrEditing && IsWindow(g_app.addrEdit);
        if (d.editCame) {
            SetWindowTextW(g_app.addrEdit, d.to.c_str());
            post(g_app.addrEdit, WM_KEYDOWN, VK_RETURN, 0, 0);
        }
        d.wait = 5;
        break;
    case 2:
        settle();
        d.navigated = !g_app.addrEditing && g_app.current == d.to;
        compare(0, L"address");
        clickAddress();  // and once more, to leave it the other way: by clicking elsewhere
        d.wait = 3;
        break;
    case 3:
        d.editCameAgain = g_app.addrEditing && IsWindow(g_app.addrEdit);
        clickName(1);  // takes the focus out of the edit box
        d.wait = 4;
        break;
    case 4:
        settle();
        // The path was not changed, so nothing was gone to again: the click's
        // own selection is what is selected.
        d.leftOk = !g_app.addrEditing && g_app.current == d.to &&
                   g_app.selection.size() == 1 && g_app.selection.count(d.to + L"\\bbb.txt");
        compare(3, L"leave");
        clickName(0);
        d.wait = 3;
        break;
    case 5:  // F2 over the selected name
        post(d.main, WM_KEYDOWN, VK_F2, 0, 0);
        d.wait = 4;
        break;
    case 6:  // the field is up: one blue for the row; then type a name, then Esc
        d.fieldCame = g_app.renaming && IsWindow(g_app.renameEdit);
        if (d.fieldCame) {
            // On screen, with the base name still selected in the box: the
            // icon's background and the selected part of the name are one
            // colour, the system's highlight.
            const Frame shown = shownFrame(d.main);
            keepFrames(L"rename-open", shown, shown);
            const RECT row = g_app.listRowRect(g_app.renameRow);
            // The strip between the row's left edge and its icon: selection
            // box only.
            const RECT margin{row.left, row.top, row.left + pxOf(g_app.scale, 3), row.bottom};
            const int area = (int)(margin.right - margin.left) * (margin.bottom - margin.top);
            d.blockOff = area - frameCount(shown, margin, isHighlight);
            d.nameHighlighted = frameCount(shown, g_app.renameField(g_app.renameRow).edit,
                                           isHighlight);
            SetWindowTextW(g_app.renameEdit, L"zzz.txt");
            post(g_app.renameEdit, WM_KEYDOWN, VK_ESCAPE, 0, 0);
        }
        d.wait = 5;
        break;
    case 7:
        settle();
        d.kept = !g_app.renaming && fsx::exists(d.to + L"\\aaa.txt");
        compare(1, L"rename-esc");
        post(d.main, WM_KEYDOWN, VK_F2, 0, 0);
        d.wait = 4;
        break;
    case 8:  // the field again: the same name, then Enter
        d.fieldCameAgain = g_app.renaming && IsWindow(g_app.renameEdit);
        if (d.fieldCameAgain) {
            SetWindowTextW(g_app.renameEdit, L"zzz.txt");
            post(g_app.renameEdit, WM_KEYDOWN, VK_RETURN, 0, 0);
        }
        d.wait = 5;
        break;
    default:
        settle();
        d.renamed =
            !g_app.renaming && fsx::exists(d.to + L"\\zzz.txt") && !fsx::exists(d.to + L"\\aaa.txt");
        compare(2, L"rename-enter");
        break;
    }
    ++d.step;
    if (d.step < kFrameTestEnd) later();
}

LRESULT CALLBACK frameTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kFrameTestTimer) {
        frameTestStep(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// -------------------------------------------------------------- crumb test
// The address bar's buttons in the real window, driven by posted input, with
// what the desktop shows read back after every step - the list under an arrow
// included, read back the same way.  The pointer is moved by calling what the
// window procedure calls: a posted move is followed at once by a "pointer left
// the window" (the pointer is not there), and that clears the hover before it
// could be seen.

constexpr UINT_PTR kCrumbTestTimer = 0x4D47;
constexpr wchar_t kCrumbTestClass[] = L"MyOldComputerCrumbTest";
constexpr int kCrumbTestEnd = 41;  // the step past the last one

bool isFaceHi(COLORREF px) { return (px & 0xFFFFFF) == (Theme::faceHi & 0xFFFFFF); }
bool isPlainWhite(COLORREF px) { return (px & 0xFFFFFF) == 0xFFFFFF; }
bool isFace(COLORREF px) { return (px & 0xFFFFFF) == (Theme::face & 0xFFFFFF); }

struct CrumbDrive {
    HWND main = nullptr;
    int step = 0;
    int wait = 0;   // ticks to let what was posted get through first
    int ticks = 0;  // the watchdog's count
    std::wstring way, dd, aa, bb, root, hiddenTarget, wide, wideName;
    bool baseline = false;
    int k = -1;  // the crumb hovered and pressed
    int hoverArea = 1, hoverShown = 0, hoverCleared = 0, hoverStale = 0;
    bool pressed = false, released = false;
    int pressShown = 0;
    bool listCame = false, listRight = false, arrowDown = false, hotPainted = false, hotMoved = false;
    bool chose = false;
    bool hiddenList = false, hiddenArrowDown = false, wentHidden = false;
    bool clicked = false, doubleOk = false;
    bool editByBlank = false, editByF4 = false, editByCtrlL = false, editByAltD = false;
    bool editsClosed = true;
    int editHighlight = 0;
    bool machineList = false, machineClosed = false;
    bool switchFrom = false, switched = false, switchClosed = false;
    bool longList = false, listFits = false, longClosed = false;
    int listWidth = 0;
    // Out of room: the row starts at the left with the machine as an icon
    // alone, a ... right after it, no gap anywhere; the ... lights up under
    // the pointer; a name too long for the field ends in an ellipsis.
    bool rowJoined = false, machineIconOnly = false, dotsShown = false;
    bool dotsHover = false, dotsHoverCleared = false;
    bool cutState = false, cutInk = false, cutWhite = false;
    int stale[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    RECT where[14] = {};
};
CrumbDrive g_crumb;

void crumbTestStep(HWND driver) {
    CrumbDrive& d = g_crumb;
    KillTimer(driver, kCrumbTestTimer);
    if (++d.ticks > 400) {  // ten seconds: something did not answer - give up, do not hang
        d.step = kCrumbTestEnd;
        if (HWND l = folderListWindow()) DestroyWindow(l);
        return;
    }
    const auto later = [driver] { SetTimer(driver, kCrumbTestTimer, 25, nullptr); };
    if (d.step >= kCrumbTestEnd) {
        // A list the test did not mean to leave open: it is closed, or the
        // loop it runs in never ends.
        if (HWND l = folderListWindow()) {
            PostMessageW(l, WM_KEYDOWN, VK_ESCAPE, 0);
            later();
        }
        return;
    }
    if (d.wait-- > 0) {
        later();
        return;
    }
    d.wait = 0;
    const auto post = [](HWND w, UINT msg, WPARAM wp, int x, int y) {
        PostMessageW(w, msg, wp, MAKELPARAM((short)x, (short)y));
    };
    const auto click = [&](int x, int y) {
        post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, x, y);
        post(d.main, WM_LBUTTONUP, 0, x, y);
    };
    const auto settle = [&] { UpdateWindow(d.main); };
    const auto compare = [&](int which, const wchar_t* name) {
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(name, shown, fresh);
        d.stale[which] = frameMismatch(shown, fresh, &d.where[which]);
    };
    const auto middle = [](const RECT& r) {
        return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    };
    const auto crumbOf = [](const wchar_t* title) {
        for (int i = 0; i < (int)g_app.crumbs.size(); ++i)
            if (g_app.crumbs[i].title == title) return i;
        return -1;
    };
    // The padding at the left of a crumb's button: no text or icon there, so
    // a hover or a press shows in it and nothing else does.
    const auto pad = [&](int crumb) {
        const RECT& b = g_app.crumbs[crumb].box;
        return RECT{b.left + 1, b.top + 1, b.left + pxOf(g_app.scale, 5), b.bottom - 1};
    };
    const auto area = [](const RECT& r) {
        return (int)(r.right - r.left) * (int)(r.bottom - r.top);
    };
    // The list under an arrow, while it is up: its rows, and what it shows of
    // the row `hotRow` (and of `otherRow`): the navy of a highlighted row.
    struct Seen {
        HWND list = nullptr;
        FolderListInfo info;
        const std::vector<FolderRow>* rows = nullptr;
        int hotNavy = 0, otherNavy = 0, rowArea = 1;
    };
    const auto seeList = [&](int hotRow, int otherRow) {
        Seen s;
        s.list = folderListWindow();
        if (!s.list || !folderListInfo(s.list, &s.info)) return s;
        s.rows = folderListRows(s.list);
        const Frame f = shownFrame(s.list);
        const RECT r = s.info.rows;
        s.rowArea = std::max(1, area(r) / std::max(1, s.info.visible));
        const auto row = [&](int i) {
            const int y = r.top + (i - s.info.first) * s.info.rowH;
            return RECT{r.left, y, r.right, y + s.info.rowH};
        };
        if (hotRow >= s.info.first && hotRow < s.info.first + s.info.visible)
            s.hotNavy = frameCount(f, row(hotRow), isNavy);
        if (otherRow >= s.info.first && otherRow < s.info.first + s.info.visible)
            s.otherNavy = frameCount(f, row(otherRow), isNavy);
        return s;
    };
    // The whole path, typed into the address bar, and Enter.
    const auto typePath = [&](const std::wstring& p) {
        if (!g_app.addrEdit) return;
        SetWindowTextW(g_app.addrEdit, p.c_str());
        post(g_app.addrEdit, WM_KEYDOWN, VK_RETURN, 0, 0);
    };
    const auto leaveEdit = [&] {
        if (g_app.addrEdit) post(g_app.addrEdit, WM_KEYDOWN, VK_ESCAPE, 0, 0);
    };

    switch (d.step) {
    case 0: {
        settle();
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(L"crumb-baseline", shown, fresh);
        d.baseline = frameMismatch(shown, fresh, &d.where[0]) == 0;
        d.stale[11] = frameMismatch(shown, fresh, &d.where[11]);
        // Out of room, as it is on screen: the machine an icon alone at the
        // left edge, its v, three dots, then the last crumbs - no gap anywhere.
        const RECT inner = g_app.addressInner();
        const RECT machine = g_app.crumbs[0].box, dots = g_app.lay.addrMore;
        const int first = g_app.crumbFirst;
        const int dot = pxOf(g_app.scale, 2);
        d.rowJoined = first >= 2 && first < (int)g_app.crumbs.size() && g_app.crumbs[0].text.empty() &&
                      machine.left == inner.left && g_app.crumbs[0].more.left == machine.right &&
                      dots.left == g_app.crumbs[0].more.right &&
                      g_app.crumbs[first].box.left == dots.right &&
                      g_app.crumbs.back().box.right + pxOf(g_app.scale, 20) <= inner.right;
        // Between the icon and the dots there is nothing on screen but the
        // v's triangle: no name.
        const RECT between{machine.left + pxOf(g_app.scale, 24), inner.top, dots.left, inner.bottom};
        const RECT dotsArea{dots.left, inner.top, dots.right, inner.bottom};
        d.machineIconOnly = d.rowJoined &&
                            machine.right - machine.left ==
                                2 * pxOf(g_app.scale, 5) + pxOf(g_app.scale, 16) &&
                            frameCount(shown, between, blackInk) <=
                                8 * pxOf(g_app.scale, 3) * pxOf(g_app.scale, 3);
        d.dotsShown = d.rowJoined && frameCount(shown, dotsArea, isBlack) == 3 * dot * dot &&
                      frameCount(shown, dotsArea, notWhite) == 3 * dot * dot;
        d.k = crumbOf(L"cc");
        d.wait = 2;
        break;
    }
    case 1: {  // the pointer is on a crumb: its button lights up on the spot
        const bool there = d.k >= g_app.crumbFirst && d.k >= 0;
        if (there) onMouseMove(d.main, g_app, middle(g_app.crumbs[d.k].box));
        settle();
        const RECT p = there ? pad(d.k) : RECT{0, 0, 0, 0};
        d.hoverArea = std::max(1, area(p));
        d.hoverShown = frameCount(shownFrame(d.main), p, isFaceHi);
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(L"crumb-hover", shown, fresh);
        d.hoverStale = frameMismatch(shown, fresh, &d.where[0]);
        break;
    }
    case 2:  // the pointer leaves the window
        post(d.main, WM_MOUSELEAVE, 0, 0, 0);
        d.wait = 3;
        break;
    case 3: {
        settle();
        const bool there = d.k >= g_app.crumbFirst && d.k >= 0;
        const RECT p = there ? pad(d.k) : RECT{0, 0, 0, 0};
        d.hoverCleared = frameCount(shownFrame(d.main), p, isFaceHi);
        if (there) {
            const POINT c = middle(g_app.crumbs[d.k].box);
            post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, c.x, c.y);
        }
        d.wait = 3;
        break;
    }
    case 4: {  // held down: the button shows pressed; let go of it far away
        settle();
        const bool there = d.k >= g_app.crumbFirst && d.k >= 0;
        const RECT p = there ? pad(d.k) : RECT{0, 0, 0, 0};
        d.pressed = there && g_app.pressCrumb == d.k && g_app.current == d.way;
        d.pressShown = frameCount(shownFrame(d.main), p, isFace);
        const RECT vp = g_app.lay.listViewport;
        post(d.main, WM_LBUTTONUP, 0, (vp.left + vp.right) / 2, (vp.top + vp.bottom) / 2);
        d.wait = 4;
        break;
    }
    case 5: {  // nothing happened; now the v after "dd": its list opens inside that press
        settle();
        const bool there = d.k >= g_app.crumbFirst && d.k >= 0;
        const RECT p = there ? pad(d.k) : RECT{0, 0, 0, 0};
        d.released = d.pressed && g_app.pressCrumb == -1 && g_app.current == d.way &&
                     frameCount(shownFrame(d.main), p, isPlainWhite) == d.hoverArea;
        compare(1, L"crumb-release");
        const int dd = crumbOf(L"dd");
        if (dd >= g_app.crumbFirst && dd >= 0) {
            const POINT c = middle(g_app.crumbs[dd].more);
            post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, c.x, c.y);
        }
        d.wait = 3;
        break;
    }
    case 6: {  // ee and yy, ee (the way) highlighted; the v on screen is pressed
        const Seen s = seeList(0, 1);
        d.listCame = s.list != nullptr;
        const int dd = crumbOf(L"dd");
        const RECT m = dd >= 0 ? g_app.crumbs[dd].more : RECT{0, 0, 0, 0};
        d.arrowDown = dd >= 0 && g_app.pressCrumbMore == dd &&
                      frameCount(shownFrame(d.main), m, isFace) > std::max(1, area(m) / 4);
        d.listRight = s.rows && s.rows->size() == 2 && (*s.rows)[0].title == L"ee" &&
                      (*s.rows)[1].title == L"yy" && s.info.highlighted == 0;
        d.hotPainted = s.hotNavy > s.rowArea / 2 && s.otherNavy == 0;
        compare(0, L"crumb-list");  // the bar under the list, v pressed and all
        if (s.list) post(s.list, WM_KEYDOWN, VK_DOWN, 0, 0);
        d.wait = 3;
        break;
    }
    case 7: {  // Down: the highlight is on the next row; Enter: gone to it
        const Seen s = seeList(1, 0);
        d.hotMoved = s.list && s.info.highlighted == 1 && s.hotNavy > s.rowArea / 2 &&
                     s.otherNavy == 0;
        if (s.list) post(s.list, WM_KEYDOWN, VK_RETURN, 0, 0);
        d.wait = 5;
        break;
    }
    case 8: {  // the list is gone and so is the press of its v; now the ... of the crumbs out of view
        settle();
        d.chose = !folderListWindow() && g_app.current == d.dd + L"\\yy" && g_app.pressCrumbMore == -1;
        compare(2, L"crumb-choose");
        const POINT c = middle(g_app.lay.addrMore);
        post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, c.x, c.y);
        d.wait = 3;
        break;
    }
    case 9: {  // them, the drive first (the machine is in view, so not in the list) and the ... pressed; a click on the last one goes there
        const Seen s = seeList(-1, -1);
        const RECT dots = g_app.lay.addrMore;
        d.hiddenList = s.list && s.rows && s.rows->size() + 1 == (size_t)g_app.crumbFirst &&
                       s.info.highlighted == -1 && !s.rows->empty() &&
                       (*s.rows)[0].kind == TreeNode::Volume &&
                       _wcsicmp((*s.rows)[0].path.c_str(), d.root.c_str()) == 0;
        d.hiddenArrowDown = g_app.pressAddrMore &&
                            frameCount(shownFrame(d.main), dots, isFace) >= area(dots) * 3 / 4;
        keepFrames(L"crumb-dots-list", shownFrame(d.main), renderedFrame(g_app));
        if (s.list && s.rows && !s.rows->empty()) {
            const int row = std::min((int)s.rows->size() - 1, s.info.first + s.info.visible - 1);
            const RECT r = s.info.rows;
            const int x = r.left + (r.right - r.left) / 3;
            const int y = r.top + (row - s.info.first) * s.info.rowH + s.info.rowH / 2;
            d.hiddenTarget = (*s.rows)[row].path;
            post(s.list, WM_LBUTTONDOWN, MK_LBUTTON, x, y);
            post(s.list, WM_LBUTTONUP, 0, x, y);
        }
        d.wait = 5;
        break;
    }
    case 10:  // gone to it, and it is the last crumb now
        settle();
        d.wentHidden = !folderListWindow() && !d.hiddenTarget.empty() &&
                       g_app.current == d.hiddenTarget && !g_app.pressAddrMore;
        compare(3, L"crumb-hidden");
        post(d.main, WM_KEYDOWN, VK_F4, 0, 0);
        d.wait = 3;
        break;
    case 11:
        d.editByF4 = g_app.addrEditing && IsWindow(g_app.addrEdit);
        typePath(d.bb);
        d.wait = 5;
        break;
    case 12: {  // click the button of "aa", two levels up
        settle();
        const int aa = crumbOf(L"aa");
        if (g_app.current == d.bb && aa >= g_app.crumbFirst && aa >= 0) {
            const POINT c = middle(g_app.crumbs[aa].box);
            click(c.x, c.y);
        }
        d.wait = 4;
        break;
    }
    case 13:
        settle();
        d.clicked = g_app.current == d.aa;
        compare(4, L"crumb-click");
        post(d.main, WM_KEYDOWN, VK_F4, 0, 0);
        d.wait = 3;
        break;
    case 14:
        typePath(d.way);
        d.wait = 5;
        break;
    case 15: {  // a double click on "aa": the first click goes there, and the second lands on whatever is in its place now
        settle();
        const int aa = crumbOf(L"aa");
        if (g_app.current == d.way && aa >= g_app.crumbFirst && aa >= 0) {
            const POINT c = middle(g_app.crumbs[aa].box);
            post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, c.x, c.y);
            post(d.main, WM_LBUTTONUP, 0, c.x, c.y);
            post(d.main, WM_LBUTTONDBLCLK, MK_LBUTTON, c.x, c.y);
            post(d.main, WM_LBUTTONUP, 0, c.x, c.y);
        }
        d.wait = 5;
        break;
    }
    case 16: {  // only once: it is at "aa", nothing opened; then the blank part
        settle();
        d.doubleOk = g_app.current == d.aa && !folderListWindow();
        compare(5, L"crumb-double");
        const POINT c = middle(g_app.lay.addrBlank);
        click(c.x, c.y);
        d.wait = 3;
        break;
    }
    case 17:  // the whole path is in the edit box on screen, selected
        settle();
        d.editByBlank = g_app.addrEditing && IsWindow(g_app.addrEdit);
        d.editHighlight = frameCount(shownFrame(d.main), g_app.lay.addrField, isHighlight);
        leaveEdit();
        d.wait = 4;
        break;
    case 18:  // Escape: the buttons are back
        settle();
        d.editsClosed = d.editsClosed && !g_app.addrEditing;
        compare(6, L"crumb-edit-esc");
        handleKey(d.main, g_app, 'L', true, false, false);  // Ctrl+L
        d.wait = 3;
        break;
    case 19:
        d.editByCtrlL = g_app.addrEditing && IsWindow(g_app.addrEdit);
        leaveEdit();
        d.wait = 4;
        break;
    case 20:
        settle();
        d.editsClosed = d.editsClosed && !g_app.addrEditing;
        post(d.main, WM_SYSKEYDOWN, 'D', 0, 0);  // Alt+D
        d.wait = 3;
        break;
    case 21:
        d.editByAltD = g_app.addrEditing && IsWindow(g_app.addrEdit);
        leaveEdit();
        d.wait = 4;
        break;
    case 22:
        settle();
        d.editsClosed = d.editsClosed && !g_app.addrEditing;
        compare(7, L"crumb-keys");
        post(d.main, WM_KEYDOWN, VK_F4, 0, 0);
        d.wait = 3;
        break;
    case 23:
        typePath(d.root);
        d.wait = 5;
        break;
    case 24: {  // the root of the drive: the machine's button is in view, and opens its drives
        settle();
        if (g_app.current == d.root && g_app.crumbs.size() >= 2) {
            const POINT c = middle(g_app.crumbs[0].box);
            click(c.x, c.y);
        }
        d.wait = 4;
        break;
    }
    case 25: {  // the drives, this one highlighted
        const Seen s = seeList(-1, -1);
        d.machineList = s.rows && !s.rows->empty() && s.info.highlighted >= 0 &&
                        _wcsicmp((*s.rows)[s.info.highlighted].path.c_str(), d.root.c_str()) == 0;
        if (s.list) post(s.list, WM_KEYDOWN, VK_ESCAPE, 0, 0);
        d.wait = 4;
        break;
    }
    case 26:
        settle();
        d.machineClosed = !folderListWindow() && g_app.current == d.root;
        compare(8, L"crumb-machine");
        post(d.main, WM_KEYDOWN, VK_F4, 0, 0);
        d.wait = 3;
        break;
    case 27:
        typePath(d.bb);
        d.wait = 5;
        break;
    case 28: {  // the v after "crumbs"
        settle();
        const int crumbs = crumbOf(L"crumbs");
        if (g_app.current == d.bb && crumbs >= g_app.crumbFirst && crumbs >= 0) {
            const POINT c = middle(g_app.crumbs[crumbs].more);
            post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, c.x, c.y);
        }
        d.wait = 3;
        break;
    }
    case 29: {  // its list; a press (it has the mouse) on the v after "aa" opens that one's list instead
        const Seen s = seeList(-1, -1);
        d.switchFrom = s.rows && s.rows->size() == 2 && s.info.highlighted == 0 &&
                       (*s.rows)[0].title == L"aa";
        const int aa = crumbOf(L"aa");
        if (s.list && aa >= 0) {
            POINT c = middle(g_app.crumbs[aa].more);
            ClientToScreen(d.main, &c);
            ScreenToClient(s.list, &c);
            post(s.list, WM_LBUTTONDOWN, MK_LBUTTON, c.x, c.y);
        }
        d.wait = 4;
        break;
    }
    case 30: {
        const Seen s = seeList(-1, -1);
        d.switched = s.rows && s.rows->size() == 2 && s.info.highlighted == 0 &&
                     (*s.rows)[0].title == L"bb" && (*s.rows)[1].title == L"zz" &&
                     g_app.current == d.bb;
        if (s.list) post(s.list, WM_KEYDOWN, VK_ESCAPE, 0, 0);
        d.wait = 4;
        break;
    }
    default: {
        settle();
        d.switchClosed = !folderListWindow() && g_app.current == d.bb;
        compare(9, L"crumb-switch");
        post(d.main, WM_KEYDOWN, VK_F4, 0, 0);  // to "cc\dd", for the list of "cc >"
        d.wait = 3;
        break;
    }
    case 32:
        typePath(d.dd);  // in "cc\dd": "cc >" is there, with dd under it
        d.wait = 5;
        break;
    case 33: {  // the v after "cc": dd, and a folder with a 255-character name
        settle();
        const int cc = crumbOf(L"cc");
        if (g_app.current == d.dd && cc >= g_app.crumbFirst && cc >= 0) {
            const POINT c = middle(g_app.crumbs[cc].more);
            post(d.main, WM_LBUTTONDOWN, MK_LBUTTON, c.x, c.y);
        }
        d.wait = 3;
        break;
    }
    case 34: {
        const Seen s = seeList(-1, -1);
        RECT wr{};
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(s.list ? s.list : d.main, MONITOR_DEFAULTTONEAREST), &mi);
        d.listWidth = s.list && GetWindowRect(s.list, &wr) ? (int)(wr.right - wr.left) : 0;
        d.longList = s.rows && s.rows->size() == 2 && s.info.highlighted == 0 &&
                     (*s.rows)[1].title.size() == 255;
        d.listFits = d.listWidth > 0 && d.listWidth <= (int)(mi.rcWork.right - mi.rcWork.left);
        if (s.list) post(s.list, WM_KEYDOWN, VK_ESCAPE, 0, 0);
        d.wait = 4;
        break;
    }
    case 35:
        settle();
        d.longClosed = !folderListWindow();
        compare(10, L"crumb-long");
        break;
    case 36:  // the pointer is on the ...
        if (g_app.lay.addrMore.right > g_app.lay.addrMore.left)
            onMouseMove(d.main, g_app, middle(g_app.lay.addrMore));
        d.wait = 3;
        break;
    case 37: {  // lit up, dots and all; then the pointer leaves the window
        settle();
        const RECT r = g_app.lay.addrMore;
        const int dot = pxOf(g_app.scale, 2);
        d.dotsHover = frameCount(shownFrame(d.main), r, isFaceHi) == area(r) - 3 * dot * dot;
        keepFrames(L"crumb-dots-hover", shownFrame(d.main), renderedFrame(g_app));
        post(d.main, WM_MOUSELEAVE, 0, 0, 0);
        d.wait = 3;
        break;
    }
    case 38: {
        settle();
        const RECT r = g_app.lay.addrMore;
        const int dot = pxOf(g_app.scale, 2);
        d.dotsHoverCleared = frameCount(shownFrame(d.main), r, isPlainWhite) == area(r) - 3 * dot * dot;
        compare(12, L"crumb-dots");
        post(d.main, WM_KEYDOWN, VK_F4, 0, 0);
        d.wait = 3;
        break;
    }
    case 39:
        typePath(d.wide);  // a folder whose name alone is too long for the bar
        d.wait = 5;
        break;
    case 40: {
        settle();
        const RECT inner = g_app.addressInner();
        const Crumb& last = g_app.crumbs.back();
        const int inset = pxOf(g_app.scale, 5);
        const int dotsW = measureUiText(L"\u2026", (int)(14.6667 * g_app.scale + 0.5));
        d.cutState = g_app.current == d.wide && !g_app.addrEditing && last.title == d.wideName &&
                     last.text.size() >= 2 && last.text != last.title &&
                     last.text.back() == L'\u2026' &&
                     g_app.crumbFirst == (int)g_app.crumbs.size() - 1 &&
                     last.box.left == g_app.lay.addrMore.right &&
                     last.box.right <= inner.right - pxOf(g_app.scale, 20);
        // On screen: the ellipsis is the last thing in the button - its dots
        // sit on the line's floor with white above them, where a W has ink -
        // and after the button there is nothing but blank.
        const RECT dots{last.box.right - inset - dotsW + 1, inner.top, last.box.right - inset,
                        inner.bottom};
        const int floor = inner.bottom - pxOf(g_app.scale, 8);
        const RECT above{dots.left, dots.top, dots.right, floor};
        const RECT onFloor{dots.left, floor, dots.right, inner.bottom};
        const RECT after{last.box.right, inner.top, inner.right, inner.bottom};
        const Frame f = shownFrame(d.main);
        // The dots are a pixel or two of ClearType: ink is anything not white.
        d.cutInk = d.cutState && frameCount(f, above, notWhite) == 0 &&
                   frameCount(f, onFloor, notWhite) >= 3;
        d.cutWhite = d.cutState && after.right - after.left >= pxOf(g_app.scale, 20) &&
                     frameCount(f, after, isPlainWhite) == area(after);
        diagLog(L"cut: box %ld,%ld %ldx%ld dots %ld,%ld %ldx%ld above notWhite %d, floor ink %d, after white %d of %d",
                last.box.left, last.box.top, last.box.right - last.box.left,
                last.box.bottom - last.box.top, dots.left, dots.top, dots.right - dots.left,
                dots.bottom - dots.top, frameCount(f, above, notWhite),
                frameCount(f, onFloor, notWhite), frameCount(f, after, isPlainWhite), area(after));
        compare(13, L"crumb-cut");
        break;
    }
    }
    ++d.step;
    if (d.step < kCrumbTestEnd) later();
}

LRESULT CALLBACK crumbTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kCrumbTestTimer) {
        crumbTestStep(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------- new window test
// A folder opened in a new window, from the real window, by each of the ways
// there are: Ctrl and a click on it in the list, Ctrl and a double click, Ctrl
// and a click on its row in the tree, Ctrl and Enter.  The new window is
// another instance of this program, really started - found by its process,
// read back from the compositor like the test's own window, asked what it
// shows (the layout dump the driver scripts use) and closed.
//
// In the UI test the new instances come up with the test's environment
// (MOC_SETTINGS_FILE, MOC_NO_FOREGROUND): never activated, behind everything.
// The foreground test (--newwin-fg, run by hand where taking the foreground is
// allowed) is the other way round: this process is the foreground process
// there and the new instances come up unactivated (MOC_SHOWNOACTIVATE), and
// what is checked is that the program brings each of them to the front.

constexpr UINT_PTR kNewWinTestTimer = 0x4D48;
constexpr wchar_t kNewWinTestClass[] = L"MyOldComputerNewWinTest";
constexpr int kNewWinScenarios = 4;
constexpr int kNewWinTestEnd = 1 + kNewWinScenarios * 5 + 1;  // the step past the last one

// Every window of this program's class there is, with the process it is of.
BOOL CALLBACK collectProgramWindows(HWND h, LPARAM lp) {
    auto* out = (std::vector<std::pair<HWND, DWORD>>*)lp;
    wchar_t cls[64]{};
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid && GetClassNameW(h, cls, 64) && lstrcmpW(cls, L"MyOldComputerWindow") == 0)
        out->emplace_back(h, pid);
    return TRUE;
}

std::vector<std::pair<HWND, DWORD>> programWindows() {
    std::vector<std::pair<HWND, DWORD>> out;
    EnumWindows(collectProgramWindows, (LPARAM)&out);
    return out;
}

// Whether a process runs this program (not a build of it lying somewhere
// else).
bool sameProgram(DWORD pid) {
    wchar_t mine[MAX_PATH * 2]{}, its[MAX_PATH * 2]{};
    GetModuleFileNameW(nullptr, mine, (DWORD)(sizeof(mine) / sizeof(mine[0])));
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return false;
    DWORD n = (DWORD)(sizeof(its) / sizeof(its[0]));
    const bool same = QueryFullProcessImageNameW(process, 0, its, &n) && _wcsicmp(mine, its) == 0;
    CloseHandle(process);
    return same;
}

// Below the foreground window in the order of the windows, as one that is
// behind everything is.  (No foreground window: nothing to be below.)
bool belowForeground(HWND h) {
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return true;
    for (HWND w = foreground; w; w = GetWindow(w, GW_HWNDNEXT))
        if (w == h) return true;
    return false;
}

// The lines of the layout dump (dumpLayout) that start with `starts`.
std::vector<std::wstring> dumpLines(const wchar_t* starts) {
    std::vector<std::wstring> out;
    wchar_t temp[MAX_PATH]{}, path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    swprintf_s(path, L"%smoc-layout.txt", temp);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"r, ccs=UTF-8") != 0 || !f) return out;
    wchar_t line[4096]{};
    while (fgetws(line, 4096, f))
        if (wcsncmp(line, starts, wcslen(starts)) == 0) out.emplace_back(line);
    fclose(f);
    return out;
}

struct NewWinDrive {
    HWND main = nullptr;
    HWND driver = nullptr;
    bool foreground = false;  // the foreground test: the new windows are to be brought to the front
    int step = 0;
    int wait = 0;      // ticks to let what was started get through first
    int ticks = 0;     // the watchdog's count
    int waiting = 0;   // ticks spent waiting for something
    std::wstring base;                        // the test's window's folder
    std::wstring folder[kNewWinScenarios];    // what each new window is to show
    POINT at[kNewWinScenarios] = {};          // where it is clicked, in the test's window
    POINT off{};                              // where a press on it is let go, off it
    RECT parent{};                            // the test's window on the screen
    int titleBar = 0;  // a title bar, in pixels
    int expected = 0;  // how far down and to the right a new window is to be
    std::vector<DWORD> before;  // the instances of the program there were
    DWORD child = 0;            // the instance of the scenario being watched
    HANDLE process = nullptr;   // it, to wait for its end
    HWND window = nullptr;      // its window
    bool there[kNewWinScenarios] = {};
    bool oneOnly[kNewWinScenarios] = {};
    bool inFolder[kNewWinScenarios] = {};
    bool named[kNewWinScenarios] = {};
    bool placed[kNewWinScenarios] = {};
    bool front[kNewWinScenarios] = {};
    bool gone[kNewWinScenarios] = {};
    std::wstring says[kNewWinScenarios];  // what each says it is in
    int dx[kNewWinScenarios] = {}, dy[kNewWinScenarios] = {};
    SIZE size[kNewWinScenarios] = {};
    bool elsewhere = false;  // let go off the folder: no instance was started
    bool quiet = true;       // never the foreground, always below it (the UI test)
    std::wstring notQuiet;   // ... which of the two it was that it was not
    bool mine = true;        // (the foreground test) the foreground window was this test's each time
    bool stays = false;      // the test's window is still in its folder
    bool baseline = false;
    int stale[3] = {0, 0, 0};
    RECT where[3] = {};
};
NewWinDrive g_newWin;

// The foreground window, for a window of this process - which the foreground
// test needs to be, to hand the foreground on.  A process that is not the
// foreground process is not allowed to take it; while it shares the
// foreground thread's input state (AttachThreadInput), the question is not
// asked.  Only the foreground test does this, where somebody allowed it.
bool takeTheForeground(HWND hwnd) {
    const HWND foreground = GetForegroundWindow();
    if (foreground == hwnd) return true;
    const DWORD mine = GetCurrentThreadId();
    const DWORD theirs = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    if (theirs && theirs != mine) AttachThreadInput(mine, theirs, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (theirs && theirs != mine) AttachThreadInput(mine, theirs, FALSE);
    return GetForegroundWindow() == hwnd;
}

// The instances of this program started since the test began and still showing
// a window.
std::vector<DWORD> instancesStartedSince(const NewWinDrive& d) {
    std::vector<DWORD> out;
    for (const auto& w : programWindows()) {
        if (w.second == GetCurrentProcessId() ||
            std::find(d.before.begin(), d.before.end(), w.second) != d.before.end())
            continue;
        if (std::find(out.begin(), out.end(), w.second) == out.end()) out.push_back(w.second);
    }
    return out;
}

// What starts the new window of scenario k: what the window procedure does
// with the input, called as it calls it.
void triggerNewWindow(const NewWinDrive& d, int k) {
    switch (k) {
    case 0:  // Ctrl and a click on the folder's name
        onLButtonDown(d.main, g_app, d.at[0], true);
        onLButtonUp(d.main, g_app, d.at[0]);
        break;
    case 1:  // Ctrl and a double click on it: one new window, not two
        onLButtonDown(d.main, g_app, d.at[1], true);
        onLButtonUp(d.main, g_app, d.at[1]);
        onDoubleClick(d.main, g_app, d.at[1], true);
        onLButtonUp(d.main, g_app, d.at[1]);
        break;
    case 2:  // Ctrl and a click on a folder's row in the tree
        onLButtonDown(d.main, g_app, d.at[2], true);
        treePressRelease(d.main, g_app, d.at[2]);
        break;
    default:  // the folder selected, Ctrl and Enter
        g_app.selection.clear();
        g_app.selection.insert(d.folder[3]);
        handleKey(d.main, g_app, VK_RETURN, true, false, false);
        break;
    }
}

void newWinTestStep(HWND driver) {
    NewWinDrive& d = g_newWin;
    KillTimer(driver, kNewWinTestTimer);
    if (++d.ticks > 2000) {  // fifty seconds: give up, do not hang
        d.step = kNewWinTestEnd;
        return;
    }
    const auto later = [driver] { SetTimer(driver, kNewWinTestTimer, 25, nullptr); };
    if (d.step >= kNewWinTestEnd) {
        // A new window the test did not mean to leave open.
        if (d.window && IsWindow(d.window)) {
            PostMessageW(d.window, WM_CLOSE, 0, 0);
            later();
        }
        return;
    }
    if (d.wait-- > 0) {
        later();
        return;
    }
    d.wait = 0;
    // Not in the step that waits for the window to be gone: on its way out, a
    // window is out of the order of the windows before it is out of the world.
    const int partOfStep = d.step >= 1 && d.step <= 5 * kNewWinScenarios ? (d.step - 1) % 5 : -1;
    if (d.window && IsWindow(d.window) && !d.foreground && d.quiet && partOfStep != 4) {
        const HWND foreground = GetForegroundWindow();
        if (foreground == d.window || !belowForeground(d.window)) {
            d.quiet = false;
            // What was seen, for the verdict and for the log: which of the two
            // it was, and the windows there were in front of the new one.
            d.notQuiet = foreground == d.window ? L"新窗口占过前台" : L"新窗口跑到了前台窗口前面";
            std::wstring inFront;
            int above = 0;
            for (HWND w = GetWindow(d.window, GW_HWNDPREV); w && above < 400;
                 w = GetWindow(w, GW_HWNDPREV), ++above) {
                if (above >= 6) continue;
                wchar_t cls[64]{};
                GetClassNameW(w, cls, 64);
                DWORD pid = 0;
                GetWindowThreadProcessId(w, &pid);
                inFront += L" | " + std::wstring(cls) + L" pid " + std::to_wstring(pid) +
                            (IsWindowVisible(w) ? L" shown" : L" hidden") +
                            ((GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TOPMOST) ? L" topmost" : L"");
            }
            wchar_t cls[64]{};
            GetClassNameW(foreground, cls, 64);
            DWORD pid = 0;
            GetWindowThreadProcessId(foreground, &pid);
            diagLog(L"newwin not quiet at step %d: new window %p%s, foreground %p class %ls pid %u, "
                    L"%d windows in front of the new one:%ls",
                    d.step, d.window,
                    (GetWindowLongPtrW(d.window, GWL_EXSTYLE) & WS_EX_TOPMOST) ? L" topmost" : L"",
                    foreground, cls, pid, above, inFront.c_str());
        }
    }
    const auto stillWaiting = [&](int limit) {
        if (++d.waiting < limit) {
            later();
            return true;
        }
        return false;
    };
    // Each scenario is one step that starts its new window, then four that
    // watch it: the instance comes up, its window shows, it is read back and
    // closed, it is gone.
    const int k = d.step >= 1 ? (d.step - 1) / 5 : -1;
    const int part = d.step >= 1 ? (d.step - 1) % 5 : 0;
    // (The foreground test) the window to hand the foreground on from has to
    // be the foreground window itself.  It is again by itself once the new
    // window has closed, and is asked to be if it is not.
    const auto backInTheFront = [&] {
        if (GetForegroundWindow() == d.driver) {
            d.waiting = 0;
            return true;
        }
        if (d.waiting == 0 && takeTheForeground(d.driver)) {
            d.waiting = 0;
            return true;
        }
        if (stillWaiting(12)) return false;
        d.waiting = 0;
        d.mine = false;
        return true;
    };

    if (d.step == 0) {
        UpdateWindow(d.main);
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(L"newwin-baseline", shown, fresh);
        d.baseline = frameMismatch(shown, fresh, &d.where[0]) == 0;
        GetWindowRect(d.main, &d.parent);
        // Counted here, not by the code under test: for every other instance
        // of this program that opens where this window does, the new window
        // is one title bar further down and to the right.
        int others = 0;
        for (const auto& w : programWindows()) {
            WINDOWPLACEMENT wp{};
            wp.length = sizeof(wp);
            if (w.first == d.main || !sameProgram(w.second) || !GetWindowPlacement(w.first, &wp))
                continue;
            if (wp.rcNormalPosition.left == d.parent.left && wp.rcNormalPosition.top == d.parent.top)
                ++others;
        }
        d.expected = (others + 1) * d.titleBar;
        for (const auto& w : programWindows()) d.before.push_back(w.second);
        // Ctrl and a press on the folder, let go off it: a click that did not
        // land on the folder opens nothing.
        onLButtonDown(d.main, g_app, d.at[0], true);
        onLButtonUp(d.main, g_app, d.off);
        d.wait = 12;  // a third of a second for an instance that is not to come
        ++d.step;
        if (d.step < kNewWinTestEnd) later();
        return;
    }
    if (k >= kNewWinScenarios) {  // the last step: the test's own window
        UpdateWindow(d.main);
        d.stays = g_app.current == d.base && !g_app.selection.empty() &&
                  fsx::isDirectory(*g_app.selection.begin());
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(L"newwin-end", shown, fresh);
        d.stale[2] = frameMismatch(shown, fresh, &d.where[2]);
        ++d.step;
        if (d.step < kNewWinTestEnd) later();
        return;
    }

    switch (part) {
    case 0: {  // the new window is started
        if (d.step == 1) d.elsewhere = instancesStartedSince(d).empty();
        if (d.foreground && !backInTheFront()) return;
        d.child = 0;
        d.window = nullptr;
        d.process = nullptr;
        d.waiting = 0;
        triggerNewWindow(d, k);
        d.wait = 3;
        break;
    }
    case 1: {  // the new instance comes up
        for (DWORD pid : instancesStartedSince(d)) d.child = pid;
        if (!d.child) {
            if (stillWaiting(300)) return;  // seven and a half seconds
        } else {
            d.process = OpenProcess(SYNCHRONIZE, FALSE, d.child);
        }
        d.there[k] = d.child != 0;
        d.waiting = 0;
        d.wait = 2;
        break;
    }
    case 2: {  // ... and its window is on the screen (in the foreground test: in front)
        for (const auto& w : programWindows())
            if (w.second == d.child && IsWindowVisible(w.first)) d.window = w.first;
        if (!d.window) {
            if (d.child && stillWaiting(300)) return;
        } else if (d.foreground) {
            // Brought to the front within a second and a half.
            if (GetForegroundWindow() != d.window && stillWaiting(60)) return;
            d.front[k] = GetForegroundWindow() == d.window;
        } else {
            d.wait = 4;  // let it paint
        }
        d.waiting = 0;
        d.wait = 2;
        break;
    }
    case 3: {  // where it is and what it shows, read back like the test's own window
        if (d.window) {
            RECT r{};
            GetWindowRect(d.window, &r);
            d.dx[k] = r.left - d.parent.left;
            d.dy[k] = r.top - d.parent.top;
            d.size[k] = SIZE{r.right - r.left, r.bottom - r.top};
            d.placed[k] = d.dx[k] == d.expected && d.dy[k] == d.expected &&
                          d.size[k].cx == d.parent.right - d.parent.left &&
                          d.size[k].cy == d.parent.bottom - d.parent.top;
            // What it says it is in, and whether its address bar shows the way
            // there: the last crumb is the folder, and has its name.
            DWORD_PTR answered = 0;
            const LRESULT asked = SendMessageTimeoutW(d.window, g_dumpMsg, 0, 0, SMTO_ABORTIFHUNG, 5000,
                                                      &answered);
            const auto crumbs = dumpLines(L"crumb ");
            const auto current = dumpLines(L"current ");
            diagLog(L"newwin dump %d: asked %d, %d crumb lines, current [%ls]", k, (int)asked,
                    (int)crumbs.size(), current.empty() ? L"" : current.front().c_str());
            RECT box{};
            if (!crumbs.empty()) {
                int i = 0, x = 0, y = 0, w = 0, h = 0;
                if (swscanf_s(crumbs.back().c_str(), L"crumb %d %d,%d %dx%d", &i, &x, &y, &w, &h) ==
                    5)
                    box = RECT{x, y, x + w, y + h};
            }
            if (!current.empty()) d.says[k] = current.front().substr(8);
            while (!d.says[k].empty() &&
                   (d.says[k].back() == L'\n' || d.says[k].back() == L'\r'))
                d.says[k].pop_back();
            d.inFolder[k] = _wcsicmp(d.says[k].c_str(), d.folder[k].c_str()) == 0;
            const Frame f = shownFrame(d.window);
            d.named[k] = box.right > box.left && frameCount(f, box, blackInk) >= 10;
            keepFrames(k == 0 ? L"newwin-by-click" : L"newwin-window", f, f);
            // Only one new window for this: by now a second one would have a window too.
            d.oneOnly[k] = instancesStartedSince(d).size() == 1;
            PostMessageW(d.window, WM_CLOSE, 0, 0);
        }
        d.wait = 2;
        break;
    }
    default: {  // closed: it is gone
        const bool ended = d.process ? WaitForSingleObject(d.process, 0) == WAIT_OBJECT_0
                                     : !d.window || !IsWindow(d.window);
        if (!ended && stillWaiting(300)) return;
        d.gone[k] = ended;
        if (d.process) {
            CloseHandle(d.process);
            d.process = nullptr;
        }
        d.child = 0;
        d.window = nullptr;
        d.waiting = 0;
        d.wait = 3;
        break;
    }
    }
    ++d.step;
    if (d.step < kNewWinTestEnd) later();
}

LRESULT CALLBACK newWinTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kNewWinTestTimer) {
        newWinTestStep(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The test in the real window.  `listDir` is what the test's window shows and
// `folder` a folder directly inside it, sorted first.
void newWindowTest(const std::wstring& listDir, const std::wstring& folder,
                   const std::function<void(const wchar_t*, bool)>& check) {
    const bool foreground = g_newWin.foreground;
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = newWinTestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kNewWinTestClass;
    RegisterClassExW(&wc);
    // In the UI test this window is never shown.  The foreground test shows
    // it - a small one, for the few seconds the test takes - because the
    // process that hands the foreground on has to be the foreground process.
    HWND driver = CreateWindowExW(0, kNewWinTestClass, L"多开测试：几秒钟后自己关上", WS_OVERLAPPED, 0, 0,
                                  160, 70, nullptr, nullptr, wc.hInstance, nullptr);
    registerMainClass(wc.hInstance);
    HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", L"new window test",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver, nullptr,
                                wc.hInstance, nullptr);
    g_newWin = NewWinDrive{};
    g_newWin.main = main;
    g_newWin.driver = driver;
    g_newWin.foreground = foreground;
    g_newWin.base = listDir;
    g_newWin.titleBar = GetSystemMetrics(SM_CYCAPTION);
    if (main) {
        g_app.hwnd = main;
        g_app.scale = dpiScaleOfWindow(main);
        g_app.init(main);
        g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
        g_app.sidebarVisible = true;
        g_app.loadDirectory(listDir, false);
        g_app.setViewMode(ViewMode::Details);
        placeWindow(main, 0, 0, g_app.width, g_app.height);
        // The settings as last written name one place; the window is then
        // moved.  A new window is to take the place the window is in now,
        // which only writing them again can tell it.
        g_app.saveSettings();
        placeWindow(main, 40, 40, g_app.width, g_app.height);
        ShowWindow(main, SW_SHOWNOACTIVATE);
        SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(main);
        // The folder's name in the list: the first row.
        const RECT r = g_app.listRowRect(0);
        const POINT on{r.left + 10, (r.top + r.bottom) / 2};
        for (int k = 0; k < kNewWinScenarios; ++k) g_newWin.at[k] = on;
        g_newWin.off = POINT{g_app.lay.listViewport.left + pxOf(g_app.scale, 30),
                             g_app.lay.listViewport.bottom - pxOf(g_app.scale, 6)};
        g_newWin.folder[0] = g_newWin.folder[1] = g_newWin.folder[3] = folder;
        // In the tree: the first row in view that is a folder - its title, not its +.
        const RECT vp = g_app.lay.treeViewport;
        for (int i = 0; i < (int)g_app.treeFlat.size(); ++i) {
            const TreeNode* n = g_app.treeFlat[i];
            const RECT row = g_app.treeRowRect(i);
            if (n->path.empty() || !fsx::isDirectory(n->path) || row.top < vp.top || row.bottom > vp.bottom)
                continue;
            g_newWin.folder[2] = n->path;
            g_newWin.at[2] = POINT{row.right - pxOf(g_app.scale, 20), (row.top + row.bottom) / 2};
            break;
        }
        if (foreground) ShowWindow(driver, SW_SHOWNORMAL);
        MSG msg;
        for (SetTimer(driver, kNewWinTestTimer, 60, nullptr);
             g_newWin.step < kNewWinTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(driver, kNewWinTestTimer);
        // An instance the test did not close (something went wrong): it is not
        // left running.
        for (DWORD pid : instancesStartedSince(g_newWin))
            if (HWND stray = windowOfProcess(pid)) PostMessageW(stray, WM_CLOSE, 0, 0);
        if (foreground && IsWindowVisible(driver)) ShowWindow(driver, SW_HIDE);
        g_app.hwnd = nullptr;  // the edit is the window's child and goes with it
        DestroyWindow(main);
        PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        g_app.addrEdit = nullptr;
        g_app.addrEditing = false;
        if (g_app.addrFont) {
            DeleteObject(g_app.addrFont);
            g_app.addrFont = nullptr;
        }
    }
    DestroyWindow(driver);

    std::wstring notes;
    if (!main) notes += L"窗口没建出来";
    else if (g_newWin.ticks > 2000) notes += L"超时";
    if (!g_newWin.baseline)
        notes += L"基准不符，范围 " + std::to_wstring(g_newWin.where[0].left) + L"," +
                 std::to_wstring(g_newWin.where[0].top) + L" " +
                 std::to_wstring(g_newWin.where[0].right - g_newWin.where[0].left) + L"x" +
                 std::to_wstring(g_newWin.where[0].bottom - g_newWin.where[0].top);
    const auto of = [](int v) { return std::to_wstring(v); };
    // A verdict that says what was seen of the k-th new window.
    const auto verdict = [&](const std::wstring& name, int k, const std::wstring& why, bool ok) {
        std::wstring label = name;
        if (!notes.empty()) label += L"：" + notes;
        else if (!g_newWin.there[k]) label += L"：新实例没有启动";
        else if (!ok) label += L"：" + why;
        check(label.c_str(), label == name);
    };
    const wchar_t* const how[kNewWinScenarios] = {
        L"按住 Ctrl 单击列表里的一个文件夹", L"按住 Ctrl 双击列表里的一个文件夹",
        L"按住 Ctrl 单击文件夹树里的一项", L"选中的文件夹按 Ctrl 和回车"};
    check(L"（测试基准）多开：屏幕上的画面读得回来，且与按状态渲染的一致", g_newWin.baseline);
    {
        const std::wstring name = L"按住 Ctrl 按下一个文件夹、在别处松开：不开新窗口";
        std::wstring s = name;
        if (!notes.empty()) s += L"：" + notes;
        else if (!g_newWin.elsewhere) s += L"：却启动了新实例";
        check(s.c_str(), s == name);
    }
    for (int k = 0; k < kNewWinScenarios; ++k) {
        std::wstring name = std::wstring(how[k]) + L"：这个程序的另一个实例打开了它";
        if (k == 1) name += L"，只开一个";
        if (k == 0) name += L"，原窗口不动";
        std::wstring why;
        if (k == 2 && g_newWin.folder[2].empty()) why = L"文件夹树里没有可点的文件夹";
        else if (!g_newWin.inFolder[k])
            why = L"新窗口在“" + g_newWin.says[k] + L"”，该在“" + g_newWin.folder[k] + L"”";
        else if (!g_newWin.named[k]) why = L"新窗口的地址栏上没有它的名字";
        else if (k == 1 && !g_newWin.oneOnly[k]) why = L"开出了不止一个";
        else if (k == 0 && !g_newWin.stays) why = L"原窗口离开了它自己的文件夹";
        else if (k == 0 && g_newWin.stale[2])
            why = L"原窗口屏幕上有 " + of(g_newWin.stale[2]) + L" 个像素与状态不符";
        verdict(name, k, why,
                g_newWin.inFolder[k] && g_newWin.named[k] && (k != 1 || g_newWin.oneOnly[k]) &&
                    (k != 0 || (g_newWin.stays && !g_newWin.stale[2])));
    }
    for (int k = 0; k < kNewWinScenarios; ++k) {
        const std::wstring name =
            std::wstring(how[k]) + L"：新窗口和原窗口一样大，开在它右下一个标题栏处，不叠在它上面";
        const std::wstring why =
            L"偏了 " + of(g_newWin.dx[k]) + L"," + of(g_newWin.dy[k]) + L"（该是 " + of(g_newWin.expected) +
            L"," + of(g_newWin.expected) + L"），大小 " + of(g_newWin.size[k].cx) + L"x" +
            of(g_newWin.size[k].cy) + L"（原窗口 " + of(g_newWin.parent.right - g_newWin.parent.left) + L"x" +
            of(g_newWin.parent.bottom - g_newWin.parent.top) + L"）";
        verdict(name, k, why, g_newWin.placed[k]);
    }
    for (int k = 0; k < kNewWinScenarios; ++k) {
        const std::wstring name = foreground
                                      ? std::wstring(how[k]) + L"：新窗口弹出来就在最前面，关得上"
                                      : std::wstring(how[k]) + L"：新窗口关得上；开着的时候没占过前台，一直在别的窗口后面";
        std::wstring why;
        if (!g_newWin.gone[k]) why = L"关不上";
        else if (foreground && !g_newWin.mine) why = L"测试窗口自己没能回到前台，交不出前台";
        else if (foreground) why = L"新窗口没有成为前台窗口";
        else why = g_newWin.notQuiet.empty() ? L"它占过前台，或跑到了别的窗口前面" : g_newWin.notQuiet;
        verdict(name, k, why, g_newWin.gone[k] && (foreground ? g_newWin.front[k] : g_newWin.quiet));
    }
}

// The foreground test on its own (--newwin-fg): this process takes the
// foreground for a few seconds, so it is run by hand, where that is allowed.
int runNewWindowForegroundTest() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring base = std::wstring(temp) + L"moc-newwin-" + std::to_wstring(GetTickCount64());
    const std::wstring folder = base + L"\\aa";
    CreateDirectoryW(base.c_str(), nullptr);
    CreateDirectoryW(folder.c_str(), nullptr);
    std::vector<std::pair<std::wstring, bool>> results;
    const auto write = [&results, temp] {
        std::wstring out;
        for (const auto& r : results) {
            if (!out.empty()) out += L" | ";
            out += r.first + (r.second ? L" OK" : L" FAIL");
        }
        FILE* f = nullptr;
        const std::wstring path = std::wstring(temp) + L"moc-newwin-fg.txt";
        if (_wfopen_s(&f, path.c_str(), L"w, ccs=UTF-8") == 0 && f) {
            fwprintf(f, L"%ls\n", out.c_str());
            fclose(f);
        }
    };
    g_newWin.foreground = true;
    newWindowTest(base, folder,
                  [&results, &write](const wchar_t* name, bool ok) {
                      results.push_back({name, ok});
                      write();
                  });
    std::error_code ignored;
    std::filesystem::remove_all(base, ignored);
    int failed = 0;
    for (const auto& r : results) failed += r.second ? 0 : 1;
    return failed;
}

// ---------------------------------------------------------- folder colour test
// A folder's colour chosen from its real context menu, in the real window:
// the menu the shell builds for the folder with our items in it, shown by our
// own Win98-style menu.  The pointer is moved onto 文件夹颜色, its submenu is
// read off the screen (the colour boxes, each in its colour), a colour is
// clicked - and the folder's icon is that colour on screen: in the list, as
// a big icon, in the tree.  Then 默认颜色 puts it back.  Last, Ctrl+W closes
// the window.

constexpr UINT_PTR kColourTestTimer = 0x4D4B;
constexpr wchar_t kColourTestClass[] = L"MyOldComputerColourTest";
constexpr int kColourTestEnd = 12;  // the step past the last one

// What the screen shows at a client point of a window.
COLORREF screenPixel(HWND window, int x, int y) {
    POINT p{x, y};
    ClientToScreen(window, &p);
    HDC screen = GetDC(nullptr);
    const COLORREF px = GetPixel(screen, p.x, p.y);
    ReleaseDC(nullptr, screen);
    return px & 0x00FFFFFF;
}

// What the screen shows in a rectangle of the screen: how many pixels are of
// the face colour, and how many are black (text) - every other pixel counted,
// both ways.  Read from the display itself (a popup that is on top of
// everything shows there literally).  Either count can be left out.
void screenTally(const RECT& screen, int* face, int* ink) {
    HDC display = GetDC(nullptr);
    int ofFace = 0, black = 0;
    for (LONG y = screen.top; y < screen.bottom; y += 2)
        for (LONG x = screen.left; x < screen.right; x += 2) {
            const COLORREF px = GetPixel(display, x, y) & 0xFFFFFF;
            ofFace += px == Theme::face;
            black += px == Theme::text;
        }
    ReleaseDC(nullptr, display);
    if (face) *face = ofFace;
    if (ink) *ink = black;
}

// The Win98 menu's rows: 22 logical pixels from 3 logical pixels down.
int menuRowCentre(double scale, int row) {
    return pxOf(scale, 3) + row * pxOf(scale, 22) + pxOf(scale, 22) / 2;
}

// A picture of what the screen shows in a rectangle, kept with the frames
// (MOC_DEBUG).
void keepScreen(const wchar_t* name, const RECT& screen) {
    wchar_t debug[8]{};
    if (!GetEnvironmentVariableW(L"MOC_DEBUG", debug, 8)) return;
    const int w = (int)(screen.right - screen.left), h = (int)(screen.bottom - screen.top);
    if (w <= 0 || h <= 0) return;
    HDC display = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(display);
    HBITMAP bmp = CreateCompatibleBitmap(display, w, h);
    HGDIOBJ old = SelectObject(dc, bmp);
    BitBlt(dc, 0, 0, w, h, display, screen.left, screen.top, SRCCOPY);
    GdiFlush();
    Frame f;
    f.w = w;
    f.h = h;
    readFrame(dc, bmp, f);
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(nullptr, display);
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const std::wstring dir = std::wstring(temp) + L"moc-shots";
    CreateDirectoryW(dir.c_str(), nullptr);
    writePng(dir + L"\\screen-" + name + L".png", f.px.data(), f.w, f.h);
}

// The pixels of an icon, BGRA, not premultiplied.
std::vector<unsigned char> iconPixels(HICON icon) {
    ICONINFO info{};
    BITMAP bm{};
    std::vector<unsigned char> out;
    if (!icon || !GetIconInfo(icon, &info)) return out;
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bm), &bm) && bm.bmBitsPixel == 32 &&
        bm.bmWidth > 0 && bm.bmHeight > 0) {
        BITMAPINFOHEADER header{};
        header.biSize = sizeof(header);
        header.biWidth = bm.bmWidth;
        header.biHeight = -bm.bmHeight;
        header.biPlanes = 1;
        header.biBitCount = 32;
        out.resize((size_t)bm.bmWidth * bm.bmHeight * 4);
        HDC dc = CreateCompatibleDC(nullptr);
        GetDIBits(dc, info.hbmColor, 0, bm.bmHeight, out.data(), (BITMAPINFO*)&header,
                  DIB_RGB_COLORS);
        DeleteDC(dc);
    }
    if (info.hbmColor) DeleteObject(info.hbmColor);
    if (info.hbmMask) DeleteObject(info.hbmMask);
    return out;
}

// The red a folder was given, as the tinted icon shows it.
bool isFolderRed(COLORREF px) {
    return GetRValue(px) >= 150 && GetGValue(px) <= 70 && GetBValue(px) <= 70;
}

// ... and its blue.
bool isFolderBlue(COLORREF px) {
    return GetBValue(px) >= 150 && GetRValue(px) <= 70 && GetGValue(px) <= 110;
}

struct ColourDrive {
    HWND main = nullptr;
    int step = 0;
    int wait = 0;
    int ticks = 0;
    std::wstring folder;       // the folder that is given a colour
    POINT menuAt{};            // where its menu is opened, on the screen
    shellmenu::ViewState state;
    shellmenu::Result picked;  // what the menu returned
    bool menuDone = false;     // ... it has returned
    HWND sub = nullptr;        // the colour submenu's window
    int rowTried = 0;          // the root's row the pointer was moved to
    int rowTicks = 0;          // ... how long its submenu has been waited for
    bool menuFound = false;    // 文件夹颜色's submenu was there
    bool swatches = true;      // ... with its eight colour boxes on screen, each in its colour
    bool pickedRed = false, pickedDefault = false;
    int backWaits = 0;  // how long the step has waited for a menu to come back
    int redBefore = 1, redInList = 0, redInIcons = 0, redInTree = 0, blueInTree = 0, redAfter = 1;
    bool baseline = false;
    bool closed = false;       // Ctrl+W closed the window
    int stale = 0;
    RECT where{};
};
ColourDrive g_colour;

void colourTestStep(HWND driver) {
    ColourDrive& d = g_colour;
    KillTimer(driver, kColourTestTimer);
    if (++d.ticks > 600) {  // fifteen seconds: give up, do not hang
        d.step = kColourTestEnd;
        menuClose();
        return;
    }
    const auto later = [driver] { SetTimer(driver, kColourTestTimer, 40, nullptr); };
    if (d.step >= kColourTestEnd) {
        menuClose();
        return;
    }
    if (d.wait-- > 0) {
        later();
        return;
    }
    d.wait = 0;
    const auto post = [](HWND w, UINT msg, int x, int y) {
        PostMessageW(w, msg, msg == WM_LBUTTONDOWN ? MK_LBUTTON : 0, MAKELPARAM((short)x, (short)y));
    };
    const double scale = g_app.scale;
    const int swatchX = pxOf(scale, 3) + pxOf(scale, 8);  // inside a colour box
    const int textX = pxOf(scale, 3) + pxOf(scale, 60);   // on an item's text
    // The folder's icon where it is on screen, and how red it is.
    const auto redIn = [](const RECT& icon) {
        return frameCount(shownFrame(g_colour.main), icon, isFolderRed);
    };
    // ... its row in the tree: the row is looked up, and says so if there is
    // none; how many of its icon's pixels `take` counts.
    const auto treeIn = [&](bool (*take)(COLORREF)) {
        for (int i = 0; i < (int)g_app.treeFlat.size(); ++i) {
            const TreeNode* n = g_app.treeFlat[i];
            if (_wcsicmp(n->path.c_str(), d.folder.c_str()) != 0) continue;
            const RECT r = g_app.treeRowRect(i);
            const int x = r.left + pxOf(scale, n->depth * 14 + 3) + pxOf(scale, 13);
            const RECT icon{x, r.top + pxOf(scale, 2), x + pxOf(scale, 16),
                            r.top + pxOf(scale, 2) + pxOf(scale, 16)};
            const RECT vp = g_app.lay.treeViewport;
            diagLog(L"colour test: the folder's tree row is %d at %d,%d %dx%d, depth %d; the tree shows %d..%d",
                    i, icon.left, icon.top, (int)(icon.right - icon.left), (int)(icon.bottom - icon.top),
                    n->depth, vp.top, vp.bottom);
            return frameCount(shownFrame(d.main), icon, take);
        }
        diagLog(L"colour test: the folder has no row in the tree (%d rows)", (int)g_app.treeFlat.size());
        return 0;
    };
    // Opens the folder's real menu and returns what was picked in it (the
    // steps that run while it is up are the ones below).
    const auto openMenu = [&] {
        RECT work{};
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(d.main, MONITOR_DEFAULTTONEAREST), &mi);
        const HWND foreground = GetForegroundWindow();
        d.menuDone = false;
        d.sub = nullptr;
        d.rowTried = 0;
        diagLog(L"colour test: opening the menu");
        SetTimer(driver, kColourTestTimer, 150, nullptr);
        d.picked = shellmenu::show(d.main, fsx::parentPath(d.folder), {d.folder}, d.menuAt, d.state);
        diagLog(L"colour test: the menu returned %d, colour %d", (int)d.picked.command,
                d.picked.colour);
        if (foreground) SetForegroundWindow(foreground);
        KillTimer(driver, kColourTestTimer);
        // What the window procedure does with it.
        applyMenuChoice(d.main, g_app, {d.folder}, d.picked);
        d.menuDone = true;
        repaint(d.main);
        UpdateWindow(d.main);
        d.wait = 3;
    };

    switch (d.step) {
    case 0: {
        UpdateWindow(d.main);
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(L"colour-baseline", shown, fresh);
        d.stale = frameMismatch(shown, fresh, &d.where);
        d.baseline = d.stale == 0;
        // The folder's icon in the first row of the list, before: not red.
        const RECT r = g_app.listRowRect(0);
        const int s16 = pxOf(scale, 16);
        d.redBefore = redIn(RECT{r.left + pxOf(scale, 3), r.top + pxOf(scale, 2),
                                 r.left + pxOf(scale, 3) + s16, r.top + pxOf(scale, 2) + s16});
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(d.main, MONITOR_DEFAULTTONEAREST), &mi);
        d.menuAt = POINT{mi.rcWork.left + 40, mi.rcWork.top + 40};
        d.state = shellmenu::ViewState{};
        d.step = 1;  // the timer goes on inside the menu's loop: those are the steps that drive it
        openMenu();
        // The steps that ran while the menu was up have moved `step` on.
        d.step = 4;
        later();
        return;
    }
    case 1: {  // the pointer goes to the next row of the menu
        HWND root = nullptr;
        menuWindows(&root);
        if (!root) {
            d.step = kColourTestEnd;
            return;
        }
        d.rowTicks = 0;
        post(root, WM_MOUSEMOVE, textX, menuRowCentre(scale, d.rowTried));
        d.wait = 2;  // the submenu opens at once; let it paint
        break;
    }
    case 2: {  // ... and opened the colour menu?
        HWND root = nullptr;
        const std::vector<HWND> wins = menuWindows(&root);
        for (HWND w : wins) {
            if (w == root) continue;
            if (screenPixel(w, swatchX, menuRowCentre(scale, 0)) == foldercolours::colour(0).rgb)
                d.sub = w;
        }
        if (!d.sub) {
            // A submenu opens at once, but is painted a moment later: each row
            // is given a moment before the next one is tried.
            if (++d.rowTicks < 8) d.step = 1;  // the same row, looked at again
            else if (++d.rowTried < 14) d.step = 0;  // the next row down
            else {
                // Not in any row: the menu is closed (it would otherwise stay
                // up for good - no step is left to close it), and counts for nothing.
                d.step = kColourTestEnd;
                menuClose();
                return;
            }
            break;
        }
        d.menuFound = true;
        d.swatches = true;
        for (int i = 0; i < foldercolours::kCount; ++i)
            d.swatches = d.swatches &&
                         screenPixel(d.sub, swatchX, menuRowCentre(scale, i)) ==
                             foldercolours::colour(i).rgb;
        // Kept with the frames: the menu and its colour submenu as they are on screen.
        RECT a{}, b{};
        GetWindowRect(root, &a);
        GetWindowRect(d.sub, &b);
        keepScreen(d.pickedRed ? L"folder-colour-default" : L"folder-colour-menu",
                   RECT{std::min(a.left, b.left), std::min(a.top, b.top),
                        std::max(a.right, b.right), std::max(a.bottom, b.bottom)});
        // A click on a colour: red the first time, 默认颜色 (the row after the
        // last colour) the second.
        post(d.sub, WM_LBUTTONDOWN, textX,
             menuRowCentre(scale, d.pickedRed ? foldercolours::kCount : 0));
        d.wait = 2;
        break;
    }
    case 3: {  // the other half of the click: the menu closes with that colour
        if (d.sub && IsWindow(d.sub))
            post(d.sub, WM_LBUTTONUP, textX,
                 menuRowCentre(scale, d.pickedRed ? foldercolours::kCount : 0));
        d.wait = 12;  // for the menu to close and openMenu to return
        break;
    }
    case 4: {  // back from the first menu: red in the list, as it is on screen
        if (!d.menuDone) {
            // The menu is not back yet: on a machine that is busy, the click
            // takes a while to be seen to.  It is given a few seconds.
            if (++d.backWaits < 150) {
                later();
                return;
            }
            menuClose();  // the click did not close it: it is closed, and counts for nothing
            d.wait = 6;
            break;
        }
        d.backWaits = 0;
        d.pickedRed = d.picked.command == shellmenu::Command::FolderColour && d.picked.colour == 0;
        const RECT r = g_app.listRowRect(0);
        const int s16 = pxOf(scale, 16);
        d.redInList = redIn(RECT{r.left + pxOf(scale, 3), r.top + pxOf(scale, 2),
                                 r.left + pxOf(scale, 3) + s16, r.top + pxOf(scale, 2) + s16});
        g_app.setViewMode(ViewMode::Icons);
        repaint(d.main);
        UpdateWindow(d.main);
        d.wait = 2;
        break;
    }
    case 5: {  // ... as a big icon
        const RECT r = g_app.gridCellRect(0);
        const int side = pxOf(scale, 48);
        d.redInIcons = redIn(RECT{(r.left + r.right) / 2 - side / 2, r.top + pxOf(scale, 4),
                                  (r.left + r.right) / 2 - side / 2 + side,
                                  r.top + pxOf(scale, 4) + side});
        // Into the folder: the tree shows its row now.
        g_app.setViewMode(ViewMode::Details);
        g_app.loadDirectory(d.folder, false);
        repaint(d.main);
        UpdateWindow(d.main);
        d.wait = 2;
        break;
    }
    case 6: {  // ... and in the tree, on its row
        d.redInTree = treeIn(isFolderRed);
        keepFrames(L"colour-tree", shownFrame(d.main), renderedFrame(g_app));
        // Another window of this program gives the folder another colour: the
        // file changes behind this window's back.  Coming to the front
        // (WM_ACTIVATE, which reads the file again), it shows that colour.
        std::wstring lower = d.folder;
        for (auto& ch : lower) ch = (wchar_t)towlower(ch);
        wchar_t entry[2048]{};
        swprintf_s(entry, L"4|%ls", lower.c_str());
        WritePrivateProfileStringW(L"folders", L"f0", entry, foldercolours::path().c_str());
        SendMessageW(d.main, WM_ACTIVATE, WA_ACTIVE, 0);
        UpdateWindow(d.main);
        d.blueInTree = treeIn(isFolderBlue);
        // The menu again: the folder is coloured now, and is to be given none.
        d.state.folderColour = foldercolours::of(d.folder);
        d.step = 1;
        openMenu();
        d.step = 7;
        later();
        return;
    }
    case 7: {  // back from the second menu: the tree's icon is the yellow one again
        d.pickedDefault = d.picked.command == shellmenu::Command::FolderColour &&
                          d.picked.colour == foldercolours::kDefault;
        d.redAfter = treeIn(isFolderRed) + treeIn(isFolderBlue);
        // Ctrl+W: the window closes (as onKeyDown hands the key on).  The close
        // is posted: it happens once this step has returned.
        handleKey(d.main, g_app, 'W', true, false, false);
        d.wait = 2;
        break;
    }
    default: {  // still there after Ctrl+W: it did not close
        d.step = kColourTestEnd;
        return;
    }
    }
    ++d.step;
    if (d.step < kColourTestEnd) later();
}

LRESULT CALLBACK colourTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kColourTestTimer) {
        colourTestStep(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The test in the real window: the menu of `folder` (a folder directly inside
// the folder the window shows, sorted first).
void folderColourTest(const std::wstring& listDir, const std::wstring& folder,
                      const std::function<void(const wchar_t*, bool)>& check) {
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = colourTestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kColourTestClass;
    RegisterClassExW(&wc);
    HWND driver = CreateWindowExW(0, kColourTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100,
                                  nullptr, nullptr, wc.hInstance, nullptr);  // never shown
    registerMainClass(wc.hInstance);
    HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", L"folder colour test",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver, nullptr,
                                wc.hInstance, nullptr);
    g_colour = ColourDrive{};
    g_colour.main = main;
    g_colour.folder = folder;
    if (main) {
        g_app.hwnd = main;
        g_app.scale = dpiScaleOfWindow(main);
        g_app.init(main);
        // The test's folder is under AppData, a hidden one: the tree can only
        // show the way to it when hidden folders are shown.
        g_app.showHidden = true;
        g_app.buildTree();
        g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
        g_app.sidebarVisible = true;
        g_app.loadDirectory(listDir, false);
        g_app.setViewMode(ViewMode::Details);
        placeWindow(main, 0, 0, g_app.width, g_app.height);
        ShowWindow(main, SW_SHOWNOACTIVATE);
        SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(main);
        MSG msg;
        for (SetTimer(driver, kColourTestTimer, 60, nullptr);
             g_colour.step < kColourTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(driver, kColourTestTimer);
        g_colour.closed = !IsWindow(main);
        if (IsWindow(main)) {
            g_app.hwnd = nullptr;  // the edit is the window's child and goes with it
            DestroyWindow(main);
        }
        PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        g_app.hwnd = nullptr;
        g_app.addrEdit = nullptr;
        g_app.addrEditing = false;
        if (g_app.addrFont) {
            DeleteObject(g_app.addrFont);
            g_app.addrFont = nullptr;
        }
    }
    DestroyWindow(driver);

    std::wstring notes;
    if (!main) notes += L"窗口没建出来";
    else if (g_colour.ticks > 600) notes += L"超时";
    if (!g_colour.baseline)
        notes += L"基准不符，范围 " + std::to_wstring(g_colour.where.left) + L"," +
                 std::to_wstring(g_colour.where.top) + L" " +
                 std::to_wstring(g_colour.where.right - g_colour.where.left) + L"x" +
                 std::to_wstring(g_colour.where.bottom - g_colour.where.top);
    const auto of = [](int v) { return std::to_wstring(v); };
    const auto verdict = [&](const std::wstring& name, const std::wstring& why, bool ok) {
        std::wstring label = name;
        if (!notes.empty()) label += L"：" + notes;
        else if (!ok) label += L"：" + why;
        check(label.c_str(), label == name);
    };
    check(L"（测试基准）文件夹颜色：屏幕上的画面读得回来，且与按状态渲染的一致", g_colour.baseline);
    verdict(L"右键一个文件夹的菜单里有“文件夹颜色”，它的子菜单在屏幕上：八个色块，各自是那个颜色",
            g_colour.menuFound ? L"有色块不是自己的颜色" : L"菜单里没有找到“文件夹颜色”的子菜单",
            g_colour.menuFound && g_colour.swatches);
    verdict(L"在子菜单里点“红色”：列表里那个文件夹的图标在屏幕上就是红色的",
            !g_colour.menuDone ? L"菜单没有关上"
            : !g_colour.pickedRed ? L"菜单交回的不是“红色”（命令 " +
                                           of((int)g_colour.picked.command) + L"，颜色 " +
                                           of(g_colour.picked.colour) + L"）"
                                  : L"图标上有 " + of(g_colour.redInList) + L" 个红色像素（原来 " +
                                        of(g_colour.redBefore) + L" 个）",
            g_colour.pickedRed && g_colour.redBefore == 0 && g_colour.redInList >= 10);
    verdict(L"同一个文件夹在大图标视图里也是红色的", L"图标上有 " + of(g_colour.redInIcons) + L" 个红色像素",
            g_colour.pickedRed && g_colour.redInIcons >= 30);
    verdict(L"在文件夹树里，它那一行的图标也是红色的",
            L"图标上有 " + of(g_colour.redInTree) + L" 个红色像素",
            g_colour.pickedRed && g_colour.redInTree >= 10);
    verdict(L"另一个窗口把这个文件夹改成了蓝色：这个窗口回到前台时，文件夹树里的图标就是蓝色的",
            L"图标上有 " + of(g_colour.blueInTree) + L" 个蓝色像素", g_colour.blueInTree >= 10);
    verdict(L"再点“默认颜色”：文件夹树里的图标回到原来的颜色",
            !g_colour.pickedDefault ? L"菜单交回的不是“默认颜色”"
                                    : L"图标上还有 " + of(g_colour.redAfter) + L" 个红色像素",
            g_colour.pickedDefault && g_colour.redAfter == 0);
    verdict(L"按 Ctrl+W：窗口关上了", L"窗口还在", g_colour.closed);
}

// ---------------------------------------------------------- menus-ahead test
// The shell's menus are built ahead on a thread of this program's own
// (shellmenu.h), and a right-click shows what was built.  In the real window,
// on a folder made for it.  The menus are there soon after the folder is
// opened, and right-clicks - on the background, on a file whose menu was
// built, on others of its kind whose were not - have their menus up at once.
// What is shown for a file nothing was built for is read off the screen: the
// shell's part of the menu of another of its kind, with this program's own
// items for the file itself; and "属性" picked in it opens the properties of
// that very file.  A command that is not where it was said to be runs nothing
// (the menu of the thing itself is shown in its place); one that has moved
// but says the same is found and runs.  The background's menus go when the
// clipboard changes; the pointer resting on a file has its menu built; a
// right-click waited for is not shown after the user does anything else; and
// with it all switched off, the menu is built on the window's thread as it was.

constexpr UINT_PTR kAheadTestTimer = 0x4D4B;
constexpr wchar_t kAheadTestClass[] = L"MyOldComputerAheadTest";
constexpr int kAheadTestEnd = 65;  // the step past the last one
constexpr int kAheadSoonMs = 150;  // "at once" (ms), with room for a busy machine: twice the processors that were busy (per cent) more

struct AheadDrive {
    HWND main = nullptr;
    int step = 0;
    int wait = 0;  // ticks to let pass before the step goes on
    int ticks = 0;
    std::wstring dir;
    double t0 = 0;  // when the last right-click was posted (ms); the menu came up at menuShownAt()
    int builtAfterMs = -1;  // after the folder opened, its menus were built
    int msBackground = -1, msOwn = -1, msAlike = -1;
    // ... and how much of the processors was busy from the click to the menu (per cent)
    int busyBackground = 0, busyOwn = 0, busyAlike = 0, busyFourth = 0, busyStale = 0, busyStaleBg = 0;
    FILETIME idleAt{}, kernelAt{};  // the processors' times when the click was posted
    bool backgroundModel = true, ownModel = true, alikeModel = true,
         thirdModel = true;  // what each was shown from (true: not its own menu)
    int topCount = 0;
    bool sameTop = false, sameTree = false;  // the menu of a2.txt itself says what the one shown for it did
    int openWith = 0;                       // what is in 打开方式 of a menu built ahead
    std::vector<std::wstring> shown;  // what the popup on screen said at once, for a3.txt
    std::vector<shellmenu::Item> standIn;  // ... and the menu it was shown from (built for another)
    bool sameShown = false;  // ... which is what it is made of
    bool differed = false;   // the menu of a3.txt itself says something else than the stand-in did
    bool ownOnScreen = false;  // ... and then that one is what is on screen
    int putUps = 0;           // menus put up since the click on a3.txt (once: none was taken down for another)
    bool fourthModel = true;  // a4.txt, nothing built for it: what came up stood for another of its kind
    int msFourth = -1, putUpsFourth = 0, putUpsFourthFrom = 0;  // ... how soon it was up; menus put up since
    // ... what 发送到 has on screen: as the stand-in's, open when its own menu came in (and what
    // the one it is shown from has in it then); and again, once the menu is its own's (and what
    // its own menu has in it then)
    int fourthSendTo = -1, fourthSendToStandIn = -2, fourthSendToAgain = -2, fourthSendToOwn = -2;
    bool fourthKept = false, fourthStillStandIn = false;  // ... the open submenu was not taken, the menu not changed under it
    bool fourthOwn = false;   // ... what is on screen for it, the submenu closed, is its own menu's say
    // Menus built "long ago" (every one counts as that: g_menuAgainMs = 0):
    // shown at once all the same, built again behind, and what is on screen
    // made to say the new one's say.
    ULONGLONG staleFrom = 0, staleBgFrom = 0;      // the one shown for a1.txt, the one for the background
    bool staleModel = true, staleBgModel = true;   // ... each stood for another (so it must not)
    int msStale = -1, msStaleBg = -1;              // ... how soon each was up
    int putUpsStale = 0, putUpsStaleBg = 0;        // ... menus put up since each click
    int putUpsStaleFrom = 0, putUpsStaleBgFrom = 0;
    bool staleOpen = false, staleBgOpen = false;   // ... the new one is in: the menu is still up, and is the new one
    bool staleOwn = false, staleBgOwn = false;     // ... and what is on screen is the new one's say
    bool rowFound = false;   // "属性" was in that menu, and its row was clicked
    HWND dialog = nullptr;   // the properties dialog that opened
    std::wstring dialogTitle;
    ULONGLONG listed = 0;
    bool refreshed = false;  // the listing was made anew after the command ran
    bool reshown = false;    // a command that is not there: the menu of the thing itself came up in its place
    int movedResult = -1;    // a command that had moved but says the same: how it went
    bool forgotten = false, builtAgain = false;  // the background's menus, after they were let go of
    bool hoverBuilt = false;  // the pointer rested on a file: its menu was built
    bool dropped = false, lateShown = false, sync = false;
    int sendTo = 0;        // what is in 发送到 of a menu built ahead, a moment after it was built
    int openWithEarly = 0, sendToEarly = 0;  // ... as built, before the extensions were asked
    int sendToOldWay = -1;  // what 发送到 has as the window opens it, the old way
    bool sendToSeen = false;
    int zip = -1;  // -1: there is no 7-Zip here to pick; 0: picked in its submenu; 1: the file came out,
                   // as it was zipped, and the window that did it closed by itself; 2: that window stayed
    FILETIME zipPickedAt{};      // when it was picked
    DWORD zipProcess = 0;        // the 7-Zip window program the command started
    HANDLE zipHandle = nullptr;  // ... to be told when it has ended (and to end it, if it does not)
    int zipTicks = 0;            // ticks spent waiting for the file, and for the window to close
    bool zipText = false;        // the file that came out says what was zipped
    int lastStep = -1, stepSince = 0, stalled = -1;  // how long the step has been waiting
    // A left click elsewhere while a menu is up: the menu is closed for it,
    // and the click is the window's own, given once - as with no menu up:
    // [0] on the menu bar's 查看, [1] on a file, [2] on blank canvas,
    // [3] held down on blank canvas, [4] a double click on a tree's +/- box.
    POINT blank{}, on{};    // a blank point of the list; where the click was made
    bool clickGone[5] = {};  // the right-click menu is gone after the click
    bool clickDone[5] = {};  // ... and the click did what a click does there
    int clickAgain[5] = {};  // ... menus put up since the click (the right-click's not again)
    int clickViewFace = 0, clickViewInk = 0;  // 查看's list as the screen shows it: its face colour, its text
    int clickFileNavy = 0;   // ... the file's row, selected, on screen
    bool clickFileKept = false;  // [1] ... something is left of that click
    bool clickCleared = false;   // [2] ... the selection is gone after the click on blank canvas
    bool clickKept = false;  // ... something is left of a click: the mouse captured, a press armed, a rubber band following the pointer
    bool clickTreeOpen = false;  // the double click's first click opened the tree row
    int clickRow = -1;       // ... the row
    bool clickTreeHad = false;  // ... and whether it was open before
    bool clickHeldOver = false;  // [3] ... and let go: the band is gone
    bool clickSelectedBefore = false;  // [2] something was selected when the click on blank canvas came
    std::wstring clickName;     // ... its title
    std::wstring path(const wchar_t* name) const { return dir + L"\\" + name; }
};
AheadDrive g_ahead;

// The processes named `exe` that this process started at or after `since`.
std::vector<DWORD> childrenNamed(const wchar_t* exe, FILETIME since) {
    std::vector<DWORD> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    const DWORD me = GetCurrentProcessId();
    for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
        if (e.th32ParentProcessID != me || _wcsicmp(e.szExeFile, exe) != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID);
        if (!h) continue;
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(h, &created, &exited, &kernel, &user);
        CloseHandle(h);
        if (CompareFileTime(&created, &since) >= 0) out.push_back(e.th32ProcessID);
    }
    CloseHandle(snap);
    return out;
}

// The windows a process is showing.
std::vector<HWND> shownWindowsOf(DWORD pid) {
    struct Found {
        DWORD pid;
        std::vector<HWND> windows;
    } found{pid, {}};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* found = (Found*)lp;
        DWORD owner = 0;
        GetWindowThreadProcessId(h, &owner);
        if (owner == found->pid && IsWindowVisible(h)) found->windows.push_back(h);
        return TRUE;
    }, (LPARAM)&found);
    return found.windows;
}

// The window of this process of the given class whose title has `name` in it
// (a properties dialog, say).
HWND windowTitled(const wchar_t* cls, const wchar_t* name) {
    struct Found {
        const wchar_t *cls, *name;
        HWND hwnd = nullptr;
    } found{cls, name, nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* found = (Found*)lp;
        DWORD pid = 0;
        GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) return TRUE;
        wchar_t name[64]{}, title[512]{};
        GetClassNameW(h, name, 64);
        GetWindowTextW(h, title, 512);
        if (wcscmp(name, found->cls) == 0 && wcsstr(title, found->name)) {
            found->hwnd = h;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&found);
    return found.hwnd;
}

// What a menu says along its top, as the Win98-style popup shows it.
std::vector<std::wstring> menuTexts(HMENU menu) {
    std::vector<std::wstring> out;
    for (int i = 0; i < GetMenuItemCount(menu); ++i) {
        wchar_t buf[512]{};
        GetMenuStringW(menu, i, buf, 512, MF_BYPOSITION);
        std::wstring text = buf;
        const size_t amp = text.find(L"(&");
        if (amp != std::wstring::npos) {
            const size_t close = text.find(L')', amp);
            if (close != std::wstring::npos) text.erase(amp, close - amp + 1);
        }
        text.erase(std::remove(text.begin(), text.end(), L'&'), text.end());
        if (!text.empty()) out.push_back(text);
    }
    return out;
}

// The items of the menu built as `entry` says, with this program's own for `paths`.
std::vector<shellmenu::Item> itemsOfEntry(ULONGLONG entry) {
    std::vector<shellmenu::Item> items;
    if (entry) shellmenu::itemsOf(entry, &items);
    return items;
}

// Where, along the top of the menu built as `entry` says, the item whose text
// has `text` in it lies, and what it says there (nowhere: -1).
int topItem(ULONGLONG entry, const wchar_t* text, std::wstring* says) {
    const std::vector<shellmenu::Item> items = itemsOfEntry(entry);
    for (int i = 0; i < (int)items.size(); ++i)
        if (items[i].text.find(text) != std::wstring::npos) {
            if (says) *says = items[i].text;
            return i;
        }
    return -1;
}

// A zip file holding one text file, stored as it is (no compression): for
// an extension's command, picked in its submenu.
const char kZipText[] = "extracted by the extension, through the menu built for the zip itself";
void writeZip(const std::wstring& path, const std::string& name, const std::string& content) {
    unsigned crc = 0xFFFFFFFF;
    for (unsigned char c : content) {
        crc ^= c;
        for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    crc ^= 0xFFFFFFFF;
    std::string z, central;
    const auto le16 = [&z](std::string& out, unsigned v) {
        out += (char)(v & 0xFF);
        out += (char)(v >> 8);
    };
    const auto le32 = [&z](std::string& out, unsigned v) {
        for (int k = 0; k < 4; ++k) out += (char)((v >> (8 * k)) & 0xFF);
    };
    const unsigned n = (unsigned)content.size();
    le32(z, 0x04034b50);  // local file header
    le16(z, 20);
    le16(z, 0);
    le16(z, 0);  // stored
    le16(z, 0);  // time
    le16(z, 0x21);  // date: 1980-01-01
    le32(z, crc);
    le32(z, n);
    le32(z, n);
    le16(z, (unsigned)name.size());
    le16(z, 0);
    z += name;
    z += content;
    le32(central, 0x02014b50);  // central directory
    le16(central, 20);
    le16(central, 20);
    le16(central, 0);
    le16(central, 0);
    le16(central, 0);  // time
    le16(central, 0x21);  // date
    le32(central, crc);
    le32(central, n);
    le32(central, n);
    le16(central, (unsigned)name.size());
    le16(central, 0);  // no extra field
    le16(central, 0);  // no comment
    le16(central, 0);  // the first disk
    le16(central, 0);  // internal attributes
    le32(central, 0);  // external attributes
    le32(central, 0);  // the local header is at the start of the file
    central += name;
    z += central;
    le32(z, 0x06054b50);  // end of the central directory: after it (it was
    le16(z, 0);           // written before, and 7-Zip said "headers error")
    le16(z, 0);
    le16(z, 1);
    le16(z, 1);
    le32(z, (unsigned)central.size());
    le32(z, (unsigned)(30 + name.size() + n));
    le16(z, 0);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    fwrite(z.data(), 1, z.size(), f);
    fclose(f);
}

// Whether two menus say the same: what each row says, and what is under a
// submenu of it (as it is now: the submenus of a menu built ahead are filled
// in further a moment after it was built).
bool sameMenuSay(HMENU a, HMENU b) {
    const int count = GetMenuItemCount(a);
    if (count != GetMenuItemCount(b)) return false;
    for (int i = 0; i < count; ++i) {
        wchar_t x[512]{}, y[512]{};
        GetMenuStringW(a, i, x, 512, MF_BYPOSITION);
        GetMenuStringW(b, i, y, 512, MF_BYPOSITION);
        if (wcscmp(x, y) != 0) return false;
        HMENU sub = GetSubMenu(a, i), other = GetSubMenu(b, i);
        if ((sub != nullptr) != (other != nullptr)) return false;
        if (sub && !sameMenuSay(sub, other)) return false;
    }
    return true;
}

void aheadTestStep(HWND driver) {
    AheadDrive& d = g_ahead;
    KillTimer(driver, kAheadTestTimer);
    if (d.step >= kAheadTestEnd) return;
    const auto next = [&]() {
        if (d.step < kAheadTestEnd) SetTimer(driver, kAheadTestTimer, 20, nullptr);
    };
    // A list of the menu bar's that a click opened in its steps, if one is up
    // (the steps run inside that list's loop too: nothing else would end it).
    const auto closeLists = [&]() {
        HWND root = nullptr;
        menuWindows(&root);
        if (g_popup && IsWindow(g_popup)) PostMessageW(g_popup, WM_KEYDOWN, VK_ESCAPE, 0);
    };
    // The steps are over.  A menu still up goes first: it is shown from inside
    // the click that asked for it, and only its own loop takes it down - the
    // steps run inside that loop, and nothing else would ever end it.
    const auto endSteps = [&]() {
        menuClose();
        closeLists();
        d.step = kAheadTestEnd;
    };
    if (++d.ticks > 3000) {  // sixty seconds: give up, do not hang
        endSteps();
        return;
    }
    if (d.wait > 0) {
        --d.wait;
        next();
        return;
    }
    if (d.step != d.lastStep) {  // a step has four seconds: what it waits for and does not come
        d.lastStep = d.step;     // is said by its own check, not by every one after it
        d.stepSince = d.ticks;
    } else if (d.ticks - d.stepSince > 200 && d.step < kAheadTestEnd) {
        d.stalled = d.step;
        menuClose();
        closeLists();  // a list of the menu bar's that no step closed
        ++d.step;
        next();
        return;
    }
    // Where a click is on an entry's name.
    const auto rowPoint = [&](const wchar_t* name) {
        for (int i = 0; i < (int)g_app.entries.size(); ++i)
            if (g_app.entries[i].name == name) {
                const RECT r = g_app.listRowRect(i);
                return POINT{r.left + 40, (r.top + r.bottom) / 2};
            }
        return POINT{-1, -1};
    };
    const auto rightClick = [&](POINT p) {
        d.t0 = diagMillis();
        GetSystemTimes(&d.idleAt, &d.kernelAt, nullptr);
        PostMessageW(d.main, WM_RBUTTONDOWN, 2, MAKELPARAM(p.x, p.y));
    };
    // A left click: press and release, one right after the other (as a quick
    // one arrives - the release is in the queue before the press is seen to).
    const auto leftClick = [&](POINT p) {
        PostMessageW(d.main, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(p.x, p.y));
        PostMessageW(d.main, WM_LBUTTONUP, 0, MAKELPARAM(p.x, p.y));
    };
    // The row of the entry called `name` (-1: there is none).
    const auto rowOf = [&](const wchar_t* name) {
        for (int i = 0; i < (int)g_app.entries.size(); ++i)
            if (g_app.entries[i].name == name) return i;
        return -1;
    };
    // How much of the processors was busy since the click was posted (per cent).
    const auto busySince = [&]() {
        const auto ticks = [](FILETIME t) {
            return (ULONGLONG)t.dwLowDateTime | ((ULONGLONG)t.dwHighDateTime << 32);
        };
        FILETIME idle{}, kernel{}, user{};
        if (!GetSystemTimes(&idle, &kernel, &user)) return 0;
        const ULONGLONG all = ticks(kernel) - ticks(d.kernelAt), doing = all - (ticks(idle) - ticks(d.idleAt));
        return all ? (int)(doing * 100 / all) : 0;
    };
    const shellmenu::Request background{d.dir, {}, {}};
    const shellmenu::Request a1{d.dir, {d.path(L"a1.txt")}, {}},
        a2{d.dir, {d.path(L"a2.txt")}, {}}, a3{d.dir, {d.path(L"a3.txt")}, {}},
        a4{d.dir, {d.path(L"a4.txt")}, {}},
        hovered{d.dir, {d.path(L"k5.zz")}, {}}, late{d.dir, {d.path(L"k6.qq")}, {}};
    shellmenu::ViewState view;  // as the window makes it for a file that is nothing special
    view.menuMask = g_app.settings.menuMask;
    // Whether the menu on screen says what a menu built says: the rows as they
    // are drawn, and what is under them (its submenus too, as they are now).
    const auto onScreenSays = [&](const std::vector<shellmenu::Item>& items,
                                  const std::vector<std::wstring>& paths) {
        HMENU menu = shellmenu::materialize(items, false, nullptr);
        shellmenu::addOwnItems(menu, paths, view);
        std::vector<std::wstring> drawn = menuRootTexts();
        drawn.erase(std::remove(drawn.begin(), drawn.end(), std::wstring()), drawn.end());
        const bool same = g_menuDisplay && drawn == menuTexts(menu) &&
                          sameMenuSay(g_menuDisplay->menu, menu);
        DestroyMenu(menu);
        return same;
    };

    switch (d.step) {
    case 0:  // the thread up: the menus of the folder asked for, as the
             // window's thread asks when a folder is listed
        if (shellmenu::running()) {
            d.t0 = diagMillis();
            prefetchMenus(g_app);
            d.step = 1;
        }
        break;
    case 1: {  // ... and built: the background's, the folder's, the four kinds'
        bool all = shellmenu::find(background).entry != 0;
        const shellmenu::Request sub{d.dir, {d.path(L"sub")}, {}};
        all = all && shellmenu::findKind(shellmenu::kindOf(sub)).entry != 0;
        for (const wchar_t* name : {L"a1.txt", L"k1.cpp", L"k2.ini", L"k3.log"}) {
            const shellmenu::Request what{d.dir, {d.path(name)}, {}};
            all = all && shellmenu::findKind(shellmenu::kindOf(what)).entry != 0;
        }
        if (all) {
            d.builtAfterMs = (int)(diagMillis() - d.t0);
            d.wait = 20;  // their submenus are filled in once more by then
            d.step = 2;
        }
        break;
    }
    case 2: {  // the background
        const RECT row = g_app.listRowRect(0);
        rightClick(POINT{g_app.lay.listViewport.left + g_app.lay.nameW + 40, row.top + 8});
        d.step = 3;
        break;
    }
    case 3:
        if (menuIsOpen()) {
            d.msBackground = (int)(menuShownAt() - d.t0);
            d.busyBackground = busySince();
            d.backgroundModel = g_menuShownModel;
            menuClose();
            d.step = 4;
        }
        break;
    case 4:
        if (!menuIsOpen()) {
            rightClick(rowPoint(L"a1.txt"));  // built ahead
            d.step = 5;
        }
        break;
    case 5:
        if (menuIsOpen()) {
            d.msOwn = (int)(menuShownAt() - d.t0);
            d.busyOwn = busySince();
            d.ownModel = g_menuShownModel;
            menuClose();
            d.step = 6;
        }
        break;
    case 6:
        if (!menuIsOpen()) {
            rightClick(rowPoint(L"a2.txt"));  // nothing was built for it: one of its kind stands for it
            d.step = 7;
        }
        break;
    case 7:
        if (menuIsOpen()) {
            d.msAlike = (int)(menuShownAt() - d.t0);
            d.busyAlike = busySince();
            d.alikeModel = g_menuShownModel;
            menuClose();
            d.step = 8;
        }
        break;
    case 8: {  // The click asked for the menu of a2.txt itself.  What the shell
               // puts in a submenu is there as the menu is built; the
               // extensions fill in the rest then, and once more a moment
               // after (for what they find out in between).
        const shellmenu::Found own = shellmenu::find(a2);
        const shellmenu::Found alike = shellmenu::findKind(shellmenu::kindOf(a2));
        if (!own.entry || !alike.entry) break;
        if (!d.openWithEarly && !d.sendToEarly) {  // as built: the extensions asked once
            const std::vector<shellmenu::Item> items = itemsOfEntry(own.entry);
            for (const shellmenu::Item& item : items) {
                if (!item.popup) continue;
                if (item.text.find(L"打开方式") != std::wstring::npos)
                    d.openWithEarly = (int)item.sub.size();
                if (item.text.find(L"发送到") != std::wstring::npos)
                    d.sendToEarly = (int)item.sub.size();
            }
        }
        if (own.ageMs < 450) break;  // filled in once more by now
        {   // 发送到's last item comes from a walk the shell makes, that is over
            // when it is: given up to two and a half seconds to turn up.
            int sendTo = 0;
            bool seen = false;
            for (const shellmenu::Item& item : itemsOfEntry(own.entry))
                if (item.popup && item.text.find(L"发送到") != std::wstring::npos) {
                    seen = true;
                    sendTo = (int)item.sub.size();
                }
            if (seen && d.sendToSeen && sendTo <= d.sendToEarly && own.ageMs < 2500) break;
        }
        const std::vector<shellmenu::Item> shown = itemsOfEntry(alike.entry),
                                           itsOwn = itemsOfEntry(own.entry);
        if (shown.empty() || itsOwn.empty()) break;
        d.sameTree = shellmenu::sameSay(shown, itsOwn);
        HMENU menu = shellmenu::materialize(shown, true, nullptr);
        shellmenu::addOwnItems(menu, a2.paths, view);
        const std::vector<std::wstring> top = menuTexts(menu);
        DestroyMenu(menu);
        const std::vector<std::wstring> built = shellmenu::labels(d.dir, a2.paths, view);
        d.topCount = (int)built.size();
        d.sameTop = top == built;
        d.openWith = 0;
        d.sendTo = 0;
        for (const shellmenu::Item& item : itsOwn) {
            if (!item.popup) continue;
            if (item.text.find(L"打开方式") != std::wstring::npos) d.openWith = (int)item.sub.size();
            if (item.text.find(L"发送到") != std::wstring::npos) {
                d.sendToSeen = true;
                d.sendTo = (int)item.sub.size();
            }
        }
        d.step = 9;
        break;
    }
    case 9:  // a third of the kind, nothing built for it: shown from one of its kind (at once)
        d.putUps = menuPutUps();
        rightClick(rowPoint(L"a3.txt"));
        d.step = 10;
        break;
    case 10: {  // up at once, from one of its kind: read off the screen
        if (!menuIsOpen()) break;
        d.thirdModel = g_menuShownModel;
        d.shown = menuRootTexts();
        d.shown.erase(std::remove(d.shown.begin(), d.shown.end(), std::wstring()), d.shown.end());
        // What it is made of: the shell's part of the menu of another of the
        // kind, and this program's items for a3.txt.
        d.standIn = itemsOfEntry(g_menuOpenEntry);
        HMENU menu = shellmenu::materialize(d.standIn, true, nullptr);
        shellmenu::addOwnItems(menu, a3.paths, view);
        d.sameShown = d.shown == menuTexts(menu);
        DestroyMenu(menu);
        d.step = 11;
        break;
    }
    case 11:  // the menu of a3.txt itself was asked for as it was shown: in by now
        if (shellmenu::find(a3).entry) {
            d.wait = 3;  // let it take the stand-in's place, if it is to
            d.step = 12;
        }
        break;
    case 12: {
        const HWND popup = windowTitled(L"MyOldComputerHMenu", L"");
        if (!popup || !menuIsOpen()) break;
        const shellmenu::Found own = shellmenu::find(a3);
        const std::vector<shellmenu::Item> itsOwn = itemsOfEntry(own.entry);
        d.differed = !itsOwn.empty() && !shellmenu::sameSay(d.standIn, itsOwn);
        d.ownOnScreen = !itsOwn.empty() && onScreenSays(itsOwn, a3.paths);
        // The menu was put up once: the stand-in was not taken down for the
        // thing's own menu, its rows were made to say that.
        d.putUps = menuPutUps() - d.putUps;
        RECT row{};
        if (!menuRowOf(L"属性", &row)) {
            endSteps();  // not to pick anything other
            break;
        }
        d.rowFound = true;
        d.listed = g_app.listSerial;
        PostMessageW(popup, WM_LBUTTONUP, MK_LBUTTON,
                     MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        d.step = 13;
        break;
    }
    case 13: {  // the properties of a3.txt open
        const HWND dialog = windowTitled(L"#32770", L"a3.txt");
        if (!dialog) break;
        d.dialog = dialog;
        wchar_t title[256]{};
        GetWindowTextW(dialog, title, 256);
        d.dialogTitle = title;
        PostMessageW(dialog, WM_CLOSE, 0, 0);
        d.step = 14;
        break;
    }
    case 14:  // ... closed, the command is over, and the folder is listed anew
        if (!IsWindow(d.dialog) && !g_menuShown.serial) {
            d.refreshed = g_app.listSerial != d.listed;
            d.step = 15;
        }
        break;
    case 15: {  // A fourth of the kind, nothing built for it: one of its kind goes
               // up at once, and is made to say its own menu's say when that is in.
        d.putUpsFourthFrom = menuPutUps();
        rightClick(rowPoint(L"a4.txt"));
        d.step = 16;
        break;
    }
    case 16: {
        const HWND popup = menuActiveWindow();
        if (!popup) break;
        d.fourthModel = g_menuShownModel;
        d.msFourth = (int)(menuShownAt() - d.t0);
        d.busyFourth = busySince();
        // 发送到 is opened at once, while the menu is still one of its kind's:
        // it is to stay open as the thing's own menu comes in.
        RECT row{};
        if (menuRowOf(L"发送到", &row))
            PostMessageW(popup, WM_MOUSEMOVE, 0,
                         MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        d.step = 17;
        break;
    }
    case 17: {  // ... its own menu is in by now (and filled in once more), 发送到 still open under the
               // pointer: the menu is up as it was - put up once, and not changed under the submenu
        const shellmenu::Found own = shellmenu::find(a4);
        if (!own.entry || !menuIsOpen() || own.ageMs < 700) break;
        d.fourthKept = menuSubmenuUp();
        d.fourthSendTo = menuRowsUp();
        // What the one it is shown from has in 发送到 now (its own's last item
        // comes from a walk the shell makes, that is over when it is).
        for (const shellmenu::Item& item : itemsOfEntry(g_menuOpenEntry))
            if (item.popup && item.text.find(L"发送到") != std::wstring::npos)
                d.fourthSendToStandIn = (int)item.sub.size();
        d.fourthStillStandIn = g_menuOpenEntry != own.entry;
        d.putUpsFourth = menuPutUps() - d.putUpsFourthFrom;
        // The pointer goes to another row of the menu: the submenu closes,
        // and the menu is made to say its own menu's say.
        RECT row{};
        if (!menuRootRowOf(L"属性", &row)) {
            menuClose();
            d.step = 20;
            break;
        }
        PostMessageW(menuRootWindow(), WM_MOUSEMOVE, 0,
                     MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        d.step = 18;
        break;
    }
    case 18: {  // ... closed, and the menu on screen is the thing's own from now on
        const shellmenu::Found own = shellmenu::find(a4);
        if (!own.entry || !menuIsOpen() || menuSubmenuUp() || g_menuOpenEntry != own.entry) break;
        d.fourthOwn = onScreenSays(itemsOfEntry(own.entry), a4.paths);
        d.putUpsFourth = menuPutUps() - d.putUpsFourthFrom;
        // 发送到 opened again: as many rows as its own menu has now.
        RECT row{};
        if (!menuRowOf(L"发送到", &row)) {
            menuClose();
            d.step = 20;
            break;
        }
        PostMessageW(menuRootWindow(), WM_MOUSEMOVE, 0,
                     MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        d.step = 19;
        break;
    }
    case 19: {  // ... open: as many rows as its own menu has in it now
        d.fourthSendToAgain = menuRowsUp();
        for (const shellmenu::Item& item : itemsOfEntry(shellmenu::find(a4).entry))
            if (item.popup && item.text.find(L"发送到") != std::wstring::npos)
                d.fourthSendToOwn = (int)item.sub.size();
        menuClose();
        d.step = 20;
        break;
    }
    case 20:
        if (!menuIsOpen()) d.step = 21;
        break;
    case 21: {  // A menu built long ago (every one counts as that now), right-clicked:
               // up at once, from the thing's own menu - which is built again behind it
        g_menuAgainMs = 0;
        d.staleFrom = shellmenu::find(a1).entry;
        d.putUpsStaleFrom = menuPutUps();
        if (!d.staleFrom) {  // nothing was built for it ahead: nothing of this can be seen
            d.step = 24;
            break;
        }
        rightClick(rowPoint(L"a1.txt"));
        d.step = 22;
        break;
    }
    case 22: {
        if (!menuIsOpen()) break;
        d.staleModel = g_menuShownModel;
        d.msStale = (int)(menuShownAt() - d.t0);
        d.busyStale = busySince();
        d.step = 23;
        break;
    }
    case 23: {  // ... the new one is in by now (and filled in once more): the menu is still
               // up, put up once, and says the new one's say
        const shellmenu::Found own = shellmenu::find(a1);
        if (!own.entry || own.entry == d.staleFrom || own.ageMs < 700) break;
        d.staleOpen = menuIsOpen() && g_menuOpenEntry == own.entry;
        d.staleOwn = onScreenSays(itemsOfEntry(own.entry), a1.paths);
        d.putUpsStale = menuPutUps() - d.putUpsStaleFrom;
        menuClose();
        d.step = 24;
        break;
    }
    case 24: {  // The background's menu, built long ago too: a right-click on the blank part
        if (menuIsOpen()) break;
        d.staleBgFrom = shellmenu::find(background).entry;
        d.putUpsStaleBgFrom = menuPutUps();
        if (!d.staleBgFrom) {
            d.step = 27;
            break;
        }
        const RECT row = g_app.listRowRect(0);
        rightClick(POINT{g_app.lay.listViewport.left + g_app.lay.nameW + 40, row.top + 8});
        d.step = 25;
        break;
    }
    case 25: {
        if (!menuIsOpen()) break;
        d.staleBgModel = g_menuShownModel;
        d.msStaleBg = (int)(menuShownAt() - d.t0);
        d.busyStaleBg = busySince();
        d.step = 26;
        break;
    }
    case 26: {  // ... built again by now (and filled in once more): still up, put up once, and the new one's say
        const shellmenu::Found own = shellmenu::find(background);
        if (!own.entry || own.entry == d.staleBgFrom || own.ageMs < 700) break;
        d.staleBgOpen = menuIsOpen() && g_menuOpenEntry == own.entry;
        d.staleBgOwn = onScreenSays(itemsOfEntry(own.entry), {});
        d.putUpsStaleBg = menuPutUps() - d.putUpsStaleBgFrom;
        g_menuAgainMs = shellmenu::kAgainMs;
        menuClose();
        d.step = 27;
        break;
    }
    case 27:
        if (!menuIsOpen()) d.step = 28;
        break;
    case 28: {  // a command that is neither where it is said to be nor anywhere
               // in that menu - asked for the way the window asks when one is
               // picked: the window is told, and shows the menu of the thing itself
        for (int i = 0; i < (int)g_app.entries.size(); ++i)
            if (g_app.entries[i].name == L"a1.txt") selectOnly(g_app, i);
        const int pos = topItem(shellmenu::find(a1).entry, L"属性", nullptr);
        if (pos < 0 || g_app.selection.size() != 1) {
            endSteps();
            break;
        }
        MenuFor& shown = g_menuShown;
        shown = MenuFor{};
        shown.what = a1;
        shown.client = rowPoint(L"a1.txt");
        shown.tick = g_inputTick;
        shown.serial = ++g_menuSerial;
        shellmenu::invoke(a1, {pos}, L"(no such command)", d.main, shown.serial);
        d.step = 29;
        break;
    }
    case 29:
        if (menuIsOpen()) {
            d.reshown = true;
            menuClose();
            d.step = 30;
        }
        break;
    case 30: {  // a command that has moved (it lies a row back) but says the same
        const shellmenu::Found own = shellmenu::find(a1);
        if (menuIsOpen() || !own.entry) break;  // the menu shown in its place is closed, and a1.txt's is built again
        const std::vector<shellmenu::Item> items = itemsOfEntry(own.entry);
        std::wstring says;
        const int pos = topItem(own.entry, L"属性", &says);
        if (pos < 1) {
            endSteps();
            break;
        }
        shellmenu::invoke(a1, {pos - 1}, says, driver, 78);
        d.step = 31;
        break;
    }
    case 31: {  // it ran: the properties of a1.txt are up, and the command is over once they close
        if (HWND dialog = windowTitled(L"#32770", L"a1.txt")) PostMessageW(dialog, WM_CLOSE, 0, 0);
        if (d.movedResult >= 0) d.step = 32;
        break;
    }
    case 32:  // the clipboard changed, as far as the menus go: the background's
              // are let go of, and the one of this folder is built again
        shellmenu::forgetBackgrounds();
        d.forgotten = shellmenu::find(background).entry == 0;
        shellmenu::prefetch(background);
        d.step = 33;
        break;
    case 33:
        if (shellmenu::find(background).entry) {
            d.builtAgain = true;
            d.step = 34;
        }
        break;
    case 34: {  // the pointer came to rest on a file
        const POINT p = rowPoint(L"k5.zz");
        PostMessageW(d.main, WM_MOUSEMOVE, 0, MAKELPARAM(p.x, p.y));
        d.wait = 10;  // it has rested there by then
        d.step = 35;
        break;
    }
    case 35:
        if (shellmenu::find(hovered).entry) {
            d.hoverBuilt = true;
            d.step = 36;
        }
        break;
    case 36:  // right-clicked where nothing was built (a kind nothing was
              // built ahead for), and a key pressed while the menu was waited for
        rightClick(rowPoint(L"k6.qq"));
        PostMessageW(d.main, WM_KEYDOWN, VK_F24, 0);
        d.wait = 60;  // a second: built by then, and not shown
        d.step = 37;
        break;
    case 37:
        d.dropped = !menuIsOpen() && shellmenu::find(late).entry != 0;
        d.step = 38;
        break;
    case 38:  // ... and the next right-click has it
        rightClick(rowPoint(L"k6.qq"));
        d.step = 39;
        break;
    case 39:
        if (menuIsOpen()) {
            d.lateShown = true;
            menuClose();
            d.step = 40;
        }
        break;
    case 40:
        if (!menuIsOpen()) {
            g_app.settings.menuMask &= ~kMenuAhead;  // switched off
            rightClick(rowPoint(L"a1.txt"));
            d.step = 41;
        }
        break;
    case 41:  // built on the window's thread: up while the click is still being handled
        d.sync = menuIsOpen() && g_menuSyncDepth > 0;
        d.wait = 25;  // half a second: the shell's walk of the Send To folder is over by then
        d.step = 42;
        break;
    case 42: {  // 发送到 is opened as the window opens it, the old way
        if (!d.sync) {
            g_app.settings.menuMask |= kMenuAhead;
            endSteps();
            break;
        }
        RECT row{};
        if (!menuRowOf(L"发送到", &row)) {
            menuClose();
            g_app.settings.menuMask |= kMenuAhead;
            d.step = 45;
            break;
        }
        PostMessageW(menuActiveWindow(), WM_MOUSEMOVE, 0,
                     MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        d.step = 43;
        break;
    }
    case 43: {  // ... open: how many it has
        d.sendToOldWay = menuRowsUp();
        menuClose();
        g_app.settings.menuMask |= kMenuAhead;
        d.step = 44;
        break;
    }
    case 44: {  // a command of an extension's own, in a submenu: a zip made for it, in the folder
        if (menuIsOpen()) break;
        writeZip(d.path(L"z.zip"), "z.txt", kZipText);
        g_app.reload();  // the listing has it
        d.step = 45;
        break;
    }
    case 45:
        rightClick(rowPoint(L"z.zip"));  // nothing was built for a zip, nor for its kind: waited for
        d.step = 46;
        break;
    case 46: {  // up when it is built; the pointer comes to the extension's submenu
        const HWND popup = menuActiveWindow();
        if (!popup) break;
        RECT row{};
        if (!menuRowOf(L"7-Zip", &row)) {
            d.zip = -1;  // no 7-Zip here (or nothing of the kind in this menu): nothing to pick
            menuClose();
            d.step = 49;
            break;
        }
        PostMessageW(popup, WM_MOUSEMOVE, 0,
                     MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        d.step = 47;
        break;
    }
    case 47: {  // ... open: 解压到当前文件夹 picked in it
        const HWND sub = menuActiveWindow();
        if (!sub) break;
        RECT row{};
        if (!menuRowOf(L"解压到当前", &row)) {
            d.zip = -1;  // not as it is named here: nothing is picked
            menuClose();
            d.step = 49;
            break;
        }
        d.zip = 0;  // picked
        GetSystemTimeAsFileTime(&d.zipPickedAt);
        PostMessageW(sub, WM_MOUSEMOVE, 0,
                     MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        PostMessageW(sub, WM_LBUTTONUP, MK_LBUTTON,
                     MAKELPARAM(pxOf(g_app.scale, 12), (row.top + row.bottom) / 2));
        d.step = 48;
        break;
    }
    case 48: {  // the file is out, as it was zipped, and the window that did it closed by itself
        // (7-Zip keeps its window up when it has something to say about the
        // archive: nothing of the test's is to stay on the screen.)
        if (!d.zipProcess) {
            const std::vector<DWORD> born = childrenNamed(L"7zG.exe", d.zipPickedAt);
            if (!born.empty()) {
                d.zipProcess = born.front();
                d.zipHandle = OpenProcess(SYNCHRONIZE | PROCESS_TERMINATE, FALSE, d.zipProcess);
            }
        }
        // Behind everything, as the test's own windows are.
        if (d.zipProcess)
            for (HWND w : shownWindowsOf(d.zipProcess))
                SetWindowPos(w, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!d.zipText) {
            std::string text;
            FILE* f = nullptr;
            if (_wfopen_s(&f, d.path(L"z.txt").c_str(), L"rb") == 0 && f) {
                char buf[256]{};
                const size_t got = fread(buf, 1, sizeof(buf), f);
                fclose(f);
                text.assign(buf, got);
            }
            d.zipText = text == kZipText;
        }
        const bool ended =
            d.zipProcess ? d.zipHandle && WaitForSingleObject(d.zipHandle, 0) == WAIT_OBJECT_0
                         : d.zipText && d.zipTicks > 15;  // not seen: gone before a look
        if (d.zipText && ended) {
            d.zip = 1;
            d.step = 49;
        } else if (++d.zipTicks > 170) {  // nearly the four seconds a step has
            d.zip = 2;
            if (d.zipHandle) TerminateProcess(d.zipHandle, 0);  // the test's own: not left on the screen
            d.step = 49;
        }
        break;
    }
    case 49:
        if (!menuIsOpen() && !g_menuShown.serial) d.step = 50;
        break;

    // ------------------------------------------------------------------ the click elsewhere
    // While a menu is up, a left press anywhere outside it closes the menu,
    // and the click is the window's own - given once, as with no menu up.
    // (The press and the release come one right after the other, as a quick
    // click arrives: the release is in the queue before the press is seen to.
    // The up that comes before the kept press is on its way is held by the
    // window and given right after it.)
    case 50:  // nothing selected; where blank canvas is
        g_app.clearSelection();
        d.blank = POINT{g_app.lay.listViewport.left + g_app.lay.nameW + 40,
                        g_app.lay.listViewport.top + 8};
        d.step = 51;
        break;
    case 51:  // the background's menu
        rightClick(d.blank);
        d.step = 52;
        break;
    case 52: {  // ... and one click on the menu bar's 查看
        if (!menuIsOpen()) break;
        const RECT title = g_app.menuItemRect(2);
        d.on = POINT{(title.left + title.right) / 2, (title.top + title.bottom) / 2};
        d.clickAgain[0] = -menuPutUps();
        leftClick(d.on);
        d.wait = 3;
        d.step = 53;
        break;
    }
    case 53: {  // ... the menu is gone, and 查看's list is up - on screen
        HWND root = nullptr;
        const std::vector<HWND> wins = menuWindows(&root);
        RECT list{};
        if (g_popup && IsWindow(g_popup)) GetClientRect(g_popup, &list);
        d.clickAgain[0] += menuPutUps();
        d.clickGone[0] = wins.empty() && !menuIsOpen();
        if (d.clickGone[0] && list.right > 0 && g_app.pressMenu == 2 && d.clickAgain[0] == 0) {
            RECT at{};
            GetWindowRect(g_popup, &at);
            const int s = pxOf(g_app.scale, 1);
            // The list's lower part lies over the window's content: blank
            // canvas there, white.
            POINT content{0, g_app.lay.content.top};
            ClientToScreen(d.main, &content);
            const LONG from = (std::max)(at.top + list.bottom * 3 / 5, content.y + 2 * s);
            const RECT lower{at.left + list.right / 5, from, at.left + list.right * 4 / 5,
                             at.top + list.bottom - 4 * s};
            screenTally(lower, &d.clickViewFace, nullptr);
            const int samples = (int)(((lower.right - lower.left + 1) / 2) *
                                      ((lower.bottom - lower.top + 1) / 2));
            // ... and its first row says something, in black
            POINT a{18 * s, 3 * s}, b{list.right - 8 * s, 25 * s};
            ClientToScreen(g_popup, &a);
            ClientToScreen(g_popup, &b);
            screenTally(RECT{a.x, a.y, b.x, b.y}, nullptr, &d.clickViewInk);
            keepScreen(L"click-through-view", at);
            diagLog(L"click-through: 查看's list %dx%d at %ld,%ld; face %d of %d, ink %d",
                    (int)list.right, (int)list.bottom, at.left, at.top, d.clickViewFace, samples,
                    d.clickViewInk);
            d.clickDone[0] = d.clickViewFace * 10 >= samples * 9 && d.clickViewInk >= 10 &&
                             !g_app.marquee && GetCapture() != d.main;
        }
        if (g_popup && IsWindow(g_popup)) PostMessageW(g_popup, WM_KEYDOWN, VK_ESCAPE, 0);
        d.wait = 4;
        d.step = 54;
        break;
    }
    case 54: {  // the list is gone: a file is right-clicked (which selects it)
        HWND root = nullptr;
        menuWindows(&root);
        if (menuIsOpen() || g_popup) break;
        if (rowOf(L"k1.cpp") < 0 || rowOf(L"a2.txt") < 0) {
            endSteps();
            break;
        }
        rightClick(rowPoint(L"k1.cpp"));
        d.step = 55;
        break;
    }
    case 55:  // the menu is up: one click on another file
        if (!menuIsOpen()) break;
        d.clickAgain[1] = -menuPutUps();
        leftClick(rowPoint(L"a2.txt"));
        d.wait = 3;
        d.step = 56;
        break;
    case 56: {  // ... the menu is gone, that file is selected - on screen - and nothing is left of the click
        if (menuIsOpen()) break;
        const int row = rowOf(L"a2.txt");
        d.clickAgain[1] += menuPutUps();
        d.clickFileNavy = row >= 0 ? frameCount(shownFrame(d.main), g_app.listRowRect(row), isNavy) : 0;
        d.clickFileKept = g_app.marquee || g_app.dragArmed || GetCapture() == d.main;
        d.clickKept = d.clickFileKept;
        d.clickGone[1] = !menuIsOpen();
        d.clickDone[1] = row >= 0 && g_app.selection.size() == 1 &&
                         g_app.selection.count(d.path(L"a2.txt")) && d.clickFileNavy >= 100 &&
                         !d.clickKept && d.clickAgain[1] == 0;
        // A file's menu now (a right-click on a file keeps what is selected):
        // the click on blank canvas must clear a selection that is there.
        rightClick(rowPoint(L"k5.zz"));
        d.step = 57;
        break;
    }
    case 57:
        if (!menuIsOpen()) break;
        d.clickSelectedBefore = !g_app.selection.empty();
        d.clickAgain[2] = -menuPutUps();
        leftClick(d.blank);
        d.wait = 3;
        d.step = 58;
        break;
    case 58: {  // ... the menu is gone, and so is the selection; no rubber band is left
        if (menuIsOpen()) break;
        d.clickAgain[2] += menuPutUps();
        d.clickGone[2] = !menuIsOpen();
        d.clickKept = g_app.marquee || g_app.dragArmed || GetCapture() == d.main;
        d.clickCleared = g_app.selection.empty();
        d.clickDone[2] = d.clickSelectedBefore && d.clickCleared && !d.clickKept &&
                        d.clickAgain[2] == 0;
        rightClick(d.blank);  // for the press that is held
        d.step = 59;
        break;
    }
    case 59:  // the menu is up: the button pressed, and kept down
        if (!menuIsOpen()) break;
        d.clickAgain[3] = -menuPutUps();
        PostMessageW(d.main, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(d.blank.x, d.blank.y));
        d.wait = 15;  // a third of a second
        d.step = 60;
        break;
    case 60: {  // ... the menu is closed by then, and the press is the window's already: the band is there
        d.clickAgain[3] += menuPutUps();
        d.clickGone[3] = !menuIsOpen();
        // (The band follows the real pointer meanwhile, wherever it is on the screen.)
        d.clickDone[3] = d.clickGone[3] && g_app.marquee && GetCapture() == d.main &&
                        d.clickAgain[3] == 0;
        PostMessageW(d.main, WM_LBUTTONUP, 0, MAKELPARAM(d.blank.x, d.blank.y));
        d.wait = 3;
        d.step = 61;
        break;
    }
    case 61:  // ... and let go: the band is gone
        d.clickHeldOver = !g_app.marquee && GetCapture() != d.main;
        g_app.clearSelection();  // the band followed the real pointer: whatever it covered
        repaint(d.main);
        if (!g_app.sidebarVisible) {
            g_app.sidebarVisible = true;
            g_app.relayout(g_app.width, g_app.height);
            repaint(d.main);
        }
        rightClick(rowPoint(L"k5.zz"));  // for the double click
        d.step = 62;
        break;
    case 62: {  // the menu is up: the first click of a double click, on a tree row's +/- box
        if (!menuIsOpen()) break;
        int row = -1;
        for (int k = 0; k < (int)g_app.treeFlat.size(); ++k) {
            TreeNode* n = g_app.treeFlat[k];
            if (n->path.empty() || n->kind == TreeNode::Favorite ||
                n->kind == TreeNode::Favorites)
                continue;
            // A row that is in view (the tree is scrolled to the folder shown)
            const RECT rr = g_app.treeRowRect(k);
            if (rr.top < g_app.lay.treeViewport.top ||
                rr.bottom > g_app.lay.treeViewport.bottom)
                continue;
            if (g_app.treeRowHasChildren(*n)) {
                row = k;
                break;
            }
        }
        if (row < 0) {  // nothing here with a +/- to double-click on: not looked at
            d.step = kAheadTestEnd;
            break;
        }
        d.clickRow = row;
        d.clickTreeHad = g_app.treeFlat[row]->expanded;
        d.clickName = g_app.treeFlat[row]->title;
        const RECT rr = g_app.treeRowRect(row);
        const int indent = pxOf(g_app.scale, g_app.treeFlat[row]->depth * 14 + 3);
        d.on = POINT{rr.left + indent + pxOf(g_app.scale, 4), rr.top + pxOf(g_app.scale, 9)};
        d.clickAgain[4] = -menuPutUps();
        leftClick(d.on);
        d.wait = 3;
        d.step = 63;
        break;
    }
    case 63: {  // ... the row opened; the second click of the double click
        if (d.clickRow >= (int)g_app.treeFlat.size()) break;
        d.clickTreeOpen = g_app.treeFlat[d.clickRow]->expanded != d.clickTreeHad;
        d.clickAgain[4] += menuPutUps();
        d.clickGone[4] = !menuIsOpen();
        PostMessageW(d.main, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(d.on.x, d.on.y));
        PostMessageW(d.main, WM_LBUTTONUP, 0, MAKELPARAM(d.on.x, d.on.y));
        d.wait = 3;
        d.step = 64;
        break;
    }
    case 64: {  // ... and shuts again: both clicks the window's
        if (d.clickRow >= (int)g_app.treeFlat.size()) break;
        const bool same = g_app.treeFlat[d.clickRow]->expanded == d.clickTreeHad;
        d.clickDone[4] = d.clickTreeOpen && same && !g_app.marquee && GetCapture() != d.main &&
                        !g_app.dragArmed && d.clickAgain[4] == 0;
        d.step = kAheadTestEnd;
        break;
    }
    default:
        endSteps();
        break;
    }
    next();
}

LRESULT CALLBACK aheadTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kAheadTestTimer) {
        aheadTestStep(hwnd);
        return 0;
    }
    if (msg == shellmenu::kMenuDoneMessage) {
        if (wp == 78) g_ahead.movedResult = (int)lp;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The test in the real window, on `dir`: four text files (a1 built ahead,
// a2, a3 and a4 not), `k1.cpp, `k2.ini`, `k3.log`, `k4.md`, `k5.zz` of five other
// kinds (three are built ahead: `k4.md`'s and `k5.zz`'s are not), and a folder.
void menusAheadTest(const std::wstring& dir, const std::function<void(const wchar_t*, bool)>& check) {
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = aheadTestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kAheadTestClass;
    RegisterClassExW(&wc);
    HWND driver = CreateWindowExW(0, kAheadTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr,
                                  nullptr, wc.hInstance, nullptr);  // never shown
    registerMainClass(wc.hInstance);
    HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", L"menus ahead test",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver, nullptr,
                                wc.hInstance, nullptr);
    g_ahead = AheadDrive{};
    g_ahead.main = main;
    g_ahead.dir = dir;
    if (main) {
        g_app.hwnd = main;
        g_app.scale = dpiScaleOfWindow(main);
        g_app.init(main);
        // (An earlier test left the scratch settings with the mask of a file
        // from before: this one is about the menus built ahead, which are on.)
        g_app.settings.menuMask = kMenuDefaults;
        g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
        g_app.loadDirectory(dir, false);
        g_app.setViewMode(ViewMode::Details);
        placeWindow(main, 0, 0, g_app.width, g_app.height);
        ShowWindow(main, SW_SHOWNOACTIVATE);
        SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(main);
        shellmenu::start(main);
        MSG msg;
        for (SetTimer(driver, kAheadTestTimer, 20, nullptr);
             g_ahead.step < kAheadTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(driver, kAheadTestTimer);
        // Clicks and keys the steps posted and nobody took: not to be had by the loop below.
        MSG left{};
        while (PeekMessageW(&left, main, WM_MOUSEFIRST, WM_MOUSELAST, PM_REMOVE)) {
        }
        while (PeekMessageW(&left, main, WM_KEYFIRST, WM_KEYLAST, PM_REMOVE)) {
        }
        menuClose();
        if (g_ahead.zipHandle) {  // whatever ended the steps: no 7-Zip window of the test's is left
            if (WaitForSingleObject(g_ahead.zipHandle, 0) != WAIT_OBJECT_0)
                TerminateProcess(g_ahead.zipHandle, 0);
            CloseHandle(g_ahead.zipHandle);
            g_ahead.zipHandle = nullptr;
        }
        if (HWND dialog = windowTitled(L"#32770", L".txt")) PostMessageW(dialog, WM_CLOSE, 0, 0);
        g_app.settings.menuMask |= kMenuAhead;
        g_menuAgainMs = shellmenu::kAgainMs;
        g_hoverMenuRow = -1;
        if (IsWindow(main)) {
            g_app.hwnd = nullptr;
            DestroyWindow(main);
        }
        // The window's going posted WM_QUIT.  This loop ended by its steps,
        // not by that message, as the other windows' loops do: it is taken
        // here, or it would end the next test's loop at once.
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        g_app.hwnd = nullptr;
        g_app.addrEdit = nullptr;
        g_app.addrEditing = false;
        if (g_app.addrFont) {
            DeleteObject(g_app.addrFont);
            g_app.addrFont = nullptr;
        }
    }
    DestroyWindow(driver);

    const AheadDrive& d = g_ahead;
    std::wstring notes;
    if (!main) notes += L"窗口没建出来";
    else if (d.ticks > 3000) notes += L"超时";
    const auto of = [](int v) { return std::to_wstring(v); };
    const auto verdict = [&](const std::wstring& name, const std::wstring& why, bool ok) {
        std::wstring label = name;
        if (!notes.empty()) label += L"：" + notes;
        else if (!ok)
            label += L"：" + why + (d.stalled >= 0
                                        ? L"（第 " + std::to_wstring(d.stalled) + L" 步等了 4 秒没等到）"
                                        : L"");
        check(label.c_str(), label == name);
    };
    // "At once": within the moment, and twice the part of the processors that were busy more.
    const auto atOnce = [](int ms, int busy) { return ms >= 0 && ms < kAheadSoonMs + 2 * busy; };
    const auto notAtOnce = [&](int busy) {
        return L"没有当场弹出（这期间处理器占用 " + of(busy) + L"%）";
    };
    const auto soon = [&](int ms) { return L"等了 " + of(ms) + L" 毫秒"; };
    verdict(L"打开文件夹后，空白处、文件夹和四种类型文件的菜单都在后台建好了",
            d.builtAfterMs < 0 ? L"没有全建出来" : L"用了 " + of(d.builtAfterMs) + L" 毫秒",
            d.builtAfterMs >= 0);
    verdict(L"右键空白处：菜单当场弹出，用的是它自己的",
            d.backgroundModel ? L"没有用它自己的" : soon(d.msBackground),
            !d.backgroundModel && atOnce(d.msBackground, d.busyBackground));
    verdict(L"右键一个文件：菜单当场弹出，用的是它自己的",
            d.ownModel ? L"没有用它自己的" : soon(d.msOwn),
            !d.ownModel && atOnce(d.msOwn, d.busyOwn));
    verdict(L"右键同类的另一个文件（没有为它建过菜单）：菜单也当场弹出，是照同类的建的",
            !d.alikeModel ? L"没有照同类的建" : soon(d.msAlike),
            d.alikeModel && atOnce(d.msAlike, d.busyAlike));
    verdict(L"照同类建的菜单与它自己的菜单说的一样：顶层 " + of(d.topCount) +
                L" 项相同，子菜单也相同；“打开方式”里有 " + of(d.openWith) + L" 项（建好时 " +
                of(d.openWithEarly) + L" 项），“发送到”里有 " + of(d.sendTo) + L" 项（建好时 " +
                of(d.sendToEarly) + L" 项；窗口当场打开、稍等后再看是 " + of(d.sendToOldWay) + L" 项）",
            !d.sameTop ? L"顶层不同" : !d.sameTree ? L"子菜单不同"
                       : d.openWith < 2 ? L"“打开方式”没有填进来"
                       : d.sendToSeen && d.sendTo == 0 ? L"“发送到”是空的"
                       : L"“发送到”比窗口当场打开的还少",
            d.sameTop && d.sameTree && d.openWith >= 2 && (!d.sendToSeen || d.sendTo > 0) &&
                (!d.sendToSeen || d.sendToOldWay < 0 || d.sendTo >= d.sendToOldWay));
    verdict(L"右键第三个同类文件：屏幕上当场弹出的菜单就是“同类的 shell 部分 + 为它现做的本程序项”",
            !d.thirdModel ? L"没有照同类的建" : L"内容与应有的不一样", d.thirdModel && d.sameShown);
    verdict(std::wstring(L"它自己的菜单建好之后，屏幕上的就是它自己的菜单") +
                (d.differed ? L"：它说的与快照不一样，就地改了过来，菜单没有关了再开"
                            : L"（本机没有因文件而异的项；菜单也只弹了一次）"),
            d.putUps != 1 ? L"菜单弹了 " + of(d.putUps) + L" 次" : L"屏幕上的不是它自己的菜单",
            d.ownOnScreen && d.putUps == 1);
    const auto noMore = [&](int n) { return L"菜单弹了 " + of(n) + L" 次"; };
    const auto fourthWhyNot = [&]() -> std::wstring {
        if (!d.fourthModel) return L"没有照同类的建";
        if (!atOnce(d.msFourth, d.busyFourth)) return notAtOnce(d.busyFourth);
        if (d.putUpsFourth != 1) return noMore(d.putUpsFourth);
        if (!d.fourthKept) return L"它自己的菜单来到时，开着的子菜单被收走了";
        if (!d.fourthStillStandIn) return L"开着的子菜单底下，菜单被改了";
        if (!d.fourthOwn) return L"屏幕上的与它自己的不一样";
        if (d.fourthSendTo < 0 || d.fourthSendTo != d.fourthSendToStandIn)
            return L"开着的子菜单里的项数不对";
        if (d.fourthSendToAgain < 0 || d.fourthSendToAgain != d.fourthSendToOwn)
            return L"再打开的“发送到”里项数不对";
        return L"";
    };
    const std::wstring fourthWhy = fourthWhyNot();
    verdict(L"右键第四个同类文件（没有为它建过菜单）：照同类的当场弹出（等了 " + of(d.msFourth) +
                L" 毫秒），只弹一次；它自己的菜单来到时“发送到”正开着：子菜单没被收走，里面 " +
                of(std::max(0, d.fourthSendTo)) + L" 项（它照的那个菜单里 " +
                of(std::max(0, d.fourthSendToStandIn)) +
                L" 项）；指针回到菜单上，屏幕上的就地改成它自己的；再打开“发送到”：" +
                of(std::max(0, d.fourthSendToAgain)) + L" 项（它的菜单里 " +
                of(std::max(0, d.fourthSendToOwn)) + L" 项）",
            fourthWhy, fourthWhy.empty());
    verdict(L"右键一个菜单早就建好的文件：当场弹出，用的就是它自己的（等了 " + of(d.msStale) +
                L" 毫秒）；后台把它重新建一遍，屏幕上的就地改成新建的那个，菜单不关不重开",
            d.staleModel ? L"没有用它自己的菜单"
                         : !atOnce(d.msStale, d.busyStale) ? notAtOnce(d.busyStale)
                                           : d.putUpsStale != 1 ? L"菜单弹了 " + of(d.putUpsStale) + L" 次"
                                                                : !d.staleOpen ? L"新的建好后，菜单不在了"
                                                                               : L"屏幕上的不是新建的那个",
            !d.staleModel && atOnce(d.msStale, d.busyStale) && d.putUpsStale == 1 && d.staleOpen && d.staleOwn);
    verdict(L"右键空白处，菜单早就建好：当场弹出（等了 " + of(d.msStaleBg) +
                L" 毫秒）；后台把它重新建一遍，屏幕上的就地改成新建的那个，菜单只弹一次",
            !atOnce(d.msStaleBg, d.busyStaleBg) ? notAtOnce(d.busyStaleBg)
                              : d.staleBgModel ? L"没有用它自己的菜单"
                                              : d.putUpsStaleBg != 1 ? L"菜单弹了 " + of(d.putUpsStaleBg) + L" 次"
                                                                    : !d.staleBgOpen ? L"新的建好后，菜单不在了"
                                                                                     : L"屏幕上的不是新建的那个",
            !d.staleBgModel && atOnce(d.msStaleBg, d.busyStaleBg) && d.putUpsStaleBg == 1 && d.staleBgOpen && d.staleBgOwn);
    verdict(L"在这个菜单里点“属性”：打开的是这个文件自己的属性（" + d.dialogTitle + L"），之后列表刷新",
            !d.rowFound ? L"菜单里没有找到“属性”"
                       : !d.dialog ? L"属性窗口没有打开" : L"列表没有刷新",
            d.rowFound && d.dialog && d.refreshed);
    verdict(L"位置和文字都对不上的命令：不执行，改为弹出它自己的真实菜单",
            d.reshown ? L"没有弹出真实菜单" : L"", d.reshown);
    verdict(L"挪了位置但文字相同的命令：照样找到并执行", L"回报 " + of(d.movedResult),
            d.movedResult == shellmenu::kVerbRan);
    verdict(L"空白处的菜单作废后立刻重新建好",
            std::wstring(d.forgotten ? L"" : L"没有作废") + (d.builtAgain ? L"" : L"；也没有重建"),
            d.forgotten && d.builtAgain);
    verdict(L"鼠标在一个文件上停一下：它的菜单就在后台建好", L"没有建", d.hoverBuilt);
    verdict(L"等菜单时按了键：菜单不再弹出；紧接着再右键就弹出",
            d.dropped ? L"再右键没有弹出" : L"等的时候还是弹出了", d.dropped && d.lateShown);
    verdict(L"关掉“提前在后台构建菜单”：菜单在窗口线程上当场建、当场弹出",
            d.sync ? L"没有弹出" : L"不是在窗口线程上建的", d.sync);
    verdict(d.zip < 0 ? L"（本机没有 7-Zip 的“解压到当前文件夹”：子菜单里扩展自己的命令这项没考到）"
                      : L"右键一个压缩包，在 7-Zip 的子菜单里点“解压到当前文件夹”：文件解出来了，内容就是打包的；解压窗口自己关上了",
            d.zip == 2 ? L"解压窗口没有自己关上，这次是替它关的（7-Zip 对这个压缩包有话说，或它太慢）"
                       : d.zip == 0 ? (d.zipText ? L"文件出来了，解压窗口还没关，这次是替它关的"
                                                 : L"点了，文件没有出来，或内容不是打包的")
                                    : L"",
            d.zip == 1);

    // A left click elsewhere while a menu is up.
    const auto clickWhy = [&](int k, const wchar_t* more) -> std::wstring {
        if (!d.clickGone[k]) return L"右键菜单没有关";
        if (d.clickAgain[k] != 0) return L"右键菜单又弹了一次";
        return more;
    };
    verdict(L"右键菜单开着时点一下菜单栏的“查看”：菜单关了，“查看”的下拉当场打开，屏幕上看得到（下拉的下沿盖在窗口的内容上：底色 " +
                of(d.clickViewFace) + L" 个采样，字 " + of(d.clickViewInk) + L" 个）",
            clickWhy(0, !d.clickDone[0] ? L"“查看”的下拉没有打开，或屏幕上那块不是它" : L""),
            d.clickDone[0]);
    verdict(L"右键菜单开着时点一下别的文件（k1.cpp 的菜单开着，点 a2.txt）：菜单关了，a2.txt 被选中，屏幕上它的行是选中的蓝色（" +
                of(d.clickFileNavy) + L" 个像素）",
            clickWhy(1, d.clickFileKept ? L"点击没有结束：鼠标还被抓着，或框选还在"
                                       : L"没有选中 a2.txt，或屏幕上不是选中的样子"),
            d.clickDone[1]);
    verdict(L"右键菜单开着时点一下空白处（k5.zz 的菜单开着，a2.txt 选中着）：菜单关了，选中没了",
            clickWhy(2, !d.clickSelectedBefore ? L"点之前就没有选中"
                        : !d.clickCleared ? L"选中没有清掉"
                                          : L"点击没有结束：留下了框选，或鼠标还被抓着"),
            d.clickDone[2]);
    verdict(std::wstring(L"右键菜单开着时按住左键不放：菜单当场就关了，按下这一下当场算数（框选已经开始）；松开后框选结束") +
                (d.clickHeldOver ? L"" : L"；松开后框选还在"),
            clickWhy(3, d.clickHeldOver ? L"按住时框选没有开始" : L"松开后框选没有结束"),
            d.clickDone[3] && d.clickHeldOver);
    const bool clicked = d.clickRow >= 0;
    verdict(clicked ? std::wstring(L"右键菜单开着时在树的“") + d.clickName + L"”的 +/- 上双击：两下都点到了，那一行开了又合上"
                    : std::wstring(L"（这棵树里没有带 +/- 的行：双击这项没考到）"),
            clicked ? clickWhy(4, L"那一行没有开了又合上") : L"", d.clickDone[4]);
}

// ---------------------------------------------------------- thumbnail test
// A folder that is mostly pictures opens as thumbnails in the real window,
// and what is on screen is read back: the icon at first, then - each when its
// thumbnail is in - the picture itself (pictures of one colour each, so the
// pixels say which is which).  A folder with a picture or two among other
// files keeps the usual view.  Back in the first one the thumbnails are there
// at once: they were made before.

constexpr UINT_PTR kThumbTestTimer = 0x4D4C;
constexpr wchar_t kThumbTestClass[] = L"MyOldComputerThumbTest";
constexpr int kThumbTestEnd = 5;  // the step past the last one
constexpr int kThumbPictures = 6;

// One colour a picture, none of them white (the cells are).
const COLORREF kThumbColours[kThumbPictures] = {RGB(255, 0, 0),  RGB(0, 0, 255), RGB(0, 192, 0),
                                                RGB(0, 255, 255), RGB(255, 0, 255), RGB(255, 192, 0)};

// A picture of one colour, as a 24-bit BMP file.
void writeBmp(const std::wstring& path, int w, int h, COLORREF colour) {
    const int row = ((w * 3 + 3) / 4) * 4;
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    file.bfSize = file.bfOffBits + (DWORD)(row * h);
    BITMAPINFOHEADER info{};
    info.biSize = sizeof(info);
    info.biWidth = w;
    info.biHeight = h;
    info.biPlanes = 1;
    info.biBitCount = 24;
    std::vector<BYTE> pixels((size_t)row * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            BYTE* p = &pixels[(size_t)y * row + (size_t)x * 3];
            p[0] = GetBValue(colour);
            p[1] = GetGValue(colour);
            p[2] = GetRValue(colour);
        }
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    fwrite(&file, sizeof(file), 1, f);
    fwrite(&info, sizeof(info), 1, f);
    fwrite(pixels.data(), 1, pixels.size(), f);
    fclose(f);
}

// The pixels of a thumbnail, BGRA.
std::vector<unsigned char> bitmapPixels(HBITMAP bitmap, int* w, int* h) {
    std::vector<unsigned char> out;
    BITMAP info{};
    if (!bitmap || !GetObjectW(bitmap, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0)
        return out;
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = info.bmWidth;
    header.biHeight = -info.bmHeight;
    header.biPlanes = 1;
    header.biBitCount = 32;
    out.resize((size_t)info.bmWidth * info.bmHeight * 4);
    HDC dc = CreateCompatibleDC(nullptr);
    GetDIBits(dc, bitmap, 0, info.bmHeight, out.data(), (BITMAPINFO*)&header, DIB_RGB_COLORS);
    DeleteDC(dc);
    *w = info.bmWidth;
    *h = info.bmHeight;
    return out;
}

COLORREF g_thumbColour = 0;
bool isThumbColour(COLORREF px) { return px == g_thumbColour; }

struct ThumbDrive {
    HWND main = nullptr;
    int step = 0;
    int wait = 0;
    int ticks = 0;
    std::wstring pictures, mixed;  // the two folders
    int cell[kThumbPictures] = {};  // the cell of each picture in the thumbnail view
    COLORREF colour[kThumbPictures] = {};
    int onScreen = 0;      // pictures on screen in their own colour, when they all are
    int atOnce = 0;        // ... back in the folder, before anything else happened
    int waited = 0;        // ticks it took
    bool usualView = false;
    bool baseline = false;
    int stale[2] = {0, 0};
    RECT where[2] = {};
};
ThumbDrive g_thumb;

// How many of the pictures are on screen in their own colour: `least` pixels
// of it in the thumbnail box of its cell.
int picturesOnScreen(const ThumbDrive& d, int least) {
    const Frame f = shownFrame(d.main);
    int n = 0;
    const int side = g_app.iconBox(), top = pxOf(g_app.scale, 4);
    for (int k = 0; k < kThumbPictures; ++k) {
        const RECT r = g_app.gridCellRect(d.cell[k]);
        RECT box{(r.left + r.right) / 2 - side / 2, r.top + top, (r.left + r.right) / 2 + side / 2,
                 r.top + top + side};
        g_thumbColour = d.colour[k];
        n += frameCount(f, box, isThumbColour) >= least ? 1 : 0;
    }
    return n;
}

void thumbTestStep(HWND driver) {
    ThumbDrive& d = g_thumb;
    KillTimer(driver, kThumbTestTimer);
    if (++d.ticks > 400) {  // ten seconds: give up, do not hang
        d.step = kThumbTestEnd;
        return;
    }
    const auto later = [driver] { SetTimer(driver, kThumbTestTimer, 50, nullptr); };
    if (d.step >= kThumbTestEnd) return;
    if (d.wait-- > 0) {
        later();
        return;
    }
    d.wait = 0;
    switch (d.step) {
    case 0: {  // the folder of pictures as it opened
        UpdateWindow(d.main);
        d.onScreen = picturesOnScreen(d, 200);
        d.wait = 3;
        break;
    }
    case 1: {  // ... and as the thumbnails come in: no repaint is asked for here
        d.onScreen = picturesOnScreen(d, 200);
        ++d.waited;
        if (d.onScreen < kThumbPictures && d.ticks < 380) {
            later();
            return;
        }
        keepFrames(L"thumb-pictures", shownFrame(d.main), renderedFrame(g_app));
        // A folder with one picture among other files: the usual view.
        g_app.loadDirectory(d.mixed, false);
        repaint(d.main);
        UpdateWindow(d.main);
        d.wait = 3;
        break;
    }
    case 2: {
        d.usualView = g_app.mode == ViewMode::Details && !g_app.thumbnails && !g_app.viewAutomatic;
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(L"thumb-mixed", shown, fresh);
        d.stale[1] = frameMismatch(shown, fresh, &d.where[1]);
        // Back among the pictures: drawn at once, and read at once.
        g_app.loadDirectory(d.pictures, false);
        repaint(d.main);
        UpdateWindow(d.main);
        d.atOnce = picturesOnScreen(d, 200);
        d.wait = 3;
        break;
    }
    default: {  // still, with every thumbnail in: what is shown is what the state renders
        const Frame shown = shownFrame(d.main);
        const Frame fresh = renderedFrame(g_app);
        keepFrames(L"thumb-baseline", shown, fresh);
        d.baseline = frameMismatch(shown, fresh, &d.where[0]) == 0;
        d.step = kThumbTestEnd;
        return;
    }
    }
    ++d.step;
    if (d.step < kThumbTestEnd) later();
}

LRESULT CALLBACK thumbTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kThumbTestTimer) {
        thumbTestStep(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The test in the real window, with the window's usual view being the list
// one.  `pictures` holds `kThumbPictures` pictures of one colour each and a
// text file; `mixed` one picture among text files.
void thumbnailTest(const std::wstring& pictures, const std::wstring& mixed,
                   const std::function<void(const wchar_t*, bool)>& check) {
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = thumbTestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kThumbTestClass;
    RegisterClassExW(&wc);
    HWND driver = CreateWindowExW(0, kThumbTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr,
                                  nullptr, wc.hInstance, nullptr);  // never shown
    registerMainClass(wc.hInstance);
    HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", L"thumbnail test",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver, nullptr,
                                wc.hInstance, nullptr);
    g_thumb = ThumbDrive{};
    g_thumb.main = main;
    g_thumb.pictures = pictures;
    g_thumb.mixed = mixed;
    if (main) {
        g_app.hwnd = main;
        g_app.scale = dpiScaleOfWindow(main);
        g_app.init(main);
        g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
        // No sidebar: all six pictures fit in the first row of the view.
        g_app.sidebarVisible = false;
        g_app.setViewMode(ViewMode::Details);
        g_app.loadDirectory(pictures, false);
        for (int k = 0; k < kThumbPictures; ++k) {
            const std::wstring name = L"p" + std::to_wstring(k) + L".bmp";
            g_thumb.colour[k] = kThumbColours[k];
            for (int i = 0; i < (int)g_app.entries.size(); ++i)
                if (g_app.entries[i].name == name) g_thumb.cell[k] = i;
        }
        placeWindow(main, 0, 0, g_app.width, g_app.height);
        ShowWindow(main, SW_SHOWNOACTIVATE);
        SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(main);
        MSG msg;
        for (SetTimer(driver, kThumbTestTimer, 60, nullptr);
             g_thumb.step < kThumbTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(driver, kThumbTestTimer);
        g_app.hwnd = nullptr;  // the edit is the window's child and goes with it
        DestroyWindow(main);
        PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        g_app.addrEdit = nullptr;
        g_app.addrEditing = false;
        if (g_app.addrFont) {
            DeleteObject(g_app.addrFont);
            g_app.addrFont = nullptr;
        }
    }
    DestroyWindow(driver);

    std::wstring notes;
    if (!main) notes += L"窗口没建出来";
    else if (g_thumb.ticks > 400) notes += L"超时";
    const auto of = [](int v) { return std::to_wstring(v); };
    const auto verdict = [&](const std::wstring& name, const std::wstring& why, bool ok) {
        std::wstring label = name;
        if (!notes.empty()) label += L"：" + notes;
        else if (!ok) label += L"：" + why;
        check(label.c_str(), label == name);
    };
    verdict(L"图片多的文件夹在真窗口里以缩略图视图打开：屏幕上六张图各自显示自己的颜色",
            L"屏幕上只有 " + of(g_thumb.onScreen) + L" 张（等了 " + of(g_thumb.waited * 50) + L" 毫秒）",
            g_thumb.onScreen == kThumbPictures);
    verdict(L"只有一两张图的文件夹还是平时的视图（列表）",
            L"视图是 " + of((int)g_app.mode) + L"，缩略图 " + of((int)g_app.thumbnails) + L"，屏幕上有 " +
                of(g_thumb.stale[1]) + L" 个像素与状态不符",
            g_thumb.usualView && g_thumb.stale[1] == 0);
    verdict(L"回到图片多的文件夹：缩略图一上来就在屏幕上（之前已经做过）",
            L"屏幕上只有 " + of(g_thumb.atOnce) + L" 张", g_thumb.atOnce == kThumbPictures);
    verdict(L"（测试基准）缩略图：屏幕上的画面读得回来，且与按状态渲染的一致", L"基准不符",
            g_thumb.baseline);
}

// The moment a menu of a folder is built that kills: the shell starts listing
// the libraries, making a window of its own (a WorkerW) on our thread, and its
// other thread reports back to that window.  What is loaded right then is what
// matters (shellmenu.h, preloadHandlers).
struct PreloadWatch {
    std::vector<std::wstring> dlls;  // the extensions' DLLs, those on disk
    int listed = 0;                  // how many times that moment came
    int missing = -1;                // of the DLLs, not loaded at the first of them
};
PreloadWatch g_preloadWatch;

LRESULT CALLBACK preloadCbtProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HCBT_CREATEWND) {
        wchar_t cls[64]{};
        if (GetClassNameW((HWND)wp, cls, 64) && lstrcmpW(cls, L"WorkerW") == 0) {
            int missing = 0;
            for (const std::wstring& dll : g_preloadWatch.dlls)
                if (!GetModuleHandleW(dll.c_str())) ++missing;
            if (g_preloadWatch.listed++ == 0) g_preloadWatch.missing = missing;
        }
    }
    return CallNextHookEx(nullptr, code, wp, lp);
}

// ------------------------------------------------------------------ faint test
// A hidden file or folder, shown because hidden files are shown, is drawn
// faint as Explorer draws it: its icon half of what it would be and half of
// what lies behind it, and its name as black as any other's.  In the real
// window, with Explorer's icons and with this program's own, in each view,
// over the white of the list and over the selection's navy, in the tree, and
// as a picture's thumbnail: what the window shows, read back from the
// compositor, is compared pixel by pixel with the item's twin that is not
// hidden (a folder and a file of the kind, one hidden, one not).
constexpr UINT_PTR kFaintTestTimer = 0x4D4F;
constexpr wchar_t kFaintTestClass[] = L"MyOldComputerFaintTest";

struct FaintLook {
    ViewMode mode;
    bool system;  // Explorer's icons (else this program's own)
    bool select;  // selected, the hidden ones and their twins: their icons lie over the navy
    const wchar_t* name;
};
constexpr FaintLook kFaintLooks[] = {
    {ViewMode::Details, true, false, L"详细资料视图，Explorer 的图标"},
    {ViewMode::Details, true, true, L"详细资料视图，隐藏项和与它相比的那个都选中着，图标在选中的蓝色上"},
    {ViewMode::Details, false, false, L"详细资料视图，本程序自己的图标"},
    {ViewMode::List, true, false, L"列表视图，Explorer 的图标"},
    {ViewMode::List, false, false, L"列表视图，本程序自己的图标"},
    {ViewMode::Icons, true, false, L"大图标视图，Explorer 的图标"},
    {ViewMode::Icons, false, false, L"大图标视图，本程序自己的图标"},
};
constexpr int kFaintLookCount = (int)(sizeof(kFaintLooks) / sizeof(kFaintLooks[0]));
constexpr int kFaintTree = kFaintLookCount;      // ... and the tree's row of the hidden folder
constexpr int kFaintThumb = kFaintTree + 1;      // ... and a hidden picture's thumbnail
constexpr int kFaintSeen = kFaintThumb + 1;      // the looks there are
constexpr int kFaintTreeSet = 2 * kFaintLookCount + 1;  // the steps: each look is set, then looked at;
constexpr int kFaintTestEnd = kFaintTreeSet + 5;        // then the tree, the thumbnail, hidden files off

// What the pictures' colour comes to at half strength over white.
COLORREF g_faintThumb = 0;
bool isFaintThumb(COLORREF px) { return px == g_faintThumb; }

struct FaintDrive {
    HWND main = nullptr;
    int step = 0;
    int wait = 0;
    int ticks = 0;
    std::wstring dir, pics;
    int row[2][2] = {{-1, -1}, {-1, -1}};  // [a folder, a file][shown whole, hidden]: their rows in the list
    int pic[2] = {-1, -1};                 // the pictures' rows: [shown whole, hidden]
    bool baseline = false;
    bool listed = false;      // with hidden files shown, the hidden twins are in the list
    bool notListed = false;   // ... and are not when they are not shown
    bool treeSeen = false;    // the tree shows the twins' rows
    // Each look: pixels of a hidden twin's icon that are not half its twin's and half of what
    // lies behind it; and the black pixels of its name (-1: not looked at).
    int off[kFaintSeen][2] = {};
    int ink[kFaintSeen][2] = {};
    bool thumbWait = false;   // the thumbnails did not come in time
    int thumbTicks = 0;       // how long they were waited for
};
FaintDrive g_faint;

void faintTestStep(HWND driver) {
    FaintDrive& d = g_faint;
    KillTimer(driver, kFaintTestTimer);
    if (++d.ticks > 800) {  // over thirty seconds: give up, do not hang
        d.step = kFaintTestEnd;
        return;
    }
    const auto later = [driver] { SetTimer(driver, kFaintTestTimer, 40, nullptr); };
    if (d.step >= kFaintTestEnd) return;
    if (d.wait > 0) {
        --d.wait;
        later();
        return;
    }
    const double scale = g_app.scale;
    const auto px = [scale](int v) { return pxOf(scale, v); };
    // The row of the entry called `name` (-1: it is not in the list).
    const auto rowOf = [](const wchar_t* name) {
        for (int i = 0; i < (int)g_app.entries.size(); ++i)
            if (g_app.entries[i].name == name) return i;
        return -1;
    };
    // Where an entry's icon is in this view.
    const auto iconBox = [&](int i) {
        if (g_app.mode == ViewMode::Details) {
            const RECT r = g_app.listRowRect(i);
            return RECT{r.left + px(3), r.top + px(2), r.left + px(3) + px(16), r.top + px(2) + px(16)};
        }
        if (g_app.mode == ViewMode::List) {
            const RECT r = g_app.gridCellRect(i);
            return RECT{r.left + px(2), r.top + px(2), r.left + px(2) + px(16), r.top + px(2) + px(16)};
        }
        const RECT r = g_app.gridCellRect(i);
        const int side = g_app.iconBox(), mid = (r.left + r.right) / 2;
        return RECT{mid - side / 2, r.top + px(4), mid + side / 2, r.top + px(4) + side};
    };
    // A frame's pixel as a colour: a frame holds what GetDIBits gives, blue,
    // green, red in memory - a COLORREF is red, green, blue (as frameCount does).
    const auto colour = [](COLORREF raw) { return RGB((raw >> 16) & 0xFF, (raw >> 8) & 0xFF, raw & 0xFF); };
    // Pixels of `faint`'s box that are not half of `whole`'s box and half of `behind`: what
    // a half-transparent icon comes to over `behind` (GDI blends 128 of 255 with 127 of 255).
    const auto halfOff = [&](const Frame& f, const RECT& whole, const RECT& faint, COLORREF behind) {
        const int w = (std::min)(whole.right - whole.left, faint.right - faint.left);
        const int h = (std::min)(whole.bottom - whole.top, faint.bottom - faint.top);
        if (w <= 0 || h <= 0) return -1;
        int n = 0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const COLORREF a = colour(f.px[(whole.top + y) * f.w + whole.left + x]);
                const COLORREF b = colour(f.px[(faint.top + y) * f.w + faint.left + x]);
                const int want[3] = {(GetRValue(a) * 128 + GetRValue(behind) * 127) / 255,
                                     (GetGValue(a) * 128 + GetGValue(behind) * 127) / 255,
                                     (GetBValue(a) * 128 + GetBValue(behind) * 127) / 255};
                const int got[3] = {GetRValue(b), GetGValue(b), GetBValue(b)};
                if (std::abs(got[0] - want[0]) > 1 || std::abs(got[1] - want[1]) > 1 ||
                    std::abs(got[2] - want[2]) > 1)
                    ++n;
            }
        return n;
    };
    // Black pixels of an entry's name: right of its icon in the details and
    // the list views, under it in the big-icon view.
    const auto nameInk = [&](const Frame& f, int i) {
        const RECT box = iconBox(i);
        const RECT r = g_app.mode == ViewMode::Details ? g_app.listRowRect(i) : g_app.gridCellRect(i);
        if (g_app.mode == ViewMode::Icons)
            return frameCount(f, RECT{r.left + px(2), r.top + g_app.labelTop(), r.right - px(2), r.bottom},
                              isBlack);
        return frameCount(f, RECT{box.right + px(2), r.top, r.right - px(2), r.bottom}, isBlack);
    };
    // The list as it is shown now: each hidden twin's icon against its twin's.
    const auto lookAt = [&](int k, bool navy) {
        UpdateWindow(d.main);
        const Frame f = shownFrame(d.main);
        for (int p = 0; p < 2; ++p) {
            d.off[k][p] = halfOff(f, iconBox(d.row[p][0]), iconBox(d.row[p][1]),
                                  navy ? Theme::select : Theme::white);
            // A selected row's name is white on the navy: not looked at for black.
            d.ink[k][p] = navy ? -1 : nameInk(f, d.row[p][1]);
        }
        keepFrames((L"faint-" + std::to_wstring(k)).c_str(), f, f);
        diagLog(L"faint look %d: selection %d, off %d and %d", k, (int)g_app.selection.size(),
                d.off[k][0], d.off[k][1]);
    };
    const auto show = [&]() {
        g_app.clampScroll();
        repaint(d.main);
        UpdateWindow(d.main);
    };

    switch (d.step) {
    case 0: {  // the baseline, and where the twins are
        UpdateWindow(d.main);
        RECT where{};
        d.baseline = frameMismatch(shownFrame(d.main), renderedFrame(g_app), &where) == 0;
        d.row[0][0] = rowOf(L"sub");
        d.row[0][1] = rowOf(L"hsub");
        d.row[1][0] = rowOf(L"a.txt");
        d.row[1][1] = rowOf(L"h.txt");
        d.listed = d.row[0][0] >= 0 && d.row[0][1] >= 0 && d.row[1][0] >= 0 && d.row[1][1] >= 0;
        for (int k = 0; k < kFaintSeen; ++k) d.off[k][0] = d.off[k][1] = d.ink[k][0] = d.ink[k][1] = -1;
        // Nothing to look at without the twins: only whether they come back when hidden files
        // are not shown (the last step), which needs no rows.
        d.step = d.listed ? 1 : kFaintTreeSet + 4;
        break;
    }
    default: {
        const int k = d.step - 1;
        if (k < 2 * kFaintLookCount) {
            const FaintLook& look = kFaintLooks[k / 2];
            if (k % 2 == 0) {  // the view is set...
                g_app.useSystemIcons = look.system;
                g_app.setViewMode(look.mode);
                g_app.clearSelection();
                // Selected, both of each pair: then what lies behind either icon is the same
                // navy, and the hidden one's is half the other's and half of that.  (Over
                // white the hidden one's would not be: where the icon is transparent, what
                // shows through is the navy itself, not half of the white behind the other.)
                if (look.select)
                    for (int p = 0; p < 2; ++p)
                        for (int t = 0; t < 2; ++t)
                            g_app.selection.insert(g_app.entries[d.row[p][t]].path);
                show();
            } else {  // ... and looked at
                lookAt(k / 2, look.select && look.mode != ViewMode::Icons);
            }
            ++d.step;
            break;
        }
        switch (d.step) {
        case kFaintTreeSet: {  // the folder of the twins, expanded in the tree, its row at the top
            g_app.useSystemIcons = true;
            g_app.clearSelection();
            g_app.hoverTree = -1;
            for (int i = 0; i < (int)g_app.treeFlat.size(); ++i) {
                TreeNode* n = g_app.treeFlat[i];
                if (_wcsicmp(n->path.c_str(), d.dir.c_str()) != 0) continue;
                if (!n->expanded) g_app.toggleTreeNode(*n);
                const RECT r = g_app.treeRowRect(i);
                g_app.treeScroll += r.top - g_app.lay.treeViewport.top;
                break;
            }
            show();
            ++d.step;
            break;
        }
        case kFaintTreeSet + 1: {  // ... and the hidden folder's icon against its twin's
            UpdateWindow(d.main);
            const Frame f = shownFrame(d.main);
            int whole = -1, hidden = -1;
            for (int i = 0; i < (int)g_app.treeFlat.size(); ++i) {
                if (_wcsicmp(g_app.treeFlat[i]->path.c_str(), (d.dir + L"\\sub").c_str()) == 0) whole = i;
                if (_wcsicmp(g_app.treeFlat[i]->path.c_str(), (d.dir + L"\\hsub").c_str()) == 0) hidden = i;
            }
            const auto box = [&](int i) {
                const RECT r = g_app.treeRowRect(i);
                const int x = r.left + px(g_app.treeFlat[i]->depth * 14 + 3) + px(13);
                return RECT{x, r.top + px(2), x + px(16), r.top + px(2) + px(16)};
            };
            const auto inView = [&](int i) {
                const RECT r = g_app.treeRowRect(i);
                return r.top >= g_app.lay.treeViewport.top && r.bottom <= g_app.lay.treeViewport.bottom;
            };
            d.treeSeen = whole >= 0 && hidden >= 0 && inView(whole) && inView(hidden);
            diagLog(L"faint test: the tree's rows are %d and %d (in view: %d), %d rows, scroll %d",
                    whole, hidden, (int)d.treeSeen, (int)g_app.treeFlat.size(), g_app.treeScroll);
            if (d.treeSeen)
                d.off[kFaintTree][0] = halfOff(f, box(whole), box(hidden), Theme::white);
            d.step = kFaintTreeSet + 2;
            break;
        }
        case kFaintTreeSet + 2: {  // the pictures: a folder of thumbnails
            g_app.useSystemIcons = true;
            g_app.loadDirectory(d.pics, false);
            d.pic[0] = rowOf(L"p0.bmp");
            d.pic[1] = rowOf(L"hp.bmp");
            show();
            ++d.step;
            break;
        }
        case kFaintTreeSet + 3: {  // ... once their thumbnails are in
            UpdateWindow(d.main);
            const Frame f = shownFrame(d.main);
            // The picture shown whole has its thumbnail when its box holds a good many pixels of it.
            const bool in = d.pic[0] >= 0 && d.pic[1] >= 0 &&
                            frameCount(f, iconBox(d.pic[0]), isThumbColour) > 200 &&
                            frameCount(f, iconBox(d.pic[1]), isFaintThumb) > 200;
            if (!in && ++d.thumbTicks < 100) {  // given four seconds
                later();
                return;
            }
            d.thumbWait = !in;
            d.off[kFaintThumb][0] = d.pic[0] >= 0 && d.pic[1] >= 0
                                        ? halfOff(f, iconBox(d.pic[0]), iconBox(d.pic[1]), Theme::white)
                                        : -1;
            keepFrames(L"faint-thumbnail", f, f);
            ++d.step;
            break;
        }
        default: {  // hidden files not shown: the hidden twins are not in the list
            g_app.showHidden = false;
            g_app.loadDirectory(d.dir, false);
            d.notListed = rowOf(L"sub") >= 0 && rowOf(L"a.txt") >= 0 && rowOf(L"hsub") < 0 &&
                          rowOf(L"h.txt") < 0;
            d.step = kFaintTestEnd;
            break;
        }
        }
        break;
    }
    }
    later();
}

LRESULT CALLBACK faintTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kFaintTestTimer) {
        faintTestStep(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// The twins are made in `dir` (a folder and a file, each hidden and each
// not) and two pictures of one colour in `pics`, one of them hidden.
void faintTest(const std::wstring& dir, const std::wstring& pics,
               const std::function<void(const wchar_t*, bool)>& check) {
    CreateDirectoryW(dir.c_str(), nullptr);
    CreateDirectoryW(pics.c_str(), nullptr);
    for (const wchar_t* name : {L"sub", L"hsub"})
        CreateDirectoryW((dir + L"\\" + name).c_str(), nullptr);
    for (const wchar_t* name : {L"a.txt", L"h.txt"})
        if (HANDLE h = CreateFileW((dir + L"\\" + name).c_str(), GENERIC_WRITE, 0, nullptr,
                                   CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    writeBmp(pics + L"\\p0.bmp", 300, 200, kThumbColours[0]);
    writeBmp(pics + L"\\hp.bmp", 300, 200, kThumbColours[0]);
    SetFileAttributesW((dir + L"\\hsub").c_str(), FILE_ATTRIBUTE_HIDDEN);
    SetFileAttributesW((dir + L"\\h.txt").c_str(), FILE_ATTRIBUTE_HIDDEN);
    SetFileAttributesW((pics + L"\\hp.bmp").c_str(), FILE_ATTRIBUTE_HIDDEN);

    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = faintTestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kFaintTestClass;
    RegisterClassExW(&wc);
    HWND driver = CreateWindowExW(0, kFaintTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr,
                                  nullptr, wc.hInstance, nullptr);  // never shown
    registerMainClass(wc.hInstance);
    HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", L"faint test",
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver, nullptr,
                                wc.hInstance, nullptr);
    g_faint = FaintDrive{};
    g_faint.main = main;
    g_faint.dir = dir;
    g_faint.pics = pics;
    const bool wasHidden = g_app.showHidden, wasSystem = g_app.useSystemIcons;
    const bool setHidden = g_app.settings.showHidden, setSystem = g_app.settings.useSystemIcons;
    if (main) {
        g_app.hwnd = main;
        g_app.scale = dpiScaleOfWindow(main);
        g_app.init(main);
        // The test's folder is under AppData, a hidden one: the tree can only show the way
        // to it when hidden folders are shown - and this test is about what is shown then.
        g_app.showHidden = true;
        g_app.buildTree();
        g_app.sidebarVisible = true;
        g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
        // The pictures' colour, and what it comes to at half strength over white.
        g_thumbColour = kThumbColours[0];
        g_faintThumb = RGB((GetRValue(kThumbColours[0]) * 128 + 255 * 127) / 255,
                           (GetGValue(kThumbColours[0]) * 128 + 255 * 127) / 255,
                           (GetBValue(kThumbColours[0]) * 128 + 255 * 127) / 255);
        g_app.useSystemIcons = true;
        g_app.loadDirectory(dir, false);
        g_app.setViewMode(ViewMode::Details);
        g_app.clearSelection();
        placeWindow(main, 0, 0, g_app.width, g_app.height);
        ShowWindow(main, SW_SHOWNOACTIVATE);
        SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        UpdateWindow(main);
        MSG msg;
        for (SetTimer(driver, kFaintTestTimer, 40, nullptr);
             g_faint.step < kFaintTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(driver, kFaintTestTimer);
        g_app.showHidden = wasHidden;
        g_app.useSystemIcons = wasSystem;
        if (IsWindow(main)) {
            g_app.hwnd = nullptr;  // the edit is the window's child and goes with it
            DestroyWindow(main);
        }
        PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
        g_app.hwnd = nullptr;
        g_app.addrEdit = nullptr;
        g_app.addrEditing = false;
        if (g_app.addrFont) {
            DeleteObject(g_app.addrFont);
            g_app.addrFont = nullptr;
        }
    }
    DestroyWindow(driver);
    g_app.settings.showHidden = setHidden;
    g_app.settings.useSystemIcons = setSystem;
    // What was made hidden is made a plain file again, so that the folder goes with the rest.
    SetFileAttributesW((dir + L"\\hsub").c_str(), FILE_ATTRIBUTE_DIRECTORY);
    SetFileAttributesW((dir + L"\\h.txt").c_str(), FILE_ATTRIBUTE_NORMAL);
    SetFileAttributesW((pics + L"\\hp.bmp").c_str(), FILE_ATTRIBUTE_NORMAL);

    const FaintDrive& d = g_faint;
    std::wstring notes;
    if (!main) notes += L"窗口没建出来";
    else if (d.ticks > 800) notes += L"超时";
    const auto of = [](int v) { return std::to_wstring(v); };
    const auto verdict = [&](const std::wstring& name, const std::wstring& why, bool ok) {
        std::wstring label = name;
        if (!notes.empty()) label += L"：" + notes;
        else if (!ok) label += L"：" + why;
        check(label.c_str(), label == name);
    };
    check(L"（测试基准）隐藏项画得虚：屏幕上的画面读得回来，且与按状态渲染的一致", d.baseline);
    verdict(L"显示隐藏文件时，隐藏的文件和文件夹也在列表里", L"没有都列出来", d.listed);
    const auto twins = [&](int k, int pairs) {
        std::wstring why;
        for (int p = 0; p < pairs && d.listed; ++p) {
            if (d.off[k][p] != 0)
                why += std::wstring(p ? L"；" : L"") + (p ? L"隐藏文件的图标有 " : L"隐藏文件夹的图标有 ") +
                       of(d.off[k][p] < 0 ? 0 : d.off[k][p]) + L" 个像素不是各一半";
            else if (d.ink[k][p] >= 0 && d.ink[k][p] < 10)
                why += std::wstring(p ? L"；" : L"") + (p ? L"隐藏文件的名字不是黑的" : L"隐藏文件夹的名字不是黑的");
        }
        return why;
    };
    for (int k = 0; k < kFaintLookCount; ++k) {
        const std::wstring why = twins(k, 2);
        verdict(std::wstring(L"隐藏的文件和文件夹画得虚一些（") + kFaintLooks[k].name +
                    L"）：图标的每个像素都是它正常时的图标与背后的颜色各一半，名字还是黑的",
                why, why.empty());
    }
    const std::wstring treeWhy =
        !d.treeSeen ? L"树的窗口里看不到这两个文件夹的行" : twins(kFaintTree, 1);
    verdict(L"文件夹树里，隐藏的文件夹的图标也是虚的（与同级的正常文件夹比，各一半）", treeWhy,
            d.treeSeen && treeWhy.empty());
    verdict(L"图片文件夹里，隐藏图片的缩略图也是虚的（与同色的正常图片比，各一半）",
            d.thumbWait ? L"缩略图没有及时出来" : L"隐藏图片的缩略图有 " + of(std::max(0, d.off[kFaintThumb][0])) +
                                                          L" 个像素不是各一半",
            !d.thumbWait && d.off[kFaintThumb][0] == 0);
    verdict(L"不显示隐藏文件时，隐藏的文件和文件夹不在列表里", L"还在列表里，或正常的那个不在了",
            d.notListed);
}

// ---------------------------------------------------------------- takeover tests
// This program in place of Explorer for opening folders (takeover.h), where
// nothing is live: under a key of HKEY_CURRENT_USER in the same shape as the
// real one (the self-test's is under its own).
std::wstring g_takeoverKey;

// What `value` of `key` says (the command itself when `value` is null):
// false when it is not there, or says something else.
bool takeoverSays(const wchar_t* key, const wchar_t* value, const wchar_t* want) {
    wchar_t buf[2048]{};
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CURRENT_USER, key, value, RRF_RT_REG_SZ, nullptr, buf, &size) ==
               ERROR_SUCCESS &&
           wcscmp(buf, want) == 0;
}

// Writes `what` under `value` of `key` (null: takes the value out).
bool takeoverPut(const wchar_t* key, const wchar_t* value, const wchar_t* what) {
    HKEY h = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &h,
                        nullptr) != ERROR_SUCCESS)
        return false;
    const LSTATUS put =
        what ? RegSetValueExW(h, value, 0, REG_SZ, (const LPBYTE)what,
                              (DWORD)((wcslen(what) + 1) * sizeof(wchar_t)))
             : RegDeleteValueW(h, value);
    RegCloseKey(h);
    return put == ERROR_SUCCESS || (!what && put == ERROR_FILE_NOT_FOUND);
}

// The command's key is gone, and so are the keys made on the way to it (the
// ...\Folder one): taking this program out left nothing of itself behind.
bool takeoverKeyGone(const std::wstring& key) {
    const size_t cut = key.rfind(L"\\shell\\");
    if (cut == std::wstring::npos) return false;
    HKEY h = nullptr;
    return RegOpenKeyExW(HKEY_CURRENT_USER, key.substr(0, cut).c_str(), 0, KEY_READ, &h) ==
               ERROR_FILE_NOT_FOUND &&
           !h;
}

constexpr UINT_PTR kTakeoverStepTimer = 0x4D50;   // the next step
constexpr UINT_PTR kTakeoverClickTimer = 0x4D51;  // ... while the Tools menu is up
constexpr wchar_t kTakeoverTestClass[] = L"MyOldComputerTakeoverTest";
constexpr int kTakeoverTestEnd = 3;
constexpr int kToolsMenu = 4;  // 文件 编辑 查看 收藏 工具 帮助
// Set: the real key is written for this program once, and taken out again.
constexpr wchar_t kTakeoverLive[] = L"MOC_LIVE_TAKEOVER";

struct TakeoverDrive {
    HWND main = nullptr;
    int step = 0;
    int ticks = 0;
    int waits = 0;            // how long the Tools menu was waited for
    int rowY = 0;             // the row of 设为默认文件管理器 in it
    int ink[2] = {-1, -1};    // black pixels of its tick, as the screen shows it, before each click
    bool clicked[2] = {};
    bool listed = false;      // the item is in the Tools menu, not ticked while Explorer opens folders
    bool on = false, written = false, delegated = false;
    bool off = false, gone = false;
};
TakeoverDrive g_takeover;

// Black pixels of the tick at the start of a row of the popup (menu.cpp draws
// it 7 to 15 px in, 3 px either side of the row's middle), read from the
// screen: the popup is on top of everything, so that is what is shown.
int takeoverTickInk(HWND popup, int rowMiddle, double scale) {
    int n = 0;
    for (int y = rowMiddle - pxOf(scale, 4); y <= rowMiddle + pxOf(scale, 4); ++y)
        for (int x = pxOf(scale, 6); x <= pxOf(scale, 16); ++x)
            n += (screenPixel(popup, x, y) & 0xFFFFFF) == (Theme::text & 0xFFFFFF);
    return n;
}

// The window of `pid`'s program, once it has one.
struct ProgramWindow {
    DWORD pid;
    HWND found;
};
HWND windowOfProgram(DWORD pid) {
    ProgramWindow found{pid, nullptr};
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* found = (ProgramWindow*)lp;
        DWORD owner = 0;
        wchar_t cls[64]{};
        GetWindowThreadProcessId(h, &owner);
        if (owner == found->pid && IsWindowVisible(h) && GetClassNameW(h, cls, 64) &&
            lstrcmpW(cls, L"MyOldComputerWindow") == 0) {
            found->found = h;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&found);
    return found.found;
}

// The folder a window of the program shows: asked (dumpLayout), it writes its
// layout out, and the line that says which folder is read back.
bool windowShowsFolder(HWND window, const std::wstring& folder) {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    DeleteFileW((std::wstring(temp) + L"moc-layout.txt").c_str());
    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(window, g_dumpMsg, 0, 0, SMTO_ABORTIFHUNG, 5000, &result)) return false;
    const std::vector<std::wstring> lines = dumpLines(L"current ");
    if (lines.empty()) return false;
    std::wstring shown = lines.front().substr(8);  // past "current "
    shown.erase(shown.find_first_of(L"\r\n"));     // (to the end of the line)
    return _wcsicmp(shown.c_str(), folder.c_str()) == 0;
}

// Explorer's windows, to tell one that comes up from those that were there.
std::vector<HWND> explorerWindows() {
    struct Found {
        std::vector<HWND> out;
    } found;
    EnumWindows([](HWND h, LPARAM lp) -> BOOL {
        auto* found = (Found*)lp;
        wchar_t cls[64]{};
        if (IsWindowVisible(h) && GetClassNameW(h, cls, 64) && lstrcmpW(cls, L"CabinetWClass") == 0)
            found->out.push_back(h);
        return TRUE;
    }, (LPARAM)&found);
    return found.out;
}

// The processes of this program started since `since` (whatever started them).
std::vector<DWORD> programSince(FILETIME since) {
    std::vector<DWORD> out;
    wchar_t exe[2048]{};
    GetModuleFileNameW(nullptr, exe, 2048);
    const std::wstring name = fsx::fileName(exe);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    const DWORD me = GetCurrentProcessId();
    for (BOOL more = Process32FirstW(snap, &e); more; more = Process32NextW(snap, &e)) {
        if (e.th32ProcessID == me || _wcsicmp(e.szExeFile, name.c_str()) != 0) continue;
        HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                               e.th32ProcessID);
        if (!h) continue;
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(h, &created, &exited, &kernel, &user);
        const bool fresh = CompareFileTime(&created, &since) >= 0;
        CloseHandle(h);
        if (fresh) out.push_back(e.th32ProcessID);
    }
    CloseHandle(snap);
    return out;
}

struct LiveDrive {
    bool on = false;         // the steps are the live test's
    int step = 0, ticks = 0, waits = 0;
    std::wstring base;       // the tests' folder
    std::wstring folder;     // opened the way the shell opens one
    FILETIME since{};        // when the key was written: what starts after that is the shell's doing
    std::vector<HWND> explorer;  // Explorer's windows before the hand-over
    DWORD handedPid = 0;     // the program's process started on what is not a folder
    HANDLE handed = nullptr;
    bool wasThere = false;   // ... which showed a window of this program, though it should have gone
    bool first = false;      // nothing was written for this user to begin with
    bool wrote = false;      // ... and this test wrote this program there
    bool written = false;    // the shell sees this program open folders now
    bool opened = false, closed = false;  // a folder opened the shell's way started this program on it; gone again
    bool handedOver = false, explorerOpened = false;  // what is not a folder went to Explorer
    bool restored = false;  // and Explorer's is what the shell sees again
};
LiveDrive g_live;
constexpr int kTakeoverLiveEnd = 6;

void takeoverTestStep(HWND driver);
void takeoverLiveStep(HWND driver);

LRESULT CALLBACK takeoverTestProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TIMER && wp == kTakeoverClickTimer) {
        // The Tools menu is up: its tick is read off the screen, then the row
        // is clicked.  (Not up yet: looked again, a few times.)
        KillTimer(hwnd, kTakeoverClickTimer);
        HWND root = nullptr;
        menuWindows(&root);  // also finds g_popup
        TakeoverDrive& d = g_takeover;
        const int k = d.step - 1;
        if (g_popup && IsWindow(g_popup) && k >= 0 && k < 2) {
            d.ink[k] = takeoverTickInk(g_popup, d.rowY, g_app.scale);
            // The menu as it is on screen just then, kept with the frames (MOC_DEBUG).
            RECT popup{};
            GetWindowRect(g_popup, &popup);
            keepScreen(k == 0 ? L"takeover-unticked" : L"takeover-ticked", popup);
            PostMessageW(g_popup, WM_LBUTTONUP, 0, MAKELPARAM(pxOf(g_app.scale, 40), d.rowY));
            d.clicked[k] = true;
        } else if (g_popup && IsWindow(g_popup)) {
            PostMessageW(g_popup, WM_KEYDOWN, VK_ESCAPE, 0);  // no step of ours: the menu is not sat in
        } else if (++d.waits < 40) {
            SetTimer(hwnd, kTakeoverClickTimer, 60, nullptr);
        }
        return 0;
    }
    if (msg == WM_TIMER && wp == kTakeoverStepTimer) {
        KillTimer(hwnd, kTakeoverStepTimer);
        if (g_live.on)
            takeoverLiveStep(hwnd);
        else if (++g_takeover.ticks > 200)  // forty seconds: give up, do not hang
            g_takeover.step = kTakeoverTestEnd;
        else
            takeoverTestStep(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Opens the Tools menu of the test's window and lets the click timer click
// the item's row: the menu's own loop runs meanwhile.
void takeoverClick(HWND driver) {
    g_takeover.waits = 0;
    SetTimer(driver, kTakeoverClickTimer, 300, nullptr);
    const RECT title = g_app.menuItemRect(kToolsMenu);
    showAppMenu(g_takeover.main, g_app, kToolsMenu,
                POINT{(title.left + title.right) / 2, (title.top + title.bottom) / 2});
    KillTimer(driver, kTakeoverClickTimer);
}

void takeoverTestStep(HWND driver) {
    TakeoverDrive& d = g_takeover;
    const double scale = g_app.scale;
    // Where the item's row is in the menu: rows from 3 px down, 22 px each,
    // 7 for a separator.
    int y = pxOf(scale, 3);
    bool found = false, ticked = false;
    for (const MenuItem& it : g_app.menuFor(L"工具")) {
        y += it.separator ? pxOf(scale, 7) : pxOf(scale, 22);
        if (it.id == ID_TAKEOVER_FOLDERS) {
            d.rowY = y - pxOf(scale, 11);
            found = true;
            ticked = it.checked;
        }
    }
    switch (d.step) {
    case 0:  // not ticked while Explorer opens folders; clicked: on
        d.listed = found && !ticked && takeover::state() == takeover::State::explorer;
        ++d.step;
        takeoverClick(driver);
        d.on = d.clicked[0] && takeover::state() == takeover::State::ours && takeover::on();
        d.written = takeoverSays(g_takeoverKey.c_str(), nullptr, takeover::command().c_str());
        d.delegated = takeoverSays(g_takeoverKey.c_str(), L"DelegateExecute", L"");
        break;
    case 1:  // ticked now; clicked again: off
        ++d.step;
        takeoverClick(driver);
        d.off = d.clicked[1] && takeover::state() == takeover::State::explorer && !takeover::on();
        d.gone = takeoverKeyGone(g_takeoverKey);
        break;
    default:
        d.step = kTakeoverTestEnd;
        break;
    }
    if (d.step < kTakeoverTestEnd) SetTimer(driver, kTakeoverStepTimer, 200, nullptr);
}

HWND takeoverDriver() {
    WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
    wc.lpfnWndProc = takeoverTestProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kTakeoverTestClass;
    RegisterClassExW(&wc);
    return CreateWindowExW(0, kTakeoverTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100, nullptr,
                           nullptr, wc.hInstance, nullptr);  // never shown
}

// The program's own window, behind everything, showing `dir` - as the tests
// of a real window have theirs.
HWND takeoverWindow(HWND driver, const wchar_t* title, const std::wstring& dir) {
    registerMainClass(GetModuleHandleW(nullptr));
    HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", title,
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver, nullptr,
                                GetModuleHandleW(nullptr), nullptr);
    if (!main) return nullptr;
    g_app.hwnd = main;
    g_app.scale = dpiScaleOfWindow(main);
    g_app.init(main);
    g_app.buildTree();
    g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
    g_app.loadDirectory(dir, false);
    g_app.setViewMode(ViewMode::Details);
    g_app.clearSelection();
    placeWindow(main, 0, 0, g_app.width, g_app.height);
    ShowWindow(main, SW_SHOWNOACTIVATE);
    SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    UpdateWindow(main);
    return main;
}

void takeoverWindowGone(HWND driver, HWND main) {
    if (IsWindow(main)) {
        g_app.hwnd = nullptr;  // the edit is the window's child and goes with it
        DestroyWindow(main);
    }
    MSG msg{};
    PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
    g_app.hwnd = nullptr;
    g_app.addrEdit = nullptr;
    g_app.addrEditing = false;
    if (g_app.addrFont) {
        DeleteObject(g_app.addrFont);
        g_app.addrFont = nullptr;
    }
    DestroyWindow(driver);
}

void takeoverMenuTest(const std::wstring& dir, const std::function<void(const wchar_t*, bool)>& check) {
    HWND driver = takeoverDriver();
    g_takeover = TakeoverDrive{};
    g_live.on = false;
    const HWND main = takeoverWindow(driver, L"takeover test", dir);
    g_takeover.main = main;
    if (main) {
        MSG msg;
        for (SetTimer(driver, kTakeoverStepTimer, 100, nullptr);
             g_takeover.step < kTakeoverTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        KillTimer(driver, kTakeoverStepTimer);
    }
    takeoverWindowGone(driver, main);

    const TakeoverDrive& d = g_takeover;
    const auto of = [](int v) { return std::to_wstring(v); };
    std::wstring notes;
    if (!main) notes += L"窗口没建出来";
    else if (d.ticks > 200) notes += L"超时";
    const auto verdict = [&](const std::wstring& name, const std::wstring& why, bool ok) {
        std::wstring label = name;
        if (!notes.empty()) label += L"：" + notes;
        else if (!ok) label += L"：" + why;
        check(label.c_str(), label == name);
    };
    std::wstring why;
    if (!d.clicked[0]) why = L"没点到那一项";
    else if (d.ink[0] < 0) why = L"没读到那一行的勾";
    else why = L"有勾的笔画：屏幕上那一行有 " + of(d.ink[0]) + L" 个黑色像素";
    verdict(L"「工具」菜单里有「设为默认文件管理器」：没接管的时候没有勾，屏幕上那一行没有勾的笔画", why,
            d.listed && d.clicked[0] && d.ink[0] == 0);
    if (!d.clicked[0]) why = L"没点到";
    else if (!d.on) why = L"注册表里没有写成接管";
    else if (!d.written) why = L"写的不是本程序加文件夹";
    else if (!d.delegated) why = L"DelegateExecute 没留空";
    else why = L"勾的笔画只有 " + of(std::max(0, d.ink[1])) + L" 个黑色像素";
    verdict(L"点一下「设为默认文件管理器」：接管打开，注册表里写的是本程序加文件夹、Explorer 的 DelegateExecute 留空；"
            L"再打开菜单，屏幕上那一行有勾",
            why, d.on && d.written && d.delegated && d.clicked[1] && d.ink[1] >= 8);
    if (!d.clicked[1]) why = L"没点到";
    else if (!d.off) why = L"注册表里还是接管";
    else why = L"留下了空的键";
    verdict(L"再点一下：接管关掉，注册表里为本程序写下的都没了，回到资源管理器", why, d.off && d.gone);
}

// ---------------------------------------------------------- the real key, once, by hand
// What the shell sees for opening a folder: the user's key and the machine's
// as one (`value`: the command itself when null).  Read as written: what is
// there for the machine is a value that gets expanded when it is read
// (%SystemRoot%\Explorer.exe).
bool shellSees(const wchar_t* value, const wchar_t* want) {
    wchar_t buf[2048]{};
    DWORD size = sizeof(buf);
    return RegGetValueW(HKEY_CLASSES_ROOT, L"Folder\\shell\\open\\command", value,
                        RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, nullptr, buf,
                        &size) == ERROR_SUCCESS &&
           wcscmp(buf, want) == 0;
}

void takeoverLiveStep(HWND driver) {
    LiveDrive& d = g_live;
    const auto later = [&](int ms) { SetTimer(driver, kTakeoverStepTimer, ms, nullptr); };
    const auto end = [&]() {
        if (d.handed) {
            CloseHandle(d.handed);
            d.handed = nullptr;
        }
        d.step = kTakeoverLiveEnd;
    };
    if (d.step >= kTakeoverLiveEnd) return;
    if (++d.ticks > 600) {  // three minutes: give up, do not hang
        end();
        return;
    }
    switch (d.step) {
    case 0: {  // nothing is written for this user; this program is written, and the shell sees it
        takeover::useKey(nullptr);  // the real key
        d.first = takeover::state() == takeover::State::explorer;
        if (!d.first) {
            end();  // somebody's is there (another program's, or this program's from before): not touched
            break;
        }
        GetSystemTimeAsFileTime(&d.since);
        d.wrote = true;
        d.written = takeover::set(true) && takeover::state() == takeover::State::ours &&
                    shellSees(nullptr, takeover::command().c_str()) &&
                    shellSees(L"DelegateExecute", L"");
        // A folder, opened the way anything opens one: what is written is what runs.
        d.folder = d.base + L"\\live";
        CreateDirectoryW(d.folder.c_str(), nullptr);
        SHELLEXECUTEINFOW sei{sizeof(sei)};
        sei.lpFile = d.folder.c_str();
        sei.lpVerb = L"open";
        sei.nShow = SW_SHOWNOACTIVATE;  // (this test's environment makes the new window stay behind)
        sei.fMask = SEE_MASK_NOASYNC;
        if (d.written) ShellExecuteExW(&sei);
        d.step = 1;
        later(400);
        break;
    }
    case 1: {  // ... and this program came up on that folder
        for (DWORD pid : programSince(d.since)) {
            const HWND w = windowOfProgram(pid);
            if (!w) continue;
            d.opened = windowShowsFolder(w, d.folder);
            PostMessageW(w, WM_CLOSE, 0, 0);
            d.waits = 0;
            d.step = 2;
            later(300);
            return;
        }
        if (++d.waits < 40)  // given twelve seconds
            later(300);
        else {
            d.waits = 0;
            d.step = 2;
        }
        break;
    }
    case 2: {  // ... and is gone again
        d.closed = programSince(d.since).empty();
        if (!d.closed) {
            if (++d.waits < 30) {  // given nine seconds
                later(300);
                break;
            }
        }
        // What is not a folder (the shell's own: the Recycle Bin): this program
        // is started on it the way the shell starts it.
        d.explorer = explorerWindows();
        wchar_t exe[2048]{};
        GetModuleFileNameW(nullptr, exe, 2048);
        std::wstring line = L"\"" + std::wstring(exe) + L"\" --path \"shell:RecycleBinFolder\"";
        std::vector<wchar_t> command(line.begin(), line.end());
        command.push_back(0);
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si,
                           &pi)) {
            d.handed = pi.hProcess;
            d.handedPid = pi.dwProcessId;
            CloseHandle(pi.hThread);
        }
        d.waits = 0;
        d.step = 3;
        later(100);
        break;
    }
    case 3: {  // ... gives it to Explorer and goes, a window of its own never up
        if (d.handed) {
            d.wasThere = d.wasThere || windowOfProgram(d.handedPid) != nullptr;
            if (WaitForSingleObject(d.handed, 0) == WAIT_OBJECT_0) {
                DWORD code = 1;
                GetExitCodeProcess(d.handed, &code);
                CloseHandle(d.handed);
                d.handed = nullptr;
                d.handedOver = code == 0 && !d.wasThere;
            } else if (++d.waits < 40) {  // given twelve seconds
                later(300);
                break;
            } else {
                TerminateProcess(d.handed, 0);  // this test's own
                CloseHandle(d.handed);
                d.handed = nullptr;
            }
        }
        d.waits = 0;
        d.step = 4;
        later(800);  // Explorer takes a moment
        break;
    }
    case 4: {  // ... and Explorer shows it: a window that was not there before
        std::vector<HWND> fresh;
        for (HWND h : explorerWindows())
            if (std::find(d.explorer.begin(), d.explorer.end(), h) == d.explorer.end())
                fresh.push_back(h);
        for (HWND h : fresh) PostMessageW(h, WM_CLOSE, 0, 0);  // the test's: closed again
        d.explorerOpened = !fresh.empty();
        if (!d.explorerOpened && ++d.waits < 8) {  // looked again, three seconds in all
            later(400);
            break;
        }
        d.step = 5;
        later(500);
        break;
    }
    default: {  // taken out: the shell sees Explorer's again
        d.restored = takeover::set(false) && takeover::state() == takeover::State::explorer &&
                     shellSees(nullptr, L"%SystemRoot%\\Explorer.exe") &&
                     shellSees(L"DelegateExecute", L"{11dbb47c-a525-400b-9e80-a54615a090c0}");
        end();
        break;
    }
    }
}

void takeoverLiveTest(const std::wstring& base, const std::function<void(const wchar_t*, bool)>& check) {
    HWND driver = takeoverDriver();
    g_live = LiveDrive{};
    g_live.on = true;
    g_live.base = base;
    MSG msg;
    for (SetTimer(driver, kTakeoverStepTimer, 100, nullptr);
         g_live.step < kTakeoverLiveEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    KillTimer(driver, kTakeoverStepTimer);
    // Whatever ended the steps, what this test wrote on the real key is taken out.
    if (g_live.wrote && takeover::on()) takeover::set(false);
    takeover::useKey(g_takeoverKey.c_str());
    DestroyWindow(driver);

    const LiveDrive& d = g_live;
    std::wstring notes;
    if (d.ticks > 600) notes += L"超时";
    const auto verdict = [&](const std::wstring& name, const std::wstring& why, bool ok) {
        std::wstring label = name;
        if (!notes.empty()) label += L"：" + notes;
        else if (!ok) label += L"：" + why;
        check(label.c_str(), label == name);
    };
    std::wstring why = d.first ? L"系统看到的不是本程序" : L"起初就已经有接管在，没动它";
    verdict(L"实机（真的注册表项）：写入后，系统看到的是本程序打开文件夹", why, d.first && d.written);
    if (!d.written) why = L"没有写入，没往下测";
    else if (!d.opened) why = L"没有等来本程序的窗口，或它显示的不是那个文件夹";
    else why = L"那个窗口没有关掉";
    verdict(L"实机：像系统那样打开一个文件夹，启动的就是本程序，显示的正是那个文件夹", why,
            d.written && d.opened && d.closed);
    if (!d.written) why = L"没有写入，没往下测";
    else if (!d.handedOver) why = L"本程序没有交出去并退出";
    else why = L"没有等来资源管理器的窗口";
    verdict(L"实机：回收站不是文件夹——本程序把它交给资源管理器、自己退出，资源管理器打开了它", why,
            d.written && d.handedOver && d.explorerOpened);
    why = d.wrote ? L"系统看到的不是资源管理器" : L"没写过，没往下测";
    verdict(L"实机：恢复后，系统看到的又是资源管理器", why, d.wrote && d.restored);
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

    // "This program opens folders" is tried under a scratch key (takeover.h):
    // the machine is somebody's, and the real one is live.
    g_takeoverKey = L"Software\\MyOldComputer\\Tests\\Ui\\Folder\\shell\\open\\command";
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\MyOldComputer\\Tests\\Ui");
    takeover::useKey(g_takeoverKey.c_str());

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
    // The tests make their own input, and read the screen back.  A mouse or a
    // keyboard in real use meanwhile can change what is on screen (a menu
    // gone, a window brought to the front): a check that fails close to some
    // is said to, for the failure may not be the program's.
    DWORD inputWas = 0, inputAt = 0;
    int inputs = 0;
    const auto realInputNear = [&]() {
        LASTINPUTINFO info{sizeof(LASTINPUTINFO), 0};
        if (GetLastInputInfo(&info) && info.dwTime && info.dwTime != inputWas) {
            if (inputWas) ++inputs;
            inputWas = info.dwTime;
            inputAt = GetTickCount();
        }
        return inputAt && GetTickCount() - inputAt < 1500;
    };
    auto check = [&results, &writeOut, &realInputNear](const wchar_t* name, bool ok) {
        const bool realInput = realInputNear();  // asked of every check, passed or failed
        results.push_back({std::wstring(name) +
                               (!ok && realInput ? L"｜这前后 1.5 秒内有真实的鼠标或键盘输入" : L""),
                           ok});
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

    // The shell's extensions are loaded before the first menu is built, never
    // while one is being built: a DLL that makes a window as it is loaded (and
    // one installed here does), loaded while a folder's menu is built, leaves
    // the window dead for good (shellmenu.h, preloadHandlers).  Seen at the
    // moment it kills: when the shell starts listing the libraries, every
    // extension's DLL that is on disk is in.  This is the first menu this
    // process builds.
    {
        const auto of = [](int v) { return std::to_wstring(v); };
        g_preloadWatch = PreloadWatch{};
        int unloadedBefore = 0;
        const std::vector<std::wstring> listed = shellmenu::handlerDlls();
        for (const std::wstring& dll : listed) {
            if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
            g_preloadWatch.dlls.push_back(dll);
            if (!GetModuleHandleW(dll.c_str())) ++unloadedBefore;
        }
        const HHOOK hook = SetWindowsHookExW(WH_CBT, preloadCbtProc, nullptr, GetCurrentThreadId());
        const std::vector<std::wstring> first = shellmenu::labels(base, {sub}, shellmenu::ViewState{});
        if (hook) UnhookWindowsHookEx(hook);
        const int missing = std::max(0, g_preloadWatch.missing);
        // The list is the registry's own, read here apart from the code under
        // test: every extension of any file, of any folder, of a drive and of
        // the background of a folder has its DLL in it.
        int handlers = 0, uncovered = 0;
        static const wchar_t* const keys[] = {
            L"*\\shellex\\ContextMenuHandlers",
            L"AllFilesystemObjects\\shellex\\ContextMenuHandlers",
            L"Folder\\shellex\\ContextMenuHandlers",
            L"Directory\\shellex\\ContextMenuHandlers",
            L"Directory\\Background\\shellex\\ContextMenuHandlers",
            L"Drive\\shellex\\ContextMenuHandlers",
        };
        for (const wchar_t* key : keys) {
            HKEY open = nullptr;
            if (RegOpenKeyExW(HKEY_CLASSES_ROOT, key, 0, KEY_READ, &open) != ERROR_SUCCESS) continue;
            for (DWORD i = 0;; ++i) {
                wchar_t name[256]{}, value[256]{}, path[1024]{};
                DWORD size = 256, valueSize = sizeof(value), pathSize = sizeof(path);
                if (RegEnumKeyExW(open, i, name, &size, nullptr, nullptr, nullptr, nullptr) !=
                    ERROR_SUCCESS)
                    break;
                HKEY handler = nullptr;
                std::wstring clsid = name;  // or the value, when there is one
                if (RegOpenKeyExW(open, name, 0, KEY_QUERY_VALUE, &handler) == ERROR_SUCCESS) {
                    if (RegQueryValueExW(handler, nullptr, nullptr, nullptr, (LPBYTE)value,
                                         &valueSize) == ERROR_SUCCESS &&
                        value[0])
                        clsid = value;
                    RegCloseKey(handler);
                }
                bool got = false;
                HKEY server = nullptr;
                const std::wstring where = L"CLSID\\" + clsid + L"\\InprocServer32";
                if (RegOpenKeyExW(HKEY_CLASSES_ROOT, where.c_str(), 0, KEY_QUERY_VALUE, &server) ==
                    ERROR_SUCCESS) {
                    got = RegQueryValueExW(server, nullptr, nullptr, nullptr, (LPBYTE)path,
                                           &pathSize) == ERROR_SUCCESS &&
                          path[0];
                    RegCloseKey(server);
                }
                if (!got) continue;
                ++handlers;
                bool in = false;  // by the file's name: the path may be written another way
                for (const std::wstring& dll : listed)
                    if (_wcsicmp(PathFindFileNameW(dll.c_str()), PathFindFileNameW(path)) == 0)
                        in = true;
                if (!in) ++uncovered;
            }
            RegCloseKey(open);
        }
        const bool ok = hook != nullptr && !first.empty() && !g_preloadWatch.dlls.empty() &&
                        unloadedBefore > 0 && g_preloadWatch.listed > 0 && missing == 0 &&
                        handlers > 0 && uncovered == 0;
        std::wstring label = L"右键扩展的 DLL 在构建菜单前就已加载好：shell 开始列库的那一刻一个不缺";
        if (!ok)
            label += L"：菜单 " + of((int)first.size()) + L" 项，磁盘上的扩展 DLL " +
                     of((int)g_preloadWatch.dlls.size()) + L" 个（构建前未加载 " + of(unloadedBefore) +
                     L" 个），shell 开始列库 " + of(g_preloadWatch.listed) + L" 次，那一刻还缺 " +
                     of(missing) + L" 个；注册的右键扩展 " + of(handlers) +
                     L" 个，不在清单里的 " + of(uncovered) + L" 个" +
                     (hook ? L"" : L"，挂钩没装上");
        check(label.c_str(), ok);
    }
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
        std::wstring onBox, band, ink, line, kept, outside, icon, one;  // first failure of each
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
                    // Where the icon sits, by the renderer's own numbers.
                    const int iconX = r.left + px(view == ViewMode::Details ? 3 : 2);
                    const RECT iconRect{iconX, r.top + px(2), iconX + px(16),
                                        r.top + px(2) + px(16)};
                    // 5. The icon keeps its selection box, and only the icon:
                    //    none of it is left under or beside the field.  An
                    //    icon-view label is the field now.  While the row is
                    //    renamed the box is the system's highlight - the
                    //    colour of the selected part of the name in the box.
                    const PixelBox blue = find(b, blueArea, isHighlight);
                    const bool iconKept =
                        icons ? blue.count == 0
                              : blue.count && blue.left == box.left && blue.right == frame.left;
                    if (!iconKept)
                        note(kept, where + L"选中底色 " + boxOf(blue) + L"（图标到 " +
                                        std::to_wstring(frame.left) + L" 为止）");
                    // 8. One blue for the row: none of the app's navy is left
                    //    in it (outside the icon, which may have any colour).
                    //    Where the system's highlight is that navy - the
                    //    Windows 98 scheme - there is nothing to tell apart.
                    int navyLeft = find(b, blueArea, isNavy).count;
                    if (!icons) navyLeft -= find(b, iconRect, isNavy).count;
                    if (isNavy(GetSysColor(COLOR_HIGHLIGHT))) navyLeft = 0;
                    if (navyLeft)
                        note(one, where + L"还有深蓝 " + std::to_wstring(navyLeft) + L" 像素");

                    // 7. The icon is still there: the selection box goes
                    //    under it, so a selected row is not a bare blue box.
                    if (!icons) {
                        if (!find(b, iconRect, iconInkOnHighlight).count)
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
        verdict(L"改名的那一行里不再有程序自己的深蓝", one);
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

    // Listing a folder in the tree does not ask every folder in it whether it
    // has a subfolder: for System32 that was some 300 of them, all before the
    // new folder could show.  A row is asked when it is drawn or clicked - and
    // the answer has to be the disk's either way.
    {
        const std::wstring parent = base + L"\\treeparent";
        CreateDirectoryW(parent.c_str(), nullptr);
        CreateDirectoryW((parent + L"\\plain").c_str(), nullptr);
        CreateDirectoryW((parent + L"\\withsub").c_str(), nullptr);
        CreateDirectoryW((parent + L"\\withsub\\inner").c_str(), nullptr);
        app.subdirCache.clear();
        app.treeProbes = 0;
        app.setScale(1.0);
        const bool sidebarWas = app.sidebarVisible;
        const std::vector<TreeNode> treeWas = app.treeRoots;
        const std::vector<std::wstring> backWas = app.back, forwardWas = app.forward;
        TreeNode node;
        node.path = parent;
        node.title = L"treeparent";
        app.loadTreeChildren(node);
        const int askedAtListing = app.treeProbes;  // the disk's answers, not the flags
        bool unasked = node.children.size() == 2;
        for (const auto& c : node.children) unasked = unasked && !c.hasChildrenKnown;
        node.expanded = true;
        app.sidebarVisible = true;
        app.treeRoots.clear();
        app.treeRoots.push_back(node);
        app.treeClickRow = -1;
        app.contextTreeRow = -1;
        app.flattenTree();  // the folder, then plain, then withsub
        app.treeScroll = 0;
        app.relayout(1100, 760);
        // The window as it would be painted now, read at one point.
        const auto pixelAt = [&app](POINT p) {
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
            return px;
        };
        // A row's +/- box, by the renderer's own numbers (depth 1).
        const auto boxOf = [&app](int row) {
            const RECT r = app.treeRowRect(row);
            const int indent = pxOf(app.scale, 1 * 14 + 3);
            return RECT{r.left + indent, r.top + pxOf(app.scale, 5),
                        r.left + indent + pxOf(app.scale, 9),
                        r.top + pxOf(app.scale, 5) + pxOf(app.scale, 9)};
        };
        const RECT plainBox = boxOf(1), withsubBox = boxOf(2);
        const int plainY = (plainBox.top + plainBox.bottom) / 2;
        const int withsubY = (withsubBox.top + withsubBox.bottom) / 2;
        // The box's left edge: its frame where there is a box, the pane's
        // white where there is none.
        const COLORREF plainEdge = pixelAt(POINT{plainBox.left, plainY});
        const COLORREF withsubEdge = pixelAt(POINT{withsubBox.left, withsubY});
        const int askedByDrawing = app.treeProbes - askedAtListing;  // the two rows in view, no more
        const bool rows = app.treeFlat.size() == 3;
        TreeNode& plain = app.treeRoots[0].children[0];
        TreeNode& withsub = app.treeRoots[0].children[1];
        const bool drawn = rows && plain.path == parent + L"\\plain" &&
                           withsub.path == parent + L"\\withsub" &&
                           plainEdge == RGB(255, 255, 255) && withsubEdge == Theme::shadow &&
                           plain.hasChildrenKnown && !plain.hasChildren && withsub.hasChildrenKnown &&
                           withsub.hasChildren;
        // Never drawn, never asked: a click on the box asks first.
        const auto forget = [&] {
            for (TreeNode* c : {&plain, &withsub}) {
                c->hasChildrenKnown = false;
                c->hasChildren = true;
                c->expanded = false;
            }
        };
        forget();
        const POINT withsubPoint{(withsubBox.left + withsubBox.right) / 2, withsubY};
        onLButtonDown(nullptr, app, withsubPoint);
        onLButtonUp(nullptr, app, withsubPoint);
        const bool opened = withsub.expanded && withsub.hasChildren && withsub.children.size() == 1;
        forget();
        const POINT plainPoint{(plainBox.left + plainBox.right) / 2, plainY};
        onDoubleClick(nullptr, app, plainPoint);  // the same question, from a double-click
        const bool notOpenedByDoubleClick = !plain.expanded && !plain.hasChildren;
        forget();
        onLButtonDown(nullptr, app, plainPoint);
        onLButtonUp(nullptr, app, plainPoint);
        const bool stayed = !plain.expanded && !plain.hasChildren;
        app.treeRoots = treeWas;
        app.back = backWas;
        app.forward = forwardWas;
        app.flattenTree();
        app.sidebarVisible = sidebarWas;
        app.treeClickRow = -1;
        app.loadDirectory(base, false);
        app.relayout(1100, 760);

        const wchar_t* const askedName = L"树列出一个文件夹时，不逐个问它的子文件夹有没有下级";
        std::wstring asked = askedName;
        if (askedAtListing)
            asked += L"：列出的时候就问了磁盘 " + std::to_wstring(askedAtListing) + L" 个文件夹";
        else if (!unasked) asked += L"：列出的时候就问过了";
        else if (askedByDrawing != 2)
            asked += L"：画出来的时候问了 " + std::to_wstring(askedByDrawing) + L" 个，应为 2";
        check(asked.c_str(), asked == askedName);

        const wchar_t* const shownName = L"树的 +/− 画出来和磁盘一致：有下级的有，没有的没有";
        std::wstring shown = shownName;
        wchar_t detail[160]{};
        if (!rows)
            swprintf_s(detail, L"：行数 %d，应为 3", (int)app.treeFlat.size());
        else if (!drawn)
            swprintf_s(detail, L"：plain 行框左缘 0x%06lX（应为白），withsub 行框左缘 0x%06lX（应为 0x%06lX）",
                       (unsigned long)plainEdge, (unsigned long)withsubEdge,
                       (unsigned long)Theme::shadow);
        shown += detail;
        check(shown.c_str(), shown == shownName);

        const wchar_t* const clickedName = L"点到还没画过的树行：+/− 也是对的";
        std::wstring clicked = clickedName;
        if (!opened) clicked += L"：有下级的那行点 + 没有展开";
        else if (!notOpenedByDoubleClick) clicked += L"：双击没有下级的那行却展开了";
        else if (!stayed) clicked += L"：单击没有下级的那行却展开了";
        check(clicked.c_str(), clicked == clickedName);
    }

    // The two that change everything say so themselves, whoever calls them:
    // the frame on screen is a cache, and it is only rendered again for that.
    const std::wstring frameDir = base + L"\\frame";
    CreateDirectoryW(frameDir.c_str(), nullptr);
    app.paintDirty = false;
    app.loadDirectory(frameDir, false);  // another folder, not a reload of this one
    const bool navSays = app.paintDirty;
    app.paintDirty = false;
    app.reload();
    const bool reloadSays = app.paintDirty;
    check(L"转到别的目录、刷新列表：都说一声画面过期了", navSays && reloadSays);
    app.loadDirectory(base, false);

    // The address bar: every folder on the way to the current one is a button,
    // with a v after it that lists its subfolders - Explorer's breadcrumbs.
    const std::wstring crumbsDir = base + L"\\crumbs";
    const std::wstring crumbAa = crumbsDir + L"\\aa", crumbBb = crumbAa + L"\\bb",
                        crumbCc = crumbBb + L"\\cc", crumbDd = crumbCc + L"\\dd",
                        crumbWay = crumbDd + L"\\ee";
    const std::wstring crumbLong(255, L'W');  // a folder name of the full 255 characters
    // A name far too long for the address bar, in a path that is still short enough to go to
    const std::wstring crumbWide(150, L'W');
    {
        for (const std::wstring& dir : {crumbsDir, crumbAa, crumbBb, crumbCc, crumbDd, crumbWay,
                                        crumbsDir + L"\\zz", crumbAa + L"\\zz", crumbDd + L"\\yy",
                                        crumbBb + L"\\" + crumbWide}) {
            CreateDirectoryW(dir.c_str(), nullptr);
        }
        const HANDLE h = CreateFileW((crumbAa + L"\\file.txt").c_str(), GENERIC_WRITE, 0, nullptr,
                                     CREATE_ALWAYS, 0, nullptr);
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        // A folder with the longest name there can be, made of the widest
        // letter (5,000 px and more at 150 %): its row has to fit on the screen.
        CreateDirectoryW((L"\\\\?\\" + crumbCc + L"\\" + crumbLong).c_str(), nullptr);
    }
    app.loadDirectory(crumbWay, false);
    const int crumbN = (int)app.crumbs.size();
    const auto crumbPx = [&app](int v) { return pxOf(app.scale, v); };
    const auto middle = [](const RECT& r) {
        return POINT{(r.left + r.right) / 2, (r.top + r.bottom) / 2};
    };
    {
        const std::wstring name = L"地址栏：从我的电脑到当前文件夹，一级一个按钮";
        std::wstring s = name;
        const std::wstring root = fsx::rootOf(crumbWay);
        if (crumbN < 6 || !app.crumbs[0].path.empty() || app.crumbs[crumbN - 1].path != crumbWay)
            s += L"：两头不对";
        else if (_wcsicmp(app.crumbs[1].path.c_str(), root.c_str()) != 0)
            s += L"：第二级不是盘";
        else {
            for (int i = 2; i < crumbN; ++i) {
                const Crumb& c = app.crumbs[i];
                if (_wcsicmp(fsx::parentPath(c.path).c_str(), app.crumbs[i - 1].path.c_str()) != 0) {
                    s += L"：第 " + std::to_wstring(i + 1) + L" 级不在上一级里面";
                    break;
                }
                if (c.title != fsx::fileName(c.path)) {
                    s += L"：第 " + std::to_wstring(i + 1) + L" 级的名字不是文件夹名";
                    break;
                }
            }
        }
        check(s.c_str(), s == name);
    }
    // The crumbs from `from` on, from left to right without a gap, the first
    // at x, inside the field.
    const auto placedInARow = [&app, middle](std::wstring& s, int from, int x) {
        const RECT inner = app.addressInner();
        for (int i = from; i < (int)app.crumbs.size(); ++i) {
            const Crumb& c = app.crumbs[i];
            const bool last = i == (int)app.crumbs.size() - 1;
            if (c.box.left != x || c.box.top != inner.top || c.box.bottom != inner.bottom ||
                c.box.right > inner.right) {
                s += L"：第 " + std::to_wstring(i + 1) + L" 级的位置不对";
                break;
            }
            x = last ? c.box.right : c.more.right;
            if (!last && (c.more.left != c.box.right || c.more.top != inner.top)) {
                s += L"：第 " + std::to_wstring(i + 1) + L" 级后面缺一个 v";
                break;
            }
        }
        return x;
    };
    {
        const std::wstring name = L"地址栏放得下：从左排到右，一级不落，空白留在最后";
        std::wstring s = name;
        app.relayout(2600, 900);
        const int blankMin = crumbPx(20);
        if (app.crumbFirst != 0 || app.lay.addrMore.right > app.lay.addrMore.left) {
            s += L"：却收起了几级";
        } else {
            const int x = placedInARow(s, 0, app.addressInner().left);
            const RECT blank = app.lay.addrBlank;
            if (s == name && (blank.left != x || blank.top != app.addressInner().top ||
                              blank.bottom != app.addressInner().bottom ||
                              blank.right != app.addressInner().right))
                s += L"：空白不在最后一级后面";
            else if (s == name && blank.right - blank.left < blankMin)
                s += L"：空白窄得点不上";
        }
        check(s.c_str(), s == name);
    }
    int hiddenAtNarrow = 0;
    {
        const std::wstring name = L"地址栏放不下：电脑只留图标排在最左，放不下的几级折成它后面的 ···，最后几级首尾相接跟在后面，空白留在末尾";
        std::wstring s = name;
        app.relayout(700, 440);
        const RECT inner = app.addressInner();
        const int pad = crumbPx(5), blankMin = crumbPx(20), moreW = crumbPx(12), dotsW = crumbPx(20);
        const int fontPx = (int)(14.6667 * app.scale + 0.5);
        const Crumb& machine = app.crumbs[0];
        const RECT dots = app.lay.addrMore;
        hiddenAtNarrow = app.crumbFirst;
        if (app.crumbFirst < 2) {
            s += L"：没有收起来的几级";
        } else if (machine.box.left != inner.left || machine.box.top != inner.top ||
                   machine.box.bottom != inner.bottom ||
                   machine.box.right - machine.box.left != 2 * pad + crumbPx(16) ||
                   !machine.text.empty()) {
            s += L"：电脑不在最左边，或还带着名字";
        } else if (machine.more.left != machine.box.right ||
                   machine.more.right - machine.more.left != moreW) {
            s += L"：电脑后面缺一个 v";
        } else if (dots.left != machine.more.right || dots.right - dots.left != dotsW ||
                   dots.top != inner.top || dots.bottom != inner.bottom) {
            s += L"：··· 不紧挨在电脑后面";
        } else {
            const int x = placedInARow(s, app.crumbFirst, dots.right);
            for (int i = 1; s == name && i < app.crumbFirst; ++i)
                if (app.crumbs[i].box.right > app.crumbs[i].box.left ||
                    app.crumbs[i].more.right > app.crumbs[i].more.left)
                    s += L"：收起来的一级还画着";
            const RECT blank = app.lay.addrBlank;
            if (s == name && (blank.left != x || blank.right != inner.right ||
                              blank.top != inner.top || blank.bottom != inner.bottom))
                s += L"：空白不在最后一级后面";
            else if (s == name && blank.right - blank.left < blankMin)
                s += L"：空白窄得点不上";
            // As many of the last crumbs as fit, and no fewer: the first one
            // out of view would not, not beside the blank a click needs.
            else if (s == name && blank.right - blank.left - blankMin >=
                                                     moreW + 2 * pad +
                                                         measureUiText(app.crumbs[app.crumbFirst - 1].title,
                                                                       fontPx))
                s += L"：还放得下一级，却收起来了";
            (void)x;
        }
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name = L"地址栏放不下：窗口宽一些，收起的就少一些；都放下了就没有 ···，电脑的名字也回来";
        std::wstring s = name;
        app.relayout(1100, 760);
        const bool dots = app.lay.addrMore.right > app.lay.addrMore.left;
        if (hiddenAtNarrow < 3) {
            s += L"：窄窗口里没有收起来的几级";
        } else if (app.crumbFirst >= hiddenAtNarrow) {
            s += L"：收起的没有变少";
        } else if (dots != (app.crumbFirst >= 2)) {
            s += L"：··· 有没有不对";
        } else if (app.crumbFirst >= 2 && !app.crumbs[0].text.empty()) {
            s += L"：还收着几级，电脑却带着名字";
        } else if (app.crumbFirst == 0 &&
                   app.lay.addrBlank.right - app.lay.addrBlank.left < crumbPx(20)) {
            s += L"：都放下了，空白却窄得点不上";
        }
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name = L"地址栏上点在哪儿：电脑的图标、它后面的 v、收起几级的 ···、某一级的按钮、它后面的 v、空白";
        std::wstring s = name;
        app.relayout(700, 440);
        const RECT inner = app.addressInner();
        const int first = app.crumbFirst;
        int k = -2;
        if (first < 2) {
            s += L"：没有收起来的几级";
        } else if (app.addressAt(middle(app.crumbs[0].box), &k) != AddressHit::Crumb || k != 0) {
            s += L"：电脑的图标不认";
        } else if (app.addressAt(middle(app.crumbs[0].more), &k) != AddressHit::CrumbMore || k != 0) {
            s += L"：电脑后面的 v 不认";
        } else if (app.addressAt(middle(app.lay.addrMore), &k) != AddressHit::More) {
            s += L"：··· 不认";
        } else if (app.addressAt(POINT{app.crumbs[first].box.left, inner.top + 1}, &k) !=
                       AddressHit::Crumb ||
                   k != first) {
            s += L"：··· 后面紧跟着的该是第一级可见的按钮";
        } else if (app.addressAt(middle(app.crumbs[first].box), &k) != AddressHit::Crumb || k != first) {
            s += L"：第一级可见的按钮不认";
        } else if (app.addressAt(middle(app.crumbs[crumbN - 2].more), &k) != AddressHit::CrumbMore ||
                   k != crumbN - 2) {
            s += L"：最后一级后面的 v 不认";
        } else if (app.addressAt(middle(app.lay.addrBlank), &k) != AddressHit::Blank) {
            s += L"：空白不认";
        } else if (app.addressAt(POINT{inner.right - 1, inner.top + 1}, &k) != AddressHit::Blank) {
            s += L"：字段最右边一列该是空白";
        } else if (app.addressAt(POINT{app.lay.addrField.left + crumbPx(1), app.lay.addrField.top},
                                 &k) != AddressHit::Blank) {
            s += L"：字段自己的边框该算空白";
        } else if (app.addressAt(middle(app.lay.addrGo), &k) != AddressHit::None) {
            s += L"：转到按钮不是地址栏";
        }
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name = L"每级的 v 列出那一级的子文件夹（文件不算），当前所在的那个是选中行";
        std::wstring s = name;
        const auto crumbOf = [&app](const wchar_t* title) {
            for (int i = 0; i < (int)app.crumbs.size(); ++i)
                if (app.crumbs[i].title == title) return i;
            return -1;
        };
        int current = -9;
        const int aa = crumbOf(L"aa");
        const std::vector<FolderRow> rows = app.crumbRows(aa, &current);
        if (rows.size() != 2) {
            s += L"：aa 的列表里有 " + std::to_wstring(rows.size()) + L" 行，该是 2 行";
        } else if (rows[0].title != L"bb" || rows[1].title != L"zz" ||
                   rows[0].path != crumbBb || rows[1].kind != TreeNode::Folder) {
            s += L"：aa 的列表里不是 bb、zz";
        } else if (current != 0) {
            s += L"：选中的不是 bb";
        }
        int driveRow = -9;
        const std::vector<FolderRow> drives = app.crumbRows(0, &driveRow);
        if (drives.empty()) {
            s += L"：我的电脑下一台盘也没有";
        } else if (driveRow < 0 || _wcsicmp(drives[driveRow].path.c_str(), app.crumbs[1].path.c_str()) != 0) {
            s += L"：我的电脑里没选中当前这个盘";
        }
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name = L"收起来的几级按顺序列在 ··· 里，盘在最前（电脑自己露在外面，不列）";
        std::wstring s = name;
        app.relayout(700, 440);
        const std::vector<FolderRow> rows = app.hiddenCrumbRows();
        if (rows.size() + 1 != (size_t)app.crumbFirst) {
            s += L"：行数不对";
        } else {
            for (size_t i = 0; i < rows.size(); ++i) {
                const TreeNode::Kind kind = i == 0 ? TreeNode::Volume : TreeNode::Folder;
                if (rows[i].title != app.crumbs[i + 1].title ||
                    rows[i].path != app.crumbs[i + 1].path || rows[i].kind != kind) {
                    s += L"：第 " + std::to_wstring(i + 1) + L" 行不对";
                    break;
                }
            }
        }
        check(s.c_str(), s == name);
    }
    {
        // A crumb's button, pressed and released: the handlers the window
        // procedure uses, without a window (the real window is the crumb test).
        const std::wstring name = L"点某一级按钮就去那一级；按住后松开在别处，哪儿也不去";
        std::wstring s = name;
        app.relayout(1400, 760);  // wide enough for the whole way
        const int bb = crumbN - 4;  // "bb"
        const POINT on = middle(app.crumbs[bb].box);
        if (app.crumbs[bb].title != L"bb" || bb < app.crumbFirst) {
            s += L"：测试要按的那一级不在";
        } else {
            POINT elsewhere{app.lay.content.right - 20, app.lay.content.bottom - 20};
            onLButtonDown(nullptr, app, on);
            if (app.pressCrumb != bb) s += L"：按下去没有记住是这一级";
            onLButtonUp(nullptr, app, elsewhere);
            if (app.current != crumbWay) s += L"：松开在别处却换了目录";
            onLButtonDown(nullptr, app, on);
            onLButtonUp(nullptr, app, on);
            if (app.current != crumbBb || app.crumbs.back().path != crumbBb)
                s += L"：松开在按钮上却没去";
            else if (app.pressCrumb != -1) s += L"：去了之后按钮还按着";
        }
        check(s.c_str(), s == name);
    }
    {
        // A folder whose name alone is too long for the bar.  Only the crumbs
        // are asked for here; the real window shows it at the end of the
        // crumb test.
        const std::wstring name = L"当前文件夹的名字独自也放不下：名字用省略号截到刚好留出空白，不再伸出字段";
        std::wstring s = name;
        const std::wstring was = app.current;
        app.current = crumbBb + L"\\" + crumbWide;
        app.buildCrumbs();
        app.relayout(700, 440);
        const RECT inner = app.addressInner();
        const int pad = crumbPx(5), blankMin = crumbPx(20);
        const int fontPx = (int)(14.6667 * app.scale + 0.5);
        const std::wstring dots = L"\u2026";
        const Crumb& last = app.crumbs.back();
        if (app.crumbs.size() < 3 || last.title != crumbWide) {
            s += L"：最后一级不是那个长名字";
        } else if (last.text.size() < 2 || last.text.back() != dots[0]) {
            s += L"：名字没有被截断";
        } else if (app.crumbFirst != (int)app.crumbs.size() - 1) {
            s += L"：长名字前面还排着别的级";
        } else if (last.box.left != app.lay.addrMore.right || last.box.top != inner.top ||
                   last.box.bottom != inner.bottom) {
            s += L"：位置不对";
        } else if (last.box.right - last.box.left != 2 * pad + measureUiText(last.text, fontPx)) {
            s += L"：按钮不是按截断后的名字量的";
        } else {
            const RECT blank = app.lay.addrBlank;
            const int room = inner.right - blankMin - last.box.left;  // what the button may take
            if (blank.left != last.box.right || blank.right != inner.right ||
                blank.right - blank.left < blankMin) {
                s += L"：留出的空白不对";
            }
            // The longest cut that fits: one character more would not.
            else if (last.box.right - last.box.left > room ||
                     2 * pad + measureUiText(last.text.substr(0, last.text.size() - 1) + L"W" + dots,
                                             fontPx) <= room)
                s += L"：截得不是刚好";
        }
        app.current = was;
        app.buildCrumbs();
        check(s.c_str(), s == name);
    }

    // What is on screen is what the state renders - after Enter in the address
    // bar, after leaving it by clicking elsewhere, and after Esc and Enter in
    // the rename box.  These end inside App, where nobody used to say the frame
    // was out of date: the old folder stayed listed (and still got clicked
    // on), and a cancelled rename left an empty white box where the name had
    // been.  The window is the real one, driven by posted input; what it shows
    // is read back from the compositor, so nothing lying on top of it can hide
    // or fake it.
    {
        for (const wchar_t* name : {L"aaa.txt", L"bbb.txt", L"ccc.txt"}) {
            const HANDLE h = CreateFileW((frameDir + L"\\" + name).c_str(), GENERIC_WRITE, 0,
                                         nullptr, CREATE_ALWAYS, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        }
        WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
        wc.lpfnWndProc = frameTestProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kFrameTestClass;
        RegisterClassExW(&wc);
        HWND driver = CreateWindowExW(0, kFrameTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100,
                                      nullptr, nullptr, wc.hInstance, nullptr);  // never shown
        registerMainClass(wc.hInstance);
        // Never activated, and behind everything: it is shown for a second or
        // two, and must not take the keyboard from whoever is using the machine.
        HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", L"frame test",
                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver,
                                    nullptr, wc.hInstance, nullptr);
        g_frame = FrameDrive{};
        g_frame.main = main;
        g_frame.to = frameDir;
        if (main) {
            g_app.hwnd = main;
            g_app.scale = dpiScaleOfWindow(main);
            g_app.init(main);
            g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
            g_app.sidebarVisible = true;
            g_app.loadDirectory(base, false);
            g_app.setViewMode(ViewMode::Details);
            placeWindow(main, 0, 0, g_app.width, g_app.height);
            ShowWindow(main, SW_SHOWNOACTIVATE);
            SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            UpdateWindow(main);
            MSG msg;
            for (SetTimer(driver, kFrameTestTimer, 60, nullptr);
                 g_frame.step < kFrameTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            KillTimer(driver, kFrameTestTimer);
            g_app.hwnd = nullptr;  // the edits are the window's children and go with it
            DestroyWindow(main);
            // Its WM_DESTROY posted WM_QUIT for a loop that is not running.
            PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
            g_app.addrEdit = nullptr;
            g_app.addrEditing = false;
            if (g_app.addrFont) {
                DeleteObject(g_app.addrFont);
                g_app.addrFont = nullptr;
            }
        }
        DestroyWindow(driver);

        std::wstring frameNotes;
        if (!main) frameNotes += L"窗口没建出来";
        else if (g_frame.ticks > 240) frameNotes += L"超时";
        if (!g_frame.baseline) frameNotes += L"基准不符";
        const auto areaOf = [](const RECT& r) {
            return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L" " +
                   std::to_wstring(r.right - r.left) + L"x" + std::to_wstring(r.bottom - r.top);
        };
        // A verdict that says what was seen: whether the test got where it
        // meant to, and if so, how much of the screen was still the old frame.
        const auto frameVerdict = [&](const wchar_t* name, const wchar_t* came, bool didCome,
                                      bool state, int stale, const RECT& where) {
            std::wstring label = name;
            if (!frameNotes.empty()) label += std::wstring(L"：") + frameNotes;
            else if (!didCome) label += std::wstring(L"：") + came + L"没有出现";
            else if (!state) label += L"：程序没有照做";
            else if (stale)
                label += L"：屏幕上有 " + std::to_wstring(stale) + L" 个像素还是旧的，范围 " +
                         areaOf(where);
            check(label.c_str(), label == name);
        };
        check(L"（测试基准）屏幕上的画面读得回来，且与按状态渲染的一致", g_frame.baseline);
        frameVerdict(L"地址栏输入目录按回车：屏幕上就是新目录", L"地址栏输入框",
                     g_frame.editCame, g_frame.navigated, g_frame.stale[0], g_frame.where[0]);
        frameVerdict(L"点别处离开地址栏：屏幕上就是点中的那一行", L"地址栏输入框",
                     g_frame.editCameAgain, g_frame.leftOk, g_frame.stale[3], g_frame.where[3]);
        // Read from the screen itself, edit box included: the row being renamed
        // is one blue - the icon's background is the colour the edit box gives
        // the selected part of the name.
        const wchar_t* const oneBlueName =
            L"改名时整行一个颜色：图标的底色与输入框里选中名字的底色相同";
        std::wstring oneBlue = oneBlueName;
        if (!g_frame.fieldCame) oneBlue += L"：改名输入框没有出现";
        else if (g_frame.blockOff)
            oneBlue += L"：图标旁边有 " + std::to_wstring(g_frame.blockOff) +
                       L" 个像素不是这个颜色";
        else if (!g_frame.nameHighlighted) oneBlue += L"：输入框里没有看到选中名字的高亮";
        check(oneBlue.c_str(), oneBlue == oneBlueName);
        frameVerdict(L"改名按 Esc：屏幕上的名字回来了", L"改名输入框", g_frame.fieldCame,
                     g_frame.kept, g_frame.stale[1], g_frame.where[1]);
        frameVerdict(L"改名按回车：屏幕上是新名字，并排在它新的位置", L"改名输入框",
                     g_frame.fieldCameAgain, g_frame.renamed, g_frame.stale[2], g_frame.where[2]);
    }

    // The address bar's buttons in the real window: the narrowest there is, so
    // that the way to the folder does not fit and some crumbs are behind the ...
    {
        WNDCLASSEXW wc{sizeof(WNDCLASSEXW)};
        wc.lpfnWndProc = crumbTestProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kCrumbTestClass;
        RegisterClassExW(&wc);
        HWND driver = CreateWindowExW(0, kCrumbTestClass, L"", WS_OVERLAPPED, 0, 0, 100, 100,
                                      nullptr, nullptr, wc.hInstance, nullptr);  // never shown
        registerMainClass(wc.hInstance);
        HWND main = CreateWindowExW(WS_EX_NOACTIVATE, L"MyOldComputerWindow", L"crumb test",
                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, 0, 0, driver,
                                    nullptr, wc.hInstance, nullptr);
        g_crumb = CrumbDrive{};
        g_crumb.main = main;
        g_crumb.way = crumbWay;
        g_crumb.dd = crumbDd;
        g_crumb.aa = crumbAa;
        g_crumb.bb = crumbBb;
        g_crumb.root = fsx::rootOf(crumbWay);
        g_crumb.wide = crumbBb + L"\\" + crumbWide;
        g_crumb.wideName = crumbWide;
        if (main) {
            g_app.hwnd = main;
            g_app.scale = dpiScaleOfWindow(main);
            g_app.init(main);
            g_app.relayout(pxOf(g_app.scale, 700), pxOf(g_app.scale, 440));
            g_app.sidebarVisible = true;
            g_app.loadDirectory(crumbWay, false);
            g_app.setViewMode(ViewMode::Details);
            placeWindow(main, 0, 0, g_app.width, g_app.height);
            ShowWindow(main, SW_SHOWNOACTIVATE);
            SetWindowPos(main, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            UpdateWindow(main);
            MSG msg;
            for (SetTimer(driver, kCrumbTestTimer, 60, nullptr);
                 g_crumb.step < kCrumbTestEnd && GetMessageW(&msg, nullptr, 0, 0) > 0;) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            KillTimer(driver, kCrumbTestTimer);
            g_app.hwnd = nullptr;  // the edit is the window's child and goes with it
            DestroyWindow(main);
            PeekMessageW(&msg, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE);
            g_app.addrEdit = nullptr;
            g_app.addrEditing = false;
            if (g_app.addrFont) {
                DeleteObject(g_app.addrFont);
                g_app.addrFont = nullptr;
            }
        }
        DestroyWindow(driver);
        // The long-named folder goes now: a path past MAX_PATH is awkward to delete by hand.
        RemoveDirectoryW((L"\\\\?\\" + crumbCc + L"\\" + crumbLong).c_str());

        std::wstring notes;
        if (!main) notes += L"窗口没建出来";
        else if (g_crumb.ticks > 400) notes += L"超时";
        if (!g_crumb.baseline)
            notes += L"基准不符，范围 " + std::to_wstring(g_crumb.where[0].left) + L"," +
                     std::to_wstring(g_crumb.where[0].top) + L" " +
                     std::to_wstring(g_crumb.where[0].right - g_crumb.where[0].left) + L"x" +
                     std::to_wstring(g_crumb.where[0].bottom - g_crumb.where[0].top);
        const auto areaOf = [](const RECT& r) {
            return std::to_wstring(r.left) + L"," + std::to_wstring(r.top) + L" " +
                   std::to_wstring(r.right - r.left) + L"x" + std::to_wstring(r.bottom - r.top);
        };
        // A verdict that says what was seen: whether the test got where it
        // meant to, and if so, whether what the state says is on screen.
        const auto verdict = [&](const wchar_t* name, bool state, int stale, const RECT& where) {
            std::wstring label = name;
            if (!notes.empty()) label += L"：" + notes;
            else if (!state) label += L"：屏幕上不是这样";
            else if (stale)
                label += L"：屏幕上有 " + std::to_wstring(stale) + L" 个像素与状态不符，范围 " +
                         areaOf(where);
            check(label.c_str(), label == name);
        };
        check(L"（测试基准）地址栏：屏幕上的画面读得回来，且与按状态渲染的一致", g_crumb.baseline);
        verdict(L"指针到某一级按钮上：屏幕上它马上亮起，离开就灭",
                g_crumb.k >= 0 && g_crumb.hoverShown >= g_crumb.hoverArea * 3 / 4 &&
                    g_crumb.hoverCleared == 0,
                g_crumb.hoverStale, g_crumb.where[0]);
        verdict(L"按住某一级按钮：屏幕上是按下的样子；松开在别处：哪儿也不去",
                g_crumb.pressed && g_crumb.released && g_crumb.pressShown >= g_crumb.hoverArea * 3 / 4,
                g_crumb.stale[1], g_crumb.where[1]);
        verdict(L"点某一级后面的 v：屏幕上列出那一级的子文件夹，v 按下，当前所在的行高亮",
                g_crumb.listCame && g_crumb.listRight && g_crumb.arrowDown && g_crumb.hotPainted,
                g_crumb.stale[0], g_crumb.where[0]);
        verdict(L"地址栏放不下：屏幕上电脑只留图标排在最左，后面是 ··· 和最后几级，首尾相接，末尾留白",
                g_crumb.rowJoined && g_crumb.machineIconOnly && g_crumb.dotsShown, g_crumb.stale[11],
                g_crumb.where[11]);
        verdict(L"指针到 ··· 上：屏幕上它亮起，离开就灭",
                g_crumb.dotsHover && g_crumb.dotsHoverCleared, g_crumb.stale[12], g_crumb.where[12]);
        verdict(L"列表里按 Down、回车：高亮跟着走，然后去了那一行", g_crumb.hotMoved && g_crumb.chose,
                g_crumb.stale[2], g_crumb.where[2]);
        verdict(L"点收起几级的 ···：屏幕上列出它们，··· 是按下的样子，点一行就去",
                g_crumb.hiddenList && g_crumb.hiddenArrowDown && g_crumb.wentHidden,
                g_crumb.stale[3], g_crumb.where[3]);
        verdict(L"点某一级按钮：就去那一级，屏幕上就是它", g_crumb.clicked, g_crumb.stale[4],
                g_crumb.where[4]);
        verdict(L"双击某一级按钮：只算一下，停在那一级", g_crumb.doubleOk, g_crumb.stale[5],
                g_crumb.where[5]);
        verdict(L"点地址栏的空白处：整条路径在输入框里；Esc 回到按钮",
                g_crumb.editByBlank && g_crumb.editHighlight > 0 && g_crumb.editsClosed,
                g_crumb.stale[6], g_crumb.where[6]);
        verdict(L"F4、Ctrl+L、Alt+D：同样是整条路径的输入框，Esc 都回得来",
                g_crumb.editByF4 && g_crumb.editByCtrlL && g_crumb.editByAltD, g_crumb.stale[7],
                g_crumb.where[7]);
        verdict(L"点我的电脑：屏幕上列出各个盘，当前盘高亮；Esc 关上",
                g_crumb.machineList && g_crumb.machineClosed, g_crumb.stale[8], g_crumb.where[8]);
        verdict(L"列表开着时点另一个 v：直接换成那一个的列表",
                g_crumb.switchFrom && g_crumb.switched && g_crumb.switchClosed, g_crumb.stale[9],
                g_crumb.where[9]);
        {
            const std::wstring name = L"列表里有一个 255 个字符的名字：列表不比屏幕宽，Esc 关得上";
            std::wstring s = name;
            if (!notes.empty()) s += L"：" + notes;
            else if (!g_crumb.longList) s += L"：列表里不是那两行";
            else if (!g_crumb.listFits) s += L"：列表宽 " + std::to_wstring(g_crumb.listWidth) + L"，比屏幕宽";
            else if (!g_crumb.longClosed) s += L"：Esc 之后列表还在";
            else if (g_crumb.stale[10])
                s += L"：Esc 之后屏幕上有 " + std::to_wstring(g_crumb.stale[10]) + L" 个像素与状态不符";
            check(s.c_str(), s == name);
        }
        verdict(L"当前文件夹的名字独自也放不下：屏幕上名字以省略号结尾，后面是空白",
                g_crumb.cutState && g_crumb.cutInk && g_crumb.cutWhite, g_crumb.stale[13],
                g_crumb.where[13]);
    }

    // A folder in a new window (see newWindowTest).  Nothing but a folder
    // opens one - and nothing is started for what does not.
    check(L"多开只对文件夹：文件、不存在的路径不开新窗口",
          app.openPathInNewWindow(base + L"\\item0.txt") == 0 &&
          app.openPathInNewWindow(base + L"\\no-such-folder") == 0);
    // A drive's root ends in a backslash: on the command line it would escape
    // the closing quote, and the new instance would get no path at all.
    check(L"多开的命令行：路径是一个参数，盘符根目录（C:\\）结尾的反斜杠不吃掉引号",
          commandArgument(L"C:\\") == L"\"C:\\\\\"" &&
          commandArgument(crumbAa) == L"\"" + crumbAa + L"\"");
    newWindowTest(crumbsDir, crumbAa, check);

    // The colours a folder can be given (its context menu: 文件夹颜色).
    {
        const std::wstring name = L"文件夹颜色：八种，各不相同";
        bool ok = foldercolours::kCount == 8;
        for (int i = 0; i < foldercolours::kCount; ++i) {
            ok = ok && foldercolours::colour(i).name && *foldercolours::colour(i).name;
            for (int j = 0; j < i; ++j)
                ok = ok && foldercolours::colour(i).rgb != foldercolours::colour(j).rgb;
        }
        check(name.c_str(), ok);
    }
    {
        const std::wstring name =
            L"给文件夹的颜色记在本程序自己的文件里：文件夹本身不动；重读（如同重新启动）还在，不区分大小写，去掉就没了";
        std::wstring s = name;
        const std::wstring file = foldercolours::path();
        // The folder itself is not touched for it: no file put into it, its
        // attributes as they were.
        const DWORD attributes = GetFileAttributesW(crumbAa.c_str());
        DeleteFileW(file.c_str());
        foldercolours::reload();
        if (foldercolours::of(crumbAa) != foldercolours::kDefault) s += L"：没给过颜色却说有";
        foldercolours::set({crumbAa, crumbBb}, 3);
        if (GetFileAttributesW(crumbAa.c_str()) != attributes || fsx::exists(crumbAa + L"\\desktop.ini"))
            s += L"：给颜色动了文件夹本身";
        foldercolours::reload();
        if (foldercolours::of(crumbAa) != 3 || foldercolours::of(crumbBb) != 3)
            s += L"：重读文件后颜色不在了";
        std::wstring upper = crumbAa;
        for (auto& ch : upper) ch = (wchar_t)towupper(ch);
        if (foldercolours::of(upper) != 3) s += L"：路径换个大小写就不认";
        if (GetFileAttributesW(file.c_str()) == INVALID_FILE_ATTRIBUTES) s += L"：没有写出文件";
        if (file.find(L"moc-uitest-settings-colours.ini") == std::wstring::npos)
            s += L"：写的不是测试自己的文件";
        foldercolours::set({crumbAa}, foldercolours::kDefault);
        foldercolours::reload();
        if (foldercolours::of(crumbAa) != foldercolours::kDefault || foldercolours::of(crumbBb) != 3)
            s += L"：去掉一个的颜色，另一个的也不对了";
        foldercolours::set({crumbBb}, foldercolours::kDefault);
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name =
            L"带颜色的文件夹图标：黄色成了那个颜色，轮廓、纸和透明不变，每种颜色都不一样";
        std::wstring s = name;
        const auto least = [](int a, int b, int c) { return std::min(a, std::min(b, c)); };
        const auto towards = [&](int r, int g, int b, COLORREF to) {
            // How much a colour points the way the target colour does: 1 is
            // the same direction from grey, 0 none at all.
            const double dr = r - least(r, g, b), dg = g - least(r, g, b), db = b - least(r, g, b);
            const int tr = GetRValue(to), tg = GetGValue(to), tb = GetBValue(to);
            const double er = tr - least(tr, tg, tb), eg = tg - least(tr, tg, tb),
                         eb = tb - least(tr, tg, tb);
            const double a = std::sqrt(dr * dr + dg * dg + db * db), e = std::sqrt(er * er + eg * eg + eb * eb);
            return a > 0 && e > 0 ? (dr * er + dg * eg + db * eb) / (a * e) : -1.0;
        };
        for (int px : {16, 48}) {
            const std::vector<unsigned char> plain =
                iconPixels(shellicons::icon(std::wstring(), true, false, px));
            for (int i = 0; s == name && i < foldercolours::kCount; ++i) {
                const std::vector<unsigned char> coloured = iconPixels(shellicons::folder(i, px));
                if (plain.empty() || coloured.size() != plain.size()) {
                    s += L"：图标取不出来";
                    break;
                }
                const COLORREF target = foldercolours::colour(i).rgb;
                const bool greyTo =
                    GetRValue(target) == GetGValue(target) && GetGValue(target) == GetBValue(target);
                int opaque = 0, changed = 0, wrong = 0, onColour = 0;
                for (size_t k = 0; k < plain.size(); k += 4) {
                    if (!plain[k + 3]) continue;
                    ++opaque;
                    // Transparency, the outline and the paper: as they were.
                    const bool grey = plain[k] == plain[k + 1] && plain[k + 1] == plain[k + 2];
                    const bool differs = std::abs(coloured[k] - plain[k]) > 24 ||
                                         std::abs(coloured[k + 1] - plain[k + 1]) > 24 ||
                                         std::abs(coloured[k + 2] - plain[k + 2]) > 24;
                    if (coloured[k + 3] != plain[k + 3] || (grey && differs)) ++wrong;
                    if (!differs) continue;
                    ++changed;
                    if (greyTo ? std::abs(coloured[k] - coloured[k + 1]) <= 3 &&
                                    std::abs(coloured[k + 1] - coloured[k + 2]) <= 3
                              : towards(coloured[k + 2], coloured[k + 1], coloured[k], target) > 0.9)
                        ++onColour;
                }
                if (changed * 3 < opaque) s += L"：颜色 " + std::to_wstring(i) + L" 没改几个像素";
                else if (wrong) s += L"：颜色 " + std::to_wstring(i) + L" 动了不该动的地方";
                else if (onColour * 10 < changed * 9)
                    s += L"：颜色 " + std::to_wstring(i) + L" 的像素里只有 " +
                         std::to_wstring(onColour) + L"/" + std::to_wstring(changed) + L" 是那个颜色";
            }
        }
        check(s.c_str(), s == name);
    }
    {
        // Without the shell's icons the folder is drawn by the program itself:
        // there too, in the colour it was given.
        const std::wstring name =
            L"不用系统图标时（文件夹是自己画的）：带颜色的文件夹在大图标视图里画成那个颜色，图标以外一个像素不变";
        std::wstring s = name;
        app.loadDirectory(crumbsDir, false);
        app.setViewMode(ViewMode::Icons);
        app.relayout(1100, 760);
        app.useSystemIcons = false;
        foldercolours::set({crumbAa}, 0);
        const Frame red = renderedFrame(app);
        foldercolours::set({crumbAa}, foldercolours::kDefault);
        const Frame plain = renderedFrame(app);
        app.useSystemIcons = true;
        const RECT r = app.gridCellRect(0);
        const int side = pxOf(app.scale, 48), left = (r.left + r.right) / 2 - side / 2;
        const RECT box{left, r.top + pxOf(app.scale, 4), left + side, r.top + pxOf(app.scale, 4) + side};
        RECT where{};
        const int stale = frameMismatch(red, plain, &where);
        if (frameCount(red, box, isFolderRed) < 10) s += L"：自己画的文件夹没有变红";
        else if (frameCount(plain, box, isFolderRed) != 0) s += L"：没给颜色时也是红的";
        else if (stale == 0) s += L"：两张图一样";
        else if (where.left < box.left || where.top < box.top || where.right > box.right ||
                 where.bottom > box.bottom)
            s += L"：图标以外有像素变了";
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name =
            L"文件夹的右键菜单里有“文件夹颜色”，子菜单依次是八种颜色和“默认颜色”；文件的菜单里没有，设置里关掉就没有";
        std::wstring s = name;
        const auto has = [](const std::vector<std::wstring>& labels, const wchar_t* text) {
            return std::find(labels.begin(), labels.end(), text) != labels.end();
        };
        shellmenu::ViewState state;
        std::vector<std::wstring> subs;
        const auto top = shellmenu::labels(crumbsDir, {crumbAa}, state, &subs);
        if (!has(top, L"文件夹颜色")) s += L"：菜单里没有“文件夹颜色”";
        std::vector<std::wstring> entries;
        for (const std::wstring& e : subs)
            if (e.compare(0, 6, L"文件夹颜色/") == 0) entries.push_back(e.substr(6));
        std::vector<std::wstring> wanted;
        for (int i = 0; i < foldercolours::kCount; ++i) wanted.push_back(foldercolours::colour(i).name);
        wanted.push_back(foldercolours::colour(foldercolours::kDefault).name);
        if (s == name && entries != wanted) s += L"：子菜单的内容或顺序不对";
        if (has(shellmenu::labels(crumbsDir, {crumbAa + L"\\file.txt"}, state), L"文件夹颜色"))
            s += L"：文件的菜单里也有";
        state.menuMask &= ~kMenuItemFolderColour;
        if (has(shellmenu::labels(crumbsDir, {crumbAa}, state), L"文件夹颜色")) s += L"：关掉了还有";
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name =
            L"设置文件是旧的（那时还没有“文件夹颜色”这一项）：第一次读进来它是开着的，之后关了就一直是关的";
        std::wstring s = name;
        const std::wstring file = settingsPath();
        WritePrivateProfileStringW(L"menu", L"colours", nullptr, file.c_str());
        WritePrivateProfileStringW(L"menu", L"mask", L"0x3FFF", file.c_str());
        Settings old;
        old.load();
        if (!(old.menuMask & kMenuItemFolderColour)) s += L"：旧设置里它是关着的";
        old.save();  // from now on the file has its say
        WritePrivateProfileStringW(L"menu", L"mask", L"0x3FFF", file.c_str());  // ... and the item is off
        Settings again;
        again.load();
        if (again.menuMask & kMenuItemFolderColour) s += L"：关掉的又被打开了";
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name =
            L"设置文件是旧的（那时还没有“提前在后台构建菜单”这一项）：第一次读进来它是开着的，之后关了就一直是关的";
        std::wstring s = name;
        const std::wstring file = settingsPath();
        WritePrivateProfileStringW(L"menu", L"ahead", nullptr, file.c_str());
        WritePrivateProfileStringW(L"menu", L"mask", L"0x7FFF", file.c_str());
        Settings old;
        old.load();
        if (!(old.menuMask & kMenuAhead)) s += L"：旧设置里它是关着的";
        old.save();  // from now on the file has its say
        WritePrivateProfileStringW(L"menu", L"mask", L"0x7FFF", file.c_str());  // ... and it is off
        Settings again;
        again.load();
        if (again.menuMask & kMenuAhead) s += L"：关掉的又被打开了";
        check(s.c_str(), s == name);
    }

    folderColourTest(crumbsDir, crumbAa, check);

    // The shell's menus are built ahead of the right-click: four text files
    // and six other kinds of file (five kinds are built ahead, of the other
    // two one is hovered and one is right-clicked), and a folder.
    const std::wstring aheadDir = base + L"\\ahead";
    {
        CreateDirectoryW(aheadDir.c_str(), nullptr);
        CreateDirectoryW((aheadDir + L"\\sub").c_str(), nullptr);
        for (const wchar_t* name : {L"a1.txt", L"a2.txt", L"a3.txt", L"a4.txt", L"k1.cpp", L"k2.ini", L"k3.log", L"k4.md",
             L"k5.zz", L"k6.qq"})
            if (HANDLE h = CreateFileW((aheadDir + L"\\" + name).c_str(), GENERIC_WRITE, 0, nullptr,
                                       CREATE_ALWAYS, 0, nullptr);
                h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
    }
    menusAheadTest(aheadDir, check);

    // A hidden file or folder, shown because hidden files are shown, is drawn
    // faint: its icon at half strength, as Explorer shows one.
    faintTest(base + L"\\hidden", base + L"\\hiddenpics", check);

    // This program in place of Explorer for opening folders, from the Tools
    // menu of a real window - on a scratch key; and, where that is asked for,
    // once on the real one.
    takeoverMenuTest(base, check);
    wchar_t liveAsked[4]{};
    if (GetEnvironmentVariableW(kTakeoverLive, liveAsked, 4)) takeoverLiveTest(base, check);

    // Thumbnails: a folder that is mostly pictures opens as thumbnails.
    const std::wstring thumbsDir = base + L"\\thumbs", picDir = thumbsDir + L"\\pics",
                        mixedDir = thumbsDir + L"\\mixed";
    {
        CreateDirectoryW(thumbsDir.c_str(), nullptr);
        CreateDirectoryW(picDir.c_str(), nullptr);
        CreateDirectoryW(mixedDir.c_str(), nullptr);
        // 300 by 200 pixels: the thumbnails keep that shape.
        for (int k = 0; k < kThumbPictures; ++k)
            writeBmp(picDir + L"\\p" + std::to_wstring(k) + L".bmp", 300, 200, kThumbColours[k]);
        writeBmp(mixedDir + L"\\p0.bmp", 300, 200, kThumbColours[0]);
        for (const std::wstring& dir : {picDir, mixedDir})
            for (int i = 0; i < 5; ++i) {
                const HANDLE h = CreateFileW((dir + L"\\note" + std::to_wstring(i) + L".txt").c_str(),
                                             GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
                if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
            }
    }
    {
        const std::wstring name = L"图片多的文件夹（过半的文件是图片）：自动用缩略图视图，格子是 112×132，图在 96 的框里，名字在下面";
        std::wstring s = name;
        app.settings.viewMode = (int)ViewMode::Details;
        app.mode = ViewMode::Details;
        app.loadDirectory(picDir, false);
        const auto px = [&app](int v) { return pxOf(app.scale, v); };
        if (!app.thumbnails || app.mode != ViewMode::Icons || !app.viewAutomatic)
            s += L"：没有自动用缩略图视图";
        else if (app.settings.viewMode != (int)ViewMode::Details) s += L"：把平时的视图改掉了";
        else if (app.gridCellW() != px(112) || app.gridCellH() != px(132) || app.iconBox() != px(96) ||
                 app.labelTop() != px(104))
            s += L"：格子是 " + std::to_wstring(app.gridCellW()) + L"x" + std::to_wstring(app.gridCellH()) +
                 L"，图标框 " + std::to_wstring(app.iconBox());
        else {
            int index = -2;
            const RECT r = app.gridCellRect(0);
            const int side = app.iconBox();
            POINT on{(r.left + r.right) / 2, r.top + px(4) + side / 2};  // in the thumbnail box
            POINT off{r.left + 2, r.top + px(4) + 2};                     // beside it, above the name
            if (!app.hitTestItem(on, &index) || index != 0) s += L"：点缩略图不算点中那一项";
            else if (app.hitTestItem(off, &index)) s += L"：点格子里的空白也算点中";
        }
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name = L"只有一两张图的文件夹：还是平时的视图（列表）";
        std::wstring s = name;
        app.loadDirectory(mixedDir, false);
        if (app.thumbnails || app.viewAutomatic || app.mode != ViewMode::Details) s += L"：视图不对";
        else if (app.iconBox() != pxOf(app.scale, 48) || app.labelTop() != pxOf(app.scale, 54))
            s += L"：图标框是 " + std::to_wstring(app.iconBox());
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name =
            L"在图片多的文件夹里自己选了别的视图：只对这个文件夹管用，不改平时的视图；离开后平时的视图回来";
        std::wstring s = name;
        app.loadDirectory(picDir, false);
        app.setViewMode(ViewMode::List);
        if (app.mode != ViewMode::List || !app.viewAutomatic) s += L"：选不成列表视图";
        else if (app.settings.viewMode != (int)ViewMode::Details) s += L"：把平时的视图改成了列表";
        app.saveSettings();
        Settings saved;
        saved.load();
        if (saved.viewMode != (int)ViewMode::Details) s += L"：存下来的视图成了 " + std::to_wstring(saved.viewMode);
        app.loadDirectory(base, false);
        if (app.mode != ViewMode::Details || app.viewAutomatic) s += L"：离开后没有回到列表";
        check(s.c_str(), s == name);
    }
    {
        // A frame of the folder (five text files, six pictures) is rendered
        // once, at a size nothing asked for before: just the six pictures are
        // asked for a thumbnail.
        const std::wstring name = L"缩略图只向图片要：画一遍有五个文本文件和六张图的文件夹，要的正好是六次";
        std::wstring s = name;
        app.loadDirectory(picDir, false);
        const size_t before = thumbs::requested();
        renderedFrame(app);
        const size_t asked = thumbs::requested() - before;
        if (asked != kThumbPictures) s += L"：要了 " + std::to_wstring(asked) + L" 次";
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name =
            L"缩略图不在这条线程上做：第一次要它时它还没好（先去要，到了再通知窗口），做好后再要就是现成的；大小不超过要的框，样子和颜色都对";
        std::wstring s = name;
        const std::wstring picture = picDir + L"\\p0.bmp";
        const int px = 64;  // a size nothing asked for before
        if (thumbs::peek(picture, px)) s += L"：还没要过就已经有了";
        const HWND notify = (HWND)1;  // nobody: only the making is asked for here
        if (thumbs::get(picture, px, notify)) s += L"：第一次要就当场做好了（挡住了要的人）";
        std::shared_ptr<const thumbs::Bitmap> made;
        for (int i = 0; i < 750 && !made; ++i) {  // up to fifteen seconds
            made = thumbs::peek(picture, px);
            if (!made) Sleep(20);
        }
        if (!made) s += L"：等了十五秒也没做好";
        else if (thumbs::get(picture, px, nullptr) != made) s += L"：做好后再要，给的不是一个";
        // 300 by 200 in a box of 64: 64 by 43.
        else if (made->w > px || made->h > px || std::abs(made->w * 200 - made->h * 300) > 600)
            s += L"：大小是 " + std::to_wstring(made->w) + L"x" + std::to_wstring(made->h);
        else {
            int w = 0, h = 0;
            const std::vector<unsigned char> pixels = bitmapPixels(made->handle, &w, &h);
            const size_t centre = ((size_t)(h / 2) * w + w / 2) * 4;
            if (pixels.size() < centre + 3) s += L"：取不出像素";
            else if (pixels[centre] != 0 || pixels[centre + 1] != 0 || pixels[centre + 2] != 255)
                s += L"：中间的像素是 " + std::to_wstring(pixels[centre + 2]) + L"," +
                     std::to_wstring(pixels[centre + 1]) + L"," + std::to_wstring(pixels[centre]) + L"（该是红的）";
            else if (pixels[centre + 3] != 255) s += L"：不透明的图成了半透明";
        }
        check(s.c_str(), s == name);
    }
    {
        const std::wstring name = L"滚动或换文件夹后，还没动手做的缩略图不再做（已经做了的照做）";
        std::wstring s = name;
        for (int k = 1; k < kThumbPictures; ++k)
            thumbs::get(picDir + L"\\p" + std::to_wstring(k) + L".bmp", 48, nullptr);
        thumbs::dropPending();
        // Four are being made at most (there are four threads); nothing waits in line.
        if (thumbs::pending() > 4) s += L"：还有 " + std::to_wstring(thumbs::pending()) + L" 个排着队";
        check(s.c_str(), s == name);
    }
    thumbnailTest(picDir, mixedDir, check);

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
    // The test's folder goes with the test.  (Runs used to leave it behind in
    // %TEMP%, one per run; and this one's path may be past MAX_PATH.)
    std::error_code ignored;
    std::filesystem::remove_all(L"\\\\?\\" + base, ignored);
    // The scratch key of "this program opens folders" goes with the rest, and
    // the real one is the program's again - with the keys above the scratch
    // ones, when nothing else is in them.
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\MyOldComputer\\Tests\\Ui");
    takeover::useKey(nullptr);
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\MyOldComputer\\Tests");
    RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\MyOldComputer");
    diagLog(L"UI test: real mouse or keyboard input noticed %d times", inputs);
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

    // This program in place of Explorer for opening folders (takeover.h), on
    // a scratch key: what is written, what is taken out, what is left alone,
    // and what goes to Explorer.
    {
        const std::wstring root = L"Software\\MyOldComputer\\Tests\\SelfTest";
        const std::wstring key = root + L"\\Folder\\shell\\open\\command";
        RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
        takeover::useKey(key.c_str());
        const wchar_t* other = L"\"C:\\Nowhere\\Other.exe\" \"%1\"";
        wchar_t exe[2048]{};
        GetModuleFileNameW(nullptr, exe, 2048);
        // This program from where it was before it was moved: there is nothing there.
        const std::wstring elsewhere =
            L"\"" + base + L"\\" + fsx::fileName(exe) + L"\" --path \"%1\"";
        const auto line = [&](const wchar_t* name, bool ok) {
            results.push_back(std::wstring(name) + (ok ? L"OK" : L"FAIL"));
        };
        line(L"接管文件夹·起初是资源管理器 ",
             takeover::state() == takeover::State::explorer && !takeover::on());
        line(L"接管文件夹·打开 ",
             takeover::set(true) && takeover::state() == takeover::State::ours && takeover::on() &&
                 takeoverSays(key.c_str(), nullptr, takeover::command().c_str()) &&
                 takeoverSays(key.c_str(), L"DelegateExecute", L""));
        line(L"接管文件夹·别的程序的不动 ",
             takeoverPut(key.c_str(), nullptr, other) &&
                 takeoverPut(key.c_str(), L"DelegateExecute", nullptr) &&
                 takeover::state() == takeover::State::other && !takeover::set(false) &&
                 takeover::state() == takeover::State::other);
        line(L"接管文件夹·程序被移动过就重写 ",
             takeoverPut(key.c_str(), nullptr, elsewhere.c_str()) &&
                 takeover::state() == takeover::State::moved && takeover::on() && takeover::heal() &&
                 takeover::state() == takeover::State::ours &&
                 takeoverSays(key.c_str(), nullptr, takeover::command().c_str()));
        // Another copy of this program, where it still is, opens folders as
        // well as this one would: it is left to it.  (The same file by another
        // way to it.)
        const std::wstring copy = L"\"\\\\?\\" + std::wstring(exe) + L"\" --path \"%1\"";
        line(L"接管文件夹·另一份还在的拷贝不动 ",
             takeoverPut(key.c_str(), nullptr, copy.c_str()) &&
                 takeover::state() == takeover::State::ours && takeover::on() && !takeover::heal() &&
                 takeoverSays(key.c_str(), nullptr, copy.c_str()));
        line(L"接管文件夹·关掉 ",
             takeover::set(false) && takeover::state() == takeover::State::explorer &&
                 takeoverKeyGone(key));
        line(L"转交 Explorer·在的文件夹留给自己，其余的转交 ",
             takeover::isFolder(dir) && takeover::isFolder(dir + L"\\sub") &&
                 !takeover::isFolder(dir + L"\\f1.txt") && !takeover::isFolder(dir + L"\\not-there") &&
                 !takeover::isFolder(L"::{645FF040-5081-101B-9F08-00AA002F954E}") &&
                 !takeover::isFolder(L"shell:RecycleBinFolder") && !takeover::isFolder(L""));
        line(L"转交 Explorer·交给它的参数是带引号的原样 ",
             takeover::explorerParameters(L"shell:RecycleBinFolder") == L"\"shell:RecycleBinFolder\"" &&
                 takeover::explorerParameters(L"C:\\A B\\::{5}") == L"\"C:\\A B\\::{5}\"");
        RegDeleteTreeW(HKEY_CURRENT_USER, root.c_str());
        takeover::useKey(nullptr);
        // ... and the keys above the scratch ones, when nothing else is in them.
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\MyOldComputer\\Tests");
        RegDeleteKeyW(HKEY_CURRENT_USER, L"Software\\MyOldComputer");
    }

    std::wstring out;
    int failed = 0;
    for (auto& r : results) {
        if (!out.empty()) out += L" | ";
        out += r;
        // A line that passed ends in "OK"; anything else is a failure.
        if (!(r.size() >= 2 && r.compare(r.size() - 2, 2, L"OK") == 0)) ++failed;
    }
    std::wstring line = L"SELFTEST " + out;
    OutputDebugStringW((line + L"\n").c_str());
    // The lines are in one file of %TEMP%, for whoever looks; the test's
    // folder goes with the test, as the UI test's does (runs used to leave
    // one behind, each).
    FILE* f = nullptr;
    if (_wfopen_s(&f, (std::wstring(temp) + L"moc-selftest.txt").c_str(), L"w, ccs=UTF-8") == 0 &&
        f) {
        fwprintf(f, L"%s\n", line.c_str());
        fclose(f);
    }
    app.loadDirectory(temp, false);
    std::error_code ignored;
    std::filesystem::remove_all(L"\\\\?\\" + base, ignored);
    return failed;
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
        // Nor may the colours the test gives folders reach the user's.
        DeleteFileW(foldercolours::path().c_str());
        // The test's lists stay in the background: the machine is somebody's.
        SetEnvironmentVariableW(L"MOC_NO_FOREGROUND", L"1");
        App app;
        int code = runUiTest(app);
        OleUninitialize();
        return code;
    }
    if (hasArg(args, L"--newwin-fg")) {
        // The foreground test of a new window (newWindowTest): it takes the
        // foreground for a few seconds, so it is run by hand, where that is
        // allowed.  Its instances start from default settings, save to a
        // scratch file, and come up unactivated - for the test to see them
        // brought to the front.
        wchar_t temp[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp);
        const std::wstring scratch = std::wstring(temp) + L"moc-uitest-settings.ini";
        DeleteFileW(scratch.c_str());
        SetEnvironmentVariableW(L"MOC_SETTINGS_FILE", scratch.c_str());
        SetEnvironmentVariableW(L"MOC_SHOWNOACTIVATE", L"1");
        const int code = runNewWindowForegroundTest();
        OleUninitialize();
        return code;
    }

    registerMainClass(instance);

    // Opened by the shell for what is not a folder this program shows - it
    // opens its own folders (the Recycle Bin, Control Panel) by the same
    // command, see takeover.h.  Explorer shows those.  Nothing of a window is
    // up yet: there is nothing to take down.
    std::wstring startPath = argValue(args, L"--path");
    if (!startPath.empty() && !takeover::isFolder(startPath)) {
        takeover::handOver(startPath);
        return 0;
    }
    // (This program opens folders, but was moved since that was written: it
    // is written again for here, before anything can be double-clicked.)
    takeover::heal();

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

    // An instance that stays in the background (a test's, see staysInBackground)
    // is never activated, not even by a click of its own.
    const DWORD exStyle = WS_EX_APPWINDOW | (staysInBackground() ? WS_EX_NOACTIVATE : 0);
    HWND hwnd = CreateWindowExW(exStyle, L"MyOldComputerWindow", L"My Old Computer",
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
    // Not exactly over the window of another instance: a title bar down and
    // to the right of it.
    const int titleH = pxOf(g_app.scale, 5) + pxOf(g_app.scale, 22);
    const POINT place = cascadePosition(POINT{x, y}, physicalW, titleH);
    placeWindow(hwnd, place.x, place.y, physicalW, physicalH);
    g_app.relayout(physicalW, physicalH);
    g_app.demo = hasArg(args, L"--demo");
    if (startPath.empty()) startPath = fsx::homeDir();
    if (g_app.demo) {
        g_app.current = startPath;
        g_app.buildTree();
    }
    g_app.loadDirectory(startPath, false);
    dnd::registerTarget(hwnd, &g_app);
    // The shell's menus are built ahead on a thread of this program's own,
    // up before the window shows: ready when the first folder's are asked for.
    shellmenu::start(hwnd);
    AddClipboardFormatListener(hwnd);  // WM_CLIPBOARDUPDATE: the background menus depend on it
    DragAcceptFiles(hwnd, TRUE);
    // (The foreground test of a new window has a new instance come up
    // unactivated on purpose, MOC_SHOWNOACTIVATE, to see the window that
    // opened it bring it to the front.)
    wchar_t shownQuietly[4]{};
    const bool unactivated =
        staysInBackground() || GetEnvironmentVariableW(L"MOC_SHOWNOACTIVATE", shownQuietly, 4);
    // An instance that stays in the background (a test's) is behind
    // everything from the first moment it is seen: shown and put there at
    // once.  (Shown first and put behind right after, it was in front for an
    // instant - on a busy machine long enough for a test looking then to find
    // it above the foreground window, which such an instance must never be.)
    if (staysInBackground())
        SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    else
        ShowWindow(hwnd, unactivated ? SW_SHOWNOACTIVATE
                                     : showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED
                                                                   : SW_SHOWNORMAL);
    UpdateWindow(hwnd);

    ULONGLONG listed = 0;
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        // A folder was listed (opened, reloaded): what a right-click in it would
        // want is built from now on.  (Until the thread is up the listing is
        // left unmarked: the first one is not to be missed.)
        if (g_app.listSerial != listed && shellmenu::running()) {
            listed = g_app.listSerial;
            prefetchMenus(g_app);
        }
    }
    // The thread that builds the menus is given a moment to be out of the
    // shell's code before the process ends under it.
    shellmenu::stop();
    g_app.saveSettings();
    OleUninitialize();
    return 0;
}
