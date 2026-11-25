#include "shellmenu.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shobjidl.h>
#include <winreg.h>

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "debug.h"
#include "foldercolours.h"
#include "fs.h"
#include "hmmenu.h"
#include "settings.h"

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
    kSettings,
    kPreview,
    kReveal,
    kCopyPath,
    kFavoriteAdd,
    kFavoriteRemove,
    kFavoriteRename,
    kFolderColourFirst,  // one id for each colour a folder can be given
    kFolderColourNone = kFolderColourFirst + foldercolours::kCount,  // back to no colour
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
    std::wstring text = buf;
    const size_t amp = text.find(L"(&");
    if (amp != std::wstring::npos) {
        const size_t close = text.find(L')', amp);
        if (close != std::wstring::npos) text.erase(amp, close - amp + 1);
    }
    // Some extensions mark accelerators with a bare "&".
    text.erase(std::remove(text.begin(), text.end(), L'&'), text.end());
    return text;
}

bool menuHasText(HMENU menu, const wchar_t* needle) {
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i)
        if (itemText(menu, i).find(needle) != std::wstring::npos) return true;
    return false;
}

// `keep`: an item of ours that may start like one of the prefixes.
void removeItemsStartingWith(HMENU menu, std::initializer_list<const wchar_t*> prefixes,
                             UINT keep = 0) {
    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        const std::wstring text = itemText(menu, i);
        for (auto* prefix : prefixes) {
            if (startsWith(text, prefix) && GetMenuItemID(menu, i) != keep) {
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
    } else if (parent) {
        parent->Release();  // bound but childless: nothing owns this reference
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

// The keys whose ContextMenuHandlers go into the menu of any file, of any
// folder, of a drive, or of the background of a folder.
const wchar_t* const kHandlerKeys[] = {
    L"*\\shellex\\ContextMenuHandlers",
    L"AllFilesystemObjects\\shellex\\ContextMenuHandlers",
    L"Folder\\shellex\\ContextMenuHandlers",
    L"Directory\\shellex\\ContextMenuHandlers",
    L"Directory\\Background\\shellex\\ContextMenuHandlers",
    L"Drive\\shellex\\ContextMenuHandlers",
};

// The default value of a key, as text (environment names in it expanded).
std::wstring keyText(HKEY key) {
    wchar_t buf[1024]{};
    DWORD size = sizeof(buf), type = 0;
    if (RegQueryValueExW(key, nullptr, nullptr, &type, (LPBYTE)buf, &size) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ))
        return std::wstring();
    std::wstring text(buf, wcsnlen(buf, size / sizeof(wchar_t)));
    if (type == REG_EXPAND_SZ) {
        wchar_t expanded[1024]{};
        ExpandEnvironmentStringsW(text.c_str(), expanded, 1024);
        text = expanded;
    }
    return text;
}

// Where COM loads the in-process server of a CLSID from, as a full path: a
// bare name is one of the system directory's, as COM takes it.
std::wstring serverDll(const std::wstring& clsid) {
    HKEY key = nullptr;
    std::wstring path;
    if (RegOpenKeyExW(HKEY_CLASSES_ROOT, (L"CLSID\\" + clsid + L"\\InprocServer32").c_str(), 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        path = keyText(key);
        RegCloseKey(key);
    }
    if (path.size() > 2 && path.front() == L'"' && path.back() == L'"')
        path = path.substr(1, path.size() - 2);
    if (!path.empty() && path.find(L'\\') == std::wstring::npos &&
        path.find(L':') == std::wstring::npos) {
        wchar_t system[MAX_PATH]{};
        GetSystemDirectoryW(system, MAX_PATH);
        path = std::wstring(system) + L"\\" + path;
    }
    return path;
}

void addViewAndSort(HMENU menu, const ViewState& state) {
    const bool wantView = (state.menuMask & kMenuBgView) != 0;
    const bool wantSort = (state.menuMask & kMenuBgSort) != 0;
    const bool wantNew = (state.menuMask & kMenuBgNew) != 0;
    const bool wantRefresh = (state.menuMask & kMenuBgRefresh) != 0;

    HMENU view = nullptr;
    if (wantView) {
        view = CreatePopupMenu();
        AppendMenuW(view, MF_STRING, kViewBig, L"大图标");
        AppendMenuW(view, MF_STRING, kViewList, L"列表");
        AppendMenuW(view, MF_STRING, kViewDetails, L"详细信息");
        const UINT viewId =
            state.mode == 1 ? kViewBig : (state.mode == 2 ? kViewList : kViewDetails);
        CheckMenuRadioItem(view, kViewBig, kViewDetails, viewId, MF_BYCOMMAND);
    }

    HMENU sort = nullptr;
    if (wantSort) {
        sort = CreatePopupMenu();
        AppendMenuW(sort, MF_STRING, kSortName, L"名称");
        AppendMenuW(sort, MF_STRING, kSortSize, L"大小");
        AppendMenuW(sort, MF_STRING, kSortType, L"类型");
        AppendMenuW(sort, MF_STRING, kSortDate, L"修改日期");
        CheckMenuRadioItem(sort, kSortName, kSortDate, kSortName + state.sortColumn,
                           MF_BYCOMMAND);
    }

    HMENU newMenu = nullptr;
    if (wantNew) {
        // Laid out like Explorer's: a "New" submenu holding the folder and the
        // text document.  The shell's own New command needs Explorer's view to
        // finish the rename, so these create the item themselves.
        newMenu = CreatePopupMenu();
        AppendMenuW(newMenu, MF_STRING, kNewFolder, L"文件夹");
        AppendMenuW(newMenu, MF_STRING, kNewTextFile, L"文本文档");
    }

    // Insert from the bottom up: each insert lands at position 0.
    InsertMenuW(menu, 0, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    if (newMenu) InsertMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)newMenu, L"新建");
    if (wantRefresh) InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING, kRefresh, L"刷新");
    if (sort) InsertMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)sort, L"排序方式");
    if (view) InsertMenuW(menu, 0, MF_BYPOSITION | MF_POPUP, (UINT_PTR)view, L"查看");
}

