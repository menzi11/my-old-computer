#include "render.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <memory>
#include <string>
#include <vector>

#include "app.h"
#include "canvas.h"
#include "foldercolours.h"
#include "icons.h"
#include "ids.h"
#include "shellicons.h"
#include "scrollbar.h"
#include "theme.h"
#include "thumbs.h"

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

// The colour a folder was given (its context menu: 文件夹颜色); kDefault for
// any other item and for a drive.
int folderColourOf(const std::wstring& path, bool isDir) {
    return isDir && !fsx::isRoot(path) ? foldercolours::of(path) : foldercolours::kDefault;
}

// Draws the icon Explorer would show for this item - a folder in the colour it
// was given, if it was given one.  Returns false when the shell has no icon
// (or system icons are switched off), so the caller can fall back to the
// code-drawn set.
bool drawShellIcon(Canvas& c, App& a, const RECT& r, const std::wstring& path, bool isDir) {
    if (!a.useSystemIcons || path.empty()) return false;
    const bool isDrive = isDir && fsx::isRoot(path);
    const int colour = folderColourOf(path, isDir);
    HICON icon = colour == foldercolours::kDefault ? nullptr
                                                   : shellicons::folder(colour, (int)(r.right - r.left));
    if (!icon) icon = shellicons::icon(path, isDir, isDrive, (int)(r.right - r.left));
    if (!icon) return false;
    DrawIconEx(c.dc(), r.left, r.top, icon, r.right - r.left, r.bottom - r.top, 0, nullptr,
               DI_NORMAL);
    return true;
}

// The code-drawn folder, in the colour it was given if it was given one.
void drawFolder(Canvas& c, const RECT& r, int colour, bool open = false) {
    Icon::folder(c, r, open,
                 colour == foldercolours::kDefault ? 0 : foldercolours::colour(colour).rgb);
}

// How faint a hidden item is drawn: half of it over what is behind it.  That
// is how Explorer shows one - a hidden folder's yellow (254, 190, 24) comes
// out (254, 222, 139) over white - and its name stays as black as any other.
constexpr BYTE kFaint = 128;

// What `paint` draws into a canvas, at half strength over this canvas's `r`.
// It is drawn whole onto a surface of `behind` - the colour that is under it
// in `r` (the white of the list, or the selection's navy under a selected
// item) - and that is blended half and half with what is here.  So the icon
// looks the same wherever it lies, as one drawn with half transparency would.
template <class Paint>
void faint(Canvas& c, const RECT& r, COLORREF behind, Paint paint) {
    const int w = r.right - r.left, h = r.bottom - r.top;
    if (w <= 0 || h <= 0) return;
    // The surface is kept from one icon to the next, and grows when an icon
    // bigger than any before comes (48 px in the big-icon view).
    static HDC memory = nullptr;
    static int keptW = 0, keptH = 0;
    if (!memory) memory = CreateCompatibleDC(c.dc());
    if (memory && (w > keptW || h > keptH)) {
        keptW = std::max(keptW, w);
        keptH = std::max(keptH, h);
        HGDIOBJ bigger = CreateCompatibleBitmap(c.dc(), keptW, keptH);
        if (bigger) {
            HGDIOBJ was = SelectObject(memory, bigger);
            if (was && was != HGDI_ERROR) DeleteObject(was);  // the surface that was
        }
    }
    if (!memory) {  // nowhere to draw it whole: as any other icon, then
        paint(c, r);
        return;
    }
    {
        Canvas own(memory, c.scale());
        const RECT box{0, 0, w, h};
        own.fill(box, behind);
        paint(own, box);
    }
    BLENDFUNCTION blend{AC_SRC_OVER, 0, kFaint, 0};
    AlphaBlend(c.dc(), r.left, r.top, w, h, memory, 0, 0, w, h, blend);
}

