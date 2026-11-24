#include "hmmenu.h"

#include <algorithm>
#include <string>
#include <vector>

#include "canvas.h"
#include "debug.h"
#include "theme.h"

namespace {

struct Node {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HMENU menu = nullptr;
    Node* parent = nullptr;
    std::vector<Node*> children;
    std::vector<RECT> rects;
    std::vector<std::wstring> texts;
    std::vector<UINT> ids;
    std::vector<bool> seps;
    std::vector<bool> subs;
    std::vector<bool> checked;
    std::vector<bool> enabled;
    std::vector<bool> isDefault;
    std::vector<COLORREF> swatch;  // a colour the item stands for, to show in a box (0: none)
    int hover = -1;
    int chosen = 0;
    double scale = 1.0;
    // Row whose submenu is open (-1 while none is).
    int openRow = -1;
    // Where the pointer leaves that row: where the path to its submenu starts.
    POINT leave{};
    // The pointer was last inside our submenu (it is on its way back out).
    bool childHadMouse = false;
};

Node* g_active = nullptr;
Node* g_root = nullptr;
bool g_finished = false;

void destroyChildren(Node* n);

void destroyNode(Node* n) {
    if (!n) return;
    destroyChildren(n);
    if (n->hwnd) DestroyWindow(n->hwnd);
    delete n;
}

void destroyChildren(Node* n) {
    for (auto* child : n->children) destroyNode(child);
    n->children.clear();
    n->openRow = -1;
    // The keyboard acted inside the submenus that are gone now; it belongs to
    // this level again.  (Leaving it on a destroyed node made Esc, Enter and
    // the arrow keys operate on freed memory.)
    g_active = n;
}

int itemHeight(bool separator, double scale) {
    return separator ? (int)(7 * scale + 0.5) : (int)(22 * scale + 0.5);
}

std::wstring itemText(HMENU menu, int index) {
    wchar_t buffer[512]{};
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = buffer;
    info.cch = 512;
    if (!GetMenuItemInfoW(menu, index, TRUE, &info)) return L"";
    std::wstring text = buffer;
    // "打开(&O)" -> "打开"：Win98 菜单不显示快捷键提示。
    const size_t amp = text.find(L"(&");
    if (amp != std::wstring::npos && text.size() > amp + 3 && text.back() == L')')
        text = text.substr(0, amp);
    return text;
}

void parseMenu(Node* n) {
    const int count = GetMenuItemCount(n->menu);
    n->rects.assign(count, RECT{});
    n->texts.assign(count, L"");
    n->ids.assign(count, 0);
    n->seps.assign(count, false);
    n->subs.assign(count, false);
    n->checked.assign(count, false);
    n->enabled.assign(count, true);
    n->isDefault.assign(count, false);
    n->swatch.assign(count, 0);
    n->hover = -1;
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_DATA;
        GetMenuItemInfoW(n->menu, i, TRUE, &info);
        n->seps[i] = (info.fType & MFT_SEPARATOR) != 0;
        n->subs[i] = info.hSubMenu != nullptr;
        n->ids[i] = info.wID;
        n->checked[i] = (info.fState & MFS_CHECKED) != 0;
        n->enabled[i] = (info.fState & MFS_DISABLED) == 0;
        n->isDefault[i] = (info.fState & MFS_DEFAULT) != 0;
        n->swatch[i] = (COLORREF)info.dwItemData;
        n->texts[i] = n->seps[i] ? L"" : itemText(n->menu, i);
    }
    int y = (int)(3 * n->scale + 0.5);
    for (int i = 0; i < count; ++i) {
        const int h = itemHeight(n->seps[i], n->scale);
        n->rects[i] = RECT{0, y, 0, y + h};
        y += h;
    }
}

int measureWidth(Node* n, Canvas& c) {
    int textW = 0;
    bool anySub = false;
    for (size_t i = 0; i < n->texts.size(); ++i) {
        if (n->seps[i]) continue;
        textW = std::max(textW, c.textWidth(n->texts[i]));
        if (n->subs[i]) anySub = true;
    }
    int w = c.px(20) + textW + c.px(20);
    if (anySub) w += c.px(18);
    return w;
}

