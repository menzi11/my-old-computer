#include "dialog.h"

#include <windowsx.h>

#include <algorithm>
#include <cstdio>
#include <string>

#include "canvas.h"
#include "debug.h"
#include "icons.h"
#include "theme.h"
#include "version.h"

namespace {

struct DialogState {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    HWND edit = nullptr;
    HFONT font = nullptr;
    std::wstring title;
    std::wstring label;
    std::wstring text;
    bool prompt = false;
    bool done = false;
    bool accepted = false;
    double scale = 1.0;
    int hover = 0;  // 1 = OK, 2 = Cancel
    int press = 0;
    std::wstring input;
    RECT okRect{}, cancelRect{}, closeRect{};
};

DialogState* g_dlg = nullptr;
WNDPROC g_editProc = nullptr;

void layoutDialog(DialogState& st) {
    RECT rc;
    GetClientRect(st.hwnd, &rc);
    int b = (int)(10 * st.scale + 0.5);
    int bw = (int)(75 * st.scale + 0.5);
    int bh = (int)(23 * st.scale + 0.5);
    int y = rc.bottom - b - bh;
    int gap = (int)(6 * st.scale + 0.5);
    // Windows order: 确定 on the left, 取消 on the right.
    int cancelX = rc.right - b - bw;
    st.cancelRect = RECT{cancelX, y, cancelX + bw, y + bh};
    st.okRect = RECT{cancelX - bw - gap, y, cancelX - gap, y + bh};
    st.closeRect = RECT{rc.right - (int)(20 * st.scale + 0.5), (int)(4 * st.scale + 0.5),
                        rc.right - (int)(4 * st.scale + 0.5), (int)(20 * st.scale + 0.5)};
}

void drawDialog(Canvas& c, HWND hwnd, DialogState& st) {
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
    c.text(st.title, bar.left + c.px(4), bar.top + c.px(3), Theme::white, true);

    RECT close = st.closeRect;
    c.fill(close, Theme::face);
    c.bevelRaised(close);
    c.line(close.left + c.px(4), close.top + c.px(4), close.right - c.px(4), close.bottom - c.px(4),
           Theme::dark);
    c.line(close.right - c.px(4), close.top + c.px(4), close.left + c.px(4), close.bottom - c.px(4),
           Theme::dark);

    if (st.prompt) {
        c.text(st.label, c.px(14), bar.bottom + c.px(14), Theme::text);
        RECT field{c.px(14), bar.bottom + c.px(36), rc.right - c.px(14), bar.bottom + c.px(58)};
        c.fill(field, Theme::white);
        c.bevelField(field);
    } else {
        RECT body{c.px(16), bar.bottom + c.px(14), rc.right - c.px(16), st.okRect.top - c.px(8)};
        c.textIn(st.text, body, Theme::text, Align::Left, false, true);
    }

    for (int i = 1; i <= (st.prompt ? 2 : 1); ++i) {
        RECT r = (i == 1) ? st.okRect : st.cancelRect;
        c.fill(r, Theme::face);
        if (st.press == i) c.bevelSunken(r);
        else c.bevelRaised(r);
        c.textClip(i == 1 ? L"确定" : L"取消", r, Theme::text, Align::Center);
    }
}

void paintDialog(HWND hwnd) {
    DialogState* st = (DialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!st) return;
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    Canvas c(dc, st->scale);
    drawDialog(c, hwnd, *st);
    EndPaint(hwnd, &ps);
}

// What is in the prompt's edit box.  It has to be read while the box still
// exists: the box is a child of the dialog window and goes with it.
std::wstring editText(const DialogState& st) {
    wchar_t buf[32768]{};
    if (st.edit && IsWindow(st.edit)) GetWindowTextW(st.edit, buf, 32768);
    return buf;
}

void finishDialog(DialogState& st, bool accepted) {
    st.accepted = accepted;
    // Before the window (and with it the edit box) goes.  Reading it
    // afterwards gave an empty string: every prompt came back with nothing.
    st.input = editText(st);
    st.done = true;
    DestroyWindow(st.hwnd);
}

LRESULT CALLBACK dialogEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            if (g_dlg) finishDialog(*g_dlg, true);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            if (g_dlg) finishDialog(*g_dlg, false);
            return 0;
        }
    }
    return CallWindowProcW(g_editProc, hwnd, msg, wp, lp);
}

LRESULT CALLBACK dialogProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DialogState* st = (DialogState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        paintDialog(hwnd);
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
            int hover = 0;
            if (PtInRect(&st->okRect, p)) hover = 1;
            else if (st->prompt && PtInRect(&st->cancelRect, p)) hover = 2;
            if (hover != st->hover) {
                st->hover = hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
        }
        return 0;
    case WM_LBUTTONDOWN:
        if (st) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            if (PtInRect(&st->okRect, p)) st->press = 1;
            else if (st->prompt && PtInRect(&st->cancelRect, p)) st->press = 2;
            else if (PtInRect(&st->closeRect, p)) finishDialog(*st, false);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (st) {
            POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
            int press = st->press;
            st->press = 0;
            if (press == 1 && PtInRect(&st->okRect, p)) finishDialog(*st, true);
            else if (press == 2 && PtInRect(&st->cancelRect, p)) finishDialog(*st, false);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT && st) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (p.y < (int)(26 * st->scale)) return HTCAPTION;
        }
        return hit;
    }
    case WM_CLOSE:
        if (st) finishDialog(*st, false);
        return 0;
    case WM_DESTROY:
        if (st) st->hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureDialogClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = dialogProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MyOldComputerDialog";
    if (!RegisterClassExW(&wc)) {
        wchar_t path[MAX_PATH]{};
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"moc-diag.txt");
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"a, ccs=UTF-8") == 0 && f) {
            fwprintf(f, L"RegisterClass failed %lu\n", GetLastError());
            fclose(f);
        }
    }
    done = true;
}