Command commandFor(UINT id) {
    switch (id) {
    case kFavoriteAdd: return Command::AddFavorite;
    case kFavoriteRemove: return Command::RemoveFavorite;
    case kFavoriteRename: return Command::RenameFavorite;
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
    case kSettings: return Command::Settings;
    case kPreview: return Command::Preview;
    case kReveal: return Command::Reveal;
    case kCopyPath: return Command::CopyPath;
    default:
        if (id >= kFolderColourFirst && id <= kFolderColourNone) return Command::FolderColour;
        return Command::None;
    }
}

struct BuiltMenu {
    HMENU menu = nullptr;
    IContextMenu* pcm = nullptr;
    IShellFolder* folder = nullptr;
    std::vector<BoundItem*> items;
    // The extensions' hand in showing the menu (IContextMenu3, else 2): taken
    // when asked (`ask`), for as long as this lives.
    IContextMenu3* menu3 = nullptr;
    IContextMenu2* menu2 = nullptr;

    BuiltMenu() = default;
    BuiltMenu(const BuiltMenu&) = delete;
    BuiltMenu& operator=(const BuiltMenu&) = delete;
    ~BuiltMenu() {
        if (menu) DestroyMenu(menu);
        if (menu3) menu3->Release();
        if (menu2) menu2->Release();
        if (pcm) pcm->Release();
        if (folder) folder->Release();
        for (auto* item : items) delete item;
    }
    void ask() {
        if (FAILED(pcm->QueryInterface(IID_IContextMenu3, (void**)&menu3))) menu3 = nullptr;
        if (!menu3 && FAILED(pcm->QueryInterface(IID_IContextMenu2, (void**)&menu2)))
            menu2 = nullptr;
    }
    bool forward(UINT msg, WPARAM wp, LPARAM lp) const {
        if (menu3) {
            LRESULT result = 0;
            if (menu3->HandleMenuMsg2(msg, wp, lp, &result) == S_OK) return true;
        }
        if (menu2 && menu2->HandleMenuMsg(msg, wp, lp) == S_OK) return true;
        return false;
    }
};

void applyHidden(HMENU menu, const std::vector<std::wstring>& hidden) {
    if (hidden.empty()) return;
    for (int i = GetMenuItemCount(menu) - 1; i >= 0; --i) {
        const std::wstring text = itemText(menu, i);
        if (text.empty()) continue;
        for (auto& hide : hidden)
            if (_wcsicmp(text.c_str(), hide.c_str()) == 0) {
                DeleteMenu(menu, i, MF_BYPOSITION);
                break;
            }
    }
}

// The shell's part of the menu of `what`: what it and the installed
// extensions put there, nothing of this program's.  On the thread that calls.
bool buildShellMenu(HWND owner, const Request& what, BuiltMenu* out) {
    const std::wstring& folderPath = what.folder;
    const std::vector<std::wstring>& paths = what.paths;
    const bool background = paths.empty();
    IShellFolder* psf = nullptr;
    IContextMenu* pcm = nullptr;

    // Before anything asks the shell for a menu: no DLL may be loaded while
    // one is being built (preloadHandlers).
    preloadHandlers();
    // What the menu cost, step by step (in the log, when MOC_DEBUG is set).
    double tStart = diagMillis(), tBound = tStart, tObject = tStart, tQueried = tStart;
    const auto built = [&]() {
        diagLog(L"menu build (%ls): bind %.1f, object %.1f, query %.1f ms",
                background ? L"the folder's background" : L"an item", tBound - tStart,
                tObject - tBound, tQueried - tObject);
        return true;
    };

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
    tBound = diagMillis();

    out->menu = CreatePopupMenu();
    if (!out->menu) return false;
    tObject = diagMillis();
    if (FAILED(pcm->QueryContextMenu(out->menu, 0, kIdFirst, kIdLast, CMF_NORMAL))) return false;
    tQueried = diagMillis();
    return built();
}

// On the thread that calls, the whole menu: the shell's part, then this
// program's own items, then the labels switched off (they may name either).
bool buildMenu(HWND owner, const std::wstring& folderPath, const std::vector<std::wstring>& paths,
               const ViewState& state, BuiltMenu* out, bool withExtras) {
    const Request what{folderPath, paths, state.hidden};
    if (!buildShellMenu(owner, what, out)) return false;
    if (withExtras) addOwnItems(out->menu, paths, state);
    applyHidden(out->menu, what.hidden);
    return true;
}

bool buildFor(HWND owner, const std::wstring& folderPath, const std::vector<std::wstring>& paths,
              const ViewState& state, BuiltMenu* out, bool withExtras = true) {
    return buildMenu(owner, folderPath, paths, state, out, withExtras);
}

}  // namespace

