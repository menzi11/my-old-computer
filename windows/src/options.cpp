#include "dialog.h"

#include <windowsx.h>

#include <algorithm>
#include <string>
#include <vector>

#include "canvas.h"
#include "settings.h"
#include "theme.h"

namespace {

struct Row {
    std::wstring label;
    unsigned bits = 0;
    int shellIndex = -1;
    bool header = false;
    int column = 0;
    RECT rect{};
};

struct OwnOption {
    const wchar_t* label;
    unsigned bits;
    bool header;
};

const OwnOption kOwn[] = {
    {L"文件和文件夹右键菜单", 0, true},
    {L"使用 Windows 原生菜单（含所有扩展）", kMenuShell, false},
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

struct State {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    double scale = 1.0;
    unsigned mask = kMenuDefaults;
    std::vector<std::wstring> shellItems;
    std::vector<bool> shellVisible;
    std::vector<Row> rows;
    int leftW = 260;
    int rightW = 260;
    bool done = false;
    bool accepted = false;
    int hover = -1;
    int press = -1;
    RECT okRect{}, cancelRect{}, closeRect{};
};

State* g_state = nullptr;

int rowHeight(bool header, double scale) {
    return (int)((header ? 22 : 20) * scale + 0.5);
}

void buildRows(State& st) {
    st.rows.clear();
    for (const auto& own : kOwn) {
        Row row;
        row.label = own.label;
        row.bits = own.bits;
        row.header = own.header;
        row.column = 0;
        st.rows.push_back(row);
    }
    Row header;
    header.label = L"系统菜单项（来自注册表）";
    header.header = true;
    header.column = 1;
    st.rows.push_back(header);
    for (size_t i = 0; i < st.shellItems.size(); ++i) {
        Row row;
        row.label = st.shellItems[i];
        row.shellIndex = (int)i;
        row.column = 1;
        st.rows.push_back(row);
    }
}

void layout(State& st) {
    RECT rc;
    GetClientRect(st.hwnd, &rc);
    const int margin = (int)(16 * st.scale + 0.5);
    const int gap = (int)(28 * st.scale + 0.5);
    const int left = margin;
    const int right = rc.right - margin;
    int leftWidth = st.leftW;
    int rightX = left + leftWidth + gap;
    if (st.shellItems.empty()) {
        leftWidth = right - left;
        rightX = right;
    }

    int y0 = (int)(3 * st.scale + 0.5) + (int)(22 * st.scale + 0.5) + (int)(10 * st.scale + 0.5) +
             (int)(18 * st.scale + 0.5);
    int yLeft = y0;
    int yRight = y0;
    for (auto& row : st.rows) {
        const int h = rowHeight(row.header, st.scale);
        if (row.column == 0) {
            row.rect = RECT{left, yLeft, left + leftWidth, yLeft + h};
            yLeft += h;
        } else {
            row.rect = RECT{rightX, yRight, right, yRight + h};
            yRight += h;
        }
    }

    const int bh = (int)(23 * st.scale + 0.5);
    const int bw = (int)(75 * st.scale + 0.5);
    const int buttonGap = (int)(6 * st.scale + 0.5);
    const int by = rc.bottom - (int)(10 * st.scale + 0.5) - bh;
    const int cancelX = rc.right - (int)(10 * st.scale + 0.5) - bw;
    st.cancelRect = RECT{cancelX, by, cancelX + bw, by + bh};
    st.okRect = RECT{cancelX - bw - buttonGap, by, cancelX - buttonGap, by + bh};
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

bool rowChecked(const State& st, const Row& row) {
    if (row.shellIndex >= 0)
        return row.shellIndex < (int)st.shellVisible.size() && st.shellVisible[row.shellIndex];
    return (st.mask & row.bits) == row.bits;
}

void drawDialog(Canvas& c, HWND hwnd, State& st) {
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
    c.text(L"勾选要显示的项目；系统项来自注册表，可逐项隐藏",
           bar.left + c.px(6), bar.bottom + c.px(3), Theme::dim);

    RECT close = st.closeRect;
    c.fill(close, Theme::face);
    c.bevelRaised(close);
    c.line(close.left + c.px(4), close.top + c.px(4), close.right - c.px(4), close.bottom - c.px(4),
           Theme::dark);
    c.line(close.right - c.px(4), close.top + c.px(4), close.left + c.px(4), close.bottom - c.px(4),
           Theme::dark);

    for (size_t i = 0; i < st.rows.size(); ++i) {
        const Row& row = st.rows[i];
        RECT r = row.rect;
        if (row.header) {
            c.textClip(row.label, RECT{r.left, r.top, r.right, r.top + c.px(22)}, Theme::text,
                       Align::Left, true);
            c.hline(r.left, r.top + c.px(22), r.right - r.left, Theme::shadow);
            continue;
        }
        if (st.hover == (int)i) c.fill(r, Theme::faceHi);
        drawCheck(c, r.left, r.top + c.px(5), rowChecked(st, row), st.scale);
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
    State* st = (State*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    Canvas c(dc, st->scale);
    drawDialog(c, hwnd, *st);
    EndPaint(hwnd, &ps);
}

void finish(State& st, bool accepted) {
    st.accepted = accepted;
    st.done = true;
    DestroyWindow(st.hwnd);
}

int rowAt(State& st, POINT p) {
    for (size_t i = 0; i < st.rows.size(); ++i) {
        if (st.rows[i].header) continue;
        if (PtInRect(&st.rows[i].rect, p)) return (int)i;
    }
    return -1;
}

void toggle(State& st, int index) {
    Row& row = st.rows[index];
    if (row.shellIndex >= 0) {
        if (row.shellIndex < (int)st.shellVisible.size())
            st.shellVisible[row.shellIndex] = !st.shellVisible[row.shellIndex];
        return;
    }
    if ((st.mask & row.bits) == row.bits) st.mask &= ~row.bits;
    else st.mask |= row.bits;
}

LRESULT CALLBACK proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    State* st = (State*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
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

bool showMenuOptions(HWND owner, unsigned* mask, const std::vector<std::wstring>& shellItems,
                     std::vector<std::wstring>& hidden) {
    ensureClass();

    const double scale = dpiScaleOfWindow(owner);

    State st;
    st.owner = owner;
    st.scale = scale;
    st.mask = mask ? *mask : kMenuDefaults;
    st.shellItems = shellItems;
    st.shellVisible.assign(shellItems.size(), true);
    for (size_t i = 0; i < shellItems.size(); ++i)
        for (auto& h : hidden)
            if (_wcsicmp(h.c_str(), shellItems[i].c_str()) == 0) st.shellVisible[i] = false;
    buildRows(st);

    HDC screen = GetDC(nullptr);
    HDC measureDc = CreateCompatibleDC(screen);
    int ownW = 0, shellW = 0;
    {
        Canvas c(measureDc, scale);
        for (const auto& own : kOwn) ownW = std::max(ownW, c.textWidth(own.label) + (int)(30 * scale));
        for (const auto& item : st.shellItems)
            shellW = std::max(shellW, c.textWidth(item) + (int)(30 * scale));
    }
    DeleteDC(measureDc);
    ReleaseDC(nullptr, screen);

    st.leftW = std::max((int)(300 * scale), ownW);
    st.rightW = std::max((int)(240 * scale), shellW);
    int w = (int)(2 * 16 * scale) + st.leftW + (int)(28 * scale) +
            (st.shellItems.empty() ? 0 : st.rightW);
    int h = (int)(3 * scale) + (int)(22 * scale) + (int)(10 * scale) + (int)(18 * scale);
    int ownRows = 0, shellRows = st.shellItems.empty() ? 0 : (int)(22 * scale + 0.5);
    for (const auto& own : kOwn) ownRows += rowHeight(own.header, scale);
    for (const auto& item : st.shellItems) {
        (void)item;
        shellRows += rowHeight(false, scale);
    }
    h += std::max(ownRows, shellRows);
    h += (int)(10 * scale) + (int)(23 * scale) + (int)(10 * scale);

    RECT ownerRect{};
    if (owner) GetWindowRect(owner, &ownerRect);
    int x = owner ? ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2 : 120;
    int y = owner ? ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2 : 120;
    MONITORINFO mi{sizeof(mi)};
    if (GetMonitorInfoW(MonitorFromWindow(owner ? owner : GetDesktopWindow(),
                                          MONITOR_DEFAULTTONEAREST),
                        &mi)) {
        if (h > mi.rcWork.bottom - mi.rcWork.top) h = mi.rcWork.bottom - mi.rcWork.top;
        if (w > mi.rcWork.right - mi.rcWork.left) w = mi.rcWork.right - mi.rcWork.left;
        x = std::clamp(x, (int)mi.rcWork.left, (int)mi.rcWork.right - w);
        y = std::clamp(y, (int)mi.rcWork.top, (int)mi.rcWork.bottom - h);
    }

    st.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MyOldComputerMenuOptions", L"右键菜单设置",
                              WS_POPUP, x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr),
                              nullptr);
    if (!st.hwnd) return false;
    SetWindowLongPtrW(st.hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    layout(st);

    HWND prevFocus = GetFocus();
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(st.hwnd, SW_SHOW);
    SetForegroundWindow(st.hwnd);
    SetFocus(st.hwnd);

    MSG msg;
    while (!st.done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            // WM_QUIT must survive the dialog: put it back for the outer loop.
            if (r == 0) PostQuitMessage((int)msg.wParam);
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (owner) EnableWindow(owner, TRUE);
    if (prevFocus) SetFocus(prevFocus);
    else if (owner) SetForegroundWindow(owner);
    if (st.accepted) {
        if (mask) *mask = st.mask;
        hidden.clear();
        for (size_t i = 0; i < st.shellItems.size(); ++i)
            if (i < st.shellVisible.size() && !st.shellVisible[i])
                hidden.push_back(st.shellItems[i]);
    }
    return st.accepted;
}
