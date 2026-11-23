#include "dialog.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "canvas.h"
#include "theme.h"

namespace {

constexpr int kMenuClassTag = 0x4D4F4331;  // "MOC1"

struct MenuState {
    HWND hwnd = nullptr;
    const std::vector<MenuItem>* items = nullptr;
    int hover = -1;
    int chosen = 0;
    int scalePx = 1;
    double scale = 1.0;
    std::vector<RECT> rects;
};

MenuState* g_menu = nullptr;

int itemHeight(const MenuItem& it, double scale) {
    return it.separator ? (int)(7 * scale + 0.5) : (int)(22 * scale + 0.5);
}

void computeRects(MenuState& st) {
    st.rects.clear();
    int y = (int)(3 * st.scale + 0.5);
    for (auto& it : *st.items) {
        int h = itemHeight(it, st.scale);
        RECT r{0, y, 0, y + h};
        st.rects.push_back(r);
        y += h;
    }
}

int measureMenu(Canvas& c, MenuState& st) {
    int textW = 0, shortcutW = 0;
    for (auto& it : *st.items) {
        if (it.separator) continue;
        textW = std::max(textW, c.textWidth(it.title));
        shortcutW = std::max(shortcutW, c.textWidth(it.shortcut));
    }
    int w = c.px(20) + textW + c.px(20);
    if (shortcutW > 0) w += shortcutW + c.px(24);
    return w;
}

void drawMenu(Canvas& c, HWND hwnd, MenuState& st) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    c.fill(rc, Theme::face);
    c.frame(rc, Theme::dark);
    RECT inner{rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1};
    c.bevelRaised(inner);

    for (size_t i = 0; i < st.items->size(); ++i) {
        const MenuItem& it = (*st.items)[i];
        RECT r = st.rects[i];
        r.left = c.px(3);
        r.right = rc.right - c.px(3);
        if (it.separator) {
            int y = (r.top + r.bottom) / 2;
            c.hline(r.left + c.px(1), y, r.right - r.left - c.px(2), Theme::shadow);
            c.hline(r.left + c.px(1), y + 1, r.right - r.left - c.px(2), Theme::white);
            continue;
        }
        const bool hot = (int)i == st.hover && it.enabled;
        if (hot) c.fill(r, Theme::select);
        COLORREF fg = !it.enabled ? Theme::dim : (hot ? Theme::white : Theme::text);
        if (it.checked) {
            int y = (r.top + r.bottom) / 2;
            int x = r.left + c.px(7);
            c.line(x, y, x + c.px(3), y + c.px(3), fg);
            c.line(x + c.px(3), y + c.px(3), x + c.px(8), y - c.px(3), fg);
        }
        c.textClip(it.title, RECT{r.left + c.px(18), r.top, r.right - c.px(6), r.bottom}, fg,
                   Align::Left);
        if (!it.shortcut.empty()) {
            c.textClip(it.shortcut, RECT{r.left, r.top, r.right - c.px(6), r.bottom}, fg,
                       Align::Right);
        }
    }
}

void paintMenu(HWND hwnd) {
    MenuState* st = (MenuState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    Canvas c(dc, st->scale);
    drawMenu(c, hwnd, *st);
    EndPaint(hwnd, &ps);
}

int itemAt(MenuState& st, POINT p) {
    for (size_t i = 0; i < st.rects.size(); ++i) {
        RECT r = st.rects[i];
        r.left = 3;
        r.right = 10000;
        if (p.y >= r.top && p.y < r.bottom) return (int)i;
    }
    return -1;
}

LRESULT CALLBACK menuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    MenuState* st = (MenuState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paintMenu(hwnd);
        return 0;
    case WM_PRINTCLIENT:
        if (st) {
            Canvas c((HDC)wp, st->scale);
            drawMenu(c, hwnd, *st);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (st) {
            int idx = itemAt(*st, POINT{(short)LOWORD(lp), (short)HIWORD(lp)});
            if (idx >= 0 && (*st->items)[idx].separator) idx = -1;
            if (idx != st->hover) {
                st->hover = idx;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (st) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            int idx = itemAt(*st, p);
            if (idx >= 0 && !(*st->items)[idx].separator && (*st->items)[idx].enabled) {
                st->chosen = (*st->items)[idx].id;
                DestroyWindow(hwnd);
            }
        }
        return 0;
    // A quick second click arrives as a double-click; to this menu it is
    // the click it looks like (clicking the menu bar's title again closes it).
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
        if (!st) return 0;
        POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        if (!PtInRect(&rc, p)) DestroyWindow(hwnd);
        return 0;
    }
    case WM_RBUTTONDOWN:
        if (st) DestroyWindow(hwnd);
        return 0;
    case WM_KEYDOWN:
        if (st) {
            int n = (int)st->items->size();
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd);
            } else if (wp == VK_RETURN) {
                if (st->hover >= 0 && !(*st->items)[st->hover].separator &&
                    (*st->items)[st->hover].enabled) {
                    st->chosen = (*st->items)[st->hover].id;
                    DestroyWindow(hwnd);
                }
            } else if (wp == VK_DOWN || wp == VK_UP) {
                int dir = (wp == VK_DOWN) ? 1 : -1;
                int idx = st->hover;
                for (int k = 0; k < n; ++k) {
                    idx = (idx + dir + n) % n;
                    if (!(*st->items)[idx].separator && (*st->items)[idx].enabled) break;
                }
                if (idx >= 0 && !(*st->items)[idx].separator) {
                    st->hover = idx;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (st) st->hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = menuProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MyOldComputerMenu";
    RegisterClassExW(&wc);
    done = true;
}

int activeMenuCount = 0;

}  // namespace

int showPopupMenu(HWND owner, const std::vector<MenuItem>& items, int x, int y, bool alignRight) {
    if (items.empty()) return 0;
    ensureClass();

    MenuState st;
    st.items = &items;
    st.scale = dpiScaleOfWindow(owner);

    HDC screen = GetDC(nullptr);
    HDC scaleDc = CreateCompatibleDC(screen);
    int w, h;
    {
        Canvas c(scaleDc, st.scale);
        computeRects(st);
        h = st.rects.empty() ? 0 : st.rects.back().bottom + c.px(3);
        w = measureMenu(c, st);
    }
    DeleteDC(scaleDc);
    ReleaseDC(nullptr, screen);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONEAREST);
    GetMonitorInfoW(mon, &mi);
    if (alignRight) x -= w;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"MyOldComputerMenu", L"",
                                WS_POPUP, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr),
                                nullptr);
    if (!hwnd) return 0;
    st.hwnd = hwnd;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    ShowWindow(hwnd, SW_SHOWNA);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    SetCapture(hwnd);
    ++activeMenuCount;

    MSG msg;
    while (st.hwnd) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            // WM_QUIT must survive the menu: put it back for the outer loop.
            if (r == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    --activeMenuCount;
    if (GetCapture() == hwnd) ReleaseCapture();
    if (st.hwnd) {
        DestroyWindow(st.hwnd);
        st.hwnd = nullptr;
    }
    return st.chosen;
}