// What this program adds to a menu of the shell's: on the thread that shows
// the menu, for the things as they are right now.
void addOwnItems(HMENU menu, const std::vector<std::wstring>& paths, const ViewState& state) {
    const bool background = paths.empty();
    if (background) {
        // The shell's own view/sort/refresh/new items talk to Explorer's view
        // and do nothing inside a host; replace them with working equivalents.
        removeItemsStartingWith(menu,
                                {L"查看", L"排序方式", L"分组依据", L"刷新", L"新建"});
        addViewAndSort(menu, state);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        if (state.menuMask & kMenuBgTerminal)
            AppendMenuW(menu, MF_STRING, kTerminal, L"在命令提示符中打开");
        if (state.menuMask & kMenuBgHidden)
            AppendMenuW(menu, MF_STRING | (state.showHidden ? MF_CHECKED : 0), kToggleHidden,
                        L"显示隐藏文件");
        if (state.menuMask & kMenuBgSettings)
            AppendMenuW(menu, MF_STRING, kSettings, L"设置…");
    } else {
        const bool single = paths.size() == 1;
        const bool isDir = fsx::isDirectory(paths.front());

        // Find the shell's default verb so our extras sit right below it.
        int openPos = -1;
        for (int i = 0; i < GetMenuItemCount(menu); ++i) {
            const std::wstring text = itemText(menu, i);
            if (startsWith(text, L"打开") && text.find(L"方式") == std::wstring::npos) {
                openPos = i;
                break;
            }
        }
        if (openPos < 0) {
            InsertMenuW(menu, 0, MF_BYPOSITION | MF_STRING, kOpen, L"打开");
            SetMenuDefaultItem(menu, kOpen, FALSE);
            openPos = 0;
        }
        int at = openPos + 1;
        if ((state.menuMask & kMenuItemPreview) && single)
            InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kPreview, L"快速查看");
        if ((state.menuMask & kMenuItemNewWindow) && isDir && single &&
            !menuHasText(menu, L"在新窗口中打开"))
            InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kOpenNewWindow,
                        L"在新窗口中打开");
        // The colour of the folder - or of each one selected, when there are
        // several: a menu of its own, the colour it has now ticked.
        if ((state.menuMask & kMenuItemFolderColour) && isDir) {
            if (HMENU colours = CreatePopupMenu()) {
                for (int i = 0; i < foldercolours::kCount; ++i) {
                    const UINT id = kFolderColourFirst + (UINT)i;
                    AppendMenuW(colours, MF_STRING | (state.folderColour == i ? MF_CHECKED : 0), id,
                                foldercolours::colour(i).name);
                    // Its swatch, for the Win98-style menu that shows it (hmmenu).
                    MENUITEMINFOW info{};
                    info.cbSize = sizeof(info);
                    info.fMask = MIIM_DATA;
                    info.dwItemData = foldercolours::colour(i).rgb;
                    SetMenuItemInfoW(colours, i, TRUE, &info);
                }
                AppendMenuW(colours,
                            MF_STRING |
                                (state.folderColour == foldercolours::kDefault ? MF_CHECKED : 0),
                            kFolderColourNone, foldercolours::colour(foldercolours::kDefault).name);
                InsertMenuW(menu, at++, MF_BYPOSITION | MF_POPUP, (UINT_PTR)colours,
                            L"文件夹颜色");
            }
        }
        // The sidebar's Favorites: a folder joins them from its own menu and
        // leaves them the same way; on its row in the tree it can be given a
        // name of its own too.
        if (isDir && single) {
            if (state.favoriteNode) {
                InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kFavoriteRemove,
                            L"从收藏夹中移除");
                InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kFavoriteRename,
                            L"重命名收藏项…");
            } else if (state.isFavorite) {
                InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kFavoriteRemove,
                            L"从收藏夹中移除");
            } else {
                InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kFavoriteAdd,
                            L"添加到收藏夹");
            }
        }
        if (state.menuMask & kMenuItemReveal)
            InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kReveal,
                        L"在资源管理器中显示");
        if (state.menuMask & kMenuItemCopyPath)
            InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kCopyPath, L"复制路径");
        if (state.menuMask & kMenuItemTerminal)
            InsertMenuW(menu, at++, MF_BYPOSITION | MF_STRING, kTerminal,
                        L"在命令提示符中打开");
        if (at > openPos + 1) InsertMenuW(menu, at++, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);

        // The shell's Rename verb needs an Explorer view; ours opens the
        // rename prompt instead.
        if (single && (state.menuMask & kMenuItemRename)) {
            // ("重命名收藏项…" is ours and stays.)
            removeItemsStartingWith(menu, {L"重命名"}, kFavoriteRename);
            int propsPos = -1;
            for (int i = 0; i < GetMenuItemCount(menu); ++i) {
                if (itemText(menu, i).find(L"属性") != std::wstring::npos) {
                    propsPos = i;
                    break;
                }
            }
            if (propsPos >= 0)
                InsertMenuW(menu, propsPos, MF_BYPOSITION | MF_STRING, kRename, L"重命名");
            else
                AppendMenuW(menu, MF_STRING, kRename, L"重命名");
        }
    }
    return;
}

std::vector<std::wstring> handlerDlls() {
    std::vector<std::wstring> out;
    for (const wchar_t* list : kHandlerKeys) {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CLASSES_ROOT, list, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;
        for (DWORD i = 0;; ++i) {
            wchar_t name[256]{};
            DWORD size = 256;
            if (RegEnumKeyExW(key, i, name, &size, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS)
                break;
            HKEY handler = nullptr;
            std::wstring clsid = name;
            if (RegOpenKeyExW(key, name, 0, KEY_QUERY_VALUE, &handler) == ERROR_SUCCESS) {
                // The value is the CLSID, or empty when the key's name is.
                const std::wstring value = keyText(handler);
                RegCloseKey(handler);
                if (!value.empty()) clsid = value;
            }
            std::wstring path = serverDll(clsid);
            if (path.empty()) continue;
            bool known = false;
            for (const std::wstring& have : out)
                if (_wcsicmp(have.c_str(), path.c_str()) == 0) known = true;
            if (!known) out.push_back(path);
        }
        RegCloseKey(key);
    }
    return out;
}

int preloadHandlers() {
    // Once for the program, whichever thread gets here first (the window's
    // builds menus of its own in the tests; otherwise the menu thread does).
    static std::once_flag once;
    static int loaded = 0;
    std::call_once(once, [] {
        const double t0 = diagMillis();
        const std::vector<std::wstring> dlls = handlerDlls();
        int missing = 0;
        for (const std::wstring& path : dlls) {
            if (GetModuleHandleW(path.c_str())) continue;
            if (LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH))
                ++loaded;
            else
                ++missing;  // registered, not on disk any more: nothing to wait for later
        }
        diagLog(L"preload: %d handler DLLs, %d loaded, %d missing, %.1f ms", (int)dlls.size(),
                loaded, missing, diagMillis() - t0);
    });
    return loaded;
}

