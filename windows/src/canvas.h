// Thin GDI wrapper: every pixel in this program is painted through Canvas.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "theme.h"

enum class Align { Left, Center, Right };

class Canvas {
public:
    Canvas(HDC dc, double scale);
    ~Canvas();

    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;

    HDC dc() const { return dc_; }
    double scale() const { return scale_; }
    int px(double v) const { return (int)(v * scale_ + 0.5); }

    // Filled rectangles.
    void fill(const RECT& r, COLORREF c);
    void fill(int x, int y, int w, int h, COLORREF c);

    // 1 px lines / outlines.
    void hline(int x, int y, int w, COLORREF c);
    void vline(int x, int y, int h, COLORREF c);
    void frame(const RECT& r, COLORREF c);

    // Win98 bevels (1 px hard edges, no gradients).
    void bevelRaised(const RECT& r);
    void bevelSunken(const RECT& r);
    void bevelField(const RECT& r);

    // Polygons in device pixels.
    void polygonFill(const POINT* pts, int n, COLORREF c);
    void polygonOutline(const POINT* pts, int n, COLORREF c);
    void line(int x1, int y1, int x2, int y2, COLORREF c);

    // Text.  y is the top of the line box, matching the AppKit draw(at:) origin.
    void text(const std::wstring& s, int x, int y, COLORREF c, bool bold = false);
    void textIn(const std::wstring& s, const RECT& r, COLORREF c, Align a, bool bold = false,
                bool wrap = false);
    // Text clipped to r; used for cells and tree labels.
    void textClip(const std::wstring& s, const RECT& r, COLORREF c, Align a, bool bold = false);
    int textWidth(const std::wstring& s, bool bold = false);
    int lineHeight(bool bold = false);

private:
    HFONT pick(bool bold);
    HDC dc_ = nullptr;
    double scale_ = 1.0;
    HFONT normal_ = nullptr;
    HFONT bold_ = nullptr;
    HGDIOBJ oldFont_ = nullptr;
    COLORREF oldText_ = 0;
    int oldBk_ = 0;
    int textHeight_ = 0;
};
