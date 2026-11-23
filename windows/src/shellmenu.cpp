#include "shellmenu.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>

#include <cwchar>
#include <initializer_list>
#include <string>
#include <vector>

#include "debug.h"
#include "fs.h"

namespace shellmenu {
namespace {

constexpr UINT kIdFirst = 1;
constexpr UINT kIdLast = 0x6FFF;
constexpr UINT kCustomBase = 0x7000;

enum CustomId {
    kOpen = kCustomBase,
    kOpenNewWindow,
    kViewBig,
    kViewList,
    kViewDetails,
    kSortName,
    kSortSize,
    kSortType,
    kSortDate,
    kRefresh,
    kToggleHidden,
    kTerminal,
    kRename,
    kNewFolder,
    kNewTextFile,
};

IContextMenu2* g_menu2 = nullptr;
IContextMenu3* g_menu3 = nullptr;

bool startsWith(const std::wstring& text, const wchar_t* prefix) {
    const size_t n = wcslen(prefix);
    return text.size() >= n && text.compare(0, n, prefix) == 0;
}

std::wstring itemText(HMENU menu, int pos) {
    wchar_t buf[512]{};
    GetMenuStringW(menu, pos, buf, 512, MF_BYPOSITION);
    return buf;
}

bool menuHasText(HMENU menu, const wchar_t* needle) {
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i)
        if (itemText(menu, i).find(needle) != std::wstring::npos) return true;
    return false;
}

void removeItemsStartingWith(HMENU menu, std::initializer_list<const wchar_t*> prefixes) {
    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        const std::wstring text = itemText(menu, i);
        for (auto* prefix : prefixes) {
            if (startsWith(text, prefix)) {
                DeleteMenu(menu, i, MF_BYPOSITION);
                break;
            }
        }
    }
}

// A file/folder and the shell folder that contains it.
struct BoundItem {
    IShellFolder* parent = nullptr;
    PITEMID_CHILD child = nullptr;

    BoundItem() = default;
    BoundItem(const BoundItem&) = delete;
    BoundItem& operator=(const BoundItem&) = delete;
    ~BoundItem() {
        if (child) CoTaskMemFree(child);
        if (parent) parent->Release();
    }
};

bool bindItem(const std::wstring& path, BoundItem* out) {
    PIDLIST_ABSOLUTE full = nullptr;
    SFGAOF attrs = 0;
    if (FAILED(SHParseDisplayName(path.c_str(), nullptr, &full, 0, &attrs))) return false;
    IShellFolder* parent = nullptr;
    PCUITEMID_CHILD child = nullptr;
    const HRESULT hr = SHBindToParent(full, IID_IShellFolder, (void**)&parent, &child);
    if (SUCCEEDED(hr) && child) {
        out->child = (PITEMID_CHILD)ILClone(child);
        out->parent = parent;
    }
    CoTaskMemFree(full);
    return out->child != nullptr;
}

bool bindFolder(const std::wstring& folder, IShellFolder** out) {
    PIDLIST_ABSOLUTE pidl = nullptr;
    SFGAOF attrs = 0;
    if (FAILED(SHParseDisplayName(folder.c_str(), nullptr, &pidl, 0, &attrs))) return false;
    const HRESULT hr = SHBindToObject(nullptr, pidl, nullptr, IID_IShellFolder, (void**)out);
    CoTaskMemFree(pidl);
    return SUCCEEDED(hr) && *out != nullptr;
}

void addViewAndSort(HMENU menu, const ViewState& state) {
    HMENU view = CreatePopupMenu();
    AppendMenuW(view, MF_STRING, kViewBig, L"大图标");
    AppendMenuW(view, MF_STRING, kViewList, L"列表");
    AppendMenuW(view, MF_STRING, kViewDetails, L"详细信息");
    const UINT viewId = state.mode == 1 ? kViewBig : (state.mode == 2 ? kViewList : kViewDetails);
    CheckMenuRadioItem(view, kViewBig, kViewDetails, viewId, MF_BYCOMMAND);

    HMENU sort = CreatePopupMenu();
    AppendMenuW(sort, MF_STRING, kSortName, L"名称");
    AppendMenuW(sort, MF_STRING, kSortSize, L"大小");
    AppendMenuW(sort, MF_STRING, kSortType, L"类型");
    AppendMenuW(sort, MF_STRING, kSortDate, L"修改日期");
    CheckMenuRadioItem(sort, kSortName, kSortDate, kSortName + state.sortColumn, MF_BYCOMMAND);

    // The shell's New command needs Explorer's view to finish the rename, so
    // it would leave nothing behind; these two create the item themselves.
    HMENU newMenu = CreatePopupMenu();
    AppendMenuW(newMenu, MF_STRING, kNewFolder, L"文件夹");
    AppendMenuW(newMenu, MF_STRING, kNewTextFile, L"文本文档");

    // Insert from the bottom up: each insert lands at position 0.
    InsertMenuW(menu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    InsertMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)newMenu, L"新建");
    InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING, kRefresh, L"刷新");
    InsertMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)sort, L"排序方式");
    InsertMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)view, L"查看");
}

