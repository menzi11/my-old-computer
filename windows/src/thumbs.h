// Thumbnails of pictures for the file view: the big-icon view of a folder
// that is mostly pictures shows each picture itself in place of its icon.
//
// Making a thumbnail is work (a picture is decoded), so none of it is done on
// the window's thread.  A thumbnail that is not there yet is asked for, and
// its cell shows the ordinary icon until it arrives; then the window that
// asked is told (kReadyMessage) and draws it.  What was made once stays in
// memory for the run, within a budget; the oldest go first.
#pragma once

#include <windows.h>

#include <memory>
#include <string>

namespace thumbs {

// A thumbnail: no larger than the box it was asked for, 32 bits a pixel,
// alpha ready for AlphaBlend (premultiplied).
struct Bitmap {
    HBITMAP handle = nullptr;
    int w = 0, h = 0;
    Bitmap() = default;
    Bitmap(const Bitmap&) = delete;
    Bitmap& operator=(const Bitmap&) = delete;
    Bitmap(Bitmap&& from) noexcept : handle(from.handle), w(from.w), h(from.h) {
        from.handle = nullptr;
    }
    Bitmap& operator=(Bitmap&& from) noexcept {
        if (this != &from) {
            if (handle) DeleteObject(handle);
            handle = from.handle;
            w = from.w;
            h = from.h;
            from.handle = nullptr;
        }
        return *this;
    }
    ~Bitmap() {
        if (handle) DeleteObject(handle);
    }
};

constexpr UINT kReadyMessage = WM_APP + 7;

// Whether thumbnails are made of a file with that name (by its extension).
bool isPicture(const std::wstring& name);

// The thumbnail of a picture, to fit in a box of `px` pixels: it at once
// when it was made before; else empty - and it is being made, and `notify`
// gets kReadyMessage when it is in (nothing if it cannot be made).
std::shared_ptr<const Bitmap> get(const std::wstring& path, int px, HWND notify);
// The same, without asking for it.
std::shared_ptr<const Bitmap> peek(const std::wstring& path, int px);
// Asked for but not started yet is not made after all: the view scrolled,
// and those cells are gone (they ask again if they come back).
void dropPending();
// Thumbnails that could not be made are asked for again.
void forgetFailed();
// How many are asked for and not made yet (the tests).
size_t pending();
// How many thumbnails were asked for since the start (the tests: only
// pictures are).
size_t requested();

}  // namespace thumbs
