#include "options.h"

#include <windowsx.h>

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

#include "canvas.h"
#include "scrollbar.h"
#include "settings.h"
#include "shellmenu.h"
#include "theme.h"

namespace {

struct OwnOption {
    const wchar_t* label;
    unsigned bits;
    bool header;
};

const OwnOption kOwn[] = {
    {L"文件和文件夹右键菜单", 0, true},
    {L"使用 Windows 原生菜单（含所有扩展）", kMenuShell, false},
    {L"提前在后台构建菜单（弹出更快）", kMenuAhead, false},
    {L"快速查看", kMenuItemPreview, false},
    {L"在新窗口中打开", kMenuItemNewWindow, false},
    {L"文件夹颜色", kMenuItemFolderColour, false},
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

// One visible line of the sectioned list on the right.
struct ListRow {
    bool header = false;
    int section = -1;
    int item = -1;
    RECT rect{};
};

struct State {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    double scale = 1.0;
    unsigned mask = kMenuDefaults;
    std::vector<MenuSection> sections;
    std::vector<ListRow> rows;
    int contentH = 0;
    int listScroll = 0;
    bool scrollDrag = false;
    int scrollDragY = 0;
    int scrollDragPos = 0;
    HWND addEdit = nullptr;  // "add a file type" input
    HFONT editFont = nullptr;
    int leftW = 300;
    int rightW = 280;
    bool done = false;
    bool accepted = false;
    int hoverLeft = -1;   // own-option row under the mouse
    int hoverList = -1;   // sectioned-list row under the mouse
    int pressBtn = 0;     // 1 OK, 2 cancel, 3 add
    RECT okRect{}, cancelRect{}, closeRect{}, addBtnRect{}, addFieldRect{}, listRect{}, listBarRect{};
    std::vector<RECT> leftRects;
};

State* g_state = nullptr;
WNDPROC g_addOldProc = nullptr;

int rowHeight(bool header, double scale) {
    return (int)((header ? 22 : 20) * scale + 0.5);
}

bool labelHidden(const MenuSection& s, const std::wstring& label) {
    for (auto& h : s.hidden)
        if (_wcsicmp(h.c_str(), label.c_str()) == 0) return true;
    return false;
}

void toggleLabel(MenuSection& s, const std::wstring& label) {
    for (auto it = s.hidden.begin(); it != s.hidden.end(); ++it) {
        if (_wcsicmp(it->c_str(), label.c_str()) == 0) {
            s.hidden.erase(it);
            return;
        }
    }
    s.hidden.push_back(label);
}

void clampScroll(State& st) {
    const int view = st.listRect.bottom - st.listRect.top;
    const int maxV = std::max(0, st.contentH - view);
    if (st.listScroll > maxV) st.listScroll = maxV;
    if (st.listScroll < 0) st.listScroll = 0;
}

// Lays out the sectioned list rows for the current scroll position.
void buildRows(State& st) {
    st.rows.clear();
    int y = 0;
    for (size_t s = 0; s < st.sections.size(); ++s) {
        ListRow head;
        head.header = true;
        head.section = (int)s;
        head.rect = RECT{st.listRect.left, st.listRect.top + y - st.listScroll, st.listRect.right,
                         st.listRect.top + y + rowHeight(true, st.scale) - st.listScroll};
        st.rows.push_back(head);
        y += rowHeight(true, st.scale);
        for (size_t i = 0; i < st.sections[s].items.size(); ++i) {
            ListRow row;
            row.section = (int)s;
            row.item = (int)i;
            row.rect = RECT{st.listRect.left, st.listRect.top + y - st.listScroll,
                            st.listRect.right,
                            st.listRect.top + y + rowHeight(false, st.scale) - st.listScroll};
            st.rows.push_back(row);
            y += rowHeight(false, st.scale);
        }
    }
    st.contentH = y;
    clampScroll(st);
}

// Pure geometry: every rect of the dialog for a given client area, no window
// needed - this is what lets --uitest render the dialog offscreen.
void layoutInto(State& st, const RECT& rc) {
    const int margin = (int)(16 * st.scale + 0.5);
    const int gap = (int)(28 * st.scale + 0.5);
    const int left = margin;
    const int right = rc.right - margin;
    const int rightX = left + st.leftW + gap;

    int y0 = (int)(3 * st.scale + 0.5) + (int)(22 * st.scale + 0.5) + (int)(10 * st.scale + 0.5) +
             (int)(18 * st.scale + 0.5);
    int yLeft = y0;
    st.leftRects.clear();
    for (const auto& own : kOwn) {
        const int h = rowHeight(own.header, st.scale);
        st.leftRects.push_back(RECT{left, yLeft, left + st.leftW, yLeft + h});
        yLeft += h;
    }

    // Right column: header line, the scrolling section list, an input row for
    // adding file types, then the shared button row.
    const int addRowH = (int)(24 * st.scale + 0.5);
    const int bh = (int)(23 * st.scale + 0.5);
    const int by = rc.bottom - (int)(10 * st.scale + 0.5) - bh;
    const int addY = by - (int)(8 * st.scale + 0.5) - addRowH;
    const int listTop = y0 + (int)(20 * st.scale + 0.5);
    const int scrollerW = (int)(16 * st.scale + 0.5);
    st.listRect = RECT{rightX, listTop, right - scrollerW - 2, addY - (int)(6 * st.scale + 0.5)};
    st.listBarRect = RECT{right - scrollerW, listTop, right, st.listRect.bottom};

    const int addLabelW = (int)(56 * st.scale + 0.5);
    const int addBtnW = (int)(56 * st.scale + 0.5);
    const int fieldW = std::min((int)(110 * st.scale + 0.5),
                                right - rightX - addLabelW - addBtnW - (int)(14 * st.scale + 0.5));
    st.addFieldRect = RECT{rightX + addLabelW, addY, rightX + addLabelW + fieldW, addY + addRowH};
    st.addBtnRect = RECT{right - addBtnW, addY, right, addY + addRowH};

    const int bw = (int)(75 * st.scale + 0.5);
    const int buttonGap = (int)(6 * st.scale + 0.5);
    const int cancelX = rc.right - (int)(10 * st.scale + 0.5) - bw;
    st.cancelRect = RECT{cancelX, by, cancelX + bw, by + bh};
    st.okRect = RECT{cancelX - bw - buttonGap, by, cancelX - buttonGap, by + bh};
    st.closeRect = RECT{rc.right - (int)(20 * st.scale + 0.5), (int)(4 * st.scale + 0.5),
                        rc.right - (int)(4 * st.scale + 0.5), (int)(20 * st.scale + 0.5)};

    if (st.addEdit) {
        SetWindowPos(st.addEdit, HWND_TOP, st.addFieldRect.left + 2, st.addFieldRect.top + 2,
                     st.addFieldRect.right - st.addFieldRect.left - 4,
                     st.addFieldRect.bottom - st.addFieldRect.top - 4, SWP_NOACTIVATE);
    }
    buildRows(st);
}

void layout(State& st) {
    RECT rc;
    GetClientRect(st.hwnd, &rc);
    layoutInto(st, rc);
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

bool rowChecked(const State& st, const ListRow& row) {
    const MenuSection& s = st.sections[row.section];
    return !labelHidden(s, s.items[row.item]);
}

void drawDialog(Canvas& c, const RECT& rc, State& st) {
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
    c.text(L"左边是本程序附加的条目；右边按右键场景分组，逐项隐藏系统菜单条目",
           bar.left + c.px(6), bar.bottom + c.px(3), Theme::dim);

    RECT close = st.closeRect;
    c.fill(close, Theme::face);
    c.bevelRaised(close);
    c.line(close.left + c.px(4), close.top + c.px(4), close.right - c.px(4), close.bottom - c.px(4),
           Theme::dark);
    c.line(close.right - c.px(4), close.top + c.px(4), close.left + c.px(4), close.bottom - c.px(4),
           Theme::dark);

    for (size_t i = 0; i < st.leftRects.size(); ++i) {
        const RECT& r = st.leftRects[i];
        if (kOwn[i].header) {
            c.textClip(kOwn[i].label, RECT{r.left, r.top, r.right, r.top + c.px(22)}, Theme::text,
                       Align::Left, true);
            c.hline(r.left, r.top + c.px(22), r.right - r.left, Theme::shadow);
            continue;
        }
        if (st.hoverLeft == (int)i) c.fill(r, Theme::faceHi);
        drawCheck(c, r.left, r.top + c.px(5),
                  (st.mask & kOwn[i].bits) == kOwn[i].bits, st.scale);
        c.textClip(kOwn[i].label, RECT{r.left + c.px(22), r.top, r.right, r.bottom}, Theme::text,
                   Align::Left);
    }

    // Sectioned list of shell items, one group per Explorer context.
    c.text(L"系统菜单项（按右键场景）", st.listRect.left, st.listRect.top - c.px(18), Theme::text,
           true);
    {
        const int saved = SaveDC(c.dc());
        IntersectClipRect(c.dc(), st.listRect.left, st.listRect.top, st.listRect.right,
                          st.listRect.bottom);
        for (size_t i = 0; i < st.rows.size(); ++i) {
            const ListRow& row = st.rows[i];
            if (row.rect.bottom <= st.listRect.top || row.rect.top >= st.listRect.bottom) continue;
            if (row.header) {
                c.fill(RECT{st.listRect.left, row.rect.top, st.listRect.right, row.rect.bottom},
                       Theme::face);
                c.textClip(st.sections[row.section].title,
                           RECT{row.rect.left, row.rect.top, row.rect.right, row.rect.bottom},
                           Theme::text, Align::Left, true);
                c.hline(row.rect.left, row.rect.bottom - c.px(2),
                        row.rect.right - row.rect.left, Theme::shadow);
                continue;
            }
            if (st.hoverList == (int)i)
                c.fill(RECT{st.listRect.left, row.rect.top, st.listRect.right, row.rect.bottom},
                       Theme::faceHi);
            drawCheck(c, row.rect.left + c.px(8), row.rect.top + c.px(5), rowChecked(st, row),
                      st.scale);
            c.textClip(st.sections[row.section].items[row.item],
                       RECT{row.rect.left + c.px(30), row.rect.top, row.rect.right - c.px(4),
                            row.rect.bottom},
                       Theme::text, Align::Left);
        }
        RestoreDC(c.dc(), saved);
    }
    c.frame(RECT{st.listRect.left - 1, st.listRect.top - 1, st.listRect.right + 1,
                 st.listRect.bottom + 1},
            Theme::shadow);
    const Metrics m = metricsFor(st.scale);
    scrollbar::draw(c, st.listBarRect, m, st.contentH,
                    st.listRect.bottom - st.listRect.top, st.listScroll);

    // Add-a-file-type row.
    c.text(L"文件类型:", st.addFieldRect.left - c.px(52), st.addFieldRect.top + c.px(4),
           Theme::text);
    c.fill(st.addFieldRect, Theme::white);
    c.bevelField(st.addFieldRect);
    RECT add = st.addBtnRect;
    c.fill(add, Theme::face);
    if (st.pressBtn == 3) c.bevelSunken(add);
    else c.bevelRaised(add);
    c.textClip(L"添加", add, Theme::text, Align::Center);

    for (int i = 1; i <= 2; ++i) {
        RECT r = (i == 1) ? st.okRect : st.cancelRect;
        c.fill(r, Theme::face);
        if (st.pressBtn == i) c.bevelSunken(r);
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
    RECT rc;
    GetClientRect(hwnd, &rc);
    drawDialog(c, rc, *st);
    EndPaint(hwnd, &ps);
}

int rowAt(POINT p, const std::vector<RECT>& rects) {
    for (size_t i = 0; i < rects.size(); ++i)
        if (PtInRect(&rects[i], p)) return (int)i;
    return -1;
}

int listRowAt(const State& st, POINT p) {
    if (!PtInRect(&st.listRect, p)) return -1;
    for (size_t i = 0; i < st.rows.size(); ++i) {
        if (st.rows[i].header) continue;
        if (PtInRect(&st.rows[i].rect, p)) return (int)i;
    }
    return -1;
}

void toggle(State& st, int leftRow) {
    unsigned bits = kOwn[leftRow].bits;
    if ((st.mask & bits) == bits) st.mask &= ~bits;
    else st.mask |= bits;
}

// Reads the input, probes the shell for that file type and appends a section.
void addFileType(State& st) {
    wchar_t buf[64]{};
    GetWindowTextW(st.addEdit, buf, 64);
    std::wstring ext;
    for (wchar_t ch : std::wstring(buf)) {
        if (iswalnum(ch)) ext.push_back((wchar_t)towlower(ch));
    }
    if (ext.empty()) {
        MessageBeep(MB_ICONASTERISK);
        return;
    }
    for (auto& s : st.sections)
        if (s.key == ext) {
            MessageBeep(MB_ICONASTERISK);
            return;
        }
    std::vector<std::wstring> items = shellmenu::probeFileLabels(ext);
    if (items.empty()) {
        MessageBeep(MB_ICONASTERISK);
        return;
    }
    MenuSection sec;
    sec.key = ext;
    sec.title = L"." + ext + L" 文件";
    sec.items = std::move(items);
    st.sections.push_back(std::move(sec));
    SetWindowTextW(st.addEdit, L"");
    // Jump to the new section so the user sees it landed.
    const int view = st.listRect.bottom - st.listRect.top;
    st.listScroll = st.contentH;  // buildRows clamps to the real maximum
    buildRows(st);
    st.listScroll = std::max(0, st.contentH - view);
    buildRows(st);
    InvalidateRect(st.hwnd, nullptr, FALSE);
}

void finish(State& st, bool accepted);

LRESULT CALLBACK addEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && g_state) {
        if (wp == VK_RETURN) {
            addFileType(*g_state);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            finish(*g_state, false);
            return 0;
        }
    }
    return CallWindowProcW(g_addOldProc, hwnd, msg, wp, lp);
}

void finish(State& st, bool accepted) {
    st.accepted = accepted;
    st.done = true;
    DestroyWindow(st.hwnd);
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
            RECT rc;
            GetClientRect(hwnd, &rc);
            drawDialog(c, rc, *st);
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (!st) return 0;
        POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
        if (st->scrollDrag) {
            const Metrics m = metricsFor(st->scale);
            const int view = st->listRect.bottom - st->listRect.top;
            int pos = scrollbar::positionFromDrag(st->listBarRect, m, st->contentH, view,
                                                  st->scrollDragPos, p.y - st->scrollDragY);
            st->listScroll = pos;
            buildRows(*st);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        const int hoverL = rowAt(p, st->leftRects);
        const int hoverR = listRowAt(*st, p);
        if (hoverL != st->hoverLeft || hoverR != st->hoverList) {
            st->hoverLeft = hoverL;
            st->hoverList = hoverR;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEWHEEL: {
        if (!st) return 0;
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &p);
        if (PtInRect(&st->listRect, p) || PtInRect(&st->listBarRect, p)) {
            const int steps = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            st->listScroll -= steps * 3 * rowHeight(false, st->scale);
            buildRows(*st);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONDOWN: {
        if (!st) return 0;
        POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
        const Metrics m = metricsFor(st->scale);
        const int view = st->listRect.bottom - st->listRect.top;
        if (PtInRect(&st->listBarRect, p)) {
            auto part = scrollbar::hitTest(st->listBarRect, p, m, st->contentH, view,
                                           st->listScroll);
            const int page = std::max(rowHeight(false, st->scale),
                                      view - rowHeight(false, st->scale));
            if (part == scrollbar::Part::Decrement)
                st->listScroll -= 3 * rowHeight(false, st->scale);
            else if (part == scrollbar::Part::Increment)
                st->listScroll += 3 * rowHeight(false, st->scale);
            else if (part == scrollbar::Part::PageUp)
                st->listScroll -= page;
            else if (part == scrollbar::Part::PageDown)
                st->listScroll += page;
            else if (part == scrollbar::Part::Knob) {
                st->scrollDrag = true;
                st->scrollDragY = p.y;
                st->scrollDragPos = st->listScroll;
                SetCapture(hwnd);
            }
            buildRows(*st);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        int leftRow = rowAt(p, st->leftRects);
        if (leftRow >= 0 && !kOwn[leftRow].header) {
            toggle(*st, leftRow);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        int listRow = listRowAt(*st, p);
        if (listRow >= 0) {
            const ListRow& row = st->rows[listRow];
            toggleLabel(st->sections[row.section], st->sections[row.section].items[row.item]);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (PtInRect(&st->okRect, p)) st->pressBtn = 1;
        else if (PtInRect(&st->cancelRect, p)) st->pressBtn = 2;
        else if (PtInRect(&st->addBtnRect, p)) {
            st->pressBtn = 3;
            SetCapture(hwnd);
        } else if (PtInRect(&st->closeRect, p)) finish(*st, false);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!st) return 0;
        POINT p{(short)LOWORD(lp), (short)HIWORD(lp)};
        const int press = st->pressBtn;
        st->pressBtn = 0;
        if (GetCapture() == hwnd) ReleaseCapture();
        if (st->scrollDrag) {
            st->scrollDrag = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (press == 1 && PtInRect(&st->okRect, p)) finish(*st, true);
        else if (press == 2 && PtInRect(&st->cancelRect, p)) finish(*st, false);
        else if (press == 3 && PtInRect(&st->addBtnRect, p)) addFileType(*st);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (st) {
            if (wp == VK_ESCAPE) finish(*st, false);
            else if (wp == VK_RETURN) finish(*st, true);
            else if (wp == VK_SPACE) {
                int row = st->hoverList >= 0 ? st->hoverList : -1;
                int leftRow = st->hoverLeft;
                if (row >= 0) {
                    const ListRow& r = st->rows[row];
                    toggleLabel(st->sections[r.section], st->sections[r.section].items[r.item]);
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (leftRow >= 0 && !kOwn[leftRow].header) {
                    toggle(*st, leftRow);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
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

bool showMenuOptions(HWND owner, unsigned* mask, std::vector<MenuSection>& sections) {
    ensureClass();

    const double scale = dpiScaleOfWindow(owner);

    State st;
    st.owner = owner;
    st.scale = scale;
    st.mask = mask ? *mask : kMenuDefaults;
    st.sections = sections;

    HDC screen = GetDC(nullptr);
    HDC measureDc = CreateCompatibleDC(screen);
    int ownW = 0, itemW = 0;
    {
        Canvas c(measureDc, scale);
        for (const auto& own : kOwn) ownW = std::max(ownW, c.textWidth(own.label) + (int)(30 * scale));
        for (auto& s : st.sections)
            for (auto& item : s.items)
                itemW = std::max(itemW, c.textWidth(item) + (int)(56 * scale));
        itemW = std::max(itemW, c.textWidth(L"系统菜单项（按右键场景）") + (int)(20 * scale));
    }
    DeleteDC(measureDc);
    ReleaseDC(nullptr, screen);

    st.leftW = std::max((int)(300 * scale), ownW);
    st.rightW = std::min(std::max((int)(260 * scale), itemW), (int)(440 * scale));
    const int scrollerW = (int)(16 * scale + 0.5);
    int w = (int)(2 * 16 * scale + 0.5) + st.leftW + (int)(28 * scale + 0.5) + st.rightW +
            scrollerW;

    int ownRows = 0;
    for (const auto& own : kOwn) ownRows += rowHeight(own.header, scale);
    int contentH = 0;
    for (auto& s : st.sections)
        contentH += rowHeight(true, scale) + (int)s.items.size() * rowHeight(false, scale);
    const int listH =
        std::min(std::max(contentH, (int)(5 * 20 * scale)), (int)(16 * 20 * scale));
    int h = (int)(3 * scale) + (int)(22 * scale) + (int)(10 * scale) + (int)(18 * scale);
    h += std::max(ownRows, (int)(20 * scale) + listH + (int)(8 * scale) + (int)(24 * scale));
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

    st.addEdit = CreateWindowExW(0, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 st.addFieldRect.left + 2, st.addFieldRect.top + 2,
                                 st.addFieldRect.right - st.addFieldRect.left - 4,
                                 st.addFieldRect.bottom - st.addFieldRect.top - 4, st.hwnd,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
    st.editFont = CreateFontW(-(int)(14.6667 * scale + 0.5), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    SendMessageW(st.addEdit, WM_SETFONT, (WPARAM)st.editFont, TRUE);
    SendMessageW(st.addEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
    g_addOldProc = (WNDPROC)SetWindowLongPtrW(st.addEdit, GWLP_WNDPROC, (LONG_PTR)addEditProc);
    g_state = &st;

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
    if (st.addEdit) {
        SetWindowLongPtrW(st.addEdit, GWLP_WNDPROC, (LONG_PTR)g_addOldProc);
        DestroyWindow(st.addEdit);
        st.addEdit = nullptr;
    }
    if (st.editFont) {
        DeleteObject(st.editFont);
        st.editFont = nullptr;
    }
    g_state = nullptr;
    if (st.accepted) {
        if (mask) *mask = st.mask;
        sections = st.sections;  // hidden lists are read from each section
        return true;
    }
    return false;
}

void drawOptionsPreview(Canvas& c, const RECT& rc, double scale, unsigned mask,
                        const std::vector<MenuSection>& sections) {
    State st;
    st.scale = scale;
    st.mask = mask;
    st.sections = sections;
    st.leftW = (int)(300 * scale + 0.5);
    st.rightW = rc.right - rc.left - (int)(2 * 16 * scale + 0.5) - st.leftW -
                (int)(28 * scale + 0.5) - (int)(16 * scale + 0.5);
    layoutInto(st, rc);
    drawDialog(c, rc, st);
}
