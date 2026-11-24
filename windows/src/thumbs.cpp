#include "thumbs.h"

#include <shlobj.h>
#include <shlwapi.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <cwctype>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>

#include "debug.h"

namespace thumbs {
namespace {

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

// The state the threads share.  Made once and never destroyed: the threads
// that use it are never joined, and would outlive a static destructor.
struct State {
    struct Job {
        std::wstring path, key;
        int px = 0;
        HWND notify = nullptr;
    };
    std::mutex mutex;
    std::condition_variable wake;
    std::unordered_map<std::wstring, std::shared_ptr<const Bitmap>> cache;  // by key
    std::unordered_map<std::wstring, ULONGLONG> used;                        // when last asked for
    std::deque<Job> queue;                                  // asked for, not started
    std::set<std::wstring> queued, inFlight, failed;        // by key
    ULONGLONG tick = 0;
    size_t bytes = 0;                                       // what the cache holds
    size_t requested = 0;                                   // thumbnails asked for since the start
};

State& state() {
    static State* s = new State;
    return *s;
}

constexpr size_t kBudget = 160u << 20;  // bytes of thumbnails kept

std::wstring keyOf(const std::wstring& path, int px) {
    return lower(path) + L"|" + std::to_wstring(px);
}

size_t bytesOf(const Bitmap& b) { return (size_t)b.w * b.h * 4; }

// What the shell gives (a thumbnail of any size, in any format) as a
// thumbnail of ours: 32 bits a pixel with the alpha premultiplied, and no
// larger than the box it was asked for - averaged down where it is larger,
// which keeps the shades (and a little of the memory).
Bitmap toThumbnail(HBITMAP from, int px) {
    Bitmap out;
    BITMAP info{};
    if (!GetObjectW(from, sizeof(info), &info) || info.bmWidth <= 0 || info.bmHeight <= 0)
        return out;
    const int w = info.bmWidth, h = info.bmHeight;
    BITMAPINFOHEADER header{};
    header.biSize = sizeof(header);
    header.biWidth = w;
    header.biHeight = -h;  // top-down
    header.biPlanes = 1;
    header.biBitCount = 32;
    std::vector<unsigned char> pixels((size_t)w * h * 4);
    HDC dc = CreateCompatibleDC(nullptr);
    GetDIBits(dc, from, 0, h, pixels.data(), (BITMAPINFO*)&header, DIB_RGB_COLORS);
    DeleteDC(dc);

    // No alpha anywhere is a picture without an alpha channel (it reads as
    // 0): opaque.  Colour beyond the alpha is alpha that is not premultiplied:
    // it is made so here.
    bool any = false, straight = false;
    for (size_t i = 0; i < pixels.size(); i += 4) {
        const unsigned char a = pixels[i + 3];
        if (a) any = true;
        if (pixels[i] > a || pixels[i + 1] > a || pixels[i + 2] > a) straight = true;
    }
    if (!any) {
        for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
    } else if (straight) {
        for (size_t i = 0; i < pixels.size(); i += 4) {
            const unsigned char a = pixels[i + 3];
            for (int c = 0; c < 3; ++c) pixels[i + c] = (unsigned char)(pixels[i + c] * a / 255);
        }
    }

    const double fit = std::min(1.0, std::min((double)px / w, (double)px / h));
    const int tw = std::max(1, (int)std::lround(w * fit)), th = std::max(1, (int)std::lround(h * fit));
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = tw;
    bmi.bmiHeader.biHeight = -th;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    out.handle = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!out.handle || !bits) {
        if (out.handle) DeleteObject(out.handle);
        out.handle = nullptr;
        return out;
    }
    if (tw == w && th == h) {
        memcpy(bits, pixels.data(), pixels.size());
    } else {
        // Each pixel of the thumbnail: the average of the piece of the
        // picture it stands for.
        std::vector<unsigned char> to((size_t)tw * th * 4);
        for (int ty = 0; ty < th; ++ty) {
            const double y0 = ty * (double)h / th, y1 = (ty + 1) * (double)h / th;
            for (int tx = 0; tx < tw; ++tx) {
                const double x0 = tx * (double)w / tw, x1 = (tx + 1) * (double)w / tw;
                unsigned sum[4] = {0, 0, 0, 0};
                size_t n = 0;
                for (int y = (int)y0; y < h && y < std::max((int)y0 + 1, (int)y1); ++y) {
                    for (int x = (int)x0; x < w && x < std::max((int)x0 + 1, (int)x1); ++x) {
                        const unsigned char* p = &pixels[((size_t)y * w + x) * 4];
                        for (int c = 0; c < 4; ++c) sum[c] += p[c];
                        ++n;
                    }
                }
                unsigned char* q = &to[((size_t)ty * tw + tx) * 4];
                for (int c = 0; c < 4; ++c) q[c] = (unsigned char)(sum[c] / std::max<size_t>(1, n));
            }
        }
        memcpy(bits, to.data(), to.size());
    }
    out.w = tw;
    out.h = th;
    return out;
}

// Asks the shell for the picture's thumbnail (it decodes the picture, the
// right way up, and keeps it in the cache it shares with Explorer).
std::shared_ptr<const Bitmap> make(const std::wstring& path, int px) {
    IShellItemImageFactory* factory = nullptr;
    if (FAILED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory))))
        return nullptr;
    HBITMAP from = nullptr;
    SIZE box{px, px};
    const HRESULT hr = factory->GetImage(box, SIIGBF_THUMBNAILONLY | SIIGBF_BIGGERSIZEOK, &from);
    factory->Release();
    if (FAILED(hr) || !from) {
        diagLog(L"thumbnail: none for %ls (0x%08lX)", path.c_str(), (unsigned long)hr);
        return nullptr;
    }
    auto made = std::make_shared<Bitmap>(toThumbnail(from, px));
    DeleteObject(from);
    if (!made->handle) return nullptr;
    return made;
}