Command commandFor(UINT id) {
    switch (id) {
    case kOpen: return Command::Open;
    case kOpenNewWindow: return Command::OpenNewWindow;
    case kViewBig: return Command::ViewBig;
    case kViewList: return Command::ViewList;
    case kViewDetails: return Command::ViewDetails;
    case kSortName: return Command::SortName;
    case kSortSize: return Command::SortSize;
    case kSortType: return Command::SortType;
    case kSortDate: return Command::SortDate;
    case kRefresh: return Command::Refresh;
    case kToggleHidden: return Command::ToggleHidden;
    case kTerminal: return Command::Terminal;
    case kRename: return Command::Rename;
    case kNewFolder: return Command::NewFolder;
    case kNewTextFile: return Command::NewTextFile;
    default: return Command::None;
    }
}

struct BuiltMenu {
    HMENU menu = nullptr;
    IContextMenu* pcm = nullptr;
    IShellFolder* folder = nullptr;
    std::vector<BoundItem*> items;

    BuiltMenu() = default;
    BuiltMenu(const BuiltMenu&) = delete;
    BuiltMenu& operator=(const BuiltMenu&) = delete;
    ~BuiltMenu() {
        if (menu) DestroyMenu(menu);
        if (pcm) pcm->Release();
        if (folder) folder->Release();
        for (auto* item : items) delete item;
    }
};

bool buildMenu(HWND owner, const std::wstring& folderPath, const std::vector<std::wstring>& paths,
               const ViewState& state, BuiltMenu* out) {
    const bool background = paths.empty();
    IShellFolder* psf = nullptr;
    IContextMenu* pcm = nullptr;

    if (background) {
        if (!bindFolder(folderPath, &psf)) return false;
        if (FAILED(psf->CreateViewObject(owner, IID_IContextMenu, (void**)&pcm))) {
            psf->Release();
            return false;
        }
    } else {
        // A selection always lives in one folder; children must be relative to
        // that single parent.
        const std::wstring parentPath = fsx::parentPath(paths.front());
        for (auto& path : paths) {
            if (_wcsicmp(fsx::parentPath(path).c_str(), parentPath.c_str()) != 0) continue;
            auto* item = new BoundItem();
            if (bindItem(path, item)) out->items.push_back(item);
            else delete item;
        }
        if (out->items.empty()) return false;
        psf = out->items.front()->parent;
        psf->AddRef();
        std::vector<PCUITEMID_CHILD> children;
        children.reserve(out->items.size());
        for (auto* item : out->items) children.push_back(item->child);
        const HRESULT hr = psf->GetUIObjectOf(owner, (UINT)children.size(), children.data(),
                                              IID_IContextMenu, nullptr, (void**)&pcm);
        if (FAILED(hr)) {
            psf->Release();
            return false;
        }
    }
    out->folder = psf;
    out->pcm = pcm;

    out->menu = CreatePopupMenu();
    if (!out->menu) return false;
    if (FAILED(pcm->QueryContextMenu(out->menu, 0, kIdFirst, kIdLast, CMF_NORMAL))) return false;

    if (background) {
        // The shell's own view/sort/refresh/new items talk to Explorer's view
        // and do nothing inside a host; replace them with working equivalents.
        removeItemsStartingWith(out->menu,
                                {L"查看", L"排序方式", L"分组依据", L"刷新", L"新建"});
        addViewAndSort(out->menu, state);
        AppendMenuW(out->menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(out->menu, MF_STRING, kTerminal, L"在命令提示符中打开");
        AppendMenuW(out->menu, MF_STRING | (state.showHidden ? MF_CHECKED : 0), kToggleHidden,
                    L"显示隐藏文件");
    } else {
        const bool single = paths.size() == 1;
        const bool isDir = fsx::isDirectory(paths.front());
        UINT insert = 0;
        if (!menuHasText(out->menu, L"打开")) {
            InsertMenuW(out->menu, insert++, MF_BYPOSITION | MF_STRING, kOpen, L"打开");
            InsertMenuW(out->menu, insert++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
            SetMenuDefaultItem(out->menu, kOpen, FALSE);
        }
        if (isDir && single) {
            // Explorer adds this one itself; the raw shell menu does not have it.
            int openPos = -1;
            for (int i = 0; i < GetMenuItemCount(out->menu); ++i) {
                const std::wstring text = itemText(out->menu, i);
                if (startsWith(text, L"打开") && text.find(L"方式") == std::wstring::npos) {
                    openPos = i;
                    break;
                }
            }
            if (openPos >= 0 && !menuHasText(out->menu, L"在新窗口中打开"))
                InsertMenuW(out->menu, openPos + 1, MF_BYPOSITION | MF_STRING, kOpenNewWindow,
                            L"在新窗口中打开");
        }
        // The shell's Rename verb needs an Explorer view; ours opens the
        // rename prompt instead.
        if (single) {
            removeItemsStartingWith(out->menu, {L"重命名"});
            int propsPos = -1;
            for (int i = 0; i < GetMenuItemCount(out->menu); ++i) {
                if (itemText(out->menu, i).find(L"属性") != std::wstring::npos) {
                    propsPos = i;
                    break;
                }
            }
            if (propsPos >= 0)
                InsertMenuW(out->menu, propsPos, MF_BYPOSITION | MF_STRING, kRename, L"重命名");
            else
                AppendMenuW(out->menu, MF_STRING, kRename, L"重命名");
        }
    }
    return true;
}

bool buildFor(HWND owner, const std::wstring& folderPath, const std::vector<std::wstring>& paths,
              const ViewState& state, BuiltMenu* out) {
    return buildMenu(owner, folderPath, paths, state, out);
}

}  // namespace

bool forwardMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (g_menu3) {
        LRESULT result = 0;
        if (g_menu3->HandleMenuMsg2(msg, wp, lp, &result) == S_OK) return true;
    }
    if (g_menu2 && g_menu2->HandleMenuMsg(msg, wp, lp) == S_OK) return true;
    return false;
}