void drawNode(Canvas& c, HWND hwnd, Node* n) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    c.fill(rc, Theme::face);
    c.frame(rc, Theme::dark);
    c.bevelRaised(RECT{rc.left + 1, rc.top + 1, rc.right - 1, rc.bottom - 1});

    for (size_t i = 0; i < n->rects.size(); ++i) {
        RECT r = n->rects[i];
        r.left = c.px(3);
        r.right = rc.right - c.px(3);
        if (n->seps[i]) {
            const int y = (r.top + r.bottom) / 2;
            c.hline(r.left + c.px(1), y, r.right - r.left - c.px(2), Theme::shadow);
            c.hline(r.left + c.px(1), y + 1, r.right - r.left - c.px(2), Theme::white);
            continue;
        }
        const bool hot = (int)i == n->hover && n->enabled[i];
        if (hot) c.fill(r, Theme::select);
        COLORREF fg = !n->enabled[i] ? Theme::dim : (hot ? Theme::white : Theme::text);
        if (n->swatch[i]) {
            // The item stands for a colour (a folder's): a box in it, where
            // the tick would be - pressed in when it is the one chosen.
            const int cy = (r.top + r.bottom) / 2;
            RECT box{r.left + c.px(4), cy - c.px(4), r.left + c.px(13), cy + c.px(4)};
            c.fill(RECT{box.left - 1, box.top - 1, box.right + 1, box.bottom + 1}, Theme::face);
            if (n->checked[i])
                c.bevelSunken(RECT{box.left - 1, box.top - 1, box.right + 1, box.bottom + 1});
            else
                c.bevelRaised(RECT{box.left - 1, box.top - 1, box.right + 1, box.bottom + 1});
            c.fill(box, n->swatch[i]);
        } else if (n->checked[i]) {
            const int cy = (r.top + r.bottom) / 2;
            const int x = r.left + c.px(7);
            c.line(x, cy, x + c.px(3), cy + c.px(3), fg);
            c.line(x + c.px(3), cy + c.px(3), x + c.px(8), cy - c.px(3), fg);
        }
        RECT textRect{r.left + c.px(18), r.top, r.right - c.px(6), r.bottom};
        c.textClip(n->texts[i], textRect, fg, Align::Left, n->isDefault[i]);
        if (n->subs[i]) {
            const int cy = (r.top + r.bottom) / 2;
            const int ax = r.right - c.px(10);
            POINT tri[3] = {{ax, cy - c.px(4)}, {ax + c.px(5), cy}, {ax, cy + c.px(4)}};
            c.polygonFill(tri, 3, fg);
        }
    }
}

void paintNode(HWND hwnd) {
    Node* n = (Node*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!n) return;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    Canvas c(dc, n->scale);
    drawNode(c, hwnd, n);
    EndPaint(hwnd, &ps);
}

// Responsiveness rule for this menu: a hover change puts its highlight on
// screen before anything else runs, and no timer stands between the pointer
// and what the user sees.  Submenus open and close on the spot.
//
// The row under the pointer is the selected row, with one exception, and it
// is a matter of place, not of time.  A submenu lies beside the rows below
// its own, so a pointer on its way there crosses them; if each of them took
// the selection the moment the pointer entered it, the submenu would be gone
// before the pointer arrived.  So while the pointer is on the path from the
// row it left to the submenu, that row keeps the selection and its submenu.
// The path is a funnel: as wide as the row where the pointer left it, as
// wide as the submenu plus a row of slack above and below where it meets the
// submenu - and never further than three rows from its own row, or a tall
// submenu would hold its row against every row beside it.  (Its far rows are
// reached by going right into it first.)  Moving along the menu leaves the
// funnel at once.

void repaintRow(HWND hwnd, const Node* n, int index) {
    if (index < 0 || index >= (int)n->rects.size()) return;
    InvalidateRect(hwnd, &n->rects[index], FALSE);
}

int hitItem(Node* n, POINT p) {
    for (size_t i = 0; i < n->rects.size(); ++i)
        if (p.y >= n->rects[i].top && p.y < n->rects[i].bottom) return (int)i;
    return -1;
}