// Within the budget again: the thumbnails longest not asked for go first.
void keepWithinBudget(State& s) {
    if (s.bytes <= kBudget) return;
    std::vector<std::pair<ULONGLONG, const std::wstring*>> order;
    for (const auto& kv : s.used) order.emplace_back(kv.second, &kv.first);
    std::sort(order.begin(), order.end());
    for (const auto& kv : order) {
        if (s.bytes <= kBudget / 2) break;
        const auto it = s.cache.find(*kv.second);
        if (it == s.cache.end()) continue;
        s.bytes -= bytesOf(*it->second);
        s.cache.erase(it);
        s.used.erase(*kv.second);
    }
}

void worker() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
    State& s = state();
    for (;;) {
        State::Job job;
        {
            std::unique_lock<std::mutex> lock(s.mutex);
            s.wake.wait(lock, [&] { return !s.queue.empty(); });
            job = std::move(s.queue.front());
            s.queue.pop_front();
            s.queued.erase(job.key);
            s.inFlight.insert(job.key);
        }
        const std::shared_ptr<const Bitmap> made = make(job.path, job.px);
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if (made) {
                s.cache[job.key] = made;
                s.used[job.key] = ++s.tick;
                s.bytes += bytesOf(*made);
                keepWithinBudget(s);
            } else {
                // Not asked for again until the folder is listed again
                // (forgetFailed).
                s.failed.insert(job.key);
            }
            s.inFlight.erase(job.key);
        }
        // The window draws it when it gets to this; nothing waits for that.
        if (made && job.notify) PostMessageW(job.notify, kReadyMessage, 0, 0);
    }
}

void startWorkers() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Four at a time: a screen of thumbnails comes in four times as fast.
        for (int i = 0; i < 4; ++i) std::thread(worker).detach();
    });
}

}  // namespace

bool isPicture(const std::wstring& name) {
    const std::wstring ext = lower(PathFindExtensionW(name.c_str()));
    static const wchar_t* const pictures[] = {L".png",  L".jpg",  L".jpeg", L".jfif", L".gif",
                                              L".bmp",  L".webp", L".tif",  L".tiff", L".heic",
                                              L".heif", L".avif", L".ico",  L".jxl"};
    for (const wchar_t* p : pictures)
        if (ext == p) return true;
    return false;
}

std::shared_ptr<const Bitmap> peek(const std::wstring& path, int px) {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    const auto it = s.cache.find(keyOf(path, px));
    return it == s.cache.end() ? nullptr : it->second;
}

std::shared_ptr<const Bitmap> get(const std::wstring& path, int px, HWND notify) {
    State& s = state();
    const std::wstring key = keyOf(path, px);
    {
        std::lock_guard<std::mutex> lock(s.mutex);
        const auto it = s.cache.find(key);
        if (it != s.cache.end()) {
            s.used[key] = ++s.tick;
            return it->second;
        }
        if (s.failed.count(key) || s.queued.count(key) || s.inFlight.count(key)) return nullptr;
        s.queued.insert(key);
        ++s.requested;
        s.queue.push_back(State::Job{path, key, px, notify});
        s.wake.notify_one();
    }
    startWorkers();
    return nullptr;
}

size_t requested() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.requested;
}

void dropPending() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.queue.clear();
    s.queued.clear();
}

void forgetFailed() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    s.failed.clear();
}

size_t pending() {
    State& s = state();
    std::lock_guard<std::mutex> lock(s.mutex);
    return s.queue.size() + s.inFlight.size();
}

}  // namespace thumbs
