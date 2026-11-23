#include "render.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>

#include "app.h"
#include "canvas.h"
#include "icons.h"
#include "ids.h"
#include "shellicons.h"
#include "scrollbar.h"
#include "theme.h"

extern const wchar_t* const kMenuTitles[6];
extern const wchar_t* const kToolTitles[7];

namespace {

struct ClipScope {
    HDC dc;
    int saved;
    ClipScope(HDC d, const RECT& r) : dc(d), saved(SaveDC(d)) {
        IntersectClipRect(dc, r.left, r.top, r.right, r.bottom);
    }
    ~ClipScope() { RestoreDC(dc, saved); }
};

std::wstring folderTitle(const App& a) {
    if (a.current.empty()) return L"我的电脑";
    std::wstring name = fsx::fileName(a.current);
    if (name.empty()) return a.current;
    return name;
}

// Shortens a label so it fits maxW pixels, ending it with a real ellipsis.
std::wstring ellipsize(Canvas& c, const std::wstring& s, int maxW) {
    if (maxW <= 0) return L"";
    if (c.textWidth(s) <= maxW) return s;
    const std::wstring dots = L"\u2026";
    int lo = 0, hi = (int)s.size();
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (c.textWidth(s.substr(0, mid) + dots) <= maxW) lo = mid;
        else hi = mid - 1;
    }
    if (lo <= 0) return dots;
    return s.substr(0, lo) + dots;
}

// Draws the icon Explorer would show for this item.  Returns false when the
// shell has no icon (or system icons are switched off), so the caller can fall
// back to the code-drawn set.
bool drawShellIcon(Canvas& c, App& a, const RECT& r, const std::wstring& path, bool isDir) {
    if (!a.useSystemIcons || path.empty()) return false;
    const bool isDrive = isDir && fsx::isRoot(path);
    HICON icon = shellicons::icon(path, isDir, isDrive, (int)(r.right - r.left));
    if (!icon) return false;
    DrawIconEx(c.dc(), r.left, r.top, icon, r.right - r.left, r.bottom - r.top, 0, nullptr,
               DI_NORMAL);
    return true;
}

void drawRoot(Canvas& c, App& a) {
    c.fill(a.lay.client, Theme::face);
    RECT border{0, 0, a.width, a.height};
    c.frame(border, Theme::dark);
    RECT raised{1, 1, a.width - 1, a.height - 1};
    c.bevelRaised(raised);
}

void drawTitleBar(Canvas& c, App& a) {
    RECT t = a.lay.title;
    TRIVERTEX v[2];
    v[0].x = t.left;
    v[0].y = t.top;
    v[0].Red = (COLOR16)(0x00 << 8);
    v[0].Green = (COLOR16)(0x00 << 8);
    v[0].Blue = (COLOR16)(0x80 << 8);
    v[0].Alpha = 0;
    v[1].x = t.right;
    v[1].y = t.bottom;
    v[1].Red = (COLOR16)(0x10 << 8);
    v[1].Green = (COLOR16)(0x84 << 8);
    v[1].Blue = (COLOR16)(0xD0 << 8);
    v[1].Alpha = 0;
    GRADIENT_RECT gr{0, 1};
    GradientFill(c.dc(), v, 2, &gr, 1, GRADIENT_FILL_RECT_H);

    const int s16 = c.px(16);
    RECT iconRect{t.left + c.px(3), t.top + c.px(3), t.left + c.px(3) + s16, t.top + c.px(3) + s16};
    Icon::folder(c, iconRect);

    c.text(folderTitle(a), t.left + c.px(23), t.top + c.px(4), Theme::white, true);

    for (int i = 0; i < 3; ++i) {
        RECT r = a.titleButtonRect(i);
        c.fill(r, Theme::face);
        if (a.pressTitle == i) c.bevelSunken(r);
        else c.bevelRaised(r);
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        switch (i) {
        case 0:
            c.hline(cx - c.px(4), cy + c.px(3), c.px(9), Theme::dark);
            break;
        case 1:
            c.frame(RECT{cx - c.px(4), cy - c.px(4), cx + c.px(4), cy + c.px(4)}, Theme::dark);
            c.hline(cx - c.px(4), cy - c.px(1), c.px(9), Theme::dark);
            break;
        default:
            c.line(cx - c.px(3), cy - c.px(3), cx + c.px(3), cy + c.px(3), Theme::dark);
            c.line(cx + c.px(3), cy - c.px(3), cx - c.px(3), cy + c.px(3), Theme::dark);
            break;
        }
    }
    c.hline(t.left, t.bottom - 1, t.right - t.left, Theme::dark);
}

