#include "canvas.h"

#include <algorithm>
#include <cmath>

Canvas::Canvas(HDC dc, double scale) : dc_(dc), scale_(scale) {
    oldFont_ = SelectObject(dc_, nullptr);
    if (oldFont_) SelectObject(dc_, oldFont_);
    oldText_ = GetTextColor(dc_);
    oldBk_ = GetBkMode(dc_);
    SetBkMode(dc_, TRANSPARENT);
    SetTextAlign(dc_, TA_LEFT | TA_TOP);
    textHeight_ = (int)std::lround(14.6667 * scale_);
    normal_ = CreateFontW(-textHeight_, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    bold_ = CreateFontW(-textHeight_, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    oldFont_ = SelectObject(dc_, normal_);
}

Canvas::~Canvas() {
    if (oldFont_) SelectObject(dc_, oldFont_);
    if (normal_) DeleteObject(normal_);
    if (bold_) DeleteObject(bold_);
    SetTextColor(dc_, oldText_);
    SetBkMode(dc_, oldBk_);
}

HFONT Canvas::pick(bool bold) { return bold && bold_ ? bold_ : normal_; }

void Canvas::fill(const RECT& r, COLORREF c) {
    fill(r.left, r.top, r.right - r.left, r.bottom - r.top, c);
}

void Canvas::fill(int x, int y, int w, int h, COLORREF c) {
    if (w <= 0 || h <= 0) return;
    RECT r{x, y, x + w, y + h};
    HBRUSH b = CreateSolidBrush(c);
    ::FillRect(dc_, &r, b);
    DeleteObject(b);
}

void Canvas::hline(int x, int y, int w, COLORREF c) { fill(x, y, w, 1, c); }
void Canvas::vline(int x, int y, int h, COLORREF c) { fill(x, y, 1, h, c); }

void Canvas::frame(const RECT& r, COLORREF c) {
    hline(r.left, r.top, r.right - r.left, c);
    hline(r.left, r.bottom - 1, r.right - r.left, c);
    vline(r.left, r.top, r.bottom - r.top, c);
    vline(r.right - 1, r.top, r.bottom - r.top, c);
}

void Canvas::bevelRaised(const RECT& r) {
    hline(r.left, r.top, r.right - r.left, Theme::white);
    vline(r.left, r.top, r.bottom - r.top, Theme::white);
    hline(r.left, r.bottom - 1, r.right - r.left, Theme::shadow);
    vline(r.right - 1, r.top, r.bottom - r.top, Theme::shadow);
}

void Canvas::bevelSunken(const RECT& r) {
    hline(r.left, r.top, r.right - r.left, Theme::shadow);
    vline(r.left, r.top, r.bottom - r.top, Theme::shadow);
    hline(r.left, r.bottom - 1, r.right - r.left, Theme::white);
    vline(r.right - 1, r.top, r.bottom - r.top, Theme::white);
}

void Canvas::bevelField(const RECT& r) {
    // Outer shadow, inner black, then a white highlight on the far edges.
    hline(r.left, r.top, r.right - r.left, Theme::shadow);
    vline(r.left, r.top, r.bottom - r.top, Theme::shadow);
    hline(r.left + 1, r.top + 1, r.right - r.left - 1, Theme::dark);
    vline(r.left + 1, r.top + 1, r.bottom - r.top - 1, Theme::dark);
    hline(r.left, r.bottom - 1, r.right - r.left, Theme::white);
    vline(r.right - 1, r.top, r.bottom - r.top, Theme::white);
}

void Canvas::line(int x1, int y1, int x2, int y2, COLORREF c) {
    // Integer Bresenham: keeps 1 px hard lines, no GDI pen state.
    int dx = std::abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    int dy = -std::abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fill(x1, y1, 1, 1, c);
        if (x1 == x2 && y1 == y2) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void Canvas::polygonFill(const POINT* pts, int n, COLORREF c) {
    if (n < 3) return;
    HBRUSH b = CreateSolidBrush(c);
    HGDIOBJ ob = SelectObject(dc_, b);
    HGDIOBJ op = SelectObject(dc_, GetStockObject(NULL_PEN));
    Polygon(dc_, pts, n);
    SelectObject(dc_, op);
    SelectObject(dc_, ob);
    DeleteObject(b);
}

void Canvas::polygonOutline(const POINT* pts, int n, COLORREF c) {
    if (n < 2) return;
    HPEN p = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ op = SelectObject(dc_, p);
    HGDIOBJ ob = SelectObject(dc_, GetStockObject(NULL_BRUSH));
    Polyline(dc_, pts, n);
    SelectObject(dc_, ob);
    SelectObject(dc_, op);
    DeleteObject(p);
}

int Canvas::textWidth(const std::wstring& s, bool bold) {
    if (s.empty()) return 0;
    HGDIOBJ of = SelectObject(dc_, pick(bold));
    SIZE sz{};
    GetTextExtentPoint32W(dc_, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc_, of);
    return sz.cx;
}

int Canvas::lineHeight(bool bold) {
    HGDIOBJ of = SelectObject(dc_, pick(bold));
    TEXTMETRICW tm{};
    GetTextMetricsW(dc_, &tm);
    SelectObject(dc_, of);
    return tm.tmHeight;
}

void Canvas::text(const std::wstring& s, int x, int y, COLORREF c, bool bold) {
    if (s.empty()) return;
    HGDIOBJ of = SelectObject(dc_, pick(bold));
    SetTextColor(dc_, c);
    TextOutW(dc_, x, y, s.c_str(), (int)s.size());
    SelectObject(dc_, of);
}

void Canvas::textIn(const std::wstring& s, const RECT& r, COLORREF c, Align a, bool bold,
                    bool wrap) {
    if (s.empty()) return;
    HGDIOBJ of = SelectObject(dc_, pick(bold));
    SetTextColor(dc_, c);
    UINT fmt = DT_NOPREFIX | (wrap ? DT_WORDBREAK : DT_SINGLELINE | DT_TOP);
    if (a == Align::Center) fmt |= DT_CENTER;
    if (a == Align::Right) fmt |= DT_RIGHT;
    RECT rr = r;
    DrawTextW(dc_, s.c_str(), (int)s.size(), &rr, fmt);
    SelectObject(dc_, of);
}

void Canvas::textClip(const std::wstring& s, const RECT& r, COLORREF c, Align a, bool bold) {
    if (s.empty() || r.right <= r.left) return;
    HGDIOBJ of = SelectObject(dc_, pick(bold));
    SetTextColor(dc_, c);
    UINT fmt = DT_NOPREFIX | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS;
    if (a == Align::Center) fmt |= DT_CENTER;
    if (a == Align::Right) fmt |= DT_RIGHT;
    RECT rr = r;
    DrawTextW(dc_, s.c_str(), (int)s.size(), &rr, fmt);
    SelectObject(dc_, of);
}
