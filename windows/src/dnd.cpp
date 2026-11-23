#include "dnd.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <vector>

#include "app.h"
#include "debug.h"
#include "fs.h"

namespace dnd {
namespace {

// Minimal IDataObject carrying CF_HDROP plus the allowed drop effect.
class HdropData : public IDataObject {
public:
    explicit HdropData(std::vector<std::wstring> paths) : paths_(std::move(paths)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) {
            *ppv = static_cast<IDataObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE GetData(FORMATETC* fmt, STGMEDIUM* stg) override {
        if (!fmt || !stg) return E_INVALIDARG;
        if (fmt->cfFormat == CF_HDROP && (fmt->tymed & TYMED_HGLOBAL)) {
            HGLOBAL mem = fsx::makeHdropData(paths_);
            if (!mem) return E_OUTOFMEMORY;
            stg->tymed = TYMED_HGLOBAL;
            stg->hGlobal = mem;
            stg->pUnkForRelease = nullptr;
            return S_OK;
        }
        if (fmt->cfFormat == fsx::preferredDropEffectFormat() && (fmt->tymed & TYMED_HGLOBAL)) {
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (!mem) return E_OUTOFMEMORY;
            auto* effect = (DWORD*)GlobalLock(mem);
            if (!effect) {
                GlobalFree(mem);
                return E_OUTOFMEMORY;
            }
            *effect = DROPEFFECT_COPY | DROPEFFECT_MOVE;
            GlobalUnlock(mem);
            stg->tymed = TYMED_HGLOBAL;
            stg->hGlobal = mem;
            stg->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* fmt) override {
        if (!fmt) return E_INVALIDARG;
        if (fmt->cfFormat == CF_HDROP && (fmt->tymed & TYMED_HGLOBAL)) return S_OK;
        if (fmt->cfFormat == fsx::preferredDropEffectFormat() && (fmt->tymed & TYMED_HGLOBAL))
            return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
        if (out) out->ptd = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override {
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** out) override {
        if (direction != DATADIR_GET || !out) return E_NOTIMPL;
        FORMATETC formats[2] = {
            {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
            {(CLIPFORMAT)fsx::preferredDropEffectFormat(), nullptr, DVASPECT_CONTENT, -1,
             TYMED_HGLOBAL},
        };
        return SHCreateStdEnumFmtEtc(2, formats, out);
    }
    HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }
    HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override {
        return OLE_E_ADVISENOTSUPPORTED;
    }

private:
    LONG ref_ = 1;
    std::vector<std::wstring> paths_;
};

class DragSource : public IDropSource {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) {
            *ppv = static_cast<IDropSource*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape, DWORD keys) override {
        if (escape) return DRAGDROP_S_CANCEL;
        if (!(keys & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
        return DRAGDROP_S_USEDEFAULTCURSORS;
    }

private:
    LONG ref_ = 1;
};

std::vector<std::wstring> pathsFromDataObject(IDataObject* data) {
    std::vector<std::wstring> out;
    if (!data) return out;
    FORMATETC fmt{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM stg{};
    if (FAILED(data->GetData(&fmt, &stg))) return out;
    HDROP drop = (HDROP)GlobalLock(stg.hGlobal);
    if (drop) {
        out = fsx::pathsFromHdrop(drop);
        GlobalUnlock(stg.hGlobal);
    }
    ReleaseStgMedium(&stg);
    return out;
}

class DropTargetImpl : public IDropTarget {
public:
    DropTargetImpl(HWND hwnd, App* app) : hwnd_(hwnd), app_(app) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropTarget) {
            *ppv = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD keys, POINTL pt,
                                        DWORD* effect) override {
        diagLog(L"dragEnter screen=%d,%d", (int)pt.x, (int)pt.y);
        data_ = data;
        if (data_) data_->AddRef();
        paths_ = pathsFromDataObject(data);
        return DragOver(keys, pt, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        POINT p{pt.x, pt.y};
        ScreenToClient(hwnd_, &p);
        std::wstring target;
        bool isTree = false;
        int row = -1;
        computeTarget(p, target, isTree, row);
        // Repaint only when the highlighted drop target changes: invalidating
        // on every mouse move made dragging in large folders crawl.
        const bool changed = target != app_->dragTargetFolder ||
                             isTree != app_->dragTargetIsTree || row != app_->dragTargetRow;
        app_->dragTargetFolder = target;
        app_->dragTargetIsTree = isTree;
        app_->dragTargetRow = row;

        DWORD allowed = effect ? *effect : (DROPEFFECT_COPY | DROPEFFECT_MOVE);
        DWORD want;
        if (keys & MK_CONTROL) want = DROPEFFECT_COPY;
        else if (keys & MK_SHIFT) want = DROPEFFECT_MOVE;
        else {
            // The same one rule performDrop applies: same volume moves,
            // different volumes copy - cursor and action can't disagree.
            const std::wstring& dest = target.empty() ? app_->current : target;
            want = fsx::dropShouldMove(paths_, dest) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        }
        if (!(allowed & want))
            want = (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
        // Nothing to do when the files are already in the folder under the
        // pointer: show the "no drop" cursor instead of a move cursor.
        if (target.empty() && allFromCurrent()) want = DROPEFFECT_NONE;
        if (effect) *effect = want;
        if (changed) {
            app_->paintDirty = true;
            InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        clearHighlight();
        if (data_) {  // DragEnter took a reference; a leave without a drop must give it back
            data_->Release();
            data_ = nullptr;
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD keys, POINTL pt,
                                   DWORD* effect) override {
        (void)keys;
        POINT p{pt.x, pt.y};
        ScreenToClient(hwnd_, &p);
        std::wstring target;
        bool isTree = false;
        int row = -1;
        computeTarget(p, target, isTree, row);
        clearHighlight();
        std::vector<std::wstring> paths = pathsFromDataObject(data);
        diagLog(L"drop target=%ls paths=%d", target.c_str(), (int)paths.size());
        DWORD performed = DROPEFFECT_NONE;
        if (!paths.empty() && app_->performDrop(paths, target)) {
            const std::wstring& dest = target.empty() ? app_->current : target;
            performed = fsx::dropShouldMove(paths, dest) ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        }
        if (effect) *effect = performed;
        if (data_) {
            data_->Release();
            data_ = nullptr;
        }
        return S_OK;
    }

private:
    bool allFromCurrent() const {
        if (paths_.empty()) return false;
        for (auto& p : paths_)
            if (_wcsicmp(fsx::parentPath(p).c_str(), app_->current.c_str()) != 0) return false;
        return true;
    }

    void clearHighlight() {
        app_->dragTargetFolder.clear();
        app_->dragTargetIsTree = false;
        app_->dragTargetRow = -1;
        paths_.clear();
        app_->paintDirty = true;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void computeTarget(POINT p, std::wstring& target, bool& isTree, int& row) {
        target.clear();
        isTree = false;
        row = -1;
        if (app_->sidebarVisible && PtInRect(&app_->lay.sidebar, p)) {
            int r = app_->treeRowAt(p);
            if (r >= 0) {
                TreeNode* n = app_->treeFlat[r];
                if (!n->path.empty()) {
                    target = n->path;
                    isTree = true;
                    row = r;
                }
            }
            return;
        }
        if (!PtInRect(&app_->lay.content, p)) return;
        // Highlight a folder only under its icon or name - the seam between
        // entries is the current folder, the same hit rule as clicks.
        int idx = -1;
        if (app_->hitTestItem(p, &idx) && app_->entries[idx].isDir)
            target = app_->entries[idx].path;
    }

    LONG ref_ = 1;
    HWND hwnd_ = nullptr;
    App* app_ = nullptr;
    IDataObject* data_ = nullptr;
    std::vector<std::wstring> paths_;
};

DropTargetImpl* g_target = nullptr;

}  // namespace

void registerTarget(HWND hwnd, App* app) {
    if (g_target) return;
    g_target = new DropTargetImpl(hwnd, app);
    HRESULT hr = RegisterDragDrop(hwnd, g_target);
    if (FAILED(hr)) {
        diagLog(L"dnd: RegisterDragDrop failed 0x%08lX", (unsigned long)hr);
        g_target->Release();
        g_target = nullptr;
    }
}

void revokeTarget(HWND hwnd) {
    if (!g_target) return;
    RevokeDragDrop(hwnd);
    g_target->Release();
    g_target = nullptr;
}

bool dragOut(HWND hwnd, const std::vector<std::wstring>& paths, DWORD* effect) {
    (void)hwnd;  // the OLE drag runs its own loop; no window handle needed
    if (paths.empty()) return false;
    auto* data = new HdropData(paths);
    auto* source = new DragSource();

    // A small drag image: the first item's icon and name.
    HBITMAP dragImage = nullptr;
    SHDRAGIMAGE shdi{};
    if (paths.size() >= 1) {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        HGDIOBJ oldFont = SelectObject(mem, font);
        std::wstring label = fsx::fileName(paths.front());
        if (paths.size() > 1) label += L"  +" + std::to_wstring(paths.size() - 1);
        SIZE textSize{};
        GetTextExtentPoint32W(mem, label.c_str(), (int)label.size(), &textSize);
        const int width = std::max(96, (int)(46 + textSize.cx + 10));
        const int height = std::max(44, (int)(textSize.cy + 16));

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        dragImage = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dragImage) {
            HGDIOBJ oldBmp = SelectObject(mem, dragImage);
            RECT all{0, 0, width, height};
            HBRUSH key = CreateSolidBrush(RGB(255, 0, 255));
            FillRect(mem, &all, key);
            DeleteObject(key);

            SHFILEINFOW sfi{};
            if (SHGetFileInfoW(paths.front().c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)) {
                DrawIconEx(mem, 6, (height - 32) / 2, sfi.hIcon, 32, 32, 0, nullptr, DI_NORMAL);
                DestroyIcon(sfi.hIcon);
            }
            SetBkMode(mem, TRANSPARENT);
            SetTextColor(mem, RGB(0, 0, 0));
            SetTextAlign(mem, TA_LEFT | TA_TOP);
            TextOutW(mem, 44, (height - textSize.cy) / 2, label.c_str(), (int)label.size());
            SelectObject(mem, oldBmp);
            shdi.hbmpDragImage = dragImage;
            shdi.crColorKey = RGB(255, 0, 255);
            shdi.sizeDragImage = SIZE{width, height};
            shdi.ptOffset = POINT{22, height / 2};
        }
        SelectObject(mem, oldFont);
        DeleteObject(font);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);

        if (dragImage) {
            IDragSourceHelper* helper = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_DragDropHelper, nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&helper))) &&
                helper) {
                helper->InitializeFromBitmap(&shdi, data);
                helper->Release();
            }
        }
    }

    DWORD performed = DROPEFFECT_NONE;
    HRESULT hr = DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &performed);
    if (dragImage) DeleteObject(dragImage);
    data->Release();
    source->Release();
    if (effect) *effect = performed;
    diagLog(L"dragOut done hr=0x%08lX effect=%lu", (unsigned long)hr, (unsigned long)performed);
    return SUCCEEDED(hr) && performed != DROPEFFECT_NONE;
}

}  // namespace dnd