bool forwardMessage(UINT msg, WPARAM wp, LPARAM lp) {
    if (g_menu3) {
        LRESULT result = 0;
        if (g_menu3->HandleMenuMsg2(msg, wp, lp, &result) == S_OK) return true;
    }
    if (g_menu2 && g_menu2->HandleMenuMsg(msg, wp, lp) == S_OK) return true;
    return false;
}

Result decodeChoice(int chosen) {
    Result result;
    if (chosen < (int)kCustomBase) return result;
    result.command = commandFor((UINT)chosen);
    if (result.command == Command::FolderColour)
        result.colour = chosen == (int)kFolderColourNone
                            ? foldercolours::kDefault
                            : chosen - (int)kFolderColourFirst;
    return result;
}

Result show(HWND owner, const std::wstring& folder, const std::vector<std::wstring>& paths,
            POINT point, const ViewState& state) {
    Result result;
    BuiltMenu built;
    if (!buildFor(owner, folder, paths, state, &built)) return result;

    built.ask();
    g_menu2 = built.menu2;  // the window's messages reach the extensions through these
    g_menu3 = built.menu3;  // while the menu is up; they are released with `built`

    // The menu is drawn by our own Win98-style popup; the shell only supplies
    // the items and runs the command that gets picked.
    const int chosen = showHMenu(owner, built.menu, point);
    result.shown = true;
    g_menu2 = nullptr;
    g_menu3 = nullptr;

    if (chosen >= (int)kCustomBase) {
        const Result picked = decodeChoice(chosen);
        result.command = picked.command;
        result.colour = picked.colour;
    } else if (chosen >= (int)kIdFirst && chosen <= (int)kIdLast) {
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
                                 const ViewState& state, std::vector<std::wstring>* subMenus) {
    std::vector<std::wstring> out;
    BuiltMenu built;
    if (!buildFor(nullptr, folder, paths, state, &built)) return out;
    const int count = GetMenuItemCount(built.menu);
    for (int i = 0; i < count; ++i) {
        const std::wstring text = itemText(built.menu, i);
        if (!text.empty()) out.push_back(text);
        if (!subMenus) continue;
        // The entries of each submenu, as "submenu/entry".
        const HMENU sub = GetSubMenu(built.menu, i);
        for (int j = 0; sub && j < GetMenuItemCount(sub); ++j) {
            const std::wstring child = itemText(sub, j);
            if (!child.empty()) subMenus->push_back(text + L"/" + child);
        }
    }
    return out;
}

std::vector<std::wstring> shellLabels(const std::wstring& folder,
                                      const std::vector<std::wstring>& paths, bool background) {
    std::vector<std::wstring> out;
    BuiltMenu built;
    if (background) {
        if (!buildFor(nullptr, folder, {}, ViewState{}, &built, false)) return out;
    } else if (!buildFor(nullptr, folder, paths, ViewState{}, &built, false)) {
        return out;
    }
    for (int i = 0; i < GetMenuItemCount(built.menu); ++i) {
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

namespace {
std::wstring probeRoot() {
    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    std::wstring dir = std::wstring(tmp) + L"MyOldComputer-probe";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}
}  // namespace

std::vector<std::wstring> probeFileLabels(const std::wstring& ext) {
    const std::wstring dir = probeRoot();
    std::wstring file = dir + L"\\probe";
    if (!ext.empty()) file += L"." + ext;
    DeleteFileW(file.c_str());
    HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    CloseHandle(h);
    std::vector<std::wstring> out = shellLabels(dir, {file}, false);
    DeleteFileW(file.c_str());
    return out;
}

std::vector<std::wstring> probeFolderLabels() {
    const std::wstring root = probeRoot();
    const std::wstring dir = root + L"\\probe-folder";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::vector<std::wstring> out = shellLabels(root, {dir}, false);
    RemoveDirectoryW(dir.c_str());
    return out;
}

// ------------------------------------------------------ built ahead, on a thread of its own

namespace {

constexpr UINT kWakeMessage = WM_APP + 0x40;  // to the thread's own window
constexpr UINT_PTR kSecondPassTimer = 1;
constexpr size_t kMaxEntries = 24;               // menus kept
constexpr ULONGLONG kKeptMs = 30000;             // older: built again before a command is run from it
constexpr ULONGLONG kWarmMs = 60000;             // younger: not built again only to have it
constexpr UINT kSecondPassMs = 160;  // then, with nothing else to do, the submenus are filled in once more

std::wstring lower(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

// What a menu was asked for, as one text (nothing in it turns on the case).
std::wstring keyOf(const Request& what) {
    std::wstring key = lower(what.folder);
    for (const std::wstring& path : what.paths) key += L'\n' + lower(path);
    key += L'\n';
    for (const std::wstring& label : what.hidden) key += lower(label) + L';';
    return key;
}

std::wstring rawText(HMENU menu, int pos) {
    wchar_t buf[512]{};
    GetMenuStringW(menu, pos, buf, 512, MF_BYPOSITION);
    return buf;
}

// What a menu says, as plain data.
std::vector<Item> readItems(HMENU menu) {
    std::vector<Item> out;
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        MENUITEMINFOW info{};
        info.cbSize = sizeof(info);
        wchar_t buf[512]{};
        info.dwTypeData = buf;
        info.cch = 512;
        info.fMask = MIIM_FTYPE | MIIM_ID | MIIM_SUBMENU | MIIM_STATE | MIIM_STRING;
        Item item;
        if (GetMenuItemInfoW(menu, i, TRUE, &info)) {
            item.text = buf;
            item.separator = (info.fType & MFT_SEPARATOR) != 0;
            item.popup = info.hSubMenu != nullptr;
            item.enabled = (info.fState & MFS_DISABLED) == 0;
            item.checked = (info.fState & MFS_CHECKED) != 0;
            item.isDefault = (info.fState & MFS_DEFAULT) != 0;
            if (info.hSubMenu)
                item.sub = readItems(info.hSubMenu);
            else
                item.id = info.wID;
        }
        out.push_back(std::move(item));
    }
    return out;
}

// The extensions fill the menu and every submenu of it in now, as the window
// would each time one opens: what is shown is a copy taken afterwards.
void fillSubmenus(BuiltMenu& built, HMENU menu, int depth) {
    const int count = GetMenuItemCount(menu);
    for (int i = 0; i < count; ++i) {
        const HMENU sub = GetSubMenu(menu, i);
        if (!sub) continue;
        built.forward(WM_INITMENUPOPUP, (WPARAM)sub, MAKELPARAM(i, 0));
        if (depth < 2) fillSubmenus(built, sub, depth + 1);
    }
}

void fillIn(BuiltMenu& built) {
    built.forward(WM_INITMENUPOPUP, (WPARAM)built.menu, 0);  // some add items to the root itself
    fillSubmenus(built, built.menu, 0);
}

// What the thread and the window's thread share.  Made once and never
// destroyed: the thread is never joined, and would outlive a static destructor.
struct Ahead {
    struct Ask {
        enum Rank { Batch, Hover, Show, Verb } rank = Batch;
        Request what;
        HWND notify = nullptr;  // Show, Verb: where the answer is posted
        ULONGLONG serial = 0;   // ... under this
        ULONGLONG freshMs = kAgainMs;  // Show: a menu of the thing built this recently is the answer
        std::vector<int> path;  // Verb: where the item picked lay
        std::wstring text;      // ... and what it said
    };
    // A menu as the shell built it, and what it says.
    struct Entry {
        ULONGLONG id = 0;
        Request what;
        std::wstring key, kind;
        ULONGLONG built = 0;
        ULONGLONG used = 0;
        bool ok = false;
        std::vector<Item> items;              // under `mutex`
        std::unique_ptr<BuiltMenu> menu;      // the thread's own, as is the shell's work of making it
    };
    using EntryPtr = std::shared_ptr<Entry>;

    std::mutex mutex;  // `entries` and `asks`
    std::vector<EntryPtr> entries;
    std::deque<Ask> asks;
    ULONGLONG nextId = 1;
    HWND owner = nullptr;  // the window the shell is shown to parent things to
    std::atomic<bool> running{false};
    std::atomic<HWND> window{nullptr};
    std::atomic<ULONGLONG> shown{0};     // the menu built as this one is on screen (0: none; showing)
    std::atomic<bool> quitting{false};  // the window is gone: nothing more is built (stop)
    std::atomic<int> busy{0};           // the thread is in the shell's code (a step of work)
    std::once_flag once;
    std::vector<EntryPtr> fresh;   // the thread's own: built, their submenus to be filled in once more
    std::deque<EntryPtr> retired;  // to be let go of by the thread, which is the one that may
};

Ahead& ahead() {
    static Ahead* a = new Ahead;
    return *a;
}

// Says the thread is in the shell's code for as long as it lives (stop waits on that).
struct Busy {
    Busy(Ahead& a) : a(a) { a.busy.fetch_add(1); }
    ~Busy() { a.busy.fetch_sub(1); }
    Ahead& a;
};

// The next thing to do, if any.  A command picked and a menu asked for go
// before what was only asked for in time; of the pointer's, only the last.
bool takeAsk(Ahead::Ask* ask) {
    Ahead& a = ahead();
    std::lock_guard<std::mutex> lock(a.mutex);
    for (int rank = Ahead::Ask::Verb; rank >= Ahead::Ask::Batch; --rank) {
        for (auto it = a.asks.begin(); it != a.asks.end(); ++it) {
            if (it->rank != rank) continue;
            *ask = std::move(*it);
            a.asks.erase(it);
            if (rank == Ahead::Ask::Hover)
                for (auto other = a.asks.begin(); other != a.asks.end();)
                    other = other->rank == rank ? a.asks.erase(other) : other + 1;
            return true;
        }
    }
    return false;
}

Ahead::EntryPtr builtEntry(const Request& what) {
    // The thread's: everything of the shell here is made, used and let go of on it.
    Ahead& a = ahead();
    auto entry = std::make_shared<Ahead::Entry>();
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        entry->id = a.nextId++;
    }
    entry->what = what;
    entry->key = keyOf(what);
    entry->kind = kindOf(what);
    entry->built = entry->used = GetTickCount64();
    entry->menu = std::make_unique<BuiltMenu>();
    if (buildShellMenu(a.owner, what, entry->menu.get())) {
        entry->menu->ask();
        applyHidden(entry->menu->menu, what.hidden);
        fillIn(*entry->menu);
        entry->items = readItems(entry->menu->menu);
        entry->ok = true;
    }
    return entry;
}

// Puts it where the window's thread finds it.  What it replaces, and what is
// too old or makes the pile too high, is let go of here - on the thread that
// made it, which is the one that may let the shell's objects go.
void publish(const Ahead::EntryPtr& entry) {
    Ahead& a = ahead();
    std::vector<Ahead::EntryPtr> gone;
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        const ULONGLONG now = GetTickCount64();
        for (auto it = a.entries.begin(); it != a.entries.end();) {
            if ((*it)->key == entry->key || now - (*it)->built > kModelAgeMs)
                gone.push_back(std::move(*it)), it = a.entries.erase(it);
            else
                ++it;
        }
        a.entries.push_back(entry);
        while (a.entries.size() > kMaxEntries) {
            auto oldest = a.entries.begin();
            for (auto it = a.entries.begin(); it != a.entries.end(); ++it)
                if ((*it)->used < (*oldest)->used) oldest = it;
            gone.push_back(std::move(*oldest));
            a.entries.erase(oldest);
        }
    }
}

// Not kept any longer (a command was run from it: what that did may change
// it).  Let go of outside the lock: what the shell does as it is released is
// not the window's thread's to wait for.
void forget(ULONGLONG id) {
    Ahead& a = ahead();
    Ahead::EntryPtr gone;
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        for (auto it = a.entries.begin(); it != a.entries.end();) {
            if ((*it)->id == id) {
                gone = std::move(*it);
                it = a.entries.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& entry : a.fresh)
            if (entry && entry->id == id) {
                if (!gone) gone = entry;
                entry.reset();
            }
    }
}

void runVerb(const Ahead::Ask& ask);

void work() {
    Ahead& a = ahead();
    {   // what the window's thread let go of in the meantime (forgetBackgrounds)
        std::deque<Ahead::EntryPtr> retired;
        {
            std::lock_guard<std::mutex> lock(a.mutex);
            retired.swap(a.retired);
        }
        for (const auto& entry : retired)
            for (auto& fresh : a.fresh)
                if (fresh == entry) fresh.reset();
        retired.clear();  // here: the shell's objects are released on the thread that made them
    }
    std::vector<Ahead::EntryPtr> fresh;
    for (;;) {
        Ahead::Ask ask;
        if (!takeAsk(&ask)) break;
        if (a.quitting.load() && ask.rank != Ahead::Ask::Verb) continue;  // the window is going
        // A menu someone is waiting for (the right-click's, or the command it
        // picked) is made at the window's own priority, machine or no machine
        // load; what is only made ahead stays behind the window.
        SetThreadPriority(GetCurrentThread(),
                          ask.rank >= Ahead::Ask::Show ? THREAD_PRIORITY_NORMAL
                                                       : THREAD_PRIORITY_BELOW_NORMAL);
        Busy busy(a);  // in the shell's code from here to the end of the step
        if (ask.rank == Ahead::Ask::Verb) {
            runVerb(ask);
            continue;
        }
        Ahead::EntryPtr entry;
        {   // Built since it was asked for - the pointer was resting on it, or it was asked for twice: that one.
            std::lock_guard<std::mutex> lock(a.mutex);
            const std::wstring key = keyOf(ask.what);
            const ULONGLONG now = GetTickCount64();
            for (auto it = a.entries.rbegin(); it != a.entries.rend(); ++it)
                if ((*it)->ok && (*it)->key == key && now - (*it)->built < ask.freshMs) {
                    entry = *it;
                    break;
                }
        }
        if (!entry) {
            entry = builtEntry(ask.what);
            publish(entry);
            fresh.push_back(entry);
        }
        if (ask.rank == Ahead::Ask::Show && ask.notify)
            PostMessageW(ask.notify, kMenuReadyMessage, ask.serial, 0);
    }
    a.fresh.insert(a.fresh.end(), fresh.begin(), fresh.end());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    if (!a.fresh.empty()) SetTimer(a.window.load(), kSecondPassTimer, kSecondPassMs, nullptr);
}

void secondPass() {
    Ahead& a = ahead();
    bool idle = false;
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        idle = a.asks.empty();
    }
    if (!idle || a.quitting.load()) return;  // the window is going: nothing more
    Busy busy(a);
    for (auto& entry : a.fresh) {
        if (!entry) continue;
        fillIn(*entry->menu);
        std::lock_guard<std::mutex> lock(a.mutex);
        entry->items = readItems(entry->menu->menu);
        // A menu on screen that was made of it says the same as before: the
        // window is told, and makes it say this.
        if (entry->id == a.shown.load() && a.owner)
            PostMessageW(a.owner, kMenuChangedMessage, (WPARAM)entry->id, 0);
    }
    a.fresh.clear();
}

// Runs, from the menu built for the thing itself, the item that lay at
// `path` and said `text` when it was shown.  It is looked up by both: when
// either is other than it was, nothing runs - the window is told so.
void runVerb(const Ahead::Ask& ask) {
    Ahead& a = ahead();
    Ahead::EntryPtr entry;
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        const std::wstring key = keyOf(ask.what);
        const ULONGLONG now = GetTickCount64();
        for (auto it = a.entries.rbegin(); it != a.entries.rend(); ++it)
            if ((*it)->ok && (*it)->key == key && now - (*it)->built <= kKeptMs) {
                entry = *it;
                break;
            }
    }
    if (!entry) {
        entry = builtEntry(ask.what);
        publish(entry);
    } else {
        fillIn(*entry->menu);  // its submenus again: one of them is where the item was
    }
    VerbResult result = kVerbNotFound;
    if (entry->ok) {
        HMENU at = entry->menu->menu;
        bool there = !ask.path.empty();
        for (size_t i = 0; there && i + 1 < ask.path.size(); ++i) {
            at = ask.path[i] >= 0 ? GetSubMenu(at, ask.path[i]) : nullptr;
            there = at != nullptr;
        }
        int pos = there ? ask.path.back() : -1;
        const int count = there ? GetMenuItemCount(at) : 0;
        if (pos < 0 || pos >= count || _wcsicmp(rawText(at, pos).c_str(), ask.text.c_str()) != 0) {
            pos = -1;  // something moved
            for (int i = 0; i < count; ++i)
                if (_wcsicmp(rawText(at, i).c_str(), ask.text.c_str()) == 0) {
                    pos = i;
                    break;
                }
        }
        const UINT id = pos >= 0 ? GetMenuItemID(at, pos) : 0;
        if (id >= kIdFirst && id <= kIdLast) {
            CMINVOKECOMMANDINFOEX info{};
            info.cbSize = sizeof(info);
            info.hwnd = ask.notify;
            info.lpVerb = MAKEINTRESOURCEA((WORD)(id - kIdFirst));
            info.nShow = SW_SHOWNORMAL;
            const HRESULT hr = entry->menu->pcm->InvokeCommand((LPCMINVOKECOMMANDINFO)&info);
            result = SUCCEEDED(hr) ? kVerbRan : kVerbFailed;
        }
    }
    diagLog(L"menu verb: %d (%ls)", (int)result, ask.text.c_str());
    forget(entry->id);
    PostMessageW(ask.notify, kMenuDoneMessage, ask.serial, (LPARAM)result);
}

