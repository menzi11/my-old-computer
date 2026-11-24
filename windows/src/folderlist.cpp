#include "folderlist.h"

#include <windowsx.h>

#include <algorithm>

#include "canvas.h"
#include "debug.h"
#include "icons.h"
#include "scrollbar.h"
#include "shellicons.h"
#include "theme.h"

// In app.cpp: the width of a text in the interface's font.
int measureUiText(const std::wstring& s, int fontPx);

namespace {

constexpr wchar_t kListClass[] = L"MyOldComputerFolderList";
constexpr UINT_PTR kRepeatTimer = 1;  // a scrollbar arrow held down repeats

struct ListState {
    HWND hwnd = nullptr;
    const std::vector<FolderRow>* rows = nullptr;
    bool systemIcons = true;
    double scale = 1.0;
    Metrics m{};
    int rowH = 20;
    RECT rowsArea{};  // where the rows are drawn, client coordinates
    RECT bar{};       // the scrollbar beside them
    int top = 0;      // the first row in view
    int hot = -1;     // the row under the pointer, -1 while it is on none
    int current = -1;
    int chosen = -1;
    // The scrollbar: the part that is held down, and where the knob was taken.
    scrollbar::Part held = scrollbar::Part::None;
    int dragY = 0, dragTop = 0;
    FolderListEnd* end = nullptr;  // how the list was left, if the caller wants to know
};

ListState* g_list = nullptr;  // the one that is up

int viewHeight(const ListState& st) { return (int)(st.rowsArea.bottom - st.rowsArea.top); }

int visibleRows(const ListState& st) { return std::max(1, viewHeight(st) / st.rowH); }

int maxTop(const ListState& st) { return std::max(0, (int)st.rows->size() - visibleRows(st)); }

int contentHeight(const ListState& st) { return (int)st.rows->size() * st.rowH; }

int rowAt(const ListState& st, POINT p) {
    if (!PtInRect(&st.rowsArea, p)) return -1;
    const int i = st.top + (p.y - st.rowsArea.top) / st.rowH;
    return i >= 0 && i < (int)st.rows->size() ? i : -1;
}

void scrollListTo(ListState& st, int top) {
    top = std::clamp(top, 0, maxTop(st));
    if (top == st.top) return;
    st.top = top;
    InvalidateRect(st.hwnd, nullptr, FALSE);
}

// The highlight follows the pointer; on none, it is on the row the way to the
// current folder goes through.
int highlighted(const ListState& st) { return st.hot >= 0 ? st.hot : st.current; }

void drawList(Canvas& c, ListState& st) {
    RECT rc{};
    GetClientRect(st.hwnd, &rc);
    c.fill(rc, Theme::face);
    c.frame(rc, Theme::dark);
    // The list sits in a field like the address bar's.
    c.bevelField(RECT{rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1});
    c.fill(st.rowsArea, Theme::white);

    const int n = (int)st.rows->size();
    const int s16 = c.px(16);
    const int sel = highlighted(st);
    const int last = std::min(n, st.top + visibleRows(st));
    for (int i = st.top; i < last; ++i) {
        RECT r{st.rowsArea.left, st.rowsArea.top + (i - st.top) * st.rowH, st.rowsArea.right, 0};
        r.bottom = r.top + st.rowH;
        c.fill(r, i == sel ? Theme::select : Theme::white);
        const FolderRow& row = (*st.rows)[i];
        RECT icon{r.left + c.px(3), r.top + (st.rowH - s16) / 2, 0, 0};
        icon.right = icon.left + s16;
        icon.bottom = icon.top + s16;
        // The icon Explorer shows, as the tree draws it; the hand-drawn set otherwise.
        HICON shell = row.kind == TreeNode::Machine || !st.systemIcons || row.path.empty()
                          ? nullptr
                          : shellicons::icon(row.path, true, row.kind == TreeNode::Volume, s16);
        if (shell) {
            DrawIconEx(c.dc(), icon.left, icon.top, shell, s16, s16, 0, nullptr, DI_NORMAL);
        } else {
            switch (row.kind) {
            case TreeNode::Machine: Icon::computer(c, icon); break;
            case TreeNode::Volume: Icon::drive(c, icon); break;
            default: Icon::folder(c, icon); break;
            }
        }
        c.textClip(row.title, RECT{icon.right + c.px(4), r.top, r.right - c.px(2), r.bottom},
                   i == sel ? Theme::white : Theme::text, Align::Left);
    }
    scrollbar::draw(c, st.bar, st.m, contentHeight(st), viewHeight(st), st.top * st.rowH);
}

void paintList(HWND hwnd, HDC dc) {
    ListState* st = g_list;
    if (!st || st->hwnd != hwnd) return;
    Canvas c(dc, st->scale);
    drawList(c, *st);
}

void stepList(ListState& st, int rows) { scrollListTo(st, st.top + rows); }

void keyList(ListState& st, WPARAM key) {
    const int n = (int)st.rows->size();
    if (n == 0) return;
    int sel = highlighted(st);
    switch (key) {
    case VK_ESCAPE:
        DestroyWindow(st.hwnd);
        return;
    case VK_RETURN:
    case VK_SPACE:
        if (sel >= 0) {
            st.chosen = sel;
            DestroyWindow(st.hwnd);
        }
        return;
    case VK_DOWN: ++sel; break;
    case VK_UP: --sel; break;
    case VK_PRIOR: sel -= visibleRows(st) - 1; break;
    case VK_NEXT: sel += visibleRows(st) - 1; break;
    case VK_HOME: sel = 0; break;
    case VK_END: sel = n - 1; break;
    default: return;
    }
    sel = std::clamp(sel, 0, n - 1);
    if (sel != st.hot) {
        st.hot = sel;
        InvalidateRect(st.hwnd, nullptr, FALSE);
    }
    // The highlighted row is kept in view.
    if (sel < st.top) scrollListTo(st, sel);
    else if (sel >= st.top + visibleRows(st)) scrollListTo(st, sel - visibleRows(st) + 1);
}

LRESULT CALLBACK listProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ListState* st = g_list;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paintList(hwnd, dc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT:
        paintList(hwnd, (HDC)wp);
        return 0;
    case WM_MOUSEMOVE:
        if (st) {
            const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (st->held == scrollbar::Part::Knob) {
                const int pos = scrollbar::positionFromDrag(st->bar, st->m, contentHeight(*st),
                                                            viewHeight(*st), st->dragTop * st->rowH,
                                                            p.y - st->dragY);
                scrollListTo(*st, (pos + st->rowH / 2) / st->rowH);
            } else {
                const int hot = rowAt(*st, p);
                if (hot != st->hot) {
                    st->hot = hot;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (st) {
            const POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT rc{};
            GetClientRect(hwnd, &rc);
            if (!PtInRect(&rc, p)) {
                // Pressed elsewhere: the list closes, and the press goes no
                // further - the caller is told where it was.
                if (st->end) {
                    st->end->pressedElsewhere = true;
                    st->end->press = p;
                    ClientToScreen(hwnd, &st->end->press);
                }
                DestroyWindow(hwnd);
                return 0;
            }
            const auto part = scrollbar::hitTest(st->bar, p, st->m, contentHeight(*st),
                                                 viewHeight(*st), st->top * st->rowH);
            if (part == scrollbar::Part::Knob) {
                st->held = part;
                st->dragY = p.y;
                st->dragTop = st->top;
            } else if (part == scrollbar::Part::Decrement || part == scrollbar::Part::Increment) {
                st->held = part;
                stepList(*st, part == scrollbar::Part::Increment ? 1 : -1);
                SetTimer(hwnd, kRepeatTimer, 300, nullptr);
            } else if (part == scrollbar::Part::PageUp || part == scrollbar::Part::PageDown) {
                stepList(*st, part == scrollbar::Part::PageDown ? visibleRows(*st) : -visibleRows(*st));
            } else {
                const int hot = rowAt(*st, p);
                if (hot >= 0 && hot != st->hot) {
                    st->hot = hot;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (st) {
            if (st->held != scrollbar::Part::None) {
                st->held = scrollbar::Part::None;
                KillTimer(hwnd, kRepeatTimer);
                return 0;
            }
            // Pressed on the arrow and released on a row, or simply clicked: that row.
            const int row = rowAt(*st, POINT{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)});
            if (row >= 0) {
                st->chosen = row;
                DestroyWindow(hwnd);
            }
        }
        return 0;
    case WM_TIMER:
        if (st && wp == kRepeatTimer && st->held != scrollbar::Part::None) {
            stepList(*st, st->held == scrollbar::Part::Increment ? 1 : -1);
            SetTimer(hwnd, kRepeatTimer, 60, nullptr);
        }
        return 0;
    case WM_MOUSEWHEEL:
        if (st) {
            const int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            stepList(*st, -steps * 3);
        }
        return 0;
    case WM_KEYDOWN:
        if (st) keyList(*st, wp);
        return 0;
    case WM_CAPTURECHANGED:
        // Something else took the mouse: never keep a half-finished scrollbar drag.
        if (st) {
            st->held = scrollbar::Part::None;
            KillTimer(hwnd, kRepeatTimer);
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (st && st->hwnd == hwnd) st->hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = listProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kListClass;
    RegisterClassExW(&wc);
    done = true;
}

BOOL CALLBACK findListWindow(HWND h, LPARAM lp) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t cls[64]{};
    if (pid == GetCurrentProcessId() && GetClassNameW(h, cls, 64) && lstrcmpW(cls, kListClass) == 0) {
        *(HWND*)lp = h;
        return FALSE;
    }
    return TRUE;
}

}  // namespace

int showFolderList(HWND owner, const std::vector<FolderRow>& rows, int current,
                   const RECT& anchor, bool systemIcons, FolderListEnd* end) {
    if (rows.empty()) return -1;
    ensureClass();

    ListState st;
    st.rows = &rows;
    st.end = end;
    if (end) *end = FolderListEnd{};
    st.current = current >= 0 && current < (int)rows.size() ? current : -1;
    st.systemIcons = systemIcons;
    st.scale = dpiScaleOfWindow(owner);
    st.m = metricsFor(st.scale);
    st.rowH = (int)(20 * st.scale + 0.5);
    auto px = [&st](double v) { return (int)(v * st.scale + 0.5); };

    // Width: the frame, the icon, the widest name, room after it, the scrollbar.
    int textW = 0;
    const int fontPx = (int)(14.6667 * st.scale + 0.5);
    for (const FolderRow& r : rows) textW = std::max(textW, measureUiText(r.title, fontPx));
    const int frame = px(3);
    int w = 2 * frame + px(3) + px(16) + px(4) + textW + px(10) + st.m.scrollerW;
    w = std::max(w, px(180));

    POINT at{anchor.left, anchor.top};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(at, MONITOR_DEFAULTTONEAREST), &mi);
    // A name may be 255 characters long: the list is never wider than the
    // screen, and such a name is cut short with an ellipsis.
    w = std::min(w, (int)(mi.rcWork.right - mi.rcWork.left) - 2 * px(8));

    // Height: every row, or as many as fit on the screen.
    const int n = (int)rows.size();
    const int onScreen = std::max(1, (int)((mi.rcWork.bottom - mi.rcWork.top) - 2 * frame) / st.rowH);
    int visible = std::min(n, onScreen);
    int h = visible * st.rowH + 2 * frame;

    int x = anchor.left;
    int y = anchor.bottom + 1;  // under the arrow it belongs to
    if (x + w > mi.rcWork.right) x = std::max((int)mi.rcWork.left, (int)mi.rcWork.right - w);
    if (y + h > mi.rcWork.bottom) {
        // Not enough room below: what fits below, unless more fits above.
        const int below = std::max(1, (int)((mi.rcWork.bottom - y) - 2 * frame) / st.rowH);
        const int above = std::max(0, (int)((anchor.top - 1 - mi.rcWork.top) - 2 * frame) / st.rowH);
        if (above > below) {
            visible = std::min(n, above);
            h = visible * st.rowH + 2 * frame;
            y = anchor.top - 1 - h;
        } else {
            visible = std::min(n, below);
            h = visible * st.rowH + 2 * frame;
        }
    }

    // An instance that stays in the background (a test's): the list is never
    // the foreground window and not above everybody else's.
    const bool background = staysInBackground();
    HWND hwnd = CreateWindowExW(
        WS_EX_TOOLWINDOW | (background ? WS_EX_NOACTIVATE : WS_EX_TOPMOST), kListClass, L"",
        WS_POPUP, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return -1;
    st.hwnd = hwnd;
    const RECT inner{frame, frame, w - frame, h - frame};
    st.bar = RECT{inner.right - st.m.scrollerW, inner.top, inner.right, inner.bottom};
    st.rowsArea = RECT{inner.left, inner.top, st.bar.left, inner.top + visible * st.rowH};
    st.top = maxTop(st) > 0 && st.current >= 0 ? std::clamp(st.current - visible / 2, 0, maxTop(st)) : 0;
    g_list = &st;

    ShowWindow(hwnd, SW_SHOWNA);
    if (!background) SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    // The list has the mouse from the start: a press that opened it and is
    // released on a row picks that row, and a press anywhere else closes it.
    SetCapture(hwnd);

    MSG msg;
    while (st.hwnd) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            // WM_QUIT must survive the list: put it back for the outer loop.
            if (r == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (GetCapture() == hwnd) ReleaseCapture();
    if (st.hwnd) {
        DestroyWindow(st.hwnd);
        st.hwnd = nullptr;
    }
    g_list = nullptr;
    return st.chosen;
}

HWND folderListWindow() {
    HWND found = nullptr;
    EnumThreadWindows(GetCurrentThreadId(), findListWindow, (LPARAM)&found);
    return found;
}

const std::vector<FolderRow>* folderListRows(HWND list) {
    return g_list && g_list->hwnd == list ? g_list->rows : nullptr;
}

bool folderListInfo(HWND list, FolderListInfo* info) {
    ListState* st = g_list;
    if (!info || !st || st->hwnd != list) return false;
    info->first = st->top;
    info->visible = visibleRows(*st);
    info->highlighted = highlighted(*st);
    info->rowH = st->rowH;
    info->rows = st->rowsArea;
    return true;
}
