#include "dialog.h"

#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "canvas.h"
#include "settings.h"
#include "theme.h"

namespace {

struct OptionRow {
    const wchar_t* label;
    unsigned bits;
    bool header;
};

const OptionRow kRows[] = {
    {L"文件和文件夹右键菜单", 0, true},
    {L"使用 Windows 原生菜单（含 7-Zip、Git 等所有扩展）", kMenuShell, false},
    {L"快速查看", kMenuItemPreview, false},
    {L"在新窗口中打开", kMenuItemNewWindow, false},
    {L"在资源管理器中显示", kMenuItemReveal, false},
    {L"复制路径", kMenuItemCopyPath, false},
    {L"在命令提示符中打开", kMenuItemTerminal, false},
    {L"重命名（由本程序处理）", kMenuItemRename, false},
    {L"空白处右键菜单", 0, true},
    {L"查看方式 / 排序方式", kMenuBgView | kMenuBgSort, false},
    {L"刷新", kMenuBgRefresh, false},
    {L"新建（文件夹 / 文本文档）", kMenuBgNew, false},
    {L"在命令提示符中打开", kMenuBgTerminal, false},
    {L"显示隐藏文件", kMenuBgHidden, false},
    {L"设置…", kMenuBgSettings, false},
};

constexpr int kRowCount = (int)(sizeof(kRows) / sizeof(kRows[0]));

struct OptionsState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    double scale = 1.0;
    unsigned mask = kMenuDefaults;
    bool done = false;
    bool accepted = false;
    int hover = -1;
    int press = -1;
    RECT rows[kRowCount]{};
    RECT okRect{}, cancelRect{}, closeRect{};
};

OptionsState* g_options = nullptr;

int rowHeight(bool header, double scale) {
    return (int)((header ? 26 : 24) * scale + 0.5);
}

void layout(OptionsState& st) {
    RECT rc;
    GetClientRect(st.hwnd, &rc);
    const int left = (int)(16 * st.scale + 0.5);
    const int right = rc.right - (int)(16 * st.scale + 0.5);
    int y = (int)(3 * st.scale + 0.5) + (int)(22 * st.scale + 0.5) + (int)(10 * st.scale + 0.5) +
            (int)(18 * st.scale + 0.5);
    for (int i = 0; i < kRowCount; ++i) {
        const int h = rowHeight(kRows[i].header, st.scale);
        st.rows[i] = RECT{left, y, right, y + h};
        y += h;
    }
    const int bh = (int)(23 * st.scale + 0.5);
    const int bw = (int)(75 * st.scale + 0.5);
    const int gap = (int)(6 * st.scale + 0.5);
    const int by = rc.bottom - (int)(10 * st.scale + 0.5) - bh;
    const int cancelX = rc.right - (int)(10 * st.scale + 0.5) - bw;
    st.cancelRect = RECT{cancelX, by, cancelX + bw, by + bh};
    st.okRect = RECT{cancelX - bw - gap, by, cancelX - gap, by + bh};
    st.closeRect = RECT{rc.right - (int)(20 * st.scale + 0.5), (int)(4 * st.scale + 0.5),
                        rc.right - (int)(4 * st.scale + 0.5), (int)(20 * st.scale + 0.5)};
}

void drawCheck(Canvas& c, int x, int y, bool checked, double scale) {
    const int box = (int)(13 * scale + 0.5);
    RECT r{x, y, x + box, y + box};
    c.fill(r, Theme::white);
    c.bevelSunken(r);
    if (!checked) return;
    c.line(x + c.px(3), y + c.px(7), x + c.px(6), y + c.px(10), Theme::dark);
    c.line(x + c.px(6), y + c.px(10), x + c.px(11), y + c.px(3), Theme::dark);
}

void drawDialog(Canvas& c, HWND hwnd, OptionsState& st) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    c.fill(rc, Theme::face);
    c.frame(rc, Theme::dark);
    c.bevelRaised(RECT{1, 1, rc.right - 1, rc.bottom - 1});

    const int titleH = c.px(22);
    RECT bar{c.px(3), c.px(3), rc.right - c.px(3), c.px(3) + titleH};
    TRIVERTEX v[2] = {
        {bar.left, bar.top, 0x0000, 0x0000, 0x8000, 0},
        {bar.right, bar.bottom, 0x1000, 0x8400, 0xD000, 0},
    };
    GRADIENT_RECT gr{0, 1};
    GradientFill(c.dc(), v, 2, &gr, 1, GRADIENT_FILL_RECT_H);
    c.text(L"右键菜单设置", bar.left + c.px(4), bar.top + c.px(3), Theme::white, true);
    c.text(L"勾选要显示的项目；7-Zip、Git 等 shell 扩展由 Windows 控制",
           bar.left + c.px(6), bar.bottom + c.px(3), Theme::dim);

    RECT close = st.closeRect;
    c.fill(close, Theme::face);
    c.bevelRaised(close);
    c.line(close.left + c.px(4), close.top + c.px(4), close.right - c.px(4), close.bottom - c.px(4),
           Theme::dark);
    c.line(close.right - c.px(4), close.top + c.px(4), close.left + c.px(4), close.bottom - c.px(4),
           Theme::dark);

    for (int i = 0; i < kRowCount; ++i) {
        const OptionRow& row = kRows[i];
        RECT r = st.rows[i];
        if (row.header) {
            c.text(row.label, r.left, r.top + c.px(6), Theme::text, true);
            c.hline(r.left, r.top + c.px(22), r.right - r.left, Theme::shadow);
            continue;
        }
        if (st.hover == i) c.fill(r, Theme::faceHi);
        const bool on = (st.mask & row.bits) == row.bits;
        drawCheck(c, r.left, r.top + c.px(5), on, st.scale);
        c.textClip(row.label, RECT{r.left + c.px(22), r.top, r.right, r.bottom}, Theme::text,
                   Align::Left);
    }

    for (int i = 1; i <= 2; ++i) {
        RECT r = (i == 1) ? st.okRect : st.cancelRect;
        c.fill(r, Theme::face);
        if (st.press == i) c.bevelSunken(r);
        else c.bevelRaised(r);
        c.textClip(i == 1 ? L"确定" : L"取消", r, Theme::text, Align::Center);
    }
}