// True while p (client coordinates of n) is on that path: the pointer has
// left the row whose submenu is open, is to the right of where it left it,
// and is inside the funnel from the row to the submenu.
bool onTheWayToSubmenu(const Node* n, POINT p) {
    if (n->children.empty() || n->hover < 0 || n->hover != n->openRow) return false;
    const Node* child = n->children.front();
    if (!child->hwnd) return false;
    RECT sub{};
    GetWindowRect(child->hwnd, &sub);
    MapWindowPoints(nullptr, n->hwnd, (LPPOINT)&sub, 2);
    const RECT& row = n->rects[n->openRow];
    const POINT from = n->leave;
    // A submenu opens to the right of its row.  One that had to open to the
    // left lies over the row itself, so the pointer is in it as soon as it
    // moves: nothing to hold.
    if (sub.left <= from.x || p.x <= from.x || p.x >= sub.left) return false;
    const int rowH = row.bottom - row.top;
    // Where the path meets the submenu: as wide as the submenu plus a row of
    // slack above and below, but no further than three rows from the row.
    const int reach = 3 * rowH;
    const LONG top = std::max(sub.top - rowH, row.top - reach);
    const LONG bottom = std::min(sub.bottom + rowH, row.bottom + reach);
    const double t = (double)(p.x - from.x) / (double)(sub.left - from.x);
    return p.y >= row.top + (double)(top - row.top) * t &&
           p.y < row.bottom + (double)(bottom - row.bottom) * t;
}

// The path starts here when the submenu was opened by the keyboard or a
// click, not by the pointer resting in the row: the row's right edge, which
// no pointer position in this menu is ever to the right of.
POINT rowEdge(const Node* n, int index) {
    const RECT& r = n->rects[index];
    return POINT{r.right, (r.top + r.bottom) / 2};
}

// True for a window of this menu: the root or any of its submenus.
bool isMenuWindow(HWND h) {
    wchar_t cls[32]{};
    DWORD pid = 0;
    return h && GetWindowThreadProcessId(h, &pid) && pid == GetCurrentProcessId() &&
           GetClassNameW(h, cls, 32) && lstrcmpW(cls, L"MyOldComputerHMenu") == 0;
}

// The pointer is on one of this menu's windows with the left button held:
// the foreground is being lost to a click of ours, not to the user leaving.
bool clickedOnMenu() {
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return false;
    POINT p{};
    return GetCursorPos(&p) && isMenuWindow(WindowFromPoint(p));
}

void closeAll(Node* root) {
    destroyChildren(root);
    if (root->hwnd) {
        DestroyWindow(root->hwnd);
        root->hwnd = nullptr;
    }
    g_active = nullptr;
    g_finished = true;
}

void createNodeWindow(Node* n, int x, int y) {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    int width = 120;
    {
        Canvas c(mem, n->scale);
        width = measureWidth(n, c);
    }
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    const int height = n->rects.empty() ? 0 : n->rects.back().bottom + (int)(3 * n->scale + 0.5);
    RECT work{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromPoint(POINT{x, y}, MONITOR_DEFAULTTONEAREST), &mi))
        work = mi.rcWork;
    if (x + width > work.right) x = work.right - width;
    if (y + height > work.bottom) y = work.bottom - height;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;

    const DWORD ex = n->parent ? (WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE)
                               : (WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
    n->hwnd = CreateWindowExW(ex, L"MyOldComputerHMenu", L"", WS_POPUP, x, y, width, height,
                              nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!n->hwnd) return;
    for (auto& r : n->rects) {
        r.left = 0;
        r.right = width;
    }
    SetWindowLongPtrW(n->hwnd, GWLP_USERDATA, (LONG_PTR)n);
    ShowWindow(n->hwnd, SW_SHOWNA);
    InvalidateRect(n->hwnd, nullptr, FALSE);
    UpdateWindow(n->hwnd);
}