LRESULT CALLBACK threadProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == kWakeMessage) {
        work();
        return 0;
    }
    if (msg == WM_TIMER && wp == kSecondPassTimer) {
        KillTimer(hwnd, kSecondPassTimer);
        secondPass();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void menuThread() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    OleInitialize(nullptr);  // the extensions expect it: the clipboard, drag and drop
    // Behind the window's thread, always: a menu being built here is never
    // to cost the pointer a highlight.  (The extensions' DLLs are loaded
    // before the first menu is - in buildShellMenu, on whatever thread builds
    // first.)
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
    WNDCLASSW wc{};
    wc.lpfnWndProc = threadProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MyOldComputerMenuThread";
    RegisterClassW(&wc);
    Ahead& a = ahead();
    a.window.store(CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                   wc.hInstance, nullptr));
    a.running.store(true);
    work();  // what was asked for before the thread was up
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

}  // namespace

void start(HWND owner) {
    Ahead& a = ahead();
    a.owner = owner;
    try {
        std::call_once(a.once, [] { std::thread(menuThread).detach(); });
    } catch (...) {
        // No thread for it: menus are built on the window's thread, as they were.
    }
}

bool running() { return ahead().running.load(); }

void showing(ULONGLONG entry) { ahead().shown.store(entry); }

