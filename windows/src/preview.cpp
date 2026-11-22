#include "preview.h"

#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "canvas.h"
#include "fs.h"
#include "icons.h"
#include "theme.h"

namespace preview {
namespace {

constexpr wchar_t kClass[] = L"MyOldComputerPreview";

struct State {
    HWND hwnd = nullptr;
    HWND owner = nullptr;
    std::wstring path;
    std::wstring title;
    std::wstring info;
    std::wstring text;
    HBITMAP bitmap = nullptr;
    int imgW = 0;
    int imgH = 0;
    bool loaded = false;
    double scale = 1.0;
    bool pressedClose = false;
};

State g;
WNDPROC g_oldProc = nullptr;

bool isImageExt(const std::wstring& ext) {
    static const wchar_t* list[] = {L"png", L"jpg", L"jpeg", L"gif", L"bmp", L"tif", L"tiff",
                                    L"webp", L"ico", L"jfif", L"avif"};
    for (auto* e : list)
        if (ext == e) return true;
    return false;
}

bool isTextExt(const std::wstring& ext) {
    static const wchar_t* list[] = {L"txt", L"md", L"markdown", L"log", L"json", L"xml", L"ini",
                                    L"cfg", L"conf", L"csv", L"yml", L"yaml", L"toml", L"c",
                                    L"h", L"cpp", L"hpp", L"cs", L"js", L"ts", L"css", L"html",
                                    L"htm", L"py", L"sh", L"bat", L"cmd", L"rs", L"go", L"java",
                                    L"swift", L"nfo", L"tex"};
    for (auto* e : list)
        if (ext == e) return true;
    return false;
}

HBITMAP decodeImage(const std::wstring& path, int maxW, int maxH, int* outW, int* outH) {
    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return nullptr;
    HBITMAP result = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* converter = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    do {
        if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand, &decoder)))
            break;
        if (FAILED(decoder->GetFrame(0, &frame))) break;
        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        IWICBitmapSource* source = frame;
        double ratio = 1.0;
        if (w > (UINT)maxW || h > (UINT)maxH)
            ratio = std::min((double)maxW / w, (double)maxH / h);
        UINT tw = (UINT)std::max(1.0, w * ratio), th = (UINT)std::max(1.0, h * ratio);
        if (tw != w || th != h) {
            if (FAILED(factory->CreateBitmapScaler(&scaler))) break;
            if (FAILED(scaler->Initialize(frame, tw, th, WICBitmapInterpolationModeFant)))
                break;
            source = scaler;
        }
        if (FAILED(factory->CreateFormatConverter(&converter))) break;
        if (FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppBGR,
                                         WICBitmapDitherTypeNone, nullptr, 0.0,
                                         WICBitmapPaletteTypeCustom)))
            break;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = (LONG)tw;
        bmi.bmiHeader.biHeight = -(LONG)th;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HDC screen = GetDC(nullptr);
        result = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!result || !bits) {
            if (result) DeleteObject(result);
            result = nullptr;
            break;
        }
        UINT stride = tw * 4;
        if (FAILED(converter->CopyPixels(nullptr, stride, stride * th, (BYTE*)bits))) {
            DeleteObject(result);
            result = nullptr;
            break;
        }
        *outW = (int)tw;
        *outH = (int)th;
    } while (false);
    if (scaler) scaler->Release();
    if (converter) converter->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    factory->Release();
    return result;
}

