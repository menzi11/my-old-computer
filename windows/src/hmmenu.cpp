#include "hmmenu.h"

#include <algorithm>
#include <string>
#include <vector>

#include "canvas.h"
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
    int hover = -1;
    int chosen = 0;
    double scale = 1.0;
};

Node* g_active = nullptr;
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
    // "打开(&O)" -> "打开": Win98 menus did not show accelerators.
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
    n->hover = -1;
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STATE;
        GetMenuItemInfoW(n->menu, i, TRUE, &info);
        n->seps[i] = (info.fType & MFT_SEPARATOR) != 0;
        n->subs[i] = info.hSubMenu != nullptr;
        n->ids[i] = info.wID;
        n->checked[i] = (info.fState & MFS_CHECKED) != 0;
        n->enabled[i] = (info.fState & MFS_DISABLED) == 0;
        n->isDefault[i] = (info.fState & MFS_DEFAULT) != 0;
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
        if (n->checked[i]) {
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

int hitItem(Node* n, POINT p) {
    for (size_t i = 0; i < n->rects.size(); ++i)
        if (p.y >= n->rects[i].top && p.y < n->rects[i].bottom) return (int)i;
    return -1;
}

void closeAll(Node* root) {
    g_active = nullptr;
    destroyChildren(root);
    if (root->hwnd) {
        DestroyWindow(root->hwnd);
        root->hwnd = nullptr;
    }
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

void openChild(Node* n, int index) {
    if (index < 0 || index >= (int)n->subs.size() || !n->subs[index] || !n->enabled[index]) return;
    if (!n->children.empty() && n->children.size() == 1 && n->hover == index) return;
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
    g_active = child;
}

void choose(Node* n, int index) {
    if (index < 0 || index >= (int)n->ids.size() || n->seps[index] || !n->enabled[index]) return;
    if (n->subs[index]) {
        n->hover = index;
        InvalidateRect(n->hwnd, nullptr, FALSE);
        openChild(n, index);
        return;
    }
    Node* root = n;
    while (root->parent) root = root->parent;
    root->chosen = (int)n->ids[index];
    closeAll(root);
}

void closeChild(Node* n) {
    if (!n->parent) return;
    Node* parent = n->parent;
    destroyChildren(parent);
    g_active = parent;
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
            int index = hitItem(n, p);
            if (index >= 0 && n->seps[index]) index = -1;
            if (index != n->hover) {
                n->hover = index;
                InvalidateRect(hwnd, nullptr, FALSE);
                if (index >= 0 && n->subs[index]) openChild(n, index);
                else destroyChildren(n);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (n) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            if (hitItem(n, p) < 0) closeAll(n);
        }
        return 0;
    case WM_LBUTTONUP:
        if (n) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
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
                if (index >= 0 && target->subs[index]) openChild(target, index);
                else destroyChildren(target);
            } else if (wp == VK_RIGHT) {
                if (target->hover >= 0 && target->subs[target->hover])
                    openChild(target, target->hover);
            } else if (wp == VK_LEFT && target->parent) {
                closeChild(target);
                InvalidateRect(g_active->hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE && n && !n->parent) closeAll(n);
        return 0;
    case WM_CLOSE:
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

int showHMenu(HWND owner, HMENU root, POINT point) {
    if (!root) return 0;
    ensureClass();

    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static auto getDpi = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                           "GetDpiForWindow");
    UINT dpi = 96;
    if (getDpi && owner) dpi = getDpi(owner);

    Node* node = new Node();
    node->menu = root;
    node->owner = owner;
    node->scale = dpi / 96.0;
    // Some extensions add items to the root itself on initialisation.
    SendMessageW(owner, WM_INITMENUPOPUP, (WPARAM)root, 0);
    parseMenu(node);
    createNodeWindow(node, point.x, point.y);
    if (!node->hwnd) {
        delete node;
        return 0;
    }

    g_active = node;
    g_finished = false;
    SetForegroundWindow(node->hwnd);
    SetFocus(node->hwnd);

    MSG msg;
    while (!g_finished && GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    const int chosen = node->chosen;
    g_active = nullptr;
    if (node->hwnd) {
        destroyChildren(node);
        DestroyWindow(node->hwnd);
        node->hwnd = nullptr;
    }
    delete node;
    return chosen;
}
