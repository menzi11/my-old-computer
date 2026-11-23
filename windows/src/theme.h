// My Old Computer - Windows 98 file manager, native Win32 port.
// Palette and metrics are shared with the macOS implementation; see docs/ui-spec.md.
#pragma once

#include <windows.h>

namespace Theme {
// Windows 98 16-colour palette.  Every colour below matches Sources/Theme.swift.
constexpr COLORREF face       = RGB(0xC0, 0xC0, 0xC0);
constexpr COLORREF faceHi     = RGB(0xDF, 0xDF, 0xDF);
constexpr COLORREF white      = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF shadow     = RGB(0x80, 0x80, 0x80);
constexpr COLORREF dark       = RGB(0x00, 0x00, 0x00);
constexpr COLORREF navy       = RGB(0x00, 0x00, 0x80);
constexpr COLORREF navyHi     = RGB(0x10, 0x84, 0xD0);
constexpr COLORREF text       = RGB(0x00, 0x00, 0x00);
constexpr COLORREF dim        = RGB(0x80, 0x80, 0x80);
constexpr COLORREF select     = RGB(0x00, 0x00, 0x80);
constexpr COLORREF green      = RGB(0x00, 0x80, 0x00);

constexpr COLORREF folderTop  = RGB(0xFF, 0xFA, 0x66);
constexpr COLORREF folderBody = RGB(0xFA, 0xD9, 0x1A);
constexpr COLORREF folderEdge = RGB(0x99, 0x80, 0x00);

constexpr COLORREF docBlue    = RGB(0x21, 0x4A, 0xA6);
constexpr COLORREF sheetGreen = RGB(0x1A, 0x6B, 0x2B);
constexpr COLORREF slideOrange= RGB(0xC7, 0x66, 0x0D);
constexpr COLORREF codeTeal   = RGB(0x0D, 0x6B, 0x73);
constexpr COLORREF dataPurple = RGB(0x6B, 0x2E, 0x94);
constexpr COLORREF designPink = RGB(0xB3, 0x2E, 0x85);
constexpr COLORREF imageBlue  = RGB(0x33, 0x66, 0xD9);
constexpr COLORREF webBlue    = RGB(0x33, 0x59, 0xCC);
constexpr COLORREF pdfRed     = RGB(0xBF, 0x1A, 0x1A);
}  // namespace Theme

// Every measurement is expressed in the same units as the macOS app and
// multiplied by the current UI scale (DPI / 96).
struct Metrics {
    int edge;        // 3
    int titleH;      // 22
    int menuH;       // 20
    int toolH;       // 32
    int addrH;       // 26
    int statusH;     // 20
    int sidebarW;    // 200
    int rowH;        // 19
    int headerH;     // 20
    int scrollerW;   // 16
    int arrowH;      // 16
    int splitterW;   // 5
    int minW;        // 700
    int minH;        // 440
};

inline Metrics metricsFor(double scale) {
    auto px = [scale](double v) { return (int)(v * scale + 0.5); };
    Metrics m{};
    m.edge      = px(3);
    m.titleH    = px(22);
    m.menuH     = px(20);
    m.toolH     = px(32);
    m.addrH     = px(26);
    m.statusH   = px(20);
    m.sidebarW  = px(200);
    m.rowH      = px(19);
    m.headerH   = px(20);
    m.scrollerW = px(16);
    m.arrowH    = px(16);
    m.splitterW = px(5);
    m.minW      = px(700);
    m.minH      = px(440);
    return m;
}

// DPI scale of the window's monitor (96 dpi == 1.0).  GetDpiForWindow is
// resolved at runtime so the exe still starts on early Windows 10 builds;
// every dialog and popup that needs it goes through this one helper.
inline double dpiScaleOfWindow(HWND hwnd) {
    if (!hwnd) return 1.0;
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static auto getDpi =
        (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (getDpi) {
        UINT dpi = getDpi(hwnd);
        if (dpi >= 96) return dpi / 96.0;
    }
    return 1.0;
}