void openChild(Node* n, int index, POINT from) {
    if (index < 0 || index >= (int)n->subs.size() || !n->subs[index] || !n->enabled[index]) return;
    // Already showing this row's submenu.  This has to be checked against the
    // row the open submenu belongs to, never against hover: every caller
    // assigns hover first, so a hover comparison was always true and the
    // first submenu shown could never be replaced by another row's.
    if (!n->children.empty() && n->openRow == index) return;
    diagLog(L"openChild idx=%d", index);
    n->leave = from;
    destroyChildren(n);
    HMENU sub = GetSubMenu(n->menu, index);
    if (!sub) return;
    // Shell extensions fill their submenu on WM_INITMENUPOPUP.
    SendMessageW(n->owner, WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(index, 0));
    auto* child = new Node();
    child->menu = sub;
    child->owner = n->owner;
    child->parent = n;
    child->scale = n->scale;
    parseMenu(child);
    RECT rc;
    GetWindowRect(n->hwnd, &rc);
    const int x = rc.right - (int)(3 * n->scale + 0.5);
    const int y = rc.top + n->rects[index].top - (int)(3 * n->scale + 0.5);
    createNodeWindow(child, x, y);
    if (!child->hwnd) {
        delete child;
        return;
    }
    n->children.push_back(child);
    n->openRow = index;
    g_active = child;
}

void choose(Node* n, int index) {
    if (index < 0 || index >= (int)n->ids.size() || n->seps[index] || !n->enabled[index]) return;
    if (n->subs[index]) {
        n->hover = index;
        InvalidateRect(n->hwnd, nullptr, FALSE);
        openChild(n, index, rowEdge(n, index));
        return;
    }
    Node* root = n;
    while (root->parent) root = root->parent;
    root->chosen = (int)n->ids[index];
    diagLog(L"choose idx=%d id=0x%X", index, root->chosen);
    closeAll(root);
}