void drawMenuBar(Canvas& c, App& a) {
    RECT bar = a.lay.menu;
    c.fill(bar, Theme::face);
    for (int i = 0; i < 6; ++i) {
        RECT r = a.menuItemRect(i);
        if (a.pressMenu == i) c.bevelSunken(r);
        else if (a.hoverMenu == i) c.bevelRaised(r);
        c.textClip(kMenuTitles[i], r, Theme::text, Align::Center);
    }
    c.hline(bar.left, bar.bottom - 1, bar.right - bar.left, Theme::shadow);
}

void drawToolbar(Canvas& c, App& a) {
    RECT bar = a.lay.tool;
    c.fill(bar, Theme::face);
    const bool canBack = !a.back.empty();
    const bool canForward = !a.forward.empty();
    const bool canUp = a.current != fsx::rootOf(a.current);
    for (int i = 0; i < 7; ++i) {
        RECT r = a.toolbarItemRect(i);
        bool enabled = true;
        if (i == 0) enabled = canBack;
        if (i == 1) enabled = canForward;
        if (i == 2) enabled = canUp;
        const bool toggled = (i == kToolbarHiddenIndex) && a.showHidden;
        if (enabled && (a.pressTool == i || toggled)) c.bevelSunken(r);
        else if (enabled && a.hoverTool == i) c.bevelRaised(r);
        const int s16 = c.px(16);
        RECT iconRect{r.left + c.px(6), (r.top + r.bottom) / 2 - s16 / 2,
                      r.left + c.px(6) + s16, (r.top + r.bottom) / 2 - s16 / 2 + s16};
        switch (i) {
        case 0: Icon::arrowLeft(c, iconRect); break;
        case 1: Icon::arrowRight(c, iconRect); break;
        case 2: Icon::arrowUp(c, iconRect); break;
        case 3: Icon::folder(c, iconRect); break;
        case 4: Icon::views(c, iconRect); break;
        case 5: Icon::refresh(c, iconRect); break;
        default: Icon::hidden(c, iconRect); break;
        }
        COLORREF color = enabled ? Theme::text : Theme::dim;
        RECT textRect{iconRect.right + c.px(4), r.top, r.right - c.px(2), r.bottom};
        c.textClip(kToolTitles[i], textRect, color, Align::Left);
    }
    c.hline(bar.left, bar.bottom - 1, bar.right - bar.left, Theme::shadow);
    c.hline(bar.left, bar.bottom - 2, bar.right - bar.left, Theme::white);
}

void drawAddressBar(Canvas& c, App& a) {
    RECT bar = a.lay.addr;
    c.fill(bar, Theme::face);
    c.text(L"地址(D)", bar.left + c.px(4), bar.top + c.px(6), Theme::text);
    c.fill(a.lay.addrField, Theme::white);
    c.bevelField(a.lay.addrField);
    if (!a.addrEditing) {
        RECT textRect{a.lay.addrField.left + c.px(4), a.lay.addrField.top,
                      a.lay.addrField.right - c.px(4), a.lay.addrField.bottom};
        c.textClip(a.current, textRect, Theme::text, Align::Left);
    }
    // Drop arrow.
    RECT arrow = a.lay.addrArrow;
    c.fill(arrow, Theme::face);
    c.bevelRaised(arrow);
    const int cx = (arrow.left + arrow.right) / 2, cy = (arrow.top + arrow.bottom) / 2;
    POINT tri[3] = {{cx - c.px(3), cy - c.px(1)}, {cx + c.px(3), cy - c.px(1)},
                    {cx, cy + c.px(2)}};
    c.polygonFill(tri, 3, Theme::dark);
    // Go button.
    RECT go = a.lay.addrGo;
    c.fill(go, Theme::face);
    if (a.pressTool == 100) c.bevelSunken(go);
    else c.bevelRaised(go);
    c.textClip(L"转到", go, Theme::text, Align::Center);
}