std::wstring readText(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"";
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    DWORD toRead = (DWORD)std::min<long long>(size.QuadPart, 128 * 1024);
    std::vector<char> data(toRead + 1, 0);
    DWORD read = 0;
    ReadFile(h, data.data(), toRead, &read, nullptr);
    CloseHandle(h);
    data.resize(read);
    if (data.empty()) return L"";
    if (std::find(data.begin(), data.end(), '\0') != data.end()) return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), nullptr, 0);
    std::wstring wide;
    if (wlen > 0) {
        wide.resize(wlen);
        MultiByteToWideChar(CP_UTF8, 0, data.data(), (int)data.size(), &wide[0], wlen);
    }
    if (wide.empty()) {
        wlen = MultiByteToWideChar(CP_ACP, 0, data.data(), (int)data.size(), nullptr, 0);
        wide.resize(wlen);
        MultiByteToWideChar(CP_ACP, 0, data.data(), (int)data.size(), &wide[0], wlen);
    }
    // Tabs to four spaces for stable layout.
    std::wstring out;
    out.reserve(wide.size());
    for (wchar_t c : wide) {
        if (c == L'\r') continue;
        if (c == L'\t') {
            out += L"    ";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

void loadContent(int viewW, int viewH) {
    g.text.clear();
    g.info.clear();
    if (g.bitmap) {
        DeleteObject(g.bitmap);
        g.bitmap = nullptr;
    }
    std::wstring ext = fsx::extensionLower(fsx::fileName(g.path));
    if (isImageExt(ext)) {
        g.bitmap = decodeImage(g.path, std::max(64, viewW), std::max(64, viewH), &g.imgW, &g.imgH);
    }
    if (!g.bitmap) {
        if (isTextExt(ext)) {
            g.text = readText(g.path);
        }
        if (g.text.empty()) {
            const bool isDir = fsx::isDirectory(g.path);
            std::wstring info = fsx::fileName(g.path);
            info += L"\n\n";
            info += isDir ? L"文件夹" : fsx::typeName(g.path, false);
            info += L"\n";
            WIN32_FILE_ATTRIBUTE_DATA fad{};
            if (GetFileAttributesExW(g.path.c_str(), GetFileExInfoStandard, &fad)) {
                if (!isDir) {
                    unsigned long long size = ((unsigned long long)fad.nFileSizeHigh << 32) |
                                              fad.nFileSizeLow;
                    info += fsx::formatSize(size) + L"\n";
                }
                info += L"修改时间：" + fsx::formatDate(fad.ftLastWriteTime);
            }
            g.info = info;
        }
    }
}

void layout() {
    if (!g.loaded && g.hwnd) {
        RECT rc;
        GetClientRect(g.hwnd, &rc);
        loadContent((int)((rc.right - rc.left) / g.scale) - 40,
                    (int)((rc.bottom - rc.top) / g.scale) - 100);
        g.loaded = true;
    }
}

void draw(Canvas& c, HWND hwnd) {
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
    c.text(g.title, bar.left + c.px(4), bar.top + c.px(3), Theme::white, true);

    RECT close{rc.right - c.px(20), c.px(4), rc.right - c.px(4), c.px(20)};
    c.fill(close, Theme::face);
    if (g.pressedClose) c.bevelSunken(close);
    else c.bevelRaised(close);
    c.line(close.left + c.px(4), close.top + c.px(4), close.right - c.px(4), close.bottom - c.px(4),
           Theme::dark);
    c.line(close.right - c.px(4), close.top + c.px(4), close.left + c.px(4), close.bottom - c.px(4),
           Theme::dark);

    RECT area{c.px(3), bar.bottom + c.px(3), rc.right - c.px(3), rc.bottom - c.px(3)};
    c.fill(area, Theme::white);
    c.bevelSunken(area);

    RECT inner{area.left + c.px(2), area.top + c.px(2), area.right - c.px(2),
               area.bottom - c.px(2)};
    if (g.bitmap) {
        int availW = inner.right - inner.left, availH = inner.bottom - inner.top;
        double ratio = std::min(1.0, std::min((double)availW / g.imgW, (double)availH / g.imgH));
        int dw = (int)(g.imgW * ratio), dh = (int)(g.imgH * ratio);
        int dx = inner.left + (availW - dw) / 2, dy = inner.top + (availH - dh) / 2;
        HDC mem = CreateCompatibleDC(c.dc());
        HGDIOBJ old = SelectObject(mem, g.bitmap);
        SetStretchBltMode(c.dc(), HALFTONE);
        StretchBlt(c.dc(), dx, dy, dw, dh, mem, 0, 0, g.imgW, g.imgH, SRCCOPY);
        SelectObject(mem, old);
        DeleteDC(mem);
    } else if (!g.text.empty()) {
        c.textIn(g.text, RECT{inner.left + c.px(4), inner.top + c.px(4), inner.right - c.px(4),
                              inner.bottom - c.px(4)},
                 Theme::text, Align::Left, false, true);
    } else {
        const int s = c.px(48);
        RECT iconRect{inner.left + c.px(16), inner.top + c.px(20), inner.left + c.px(16) + s,
                      inner.top + c.px(20) + s};
        if (fsx::isDirectory(g.path)) Icon::folder(c, iconRect);
        else Icon::file(c, iconRect, fsx::extensionLower(fsx::fileName(g.path)));
        c.textIn(g.info,
                 RECT{iconRect.right + c.px(16), inner.top + c.px(20), inner.right - c.px(8),
                      inner.bottom - c.px(8)},
                 Theme::text, Align::Left, false, true);
    }
}

LRESULT CALLBACK previewProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        Canvas c(dc, g.scale);
        draw(c, hwnd);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_PRINTCLIENT: {
        Canvas c((HDC)wp, g.scale);
        draw(c, hwnd);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE || wp == VK_SPACE) {
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT close{rc.right - (int)(20 * g.scale), (int)(4 * g.scale), rc.right - (int)(4 * g.scale),
                   (int)(20 * g.scale)};
        if (PtInRect(&close, p)) {
            g.pressedClose = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        RECT rc;
        GetClientRect(hwnd, &rc);
        RECT close{rc.right - (int)(20 * g.scale), (int)(4 * g.scale), rc.right - (int)(4 * g.scale),
                   (int)(20 * g.scale)};
        bool wasPressed = g.pressedClose;
        g.pressedClose = false;
        if (wasPressed && PtInRect(&close, p)) DestroyWindow(hwnd);
        else InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_NCHITTEST: {
        LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
        if (hit == HTCLIENT) {
            POINT p{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &p);
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (p.y < (int)(26 * g.scale) && p.x < rc.right - (int)(24 * g.scale))
                return HTCAPTION;
        }
        return hit;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g.hwnd = nullptr;
        if (g.bitmap) {
            DeleteObject(g.bitmap);
            g.bitmap = nullptr;
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ensureClass() {
    static bool done = false;
    if (done) return;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = previewProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);
    done = true;
}

}  // namespace

void show(HWND owner, const std::wstring& path) {
    ensureClass();
    if (g.hwnd) {
        DestroyWindow(g.hwnd);
        g.hwnd = nullptr;
    }
    g.owner = owner;
    g.path = path;
    g.title = L"快速查看 - " + fsx::fileName(path);
    g.loaded = false;
    g.pressedClose = false;

    HDC screen = GetDC(nullptr);
    typedef UINT(WINAPI * GetDpiForWindowFn)(HWND);
    static auto getDpi = (GetDpiForWindowFn)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                           "GetDpiForWindow");
    int dpi = 96;
    if (getDpi && owner) dpi = (int)getDpi(owner);
    g.scale = dpi / 96.0;
    ReleaseDC(nullptr, screen);

    RECT ownerRect{0, 0, 1280, 800};
    if (owner) GetWindowRect(owner, &ownerRect);
    int w = (int)(720 * g.scale), h = (int)(520 * g.scale);
    int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - w) / 2;
    int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - h) / 2;
    g.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, g.title.c_str(), WS_POPUP, x, y, w, h,
                             owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g.hwnd) return;
    loadContent(w - (int)(40 * g.scale), h - (int)(100 * g.scale));
    g.loaded = true;
    ShowWindow(g.hwnd, SW_SHOW);
    SetForegroundWindow(g.hwnd);
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

void hide() {
    if (g.hwnd) DestroyWindow(g.hwnd);
    g.hwnd = nullptr;
}

bool visible() { return g.hwnd != nullptr; }

}  // namespace preview