// An entry's icon in a box of the list or of a cell: the one Explorer shows
// for it, or this program's own when the shell has none (a file's own one,
// `inset` in from the box).  A hidden entry's at half strength over `behind`,
// the colour under the icon.
void drawItemIcon(Canvas& c, App& a, const RECT& r, const Entry& e, int inset, COLORREF behind) {
    const auto paint = [&](Canvas& on, const RECT& box) {
        if (drawShellIcon(on, a, box, e.path, e.isDir)) return;
        if (e.isDir) {
            drawFolder(on, box, folderColourOf(e.path, true));
        } else {
            Icon::file(on, RECT{box.left + inset, box.top + inset, box.right - inset, box.bottom - inset},
                       fsx::extensionLower(e.name));
        }
    };
    if (e.hidden)
        faint(c, r, behind, paint);
    else
        paint(c, r);
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

// The address bar's crumbs: every folder on the way to the current one, from
// the machine down, each a button; a v after each one but the last, which
// lists that folder's subfolders.  When the way is too long for the bar, the
// machine is an icon alone and a ... after it stands for the crumbs that do
// not fit.
void drawAddressCrumbs(Canvas& c, App& a) {
    const RECT inner = a.addressInner();
    ClipScope clip(c.dc(), inner);
    const int pad = c.px(5), s16 = c.px(16);
    // A flat button until the pointer is on it (the tree's hover colour), a
    // pressed one while its list is up or the button is held.
    const auto button = [&](const RECT& r, bool hot, bool down) {
        if (down) {
            c.fill(r, Theme::face);
            c.bevelSunken(r);
        } else if (hot) {
            c.fill(r, Theme::faceHi);
        }
    };
    const auto triangle = [&](const RECT& r) {
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        const int dx = c.px(2), dy = c.px(3);
        POINT tri[3] = {{cx - dx, cy - dy}, {cx + dx, cy}, {cx - dx, cy + dy}};
        c.polygonFill(tri, 3, Theme::dark);
    };
    const int textTop = inner.top + 1, textBottom = inner.bottom - 1;
    // A crumb's button: the machine's with its icon, then its name - or what
    // is left of it - and the v after it.
    const auto drawCrumb = [&](int i) {
        const Crumb& crumb = a.crumbs[i];
        const RECT box = crumb.box;
        if (box.right <= box.left) return;
        button(box, a.hoverCrumb == i, a.pressCrumb == i);
        int text = box.left + pad;
        if (i == 0) {  // the machine: its icon, as the tree's root row shows it
            RECT iconRect{box.left + pad, (box.top + box.bottom - s16) / 2, box.left + pad + s16,
                          (box.top + box.bottom - s16) / 2 + s16};
            Icon::computer(c, iconRect);
            text = iconRect.right + c.px(3);
        }
        if (!crumb.text.empty())
            c.textClip(crumb.text, RECT{text, textTop, box.right - pad, textBottom}, Theme::text,
                       Align::Left);
        const RECT more = crumb.more;
        if (more.right > more.left) {
            button(more, a.hoverCrumbMore == i, a.pressCrumbMore == i);
            triangle(more);
        }
    };
    if (a.crumbFirst > 0) drawCrumb(0);
    // The ... that stands for the crumbs out of view.
    if (a.lay.addrMore.right > a.lay.addrMore.left) {
        const RECT r = a.lay.addrMore;
        button(r, a.hoverAddrMore, a.pressAddrMore);
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        const int d = c.px(2), step = c.px(4);
        for (int i = -1; i <= 1; ++i) c.fill(cx + i * step - d / 2, cy - d / 2, d, d, Theme::dark);
    }
    for (int i = a.crumbFirst; i < (int)a.crumbs.size(); ++i) drawCrumb(i);
}

void drawAddressBar(Canvas& c, App& a) {
    RECT bar = a.lay.addr;
    c.fill(bar, Theme::face);
    c.text(L"地址(D)", bar.left + c.px(4), bar.top + c.px(6), Theme::text);
    c.fill(a.lay.addrField, Theme::white);
    c.bevelField(a.lay.addrField);
    // While a path is being typed, the edit control is over the field and
    // draws it; otherwise the way to the current folder is there as buttons.
    if (!a.addrEditing) {
        if (a.crumbs.empty()) {
            RECT textRect{a.lay.addrField.left + c.px(4), a.lay.addrField.top,
                          a.lay.addrField.right - c.px(4), a.lay.addrField.bottom};
            c.textClip(a.current, textRect, Theme::text, Align::Left);
        } else {
            drawAddressCrumbs(c, a);
        }
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
            // Asked for here, the first time the row is drawn (see
            // App::treeRowHasChildren): a folder just listed is not asked
            // about until its row is in view.
            if (a.treeRowHasChildren(*n)) {
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
            const auto paintIcon = [&](Canvas& on, const RECT& box) {
                if (n->kind != TreeNode::Machine && !n->path.empty() &&
                    drawShellIcon(on, a, box, n->path, true))
                    return;
                switch (n->kind) {
                case TreeNode::Machine: Icon::computer(on, box); break;
                case TreeNode::Volume: Icon::drive(on, box); break;
                default: drawFolder(on, box, folderColourOf(n->path, true), n->expanded); break;
                }
            };
            // A hidden folder's icon at half strength, over what its row shows behind it.
            if (n->hidden) {
                const bool lit = a.hoverTree == i || (a.dragTargetIsTree && a.dragTargetRow == i);
                const COLORREF behind = selected ? Theme::select : lit ? Theme::faceHi : Theme::white;
                faint(c, iconRect, behind, paintIcon);
            } else {
                paintIcon(c, iconRect);
            }
            RECT textRect{iconRect.right + c.px(3), r.top, r.right - c.px(4), r.bottom};
            c.textClip(n->title, textRect,
                       selected ? Theme::white : (n->missing ? Theme::dim : Theme::text),
                       Align::Left);
        }
        // A favorite dragged to another place in its group: the line it would
        // drop onto.
        if (a.favDragging && a.favLine.right > a.favLine.left) {
            c.fill(a.favLine, Theme::dark);
            const int y = (a.favLine.top + a.favLine.bottom) / 2;
            POINT tip[3] = {{a.favLine.left, y - c.px(3)},
                            {a.favLine.left + c.px(4), y},
                            {a.favLine.left, y + c.px(3)}};
            c.polygonFill(tip, 3, Theme::dark);
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

// The colour of a row's selection box.  While the name is being edited it is
// the colour the edit box highlights the selected part of the name with, so
// the row being renamed is one blue and not the app's navy beside the
// system's.  (Windows 98's own highlight was that navy.  Where the system's
// is lighter - #0078D7 on Windows 10 and 11 - the two did not match.)
COLORREF selectionColour(const App& a, int i) {
    return renamingRow(a, i) ? GetSysColor(COLOR_HIGHLIGHT) : Theme::select;
}

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
    if (sel) c.fill(RECT{r.left, r.top, r.left + extent, r.bottom}, selectionColour(a, i));
    const int s16 = c.px(16);
    RECT iconRect{r.left + c.px(3), r.top + c.px(2), r.left + c.px(3) + s16,
                  r.top + c.px(2) + s16};
    drawItemIcon(c, a, iconRect, e, 0, sel ? selectionColour(a, i) : Theme::white);
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
// A picture in its cell: the thumbnail centred in the box and, when the item
// is selected, a frame in the selection's colour around it.  A hidden
// picture's at half strength (alpha 128).
void drawThumbnail(Canvas& c, const thumbs::Bitmap& thumbnail, const RECT& box, COLORREF selected,
                   BYTE alpha = 255) {
    const int w = std::min(thumbnail.w, (int)(box.right - box.left));
    const int h = std::min(thumbnail.h, (int)(box.bottom - box.top));
    if (w <= 0 || h <= 0) return;
    const int x = box.left + ((int)(box.right - box.left) - w) / 2;
    const int y = box.top + ((int)(box.bottom - box.top) - h) / 2;
    HDC memory = CreateCompatibleDC(c.dc());
    if (!memory) return;
    HGDIOBJ old = SelectObject(memory, thumbnail.handle);
    BLENDFUNCTION blend{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    AlphaBlend(c.dc(), x, y, w, h, memory, 0, 0, w, h, blend);
    SelectObject(memory, old);
    DeleteDC(memory);
    if (selected) c.frame(RECT{x - 1, y - 1, x + w + 1, y + h + 1}, selected);
}

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
                   selectionColour(a, i));
        }
        drawItemIcon(c, a, iconRect, e, 0, sel ? selectionColour(a, i) : Theme::white);
        if (renamingRow(a, i)) {
            drawRenameField(c, a, i);
            return;
        }
        c.textClip(label, RECT{textLeft, r.top, r.right - c.px(2), r.bottom},
                   sel ? Theme::white : Theme::text, Align::Left);
        return;
    }
    const int iconSize = a.iconBox();
    RECT iconBox{(r.left + r.right) / 2 - iconSize / 2, r.top + c.px(4),
                 (r.left + r.right) / 2 + iconSize / 2, r.top + c.px(4) + iconSize};
    // A folder of pictures: the picture itself, once its thumbnail is in.  It
    // is asked for here if it is not (thumbs.h); the icon is shown until then.
    const std::shared_ptr<const thumbs::Bitmap> thumbnail =
        a.thumbnails && thumbs::isPicture(e.name) ? thumbs::get(e.path, iconSize, a.hwnd) : nullptr;
    if (thumbnail) {
        drawThumbnail(c, *thumbnail, iconBox, sel ? selectionColour(a, i) : 0,
                      e.hidden ? kFaint : 255);
        // For whoever shows this frame: this one is drawn with its thumbnail.
        a.thumbRendered.push_back(i);
    } else {
        drawItemIcon(c, a, iconBox, e, c.px(8), Theme::white);
    }
    const int bandTop = r.top + a.labelTop();
    const int bandH = std::max(0, (int)(r.bottom - c.px(2)) - bandTop);
    std::wstring label = ellipsize(c, e.name, (int)(r.right - r.left) - c.px(10));
    const int tw = c.textWidth(label);
    const int lh = c.lineHeight();
    const int lx = (r.left + r.right - tw) / 2;
    const int ly = bandTop + std::max(0, (bandH - lh) / 2);
    if (sel) c.fill(RECT{lx - c.px(3), ly, lx + tw + c.px(3), ly + lh}, selectionColour(a, i));
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