void drawSplitter(Canvas& c, App& a) {
    if (!a.sidebarVisible) return;
    RECT r = a.lay.splitter;
    c.fill(r, Theme::face);
    c.vline(r.left + c.px(1), r.top, r.bottom - r.top, Theme::shadow);
    c.vline(r.left + c.px(2), r.top, r.bottom - r.top, Theme::white);
}

void drawScrollBarAt(Canvas& c, App& a, const RECT& bar, int content, int view, int pos);

void drawTree(Canvas& c, App& a) {
    if (!a.sidebarVisible) return;
    RECT pane = a.lay.sidebar;
    c.fill(pane, Theme::face);

    c.fill(a.lay.treeHeader, Theme::face);
    c.bevelRaised(a.lay.treeHeader);
    c.text(L"文件夹", a.lay.treeHeader.left + c.px(5), a.lay.treeHeader.top + c.px(3), Theme::text,
           true);

    RECT vp = a.lay.treeViewport;
    c.fill(vp, Theme::white);

    // While a row's context menu is up, that row is the selected one; the
    // current folder's highlight comes back when the menu is gone.  (The
    // hover colour could not do this job: the menu opens under the pointer,
    // the window gets WM_MOUSELEAVE, and the hover is gone.)
    const int selectedRow = a.contextTreeRow >= 0 ? a.contextTreeRow : a.treeSelectedRow();

    {
        ClipScope clip(c.dc(), vp);
        for (int i = 0; i < (int)a.treeFlat.size(); ++i) {
            TreeNode* n = a.treeFlat[i];
            RECT r = a.treeRowRect(i);
            if (r.bottom <= vp.top || r.top >= vp.bottom) continue;
            const bool selected = i == selectedRow;
            if (selected) c.fill(r, Theme::select);
            else if (a.hoverTree == i || (a.dragTargetIsTree && a.dragTargetRow == i))
                c.fill(r, Theme::faceHi);

            const int indent = c.px(n->depth * 14 + 3);
            if (n->hasChildren) {
                RECT box{r.left + indent, r.top + c.px(5), r.left + indent + c.px(9),
                         r.top + c.px(5) + c.px(9)};
                c.fill(box, Theme::white);
                c.frame(RECT{box.left, box.top, box.right, box.bottom}, Theme::shadow);
                c.hline(box.left + c.px(3), (box.top + box.bottom) / 2, c.px(4), Theme::dark);
                if (!n->expanded)
                    c.vline((box.left + box.right) / 2, box.top + c.px(3), c.px(4), Theme::dark);
            }
            RECT iconRect{r.left + indent + c.px(13), r.top + c.px(2),
                          r.left + indent + c.px(13) + c.px(16), r.top + c.px(2) + c.px(16)};
            bool drewIcon = false;
            if (n->kind != TreeNode::Machine && !n->path.empty())
                drewIcon = drawShellIcon(c, a, iconRect, n->path, true);
            if (!drewIcon) {
                switch (n->kind) {
                case TreeNode::Machine: Icon::computer(c, iconRect); break;
                case TreeNode::Volume: Icon::drive(c, iconRect); break;
                default: Icon::folder(c, iconRect, n->expanded); break;
                }
            }
            RECT textRect{iconRect.right + c.px(3), r.top, r.right - c.px(4), r.bottom};
            c.textClip(n->title, textRect, selected ? Theme::white : Theme::text, Align::Left);
        }
    }
    c.frame(RECT{vp.left - 1, vp.top - 1, vp.right + 1, vp.bottom + 1}, Theme::shadow);
    drawScrollBarAt(c, a, a.lay.treeBar, a.treeContentHeight(), vp.bottom - vp.top, a.treeScroll);
}

void drawScrollBarAt(Canvas& c, App& a, const RECT& bar, int content, int view, int pos) {
    scrollbar::draw(c, bar, a.m, content, view, pos);
}