bool runDialog(HWND owner, const std::wstring& title, const std::wstring& label,
               const std::wstring& text, bool prompt, std::wstring* io) {
    ensureDialogClass();

    const double scale = dpiScaleOfWindow(owner);
    HDC screen = GetDC(nullptr);
    HDC measureDc = CreateCompatibleDC(screen);
    int textW = 0;
    {
        Canvas c(measureDc, scale);
        textW = prompt ? std::max(c.textWidth(label), c.textWidth(*io)) : c.textWidth(text);
    }
    DeleteDC(measureDc);
    ReleaseDC(nullptr, screen);

    int w = std::max((int)(320 * scale), textW + (int)(48 * scale));
    int h = prompt ? (int)(150 * scale) : (int)(170 * scale);
    RECT ownerRect{};
    if (owner) GetWindowRect(owner, &ownerRect);
    int x = owner ? ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2 : 100;
    int y = owner ? ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2 : 100;

    DialogState st;
    st.owner = owner;
    st.title = title;
    st.label = label;
    st.text = text;
    st.prompt = prompt;
    st.scale = scale;
    st.input = io ? *io : L"";

    st.hwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MyOldComputerDialog", title.c_str(), WS_POPUP,
                              x, y, w, h, owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!st.hwnd) {
        wchar_t path[MAX_PATH]{};
        GetTempPathW(MAX_PATH, path);
        wcscat_s(path, L"moc-diag.txt");
        FILE* f = nullptr;
        if (_wfopen_s(&f, path, L"a, ccs=UTF-8") == 0 && f) {
            fwprintf(f, L"CreateWindow dialog failed %lu\n", GetLastError());
            fclose(f);
        }
        return false;
    }
    SetWindowLongPtrW(st.hwnd, GWLP_USERDATA, (LONG_PTR)&st);
    layoutDialog(st);

    if (prompt) {
        st.edit = CreateWindowExW(0, L"EDIT", st.input.c_str(), WS_CHILD | WS_VISIBLE |
                                      ES_AUTOHSCROLL,
                                  (int)(14 * scale + 0.5), (int)(3 * scale + 0.5) + (int)(22 * scale) + (int)(38 * scale + 0.5),
                                  w - (int)(28 * scale + 0.5), (int)(18 * scale + 0.5), st.hwnd,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
        st.font = CreateFontW(-(int)(14.6667 * scale + 0.5), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        SendMessageW(st.edit, WM_SETFONT, (WPARAM)st.font, TRUE);
        SendMessageW(st.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
        g_editProc = (WNDPROC)SetWindowLongPtrW(st.edit, GWLP_WNDPROC, (LONG_PTR)dialogEditProc);
        g_dlg = &st;
    }

    if (owner) EnableWindow(owner, FALSE);
    // An instance that stays in the background (a test's) takes no foreground
    // for its dialogs - not when its edit box is given the focus either: a
    // window that cannot be activated.  Whoever is at the machine keeps it.
    const bool background = staysInBackground();
    if (background) SetWindowLongPtrW(st.hwnd, GWL_EXSTYLE, WS_EX_DLGMODALFRAME | WS_EX_NOACTIVATE);
    ShowWindow(st.hwnd, background ? SW_SHOWNOACTIVATE : SW_SHOW);
    if (!background) SetForegroundWindow(st.hwnd);
    if (prompt) {
        SetFocus(st.edit);
        SendMessageW(st.edit, EM_SETSEL, 0, -1);
    } else {
        SetFocus(st.hwnd);
    }

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
    if (!background) SetForegroundWindow(owner);
    if (prompt && st.edit) {
        // The dialog was not finished (the loop ended on WM_QUIT): the edit
        // box is still there and still has to be read and removed.
        if (IsWindow(st.edit)) {
            st.input = editText(st);
            SetWindowLongPtrW(st.edit, GWLP_WNDPROC, (LONG_PTR)g_editProc);
            DestroyWindow(st.edit);
        }
        st.edit = nullptr;
        g_dlg = nullptr;
    }
    if (st.font) DeleteObject(st.font);
    if (io) *io = st.input;
    return st.accepted;
}

}  // namespace

bool promptText(HWND owner, const std::wstring& title, const std::wstring& label,
                const std::wstring& initial, std::wstring* result) {
    std::wstring value = initial;
    bool ok = runDialog(owner, title, label, L"", true, &value);
    if (ok && result) *result = value;
    return ok;
}

void showAbout(HWND owner) {
    runDialog(owner, L"关于 My Old Computer", L"",
              L"My Old Computer " MOC_VERSION_DISPLAY L"\n\n"
              L"Windows 98 风格的文件管理器。\n"
              L"原生 Win32 / C++，无第三方依赖，界面全部由代码绘制。\n\n"
              L"macOS 版：Swift / AppKit。\n"
              L"MIT License。",
              false, nullptr);
}

void showError(HWND owner, const std::wstring& title, const std::wstring& text) {
    runDialog(owner, title, L"", text, false, nullptr);
}