void stop() {
    Ahead& a = ahead();
    a.quitting.store(true);
    if (!a.running.load()) return;
    // What is being built (or run) is given a moment to be over: a second and a half at most.
    for (int i = 0; i < 150 && a.busy.load() > 0; ++i) Sleep(10);
}

std::wstring kindOf(const Request& what) {
    if (what.paths.empty()) return L"background|" + lower(what.folder);
    if (what.paths.size() != 1) return std::wstring();  // several things: no two selections alike
    const std::wstring& path = what.paths.front();
    if (fsx::isDirectory(path)) return L"folder|" + lower(fsx::parentPath(path));
    return L"file|" + lower(fsx::parentPath(path)) + L"|" + lower(fsx::extensionLower(fsx::fileName(path)));
}

void wake() {
    if (const HWND window = ahead().window.load()) PostMessageW(window, kWakeMessage, 0, 0);
}

void newBatch() {
    Ahead& a = ahead();
    std::lock_guard<std::mutex> lock(a.mutex);
    for (auto it = a.asks.begin(); it != a.asks.end();)
        it = (it->rank == Ahead::Ask::Batch || it->rank == Ahead::Ask::Hover) ? a.asks.erase(it)
                                                                             : it + 1;
}

// Whether one was built for exactly this not long ago (and could be).
bool warm(const Request& what, ULONGLONG maxAgeMs) {
    Ahead& a = ahead();
    std::lock_guard<std::mutex> lock(a.mutex);
    const std::wstring key = keyOf(what);
    for (auto it = a.entries.rbegin(); it != a.entries.rend(); ++it)
        if ((*it)->ok && (*it)->key == key && GetTickCount64() - (*it)->built < maxAgeMs) return true;
    return false;
}