void closeChild(Node* n) {
    if (!n->parent) return;
    destroyChildren(n->parent);  // also hands the keyboard back to the parent
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Node* n = (Node*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paintNode(hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (n) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            // Coming back out of the submenu, the pointer is on its way to
            // wherever it is now - which is never toward the submenu.
            const bool fromSubmenu = n->childHadMouse;
            n->childHadMouse = false;
            if (n->parent) n->parent->childHadMouse = true;
            int index = hitItem(n, p);
            if (index >= 0 && n->seps[index]) index = -1;
            const bool hasSub = index >= 0 && index < (int)n->subs.size() && n->subs[index];
            diagLog(L"hmmenu move %d,%d idx=%d hover=%d sub=%d", p.x, p.y, index, n->hover,
                    (int)hasSub);
            if (index == n->hover) {
                n->leave = p;  // still on the row: this is where it would leave it
            } else {
                if (!fromSubmenu && onTheWayToSubmenu(n, p)) return 0;
                const int previous = n->hover;
                n->hover = index;
                // Highlight first and on screen before any submenu work, so a
                // slow shell extension can never hold the highlight back.
                repaintRow(hwnd, n, previous);
                repaintRow(hwnd, n, index);
                UpdateWindow(hwnd);
                if (hasSub)
                    openChild(n, index, p);
                else
                    destroyChildren(n);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (n) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            diagLog(L"hmmenu down %d,%d idx=%d child=%d", p.x, p.y, hitItem(n, p),
                    (int)(n->parent != nullptr));
            if (hitItem(n, p) < 0) closeAll(n);
        }
        return 0;
    case WM_LBUTTONUP:
        if (n) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            diagLog(L"hmmenu up %d,%d idx=%d root=%d", p.x, p.y, hitItem(n, p),
                    (int)(n->parent != nullptr));
            choose(n, hitItem(n, p));
        }
        return 0;
    case WM_KEYDOWN:
        if (n) {
            Node* target = g_active ? g_active : n;
            const int count = (int)target->ids.size();
            if (wp == VK_ESCAPE) {
                if (target->parent) {
                    closeChild(target);
                    InvalidateRect(g_active->hwnd, nullptr, FALSE);
                } else {
                    closeAll(target);
                }
            } else if (wp == VK_RETURN) {
                if (target->hover >= 0) choose(target, target->hover);
            } else if (wp == VK_DOWN || wp == VK_UP) {
                const int dir = wp == VK_DOWN ? 1 : -1;
                int index = target->hover;
                for (int k = 0; k < count; ++k) {
                    index = (index + dir + count) % count;
                    if (!target->seps[index] && target->enabled[index]) break;
                }
                target->hover = index;
                InvalidateRect(target->hwnd, nullptr, FALSE);
                if (index >= 0 && target->subs[index])
                    openChild(target, index, rowEdge(target, index));
                else
                    destroyChildren(target);
            } else if (wp == VK_RIGHT) {
                if (target->hover >= 0 && target->subs[target->hover])
                    openChild(target, target->hover, rowEdge(target, target->hover));
            } else if (wp == VK_LEFT && target->parent) {
                closeChild(target);
                InvalidateRect(g_active->hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_MOUSEACTIVATE:
        // A click in a menu window activates nothing.  The root is the
        // foreground window and a submenu cannot be activated
        // (WS_EX_NOACTIVATE), so letting the system run its activation on a
        // click in a submenu only deactivated the root - which took that for
        // the user leaving the menu and closed the whole menu before the
        // click reached the submenu.  Nothing in a submenu could be clicked
        // with a real mouse (posted messages skip activation, so no test
        // showed it).
        diagLog(L"hmmenu mouseactivate child=%d -> MA_NOACTIVATE",
                n ? (int)(n->parent != nullptr) : -1);
        return MA_NOACTIVATE;
    case WM_POINTERACTIVATE:
        // The same question, asked for a finger or a pen.
        return PA_NOACTIVATE;
    case WM_ACTIVATE:
        diagLog(L"hmmenu activate %04X child=%d", (unsigned)LOWORD(wp),
                n ? (int)(n->parent != nullptr) : -1);
        // Losing the foreground means the user went elsewhere - unless this
        // menu itself caused it (one of its windows taking it, or a click of
        // ours on it).  A click in a submenu must never read as leaving the
        // menu; then the foreground is simply taken back, so that the menu
        // still closes when the user does go elsewhere.
        if (LOWORD(wp) == WA_INACTIVE && n && !n->parent) {
            if (isMenuWindow((HWND)lp) || clickedOnMenu()) {
                diagLog(L"hmmenu activate lost to its own click: taken back");
                if (!SetForegroundWindow(n->hwnd)) closeAll(n);
            } else {
                closeAll(n);
            }
        }
        return 0;
    case WM_CLOSE:
        diagLog(L"hmmenu close child=%d", n ? (int)(n->parent != nullptr) : -1);
        if (n && !n->parent) closeAll(n);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MyOldComputerHMenu";
    RegisterClassExW(&wc);
    done = true;
}

}  // namespace

bool menuIsOpen() { return g_root != nullptr; }

void menuClose() {
    if (g_root && g_root->hwnd) PostMessageW(g_root->hwnd, WM_CLOSE, 0, 0);
}

int showHMenu(HWND owner, HMENU root, POINT point) {
    if (!root) return 0;
    ensureClass();

    Node* node = new Node();
    node->menu = root;
    node->owner = owner;
    node->scale = dpiScaleOfWindow(owner);
    // Some extensions add items to the root itself on initialisation.
    SendMessageW(owner, WM_INITMENUPOPUP, (WPARAM)root, 0);
    parseMenu(node);
    createNodeWindow(node, point.x, point.y);
    if (!node->hwnd) {
        delete node;
        return 0;
    }

    g_active = node;
    g_root = node;
    g_finished = false;
    SetForegroundWindow(node->hwnd);
    SetFocus(node->hwnd);

    MSG msg;
    while (!g_finished) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            // WM_QUIT must survive the menu: put it back for the outer loop.
            if (r == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    const int chosen = node->chosen;
    g_active = nullptr;
    g_root = nullptr;
    if (node->hwnd) {
        destroyChildren(node);
        DestroyWindow(node->hwnd);
        node->hwnd = nullptr;
    }
    delete node;
    return chosen;
}

