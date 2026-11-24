#include "snapshot.h"

#include <wincodec.h>

#include <string>
#include <cstring>
#include <vector>

#include "app.h"
#include "canvas.h"
#include "render.h"

namespace {

bool savePng(const std::wstring& path, const BYTE* bits, int w, int h, int stride) {
    IWICImagingFactory* factory = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return false;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* encoder = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = false;
    do {
        if (FAILED(factory->CreateStream(&stream))) break;
        if (FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE))) break;
        if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) break;
        if (FAILED(encoder->Initialize(stream, WICBitmapEncoderNoCache))) break;
        if (FAILED(encoder->CreateNewFrame(&frame, nullptr))) break;
        if (FAILED(frame->Initialize(nullptr))) break;
        if (FAILED(frame->SetSize(w, h))) break;
        WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
        if (FAILED(frame->SetPixelFormat(&format))) break;
        if (FAILED(frame->WritePixels(h, stride, stride * h, (BYTE*)bits))) break;
        if (FAILED(frame->Commit())) break;
        if (FAILED(encoder->Commit())) break;
        ok = true;
    } while (false);
    if (frame) frame->Release();
    if (encoder) encoder->Release();
    if (stream) stream->Release();
    factory->Release();
    return ok;
}

}  // namespace

bool writePng(const std::wstring& path, const void* bgrx, int w, int h) {
    // GDI leaves the unused byte at zero; PNG consumers take it for alpha.
    const size_t stride = (size_t)w * 4;
    std::vector<BYTE> pixels(stride * h);
    memcpy(pixels.data(), bgrx, pixels.size());
    for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    return savePng(path, pixels.data(), w, h, (int)stride);
}

bool renderSnapshot(App& app, const std::wstring& path, int width, int height) {
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!mem || !bmp || !bits) {
        if (mem) DeleteDC(mem);
        if (bmp) DeleteObject(bmp);
        return false;
    }
    HGDIOBJ old = SelectObject(mem, bmp);

    app.relayout(width, height);
    {
        Canvas canvas(mem, app.scale);
        renderApp(canvas, app);
    }

    SelectObject(mem, old);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool ok = writePng(path, bits, width, height);
    // The DIB section must stay alive until the pixels have been read out.
    DeleteObject(bmp);
    DeleteDC(mem);
    return ok;
}