void paint(HWND hwnd) {
    OptionsState* st = (OptionsState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    Canvas c(dc, st->scale);
    drawDialog(c, hwnd, *st);
    EndPaint(hwnd, &ps);
}

void finish(OptionsState& st, bool accepted) {
    st.accepted = accepted;
    st.done = true;
    DestroyWindow(st.hwnd);
}

void toggle(OptionsState& st, int row) {
    const unsigned bits = kRows[row].bits;
    if ((st.mask & bits) == bits) st.mask &= ~bits;
    else st.mask |= bits;
}

int rowAt(OptionsState& st, POINT p) {
    for (int i = 0; i < kRowCount; ++i) {
        if (kRows[i].header) continue;
        if (PtInRect(&st.rows[i], p)) return i;
    }
    return -1;
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    OptionsState* st = (OptionsState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paint(hwnd);
        return 0;
    case WM_PRINTCLIENT:
        if (st) {
            Canvas c((HDC)wp, st->scale);
            drawDialog(c, hwnd, *st);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (st) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            int hover = rowAt(*st, p);
            if (hover != st->hover) {
                st->hover = hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (st) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            int row = rowAt(*st, p);
            if (row >= 0) {
                toggle(*st, row);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (PtInRect(&st->okRect, p)) st->press = 1;
            else if (PtInRect(&st->cancelRect, p)) st->press = 2;
            else if (PtInRect(&st->closeRect, p)) finish(*st, false);
        }
        return 0;
    case WM_LBUTTONUP:
        if (st) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            int press = st->press;
            st->press = 0;
            if (press == 1 && PtInRect(&st->okRect, p)) finish(*st, true);
            else if (press == 2 && PtInRect(&st->cancelRect, p)) finish(*st, false);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_KEYDOWN:
        if (st) {
            if (wp == VK_ESCAPE) finish(*st, false);
            else if (wp == VK_RETURN) finish(*st, true);
            else if (wp == VK_SPACE && st->hover >= 0) {
                toggle(*st, st->hover);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT && st) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            if (p.y < (int)(26 * st->scale)) return HTCAPTION;
        }
        return hit;
    }
    case WM_CLOSE:
        if (st) finish(*st, false);
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
    wc.lpfnWndProc = proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MyOldComputerMenuOptions";
    RegisterClassExW(&wc);
    done = true;
}

}  // namespace

bool showMenuOptions(HWND owner, unsigned* mask) {
    ensureClass();

    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static auto getDpi = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                           "GetDpiForWindow");
    UINT dpi = 96;
    if (getDpi && owner) dpi = getDpi(owner);
    const double scale = dpi / 96.0;

    HDC screen = GetDC(nullptr);
    HDC measureDc = CreateCompatibleDC(screen);
    int textW = 0;
    {
        Canvas c(measureDc, scale);
        for (const auto& row : kRows) textW = std::max(textW, c.textWidth(row.label));
    }
    DeleteDC(measureDc);
    ReleaseDC(nullptr, screen);

    int w = std::max((int)(480 * scale), textW + (int)(110 * scale));
    int h = (int)(3 * scale) + (int)(22 * scale) + (int)(10 * scale);
    for (const auto& row : kRows) h += rowHeight(row.header, scale);
    h += (int)(10 * scale) + (int)(23 * scale) + (int)(10 * scale);

    RECT ownerRect{};
    if (owner) GetWindowRect(owner, &ownerRect);
    const int x = owner ? ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2 : 120;
    const int y = owner ? ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2 : 120;

    OptionsState st;
    st.owner = owner;
    st.scale = scale;
    st.mask = mask ? *mask : kMenuDefaults;
    st.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MyOldComputerMenuOptions",
                              L"右键菜单设置", WS_POPUP, x, y, w, h, owner, nullptr,
                              GetModuleHandleW(nullptr), nullptr);
    if (!st.hwnd) return false;
    SetWindowLongPtrW(st.hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    layout(st);

    HWND prevFocus = GetFocus();
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
    SetFocus(st.hwnd);

    MSG msg;
    while (!st.done && GetMessageW(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_QUIT) break;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (owner) EnableWindow(owner, TRUE);
    if (prevFocus) SetFocus(prevFocus);
    else if (owner) SetForegroundWindow(owner);
    if (st.accepted && mask) *mask = st.mask;
    return st.accepted;
}