void prefetch(const Request& what) {
    if (warm(what, kWarmMs)) return;
    Ahead& a = ahead();
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        Ahead::Ask ask;
        ask.rank = Ahead::Ask::Batch;
        ask.what = what;
        a.asks.push_back(std::move(ask));
    }
    wake();
}

void hover(const Request& what) {
    if (warm(what, kWarmMs)) return;
    Ahead& a = ahead();
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        for (auto it = a.asks.begin(); it != a.asks.end();)
            it = it->rank == Ahead::Ask::Hover ? a.asks.erase(it) : it + 1;
        Ahead::Ask ask;
        ask.rank = Ahead::Ask::Hover;
        ask.what = what;
        a.asks.push_back(std::move(ask));
    }
    wake();
}

void ask(const Request& what, HWND notify, ULONGLONG serial, ULONGLONG freshMs) {
    Ahead& a = ahead();
    bool ready = false, asked = false;
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        const std::wstring key = keyOf(what);
        const ULONGLONG now = GetTickCount64();
        for (auto it = a.entries.rbegin(); it != a.entries.rend(); ++it)
            if ((*it)->ok && (*it)->key == key && now - (*it)->built < freshMs) ready = true;
        // What was asked for before is not waited for any more: only the last right-click is.
        for (auto it = a.asks.begin(); it != a.asks.end();) {
            const bool stale = it->rank == Ahead::Ask::Show || it->rank == Ahead::Ask::Hover;
            const bool same = it->rank == Ahead::Ask::Batch && keyOf(it->what) == key;
            if (same) {  // on its way already: it is waited for now, and says so
                it->rank = Ahead::Ask::Show;
                it->notify = notify;
                it->serial = serial;
                it->freshMs = (std::min)(it->freshMs, freshMs);
                asked = true;
                ++it;
            } else if (stale) {
                it = a.asks.erase(it);
            } else {
                ++it;
            }
        }
        if (!ready && !asked) {
            Ahead::Ask ask;
            ask.rank = Ahead::Ask::Show;
            ask.what = what;
            ask.notify = notify;
            ask.serial = serial;
            ask.freshMs = freshMs;
            a.asks.push_back(std::move(ask));
        }
    }
    if (ready) {
        if (notify) PostMessageW(notify, kMenuReadyMessage, serial, 0);
    } else {
        wake();
    }
}