void drawStatusBar(Canvas& c, App& a) {
    RECT bar = a.lay.status;
    c.fill(bar, Theme::face);
    for (RECT p : {a.lay.statusLeft, a.lay.statusRight}) {
        c.fill(p, Theme::face);
        c.bevelSunken(p);
    }
    {
        ClipScope clip(c.dc(), RECT{a.lay.statusLeft.left + c.px(2), a.lay.statusLeft.top,
                                    a.lay.statusLeft.right - c.px(2), a.lay.statusLeft.bottom});
        c.textClip(a.statusLeftText(), RECT{a.lay.statusLeft.left + c.px(3), a.lay.statusLeft.top,
                                            a.lay.statusLeft.right, a.lay.statusLeft.bottom},
                   Theme::text, Align::Left);
    }
    c.textClip(folderTitle(a),
               RECT{a.lay.statusRight.left, a.lay.statusRight.top,
                    a.lay.statusRight.right - c.px(4), a.lay.statusRight.bottom},
               Theme::text, Align::Right);
}

// ---------------------------------------------------------------- content

// True for the row whose name is being edited in place.
bool renamingRow(const App& a, int i) { return a.renaming && a.renameRow == i; }

// The rename field (App::renameField): white, over the name's selection box,
// framed in black - all round when the row has a pixel to spare above and
// below the name, down its two sides only when it has not.
void drawRenameField(Canvas& c, const App& a, int i) {
    const RenameField f = a.renameField(i);
    c.fill(f.frame, Theme::white);
    if (f.frame.top < f.edit.top) {
        c.frame(f.frame, Theme::dark);
    } else {
        c.vline(f.frame.left, f.frame.top, f.frame.bottom - f.frame.top, Theme::dark);
        c.vline(f.frame.right - 1, f.frame.top, f.frame.bottom - f.frame.top, Theme::dark);
    }
}

// One list row; also used to repaint a single row while a rubber band is
// dragged, so selection feedback costs one row instead of the whole window.
void drawDetailsRow(Canvas& c, App& a, int i) {
    RECT r = a.listRowRect(i);
    const Entry& e = a.entries[i];
    const bool sel = a.selection.count(e.path) > 0;
    const bool editing = renamingRow(a, i);
    c.fill(r, Theme::white);
    const int extent = std::min(a.lay.nameW, a.nameExtent(i));
    if (sel) c.fill(RECT{r.left, r.top, r.left + extent, r.bottom}, Theme::select);
    const int s16 = c.px(16);
    RECT iconRect{r.left + c.px(3), r.top + c.px(2), r.left + c.px(3) + s16,
                  r.top + c.px(2) + s16};
    if (!drawShellIcon(c, a, iconRect, e.path, e.isDir)) {
        if (e.isDir) {
            Icon::folder(c, iconRect);
        } else {
            Icon::file(c, iconRect, fsx::extensionLower(e.name));
        }
    }
    RECT nameRect{r.left + c.px(22), r.top, r.left + a.lay.nameW - c.px(4), r.bottom};
    if (!editing) c.textClip(e.name, nameRect, sel ? Theme::white : Theme::text, Align::Left);
    if (e.hasSize)
        c.textClip(fsx::formatSize(e.size),
                   RECT{a.lay.colX[1], r.top, a.lay.colX[1] + a.lay.colW[1] - c.px(8), r.bottom},
                   Theme::text, Align::Right);
    c.textClip(e.type,
               RECT{a.lay.colX[2] + c.px(5), r.top, a.lay.colX[2] + a.lay.colW[2], r.bottom},
               Theme::text, Align::Left);
    if (e.hasTime)
        c.textClip(fsx::formatDate(e.mtime),
                   RECT{a.lay.colX[3] + c.px(5), r.top, a.lay.colX[3] + a.lay.colW[3], r.bottom},
                   Theme::text, Align::Left);
    if (editing) drawRenameField(c, a, i);
}