Result show(HWND owner, const std::wstring& folder, const std::vector<std::wstring>& paths,
            POINT point, const ViewState& state) {
    Result result;
    BuiltMenu built;
    if (!buildFor(owner, folder, paths, state, &built)) return result;

    g_menu2 = nullptr;
    g_menu3 = nullptr;
    if (FAILED(built.pcm->QueryInterface(IID_IContextMenu3, (void**)&g_menu3))) g_menu3 = nullptr;
    if (!g_menu3 && FAILED(built.pcm->QueryInterface(IID_IContextMenu2, (void**)&g_menu2)))
        g_menu2 = nullptr;

    const UINT chosen = TrackPopupMenuEx(
        built.menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN, point.x, point.y, owner,
        nullptr);
    result.shown = true;

    if (g_menu3) {
        g_menu3->Release();
        g_menu3 = nullptr;
    }
    if (g_menu2) {
        g_menu2->Release();
        g_menu2 = nullptr;
    }

    if (chosen >= kCustomBase) {
        result.command = commandFor(chosen);
    } else if (chosen >= kIdFirst && chosen <= kIdLast) {
        CMINVOKECOMMANDINFOEX info{};
        info.cbSize = sizeof(info);
        info.hwnd = owner;
        info.lpVerb = MAKEINTRESOURCEA((WORD)(chosen - kIdFirst));
        info.nShow = SW_SHOWNORMAL;
        if (SUCCEEDED(built.pcm->InvokeCommand((LPCMINVOKECOMMANDINFO)&info)))
            result.invokedShell = true;
    }
    diagLog(L"shellmenu background=%d paths=%d shown=%d shell=%d cmd=%d", (int)paths.empty(),
            (int)paths.size(), (int)result.shown, (int)result.invokedShell,
            (int)result.command);
    return result;
}

std::vector<std::wstring> labels(const std::wstring& folder, const std::vector<std::wstring>& paths,
                                 const ViewState& state) {
    std::vector<std::wstring> out;
    BuiltMenu built;
    if (!buildFor(nullptr, folder, paths, state, &built)) return out;
    const int count = GetMenuItemCount(built.menu);
    for (int i = 0; i < count; ++i) {
        const std::wstring text = itemText(built.menu, i);
        if (!text.empty()) out.push_back(text);
    }
    return out;
}

bool canBuildItemMenu(const std::wstring& path) {
    return !labels(fsx::parentPath(path), {path}, ViewState{}).empty();
}

bool canBuildBackgroundMenu(const std::wstring& folder) {
    return !labels(folder, {}, ViewState{}).empty();
}

}  // namespace shellmenu