Found find(const Request& what) {
    Ahead& a = ahead();
    std::lock_guard<std::mutex> lock(a.mutex);
    Found found;
    const std::wstring key = keyOf(what);
    for (auto it = a.entries.rbegin(); it != a.entries.rend(); ++it) {
        if (!(*it)->ok || (*it)->key != key) continue;
        found.entry = (*it)->id;
        found.ageMs = (int)(GetTickCount64() - (*it)->built);
        (*it)->used = GetTickCount64();
        break;
    }
    return found;
}

Found findKind(const std::wstring& kind) {
    Found found;
    if (kind.empty()) return found;
    Ahead& a = ahead();
    std::lock_guard<std::mutex> lock(a.mutex);
    for (auto it = a.entries.rbegin(); it != a.entries.rend(); ++it) {
        if (!(*it)->ok || (*it)->kind != kind) continue;
        found.entry = (*it)->id;
        found.ageMs = (int)(GetTickCount64() - (*it)->built);
        break;
    }
    return found;
}

bool itemsOf(ULONGLONG entry, std::vector<Item>* out) {
    Ahead& a = ahead();
    std::lock_guard<std::mutex> lock(a.mutex);
    for (const auto& have : a.entries)
        if (have->id == entry && have->ok) {
            *out = have->items;
            return true;
        }
    return false;
}

bool sameSay(const std::vector<Item>& a, const std::vector<Item>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const Item& x = a[i];
        const Item& y = b[i];
        if (x.text != y.text || x.separator != y.separator || x.enabled != y.enabled ||
            x.checked != y.checked || x.isDefault != y.isDefault || x.popup != y.popup)
            return false;
        if (!sameSay(x.sub, y.sub)) return false;
    }
    return true;
}

bool sameRows(const std::vector<Item>& a, const std::vector<Item>& b) {
    // Along the top only: a submenu is read when it is opened, and may have
    // as many rows as it has (发送到 gets its last one a moment after the
    // menu was built).
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].separator != b[i].separator || a[i].popup != b[i].popup) return false;
    return true;
}

void forgetBackgrounds() {
    Ahead& a = ahead();
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        for (auto it = a.entries.begin(); it != a.entries.end();) {
            if ((*it)->kind.compare(0, 11, L"background|") == 0) {
                a.retired.push_back(std::move(*it));
                it = a.entries.erase(it);
            } else {
                ++it;
            }
        }
    }
    wake();
}

void invoke(const Request& what, const std::vector<int>& path, const std::wstring& text,
            HWND notify, ULONGLONG serial) {
    Ahead& a = ahead();
    {
        std::lock_guard<std::mutex> lock(a.mutex);
        Ahead::Ask ask;
        ask.rank = Ahead::Ask::Verb;
        ask.what = what;
        ask.notify = notify;
        ask.serial = serial;
        ask.path = path;
        ask.text = text;
        a.asks.push_back(std::move(ask));
    }
    wake();
}

void makeMenu(HMENU menu, const std::vector<Item>& items, bool asModel, UINT* nextId,
              std::vector<Where>* wheres, std::vector<int>* path) {
    for (int i = 0; i < (int)items.size(); ++i) {
        const Item& item = items[i];
        if (item.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        path->push_back(i);
        const UINT id = asModel ? (*nextId)++ : item.id;
        UINT flags = MF_STRING;
        if (!item.enabled) flags |= MF_GRAYED;
        if (item.checked) flags |= MF_CHECKED;
        if (item.isDefault) flags |= MF_DEFAULT;
        if (item.popup && item.sub.empty()) flags |= MF_GRAYED;  // nothing came into it
        if (item.popup && !item.sub.empty()) {
            HMENU sub = CreatePopupMenu();
            makeMenu(sub, item.sub, asModel, nextId, wheres, path);
            AppendMenuW(menu, flags | MF_POPUP, (UINT_PTR)sub, item.text.c_str());
        } else {
            AppendMenuW(menu, flags, id, item.text.c_str());
        }
        if (wheres && !item.popup && id >= kIdFirst && id <= kIdLast) {
            if (wheres->size() <= id - kIdFirst) wheres->resize(id - kIdFirst + 1);
            (*wheres)[id - kIdFirst].path = *path;
            (*wheres)[id - kIdFirst].text = item.text;
        }
        path->pop_back();
    }
}

HMENU materialize(const std::vector<Item>& items, bool asModel, std::vector<Where>* wheres) {
    if (wheres) wheres->clear();  // (none asked for: null)
    HMENU menu = CreatePopupMenu();
    UINT nextId = kIdFirst;
    std::vector<int> path;
    makeMenu(menu, items, asModel, &nextId, wheres, &path);
    return menu;
}

}  // namespace shellmenu