void drawDetails(Canvas& c, App& a) {
    RECT content = a.lay.content;
    const int contentW = content.right - content.left;
    const int tableW = std::max(0, contentW - 4 - a.m.scrollerW);

    c.fill(content, Theme::face);
    RECT area{content.left + 1, content.top + a.m.headerH + 1, content.left + 1 + tableW,
              content.bottom - 2};
    c.fill(area, Theme::white);
    c.bevelSunken(area);

    // Header cells.
    static const wchar_t* titles[4] = {L"名称", L"大小", L"类型", L"修改日期"};
    for (int i = 0; i < 4; ++i) {
        RECT cell{a.lay.colX[i] + 1, content.top + 1, a.lay.colX[i] + a.lay.colW[i] - 1,
                  content.top + a.m.headerH - 1};
        c.fill(cell, Theme::face);
        c.bevelRaised(cell);
        if (i == 1) {
            c.textClip(titles[i], RECT{cell.left, cell.top, cell.right - c.px(6), cell.bottom},
                       Theme::text, Align::Right);
        } else {
            c.textClip(titles[i], RECT{cell.left + c.px(5), cell.top, cell.right, cell.bottom},
                       Theme::text, Align::Left);
        }
        if (a.sortColumn == i) {
            const int tw = c.textWidth(titles[i]);
            int ax = (i == 1) ? cell.left + c.px(3) : cell.left + c.px(5) + tw + c.px(3);
            RECT ar{ax, (cell.top + cell.bottom) / 2 - c.px(4), ax + c.px(8),
                    (cell.top + cell.bottom) / 2 + c.px(4)};
            Icon::sortArrow(c, ar, a.sortAsc);
        }
    }

    RECT vp = a.lay.listViewport;
    {
        ClipScope clip(c.dc(), vp);
        for (int i = 0; i < (int)a.entries.size(); ++i) {
            RECT r = a.listRowRect(i);
            if (r.bottom <= vp.top || r.top >= vp.bottom) continue;
            drawDetailsRow(c, a, i);
        }
    }
    c.frame(RECT{vp.left - 1, vp.top - 1, vp.right + 1, vp.bottom + 1}, Theme::shadow);
    drawScrollBarAt(c, a, a.lay.listBar, a.listContentHeight(), vp.bottom - vp.top, a.listScroll);
}

// One list/icon cell; also used for single-cell repaints during a drag.
void drawGridCell(Canvas& c, App& a, int i) {
    RECT r = a.gridCellRect(i);
    const Entry& e = a.entries[i];
    const bool sel = a.selection.count(e.path) > 0;
    const int s16 = c.px(16);
    c.fill(r, Theme::white);
    if (a.mode == ViewMode::List) {
        RECT iconRect{r.left + c.px(2), r.top + c.px(2), r.left + c.px(2) + s16,
                      r.top + c.px(2) + s16};
        const int textLeft = iconRect.right + c.px(4);
        std::wstring label = ellipsize(c, e.name, (int)(r.right - c.px(4)) - textLeft);
        // The selection box goes under the icon, never over it.
        if (sel) {
            int tw = c.textWidth(label);
            c.fill(RECT{r.left + c.px(1), r.top, textLeft + tw + c.px(3), r.bottom},
                   Theme::select);
        }
        if (!drawShellIcon(c, a, iconRect, e.path, e.isDir)) {
            if (e.isDir) Icon::folder(c, iconRect);
            else Icon::file(c, iconRect, fsx::extensionLower(e.name));
        }
        if (renamingRow(a, i)) {
            drawRenameField(c, a, i);
            return;
        }
        c.textClip(label, RECT{textLeft, r.top, r.right - c.px(2), r.bottom},
                   sel ? Theme::white : Theme::text, Align::Left);
        return;
    }
    const int iconSize = c.px(48);
    RECT iconBox{(r.left + r.right) / 2 - iconSize / 2, r.top + c.px(4),
                 (r.left + r.right) / 2 + iconSize / 2, r.top + c.px(4) + iconSize};
    if (!drawShellIcon(c, a, iconBox, e.path, e.isDir)) {
        if (e.isDir) {
            Icon::folder(c, iconBox);
        } else {
            const int inset = c.px(8);
            Icon::file(c,
                       RECT{iconBox.left + inset, iconBox.top + inset, iconBox.right - inset,
                            iconBox.bottom - inset},
                       fsx::extensionLower(e.name));
        }
    }
    const int bandTop = r.top + c.px(54);
    const int bandH = std::max(0, (int)(r.bottom - c.px(2)) - bandTop);
    std::wstring label = ellipsize(c, e.name, (int)(r.right - r.left) - c.px(10));
    const int tw = c.textWidth(label);
    const int lh = c.lineHeight();
    const int lx = (r.left + r.right - tw) / 2;
    const int ly = bandTop + std::max(0, (bandH - lh) / 2);
    if (sel) c.fill(RECT{lx - c.px(3), ly, lx + tw + c.px(3), ly + lh}, Theme::select);
    if (renamingRow(a, i)) {
        drawRenameField(c, a, i);
        return;
    }
    c.text(label, lx, ly, sel ? Theme::white : Theme::text);
}

