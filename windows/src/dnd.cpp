#include "dnd.h"

#include <shlobj.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlwapi.h>

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
            std::wstring list;
            for (auto& p : paths_) {
                list += p;
                list.push_back(L'\0');
            }
            list.push_back(L'\0');
            const SIZE_T bytes = sizeof(DROPFILES) + (list.size() + 1) * sizeof(wchar_t);
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!mem) return E_OUTOFMEMORY;
            auto* drop = (DROPFILES*)GlobalLock(mem);
            if (!drop) {
                GlobalFree(mem);
                return E_OUTOFMEMORY;
            }
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            memcpy((char*)drop + sizeof(DROPFILES), list.c_str(),
                   (list.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(mem);
            stg->tymed = TYMED_HGLOBAL;
            stg->hGlobal = mem;
            stg->pUnkForRelease = nullptr;
            return S_OK;
        }
        if (fmt->cfFormat == preferredDropEffect_ && (fmt->tymed & TYMED_HGLOBAL)) {
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (!mem) return E_OUTOFMEMORY;
            *(DWORD*)GlobalLock(mem) = DROPEFFECT_COPY | DROPEFFECT_MOVE;
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
        if (fmt->cfFormat == preferredDropEffect_ && (fmt->tymed & TYMED_HGLOBAL)) return S_OK;
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
            {preferredDropEffect_, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
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
    static CLIPFORMAT preferredDropEffect_;
};

CLIPFORMAT HdropData::preferredDropEffect_ =
    (CLIPFORMAT)RegisterClipboardFormatW(L"Preferred DropEffect");

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
        UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < count; ++i) {
            wchar_t buf[32768];
            if (DragQueryFileW(drop, i, buf, 32768)) out.push_back(buf);
        }
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
        return DragOver(keys, pt, effect);
    }

    HRESULT STDMETHODCALLTYPE DragOver(DWORD keys, POINTL pt, DWORD* effect) override {
        POINT p{pt.x, pt.y};
        ScreenToClient(hwnd_, &p);
        std::wstring target;
        bool isTree = false;
        int row = -1;
        computeTarget(p, target, isTree, row);
        app_->dragTargetFolder = target;
        app_->dragTargetIsTree = isTree;
        app_->dragTargetRow = row;

        DWORD allowed = effect ? *effect : (DROPEFFECT_COPY | DROPEFFECT_MOVE);
        DWORD want = DROPEFFECT_MOVE;
        if (keys & MK_CONTROL) want = DROPEFFECT_COPY;
        else if (keys & MK_SHIFT) want = DROPEFFECT_MOVE;
        else if (!target.empty() && !fsx::sameVolume(target, app_->current))
            want = DROPEFFECT_COPY;
        if (!(allowed & want))
            want = (allowed & DROPEFFECT_COPY) ? DROPEFFECT_COPY : DROPEFFECT_MOVE;
        if (effect) *effect = want;
        InvalidateRect(hwnd_, nullptr, FALSE);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DragLeave() override {
        clearHighlight();
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
            const bool moved = target.empty() || fsx::sameVolume(paths.front(), target);
            performed = moved ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
        }
        if (effect) *effect = performed;
        if (data_) {
            data_->Release();
            data_ = nullptr;
        }
        return S_OK;
    }

private:
    void clearHighlight() {
        app_->dragTargetFolder.clear();
        app_->dragTargetIsTree = false;
        app_->dragTargetRow = -1;
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
        if (app_->mode == ViewMode::Details) {
            int r = app_->listRowAt(p);
            if (r >= 0 && app_->entries[r].isDir) target = app_->entries[r].path;
        } else {
            int i = app_->gridIndexAt(p);
            if (i >= 0 && app_->entries[i].isDir) target = app_->entries[i].path;
        }
    }

    LONG ref_ = 1;
    HWND hwnd_ = nullptr;
    App* app_ = nullptr;
    IDataObject* data_ = nullptr;
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
    if (paths.empty()) return false;
    auto* data = new HdropData(paths);
    auto* source = new DragSource();
    DWORD performed = DROPEFFECT_NONE;
    HRESULT hr = DoDragDrop(data, source, DROPEFFECT_COPY | DROPEFFECT_MOVE, &performed);
    data->Release();
    source->Release();
    if (effect) *effect = performed;
    diagLog(L"dragOut done hr=0x%08lX effect=%lu", (unsigned long)hr, (unsigned long)performed);
    return SUCCEEDED(hr) && performed != DROPEFFECT_NONE;
}

}  // namespace dnd