void drawGrid(Canvas& c, App& a) {
    RECT content = a.lay.content;
    const int contentW = content.right - content.left;
    const int bodyW = std::max(0, contentW - 4 - a.m.scrollerW);

    c.fill(content, Theme::face);
    RECT area{content.left + 1, content.top + 1, content.left + 1 + bodyW, content.bottom - 2};
    c.fill(area, Theme::white);
    c.bevelSunken(area);

    RECT vp = a.lay.gridViewport;
    {
        ClipScope clip(c.dc(), vp);
        for (int i = 0; i < (int)a.entries.size(); ++i) {
            RECT r = a.gridCellRect(i);
            if (r.bottom <= vp.top || r.top >= vp.bottom) continue;
            drawGridCell(c, a, i);
        }
    }
    c.frame(RECT{vp.left - 1, vp.top - 1, vp.right + 1, vp.bottom + 1}, Theme::shadow);
    drawScrollBarAt(c, a, a.lay.gridBar, a.gridContentHeight(), vp.bottom - vp.top, a.gridScroll);
}

void drawMarquee(Canvas& c, App& a) {
    if (!a.marquee) return;
    const RECT& vp = a.mode == ViewMode::Details ? a.lay.listViewport : a.lay.gridViewport;
    RECT r{std::min(a.marqueeStart.x, a.marqueeEnd.x), std::min(a.marqueeStart.y, a.marqueeEnd.y),
           std::max(a.marqueeStart.x, a.marqueeEnd.x), std::max(a.marqueeStart.y, a.marqueeEnd.y)};
    r.left = std::max(r.left, (LONG)vp.left);
    r.top = std::max(r.top, (LONG)vp.top);
    r.right = std::min(r.right, (LONG)vp.right);
    r.bottom = std::min(r.bottom, (LONG)vp.bottom);
    if (r.right <= r.left || r.bottom <= r.top) return;
    // Win98 drew a dotted rubber band by XOR.  One pixel per dot, not a brush
    // per dot.
    for (int x = r.left; x < r.right; x += 2) {
        SetPixelV(c.dc(), x, r.top, Theme::dark);
        SetPixelV(c.dc(), x, r.bottom - 1, Theme::dark);
    }
    for (int y = r.top; y < r.bottom; y += 2) {
        SetPixelV(c.dc(), r.left, y, Theme::dark);
        SetPixelV(c.dc(), r.right - 1, y, Theme::dark);
    }
}

void drawDragTarget(Canvas& c, App& a) {
    if (!a.dragging || a.dragTargetFolder.empty() || a.dragTargetIsTree) return;
    RECT target{};
    bool found = false;
    for (int i = 0; i < (int)a.entries.size(); ++i) {
        if (_wcsicmp(a.entries[i].path.c_str(), a.dragTargetFolder.c_str()) == 0) {
            target = a.mode == ViewMode::Details ? a.listRowRect(i) : a.gridCellRect(i);
            found = true;
            break;
        }
    }
    if (!found) return;
    if (a.mode == ViewMode::Details) c.frame(target, Theme::navy);
    else {
        RECT r = target;
        c.frame(RECT{r.left + c.px(1), r.top + c.px(1), r.right - c.px(1), r.bottom - c.px(1)},
                Theme::navy);
        c.frame(RECT{r.left + c.px(2), r.top + c.px(2), r.right - c.px(2), r.bottom - c.px(2)},
                Theme::navy);
    }
}

}  // namespace

void renderApp(Canvas& c, App& a) {
    drawRoot(c, a);
    drawTitleBar(c, a);
    drawMenuBar(c, a);
    drawToolbar(c, a);
    drawAddressBar(c, a);
    drawTree(c, a);
    drawSplitter(c, a);
    if (a.mode == ViewMode::Details) drawDetails(c, a);
    else drawGrid(c, a);
    drawDragTarget(c, a);
    drawStatusBar(c, a);
}

void renderMarquee(Canvas& c, App& a) { drawMarquee(c, a); }

void renderItemRow(Canvas& c, App& a, int index) {
    if (index < 0 || index >= (int)a.entries.size()) return;
    if (a.mode == ViewMode::Details) drawDetailsRow(c, a, index);
    else drawGridCell(c, a, index);
}
