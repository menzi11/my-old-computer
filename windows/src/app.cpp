#include "app.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <functional>
#include <map>

#include "dialog.h"
#include "ids.h"
#include "preview.h"

namespace {

int clampInt(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

std::wstring volumeTitle(const std::wstring& root) {
    wchar_t letter = root.empty() ? L'C' : root[0];
    wchar_t label[MAX_PATH + 1]{};
    DWORD serial = 0, maxLen = 0, flags = 0;
    wchar_t fsName[64]{};
    UINT type = GetDriveTypeW(root.c_str());
    bool hasLabel = GetVolumeInformationW(root.c_str(), label, MAX_PATH, &serial, &maxLen, &flags,
                                          fsName, 64) &&
                    label[0] != 0;
    std::wstring prefix;
    switch (type) {
    case DRIVE_REMOVABLE: prefix = L"可移动磁盘"; break;
    case DRIVE_FIXED: prefix = L"本地磁盘"; break;
    case DRIVE_REMOTE: prefix = L"网络驱动器"; break;
    case DRIVE_CDROM: prefix = L"光盘"; break;
    case DRIVE_RAMDISK: prefix = L"内存磁盘"; break;
    default: prefix = L"驱动器"; break;
    }
    std::wstring letterPart;
    letterPart += L" (";
    letterPart += letter;
    letterPart += L":)";
    if (hasLabel) return std::wstring(label) + letterPart;
    return prefix + letterPart;
}

}  // namespace

void App::init(HWND h) {
    hwnd = h;
    settings.load();
    sidebarWidth = settings.sidebarWidth > 0 ? settings.sidebarWidth : 200;
    mode = (ViewMode)clampInt(settings.viewMode, 0, 2);
    showHidden = settings.showHidden;
    useSystemIcons = settings.useSystemIcons;
    preview::setSystemIcons(useSystemIcons);
    buildTree();
    refreshTree();
}

void App::setScale(double s) {
    scale = s;
    m = metricsFor(s);
    relayout(width, height);
}

void App::relayout(int w, int h) {
    width = w;
    height = h;
    m = metricsFor(scale);
    const int e = m.edge;
    const int innerW = w - 2 * e;
    lay.client = RECT{0, 0, w, h};
    lay.title = RECT{e, e, e + innerW, e + m.titleH};
    lay.menu = RECT{e, lay.title.bottom, e + innerW, lay.title.bottom + m.menuH};
    lay.tool = RECT{e, lay.menu.bottom, e + innerW, lay.menu.bottom + m.toolH};
    lay.addr = RECT{e, lay.tool.bottom, e + innerW, lay.tool.bottom + m.addrH};
    const int mainY = lay.addr.bottom;
    const int statusY = h - e - m.statusH;
    lay.status = RECT{e, statusY, e + innerW, h - e};
    int mainH = statusY - mainY;
    if (mainH < 0) mainH = 0;

    auto px = [this](double v) { return (int)(v * scale + 0.5); };
    if (sidebarVisible) {
        int minSw = px(120);
        int maxSw = std::max(minSw, innerW - px(260));
        int sw = clampInt((int)(sidebarWidth * scale + 0.5), minSw, maxSw);
        lay.sidebar = RECT{e, mainY, e + sw, mainY + mainH};
        lay.splitter = RECT{e + sw, mainY, e + sw + m.splitterW, mainY + mainH};
        lay.content = RECT{e + sw + m.splitterW, mainY, e + innerW, mainY + mainH};
    } else {
        lay.sidebar = RECT{0, 0, 0, 0};
        lay.splitter = RECT{0, 0, 0, 0};
        lay.content = RECT{e, mainY, e + innerW, mainY + mainH};
    }

    // Tree pane internals.
    lay.treeHeader = RECT{lay.sidebar.left + 1, lay.sidebar.top + 1,
                          lay.sidebar.right - 1, lay.sidebar.top + m.headerH};
    lay.treeViewport = RECT{lay.sidebar.left + 1, lay.sidebar.top + m.headerH + 1,
                            lay.sidebar.right - 1 - m.scrollerW, lay.sidebar.bottom - 1};
    lay.treeBar = RECT{lay.sidebar.right - 1 - m.scrollerW, lay.sidebar.top + m.headerH + 1,
                       lay.sidebar.right - 1, lay.sidebar.bottom - 1};

    // Details view internals.
    const int contentW = lay.content.right - lay.content.left;
    const int contentH = lay.content.bottom - lay.content.top;
    const int tableW = std::max(0, contentW - 4 - m.scrollerW);
    lay.listHeader = RECT{lay.content.left + 2, lay.content.top, lay.content.left + 2 + tableW,
                          lay.content.top + m.headerH};
    lay.listViewport = RECT{lay.content.left + 2, lay.content.top + m.headerH,
                            lay.content.left + 2 + tableW, lay.content.bottom - 2};
    lay.listBar = RECT{lay.content.right - 2 - m.scrollerW, lay.content.top + m.headerH,
                       lay.content.right - 2, lay.content.bottom - 2};
    lay.gridViewport = RECT{lay.content.left + 2, lay.content.top + 2,
                            lay.content.right - 2 - m.scrollerW, lay.content.bottom - 2};
    lay.gridBar = RECT{lay.content.right - 2 - m.scrollerW, lay.content.top + 2,
                       lay.content.right - 2, lay.content.bottom - 2};

    const int fixed = px(80) + px(100) + px(130);
    lay.nameW = std::max(px(120), tableW - fixed);
    lay.colW[0] = lay.nameW;
    lay.colW[1] = px(80);
    lay.colW[2] = px(100);
    lay.colW[3] = px(130);
    lay.colX[0] = lay.listHeader.left;
    for (int i = 1; i < 4; ++i) lay.colX[i] = lay.colX[i - 1] + lay.colW[i - 1];

    // Address bar internals.
    const int addrW = lay.addr.right - lay.addr.left;
    const int labelW = px(52), goW = px(56);
    lay.addrField = RECT{lay.addr.left + labelW + px(6), lay.addr.top + px(3),
                         lay.addr.right - goW - px(18) - px(17), lay.addr.top + px(23)};
    lay.addrArrow = RECT{lay.addr.right - goW - px(5) - px(17), lay.addr.top + px(5),
                         lay.addr.right - goW - px(5) - px(2), lay.addr.top + px(21)};
    lay.addrGo = RECT{lay.addr.right - goW - px(5), lay.addr.top + px(3), lay.addr.right - px(5),
                      lay.addr.top + px(23)};

    // Status panels.
    lay.statusLeft = RECT{lay.status.left + 1, lay.status.top + 2, lay.status.left + 1 + px(240),
                          lay.status.bottom - 1};
    lay.statusRight = RECT{lay.status.left + 1 + px(243), lay.status.top + 2, lay.status.right - 1,
                           lay.status.bottom - 1};

    clampScroll();
}

// ---------------------------------------------------------------- navigation

void App::loadDirectory(const std::wstring& path, bool recordHistory, bool fromTreeClick) {
    std::wstring target = path;
    while (target.size() > 3 && (target.back() == L'\\' || target.back() == L'/')) target.pop_back();
    if (target.empty()) target = fsx::rootOf(current);
    if (!fsx::exists(target) || !fsx::isDirectory(target)) {
        MessageBeep(MB_ICONASTERISK);
        return;
    }
    if (_wcsicmp(target.c_str(), current.c_str()) == 0) {
        if (!fromTreeClick) {
            treeClickRow = -1;
            revealInTree();
            scrollTreeToSelection();
        }
        reload();
        return;
    }
    if (recordHistory && !current.empty()) {
        back.push_back(current);
        forward.clear();
    }
    current = target;
    contextFolder.clear();
    selection.clear();
    entries = fsx::listDirectory(current, showHidden);
    fsx::sortEntries(entries, sortColumn, sortAsc);
    listScroll = 0;
    gridScroll = 0;
    if (fromTreeClick) {
        // The tree keeps its expansion and scroll position; only the highlight
        // and the right-hand pane change (see the click handler).
    } else {
        treeClickRow = -1;
        revealInTree();
        flattenTree();
        scrollTreeToSelection();
    }
    clampScroll();
    if (addrEdit && addrEditing) SetWindowTextW(addrEdit, fsx::abbreviateHome(current).c_str());
}

void App::reload() {
    entries = fsx::listDirectory(current, showHidden);
    fsx::sortEntries(entries, sortColumn, sortAsc);
    std::set<std::wstring> keep;
    for (auto& e : entries)
        if (selection.count(e.path)) keep.insert(e.path);
    selection = std::move(keep);
    clampScroll();
    noteChildren(current, std::any_of(entries.begin(), entries.end(), [](const Entry& e) { return e.isDir; }));
}

void App::goBack() {
    if (back.empty()) return;
    std::wstring prev = back.back();
    back.pop_back();
    forward.push_back(current);
    loadDirectory(prev, false);
}

void App::goForward() {
    if (forward.empty()) return;
    std::wstring next = forward.back();
    forward.pop_back();
    back.push_back(current);
    loadDirectory(next, false);
}

void App::goUp() {
    std::wstring parent = fsx::parentPath(current);
    if (_wcsicmp(parent.c_str(), current.c_str()) == 0) return;
    loadDirectory(parent);
}

bool App::goToTypedPath(const std::wstring& text) {
    std::wstring p = text;
    while (!p.empty() && (p.front() == L' ' || p.front() == L'\t')) p.erase(p.begin());
    while (!p.empty() && (p.back() == L' ' || p.back() == L'\t')) p.pop_back();
    p = fsx::expandHome(p);
    if (p.empty()) return false;
    if (!fsx::exists(p) || !fsx::isDirectory(p)) {
        MessageBeep(MB_ICONASTERISK);
        return false;
    }
    loadDirectory(p);
    return true;
}

void App::setViewMode(ViewMode v) {
    if (mode == v) return;
    mode = v;
    clampScroll();
    settings.viewMode = (int)v;
    flattenTree();
}

void App::cycleViewMode() {
    switch (mode) {
    case ViewMode::Details: setViewMode(ViewMode::Icons); break;
    case ViewMode::Icons: setViewMode(ViewMode::List); break;
    case ViewMode::List: setViewMode(ViewMode::Details); break;
    }
}

// ---------------------------------------------------------------- selection

void App::selectAll() {
    selection.clear();
    for (auto& e : entries) selection.insert(e.path);
}

void App::clearSelection() { selection.clear(); }

std::vector<const Entry*> App::selectedEntries() const {
    std::vector<const Entry*> out;
    for (auto& e : entries)
        if (selection.count(e.path)) out.push_back(&e);
    return out;
}

const Entry* App::firstSelected() const {
    for (auto& e : entries)
        if (selection.count(e.path)) return &e;
    return nullptr;
}

// ---------------------------------------------------------------- operations

void App::openEntry(const Entry& e) {
    if (e.isDir) {
        loadDirectory(e.path);
    } else {
        if (!fsx::openWithShell(e.path)) showError(hwnd, L"打开", L"无法打开该项。");
    }
}

void App::openSelection() {
    if (const Entry* e = firstSelected()) openEntry(*e);
}

void App::newFolder() {
    std::wstring base = contextFolder.empty() ? current : contextFolder;
    contextFolder.clear();
    std::wstring created;
    if (!fsx::makeFolder(base, L"新建文件夹", &created)) {
        showError(hwnd, L"新建文件夹", L"无法创建文件夹。");
        return;
    }
    if (_wcsicmp(base.c_str(), current.c_str()) != 0) loadDirectory(base);
    else reload();
    selection.clear();
    selection.insert(created);
}

void App::renameSelection() {
    const Entry* e = firstSelected();
    if (!e) return;
    std::wstring name = e->name;
    std::wstring full = e->path;
    if (!promptText(hwnd, L"重命名", L"输入新名称：", name, &name)) return;
    if (name.empty() || name == fsx::fileName(full)) return;
    std::wstring error;
    if (!fsx::renameItem(full, name, &error)) {
        showError(hwnd, L"重命名", error);
        return;
    }
    std::wstring renamed = fsx::joinPath(fsx::parentPath(full), name);
    reload();
    selection.clear();
    selection.insert(renamed);
}

void App::deleteSelection() {
    auto sel = selectedEntries();
    if (sel.empty()) return;
    std::vector<std::wstring> paths;
    for (auto* e : sel) paths.push_back(e->path);
    if (!fsx::recycle(paths)) showError(hwnd, L"删除", L"无法将选定的项目移入回收站。");
    selection.clear();
    reload();
}

void App::copySelection(bool cut) {
    auto sel = selectedEntries();
    if (sel.empty()) return;
    clip.clear();
    for (auto* e : sel) clip.push_back(e->path);
    clipCut = cut;

    // Publish the file list so Explorer and other apps can paste it.
    std::wstring list;
    for (auto& p : clip) {
        list += p;
        list.push_back(L'\0');
    }
    list.push_back(L'\0');
    size_t bytes = (list.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, sizeof(DROPFILES) + bytes);
    if (mem) {
        auto* df = (DROPFILES*)GlobalLock(mem);
        df->pFiles = sizeof(DROPFILES);
        df->fWide = TRUE;
        memcpy((char*)df + sizeof(DROPFILES), list.c_str(), bytes);
        GlobalUnlock(mem);
        if (OpenClipboard(hwnd)) {
            EmptyClipboard();
            SetClipboardData(CF_HDROP, mem);
            UINT effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
            HGLOBAL fx = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
            if (fx) {
                *(DWORD*)GlobalLock(fx) = effect;
                GlobalUnlock(fx);
                SetClipboardData(RegisterClipboardFormatW(L"Preferred DropEffect"), fx);
            }
            CloseClipboard();
        } else {
            GlobalFree(mem);
        }
    }
}

void App::paste() {
    std::vector<std::wstring> urls = clip;
    bool cut = clipCut;
    if (urls.empty()) {
        if (OpenClipboard(hwnd)) {
            HANDLE h = GetClipboardData(CF_HDROP);
            if (h) {
                HDROP drop = (HDROP)h;
                UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
                for (UINT i = 0; i < n; ++i) {
                    wchar_t buf[32768];
                    if (DragQueryFileW(drop, i, buf, 32768)) urls.push_back(buf);
                }
            }
            CloseClipboard();
        }
        cut = false;
    }
    if (urls.empty()) {
        MessageBeep(MB_ICONASTERISK);
        return;
    }
    std::wstring dest = contextFolder.empty() ? current : contextFolder;
    contextFolder.clear();
    std::vector<std::wstring> items;
    for (auto& u : urls) {
        if (_wcsicmp(u.c_str(), dest.c_str()) == 0) continue;
        if (fsx::isInside(dest, u)) {
            MessageBeep(MB_ICONASTERISK);
            continue;
        }
        items.push_back(u);
    }
    if (!items.empty()) {
        bool ok = cut ? fsx::moveItems(items, dest) : fsx::copyItems(items, dest);
        if (!ok) MessageBeep(MB_ICONASTERISK);
    }
    if (cut && !items.empty()) {
        clip.clear();
        clipCut = false;
    }
    reload();
}

void App::copyPaths() {
    std::wstring text;
    for (auto* e : selectedEntries()) {
        if (!text.empty()) text += L"\r\n";
        text += e->path;
    }
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            memcpy(GlobalLock(mem), text.c_str(), bytes);
            GlobalUnlock(mem);
            SetClipboardData(CF_UNICODETEXT, mem);
        }
        CloseClipboard();
    }
}

void App::revealSelection() {
    if (const Entry* e = firstSelected()) fsx::revealInExplorer(e->path);
}

void App::openTerminal() { fsx::openTerminalAt(current); }

bool App::performDrop(const std::vector<std::wstring>& paths, const std::wstring& folder) {
    std::wstring dest = folder.empty() ? current : folder;
    if (!fsx::isDirectory(dest)) {
        MessageBeep(MB_ICONASTERISK);
        return false;
    }
    std::vector<std::wstring> items;
    for (auto& p : paths) {
        if (_wcsicmp(p.c_str(), dest.c_str()) == 0) continue;
        // Dropping an item back into its own folder is a no-op, not a copy.
        if (_wcsicmp(fsx::parentPath(p).c_str(), dest.c_str()) == 0) continue;
        if (fsx::isInside(dest, p)) {
            MessageBeep(MB_ICONASTERISK);
            continue;
        }
        items.push_back(p);
    }
    if (items.empty()) return false;
    bool ok = fsx::sameVolume(items.front(), dest) ? fsx::moveItems(items, dest)
                                                  : fsx::copyItems(items, dest);
    if (!ok) MessageBeep(MB_ICONASTERISK);
    reload();
    return ok;
}

// ---------------------------------------------------------------- favorites

void App::addFavorite(const std::wstring& path) {
    std::wstring p = path;
    while (p.size() > 3 && p.back() == L'\\') p.pop_back();
    if (p.empty()) return;
    for (auto& f : settings.favorites)
        if (_wcsicmp(f.c_str(), p.c_str()) == 0) return;
    settings.favorites.push_back(p);
    settings.save();
    refreshTree();
}

void App::removeFavorite(const std::wstring& path) {
    settings.favorites.erase(
        std::remove_if(settings.favorites.begin(), settings.favorites.end(),
                       [&](const std::wstring& f) { return _wcsicmp(f.c_str(), path.c_str()) == 0; }),
        settings.favorites.end());
    settings.save();
    refreshTree();
}

// ---------------------------------------------------------------- tree

bool App::treeHasSubdir(const std::wstring& path) {
    static std::map<std::wstring, bool, std::less<std::wstring>> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second;
    bool v = fsx::hasAnySubdirectory(path, showHidden);
    cache[path] = v;
    return v;
}

void App::noteChildren(const std::wstring& dir, bool hasSubdir) {
    // Refresh the +/- state of the tree row for dir without a full rebuild.
    for (auto& root : treeRoots) {
        std::vector<TreeNode*> stack{&root};
        while (!stack.empty()) {
            TreeNode* n = stack.back();
            stack.pop_back();
            if (n->path == dir) n->hasChildren = hasSubdir;
            for (auto& c : n->children) stack.push_back(&c);
        }
    }
}

void App::buildTree() {
    treeRoots.clear();
    treeClickRow = -1;
    if (demo) {
        TreeNode machine;
        machine.kind = TreeNode::Machine;
        machine.title = L"我的电脑";
        machine.path = L"";
        machine.depth = 0;
        machine.expanded = true;
        machine.loaded = true;
        TreeNode vol;
        vol.kind = TreeNode::Volume;
        vol.path = current;
        vol.title = L"My Old Computer";
        vol.depth = 1;
        vol.hasChildren = true;
        machine.children.push_back(std::move(vol));
        machine.hasChildren = true;
        treeRoots.push_back(std::move(machine));
        flattenTree();
        return;
    }
    TreeNode machine;
    machine.kind = TreeNode::Machine;
    machine.title = L"我的电脑";
    machine.path = L"";
    machine.depth = 0;
    machine.expanded = true;
    machine.loaded = true;
    wchar_t drives[1024]{};
    DWORD n = GetLogicalDriveStringsW(1024, drives);
    if (n > 0) {
        for (wchar_t* p = drives; *p; p += wcslen(p) + 1) {
            std::wstring root(p);
            UINT type = GetDriveTypeW(root.c_str());
            if (type == DRIVE_UNKNOWN || type == DRIVE_NO_ROOT_DIR) continue;
            TreeNode vol;
            vol.kind = TreeNode::Volume;
            vol.path = root;
            vol.title = volumeTitle(root);
            vol.depth = 1;
            vol.hasChildren = treeHasSubdir(root);
            machine.children.push_back(std::move(vol));
        }
    }
    machine.hasChildren = !machine.children.empty();
    treeRoots.push_back(std::move(machine));

    if (!settings.favorites.empty()) {
        TreeNode group;
        group.kind = TreeNode::Favorites;
        group.title = L"收藏夹";
        group.path = fsx::homeDir();
        group.depth = 0;
        group.expanded = true;
        group.loaded = true;
        for (auto& f : settings.favorites) {
            if (!fsx::exists(f) || !fsx::isDirectory(f)) continue;
            TreeNode fav;
            fav.kind = TreeNode::Favorite;
            fav.path = f;
            fav.title = fsx::fileName(f);
            if (fav.title.empty()) fav.title = f;
            fav.depth = 1;
            fav.hasChildren = treeHasSubdir(f);
            group.children.push_back(std::move(fav));
        }
        if (!group.children.empty()) {
            group.hasChildren = true;
            treeRoots.push_back(std::move(group));
        }
    }

    struct Special { const wchar_t* title; REFKNOWNFOLDERID id; };
    const Special specials[] = {
        {L"桌面", FOLDERID_Desktop},
        {L"我的文档", FOLDERID_Documents},
        {L"下载", FOLDERID_Downloads},
        {L"我的图片", FOLDERID_Pictures},
        {L"我的音乐", FOLDERID_Music},
        {L"我的影片", FOLDERID_Videos},
    };
    for (auto& s : specials) {
        std::wstring p = fsx::knownFolder(s.id);
        if (p.empty()) continue;
        TreeNode node;
        node.kind = TreeNode::Special;
        node.path = p;
        node.title = s.title;
        node.depth = 0;
        node.hasChildren = treeHasSubdir(p);
        treeRoots.push_back(std::move(node));
    }
    flattenTree();
}

void App::loadTreeChildren(TreeNode& node) {
    if (node.loaded) return;
    node.loaded = true;
    node.children.clear();
    if (node.path.empty()) {
        node.hasChildren = false;
        return;
    }
    auto list = fsx::listDirectory(node.path, showHidden);
    for (auto& e : list) {
        if (!e.isDir) continue;
        TreeNode kid;
        kid.kind = TreeNode::Folder;
        kid.path = e.path;
        kid.title = e.name;
        kid.depth = node.depth + 1;
        kid.hasChildren = true;
        node.children.push_back(std::move(kid));
    }
    std::sort(node.children.begin(), node.children.end(),
              [](const TreeNode& a, const TreeNode& b) {
                  return StrCmpLogicalW(a.title.c_str(), b.title.c_str()) < 0;
              });
    for (auto& c : node.children) c.hasChildren = treeHasSubdir(c.path);
    node.hasChildren = !node.children.empty();
}

void App::flattenTree() {
    treeFlat.clear();
    std::function<void(TreeNode&)> walk = [&](TreeNode& n) {
        treeFlat.push_back(&n);
        if (n.expanded)
            for (auto& c : n.children) walk(c);
    };
    for (auto& r : treeRoots) walk(r);
}

int App::treeSelectedRow() const {
    if (treeClickRow >= 0 && treeClickRow < (int)treeFlat.size() &&
        _wcsicmp(treeFlat[treeClickRow]->path.c_str(), current.c_str()) == 0)
        return treeClickRow;
    int fallback = -1;
    for (int i = 0; i < (int)treeFlat.size(); ++i) {
        TreeNode* n = treeFlat[i];
        if (_wcsicmp(n->path.c_str(), current.c_str()) != 0) continue;
        if (n->kind == TreeNode::Special || n->kind == TreeNode::Favorite ||
            n->kind == TreeNode::Favorites) {
            if (fallback < 0) fallback = i;
        } else {
            return i;
        }
    }
    return fallback;
}

void App::scrollTreeToSelection() {
    int idx = treeSelectedRow();
    if (idx < 0) return;
    RECT r = treeRowRect(idx);
    const int top = lay.treeViewport.top + (int)(2 * scale + 0.5);
    const int bottom = lay.treeViewport.bottom;
    if (r.top < top) treeScroll -= (top - r.top);
    else if (r.bottom > bottom) treeScroll += (r.bottom - bottom);
    clampScroll();
}

void App::revealInTree() {
    std::function<bool(TreeNode&, const std::wstring&)> walk = [&](TreeNode& n,
                                                                    const std::wstring& target) -> bool {
        if (n.kind == TreeNode::Favorites) {
            for (auto& c : n.children)
                if (_wcsicmp(c.path.c_str(), target.c_str()) == 0) return false;
            return false;
        }
        std::wstring base = n.path;
        bool inside = base.empty() ? !target.empty() : fsx::isInside(target, base);
        if (!inside) return false;
        if (n.kind == TreeNode::Machine) {
            n.expanded = true;
            if (!n.loaded) loadTreeChildren(n);
        }
        if (_wcsicmp(target.c_str(), base.c_str()) == 0) {
            if (n.kind != TreeNode::Machine) {
                if (!n.loaded) loadTreeChildren(n);
                n.expanded = n.hasChildren;
            }
            return true;
        }
        if (!n.loaded) loadTreeChildren(n);
        n.expanded = true;
        for (auto& c : n.children)
            if (walk(c, target)) return true;
        return false;
    };

    bool found = false;
    for (auto& r : treeRoots)
        if (walk(r, current)) {
            found = true;
            break;
        }
    if (!found) {
        for (auto& r : treeRoots) {
            if (r.kind == TreeNode::Special || r.kind == TreeNode::Favorite) {
                if (_wcsicmp(r.path.c_str(), current.c_str()) == 0) break;
            }
        }
    }
    flattenTree();
}

void App::toggleTreeNode(TreeNode& node) {
    treeClickRow = -1;
    if (!node.expanded) {
        loadTreeChildren(node);
        node.expanded = true;
    } else {
        node.expanded = false;
    }
    flattenTree();
}

void App::refreshTree() {
    buildTree();
    revealInTree();
    flattenTree();
}

// ---------------------------------------------------------------- geometry

// Text measurement for the custom chrome.  A throwaway DC keeps geometry and
// hit testing identical to what the renderer draws.
int measureUiText(const std::wstring& s, int fontPx) {
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return (int)s.size() * fontPx / 2;
    HFONT f = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                          OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    HGDIOBJ of = SelectObject(dc, f);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    SelectObject(dc, of);
    DeleteObject(f);
    DeleteDC(dc);
    return sz.cx;
}

extern const wchar_t* const kMenuTitles[6] = {L"文件", L"编辑", L"查看", L"收藏", L"工具", L"帮助"};
extern const wchar_t* const kToolTitles[7] = {L"后退", L"前进", L"向上", L"文件夹",
                                              L"查看", L"刷新", L"隐藏文件"};

RECT App::titleButtonRect(int i) const {
    const int e = m.edge;
    const int innerW = width - 2 * e;
    const int btW = (int)(16 * scale + 0.5), btH = (int)(14 * scale + 0.5);
    const int gap = (int)(2 * scale + 0.5);
    const int right = e + innerW - 2;
    const int x = right - (3 - i) * (btW + gap);
    return RECT{x, e + (int)(4 * scale + 0.5), x + btW, e + (int)(4 * scale + 0.5) + btH};
}

int App::toolbarItemCount() const { return 7; }

RECT App::toolbarItemRect(int i) const {
    const int fontPx = (int)(14.6667 * scale + 0.5);
    int x = lay.tool.left + (int)(3 * scale + 0.5);
    const int y = lay.tool.top + (int)(2 * scale + 0.5);
    const int h = (lay.tool.bottom - lay.tool.top) - (int)(4 * scale + 0.5);
    for (int j = 0; j < 7; ++j) {
        int tw = measureUiText(kToolTitles[j], fontPx);
        int w = (int)((6 + 16 + 4) * scale + 0.5) + tw + (int)(6 * scale + 0.5);
        if (j == i) return RECT{x, y, x + w, y + h};
        x += w + (int)(1 * scale + 0.5);
    }
    return RECT{0, 0, 0, 0};
}

int App::toolbarIndexAt(POINT p) const {
    for (int i = 0; i < 7; ++i)
        if (PtInRect(&toolbarItemRect(i), p)) return i;
    return -1;
}

RECT App::menuItemRect(int i) const {
    const int fontPx = (int)(14.6667 * scale + 0.5);
    int x = lay.menu.left + (int)(2 * scale + 0.5);
    for (int j = 0; j < 6; ++j) {
        int w = measureUiText(kMenuTitles[j], fontPx) + (int)(16 * scale + 0.5);
        if (j == i)
            return RECT{x, lay.menu.top + (int)(1 * scale + 0.5), x + w,
                        lay.menu.top + m.menuH - (int)(1 * scale + 0.5)};
        x += w;
    }
    return RECT{0, 0, 0, 0};
}

int App::menuItemAt(POINT p) const {
    for (int i = 0; i < 6; ++i)
        if (PtInRect(&menuItemRect(i), p)) return i;
    return -1;
}

RECT App::treeRowRect(int i) const {
    const int x = lay.treeViewport.left;
    const int y = lay.treeViewport.top + (int)(2 * scale + 0.5) + i * m.rowH - treeScroll;
    return RECT{x, y, lay.treeViewport.right, y + m.rowH};
}

int App::treeRowAt(POINT p) const {
    if (p.x < lay.treeViewport.left || p.x >= lay.treeViewport.right) return -1;
    if (p.y < lay.treeViewport.top || p.y >= lay.treeViewport.bottom) return -1;
    int local = p.y - lay.treeViewport.top - (int)(2 * scale + 0.5) + treeScroll;
    if (local < 0) return -1;
    int idx = local / std::max(1, m.rowH);
    return idx < (int)treeFlat.size() ? idx : -1;
}

int App::treeRowCount() const { return (int)treeFlat.size(); }

RECT App::listRowRect(int i) const {
    const int y = lay.listViewport.top + i * m.rowH - listScroll;
    return RECT{lay.listViewport.left, y, lay.listViewport.right, y + m.rowH};
}

int App::listRowAt(POINT p) const {
    if (p.x < lay.listViewport.left || p.x >= lay.listViewport.right) return -1;
    if (p.y < lay.listViewport.top || p.y >= lay.listViewport.bottom) return -1;
    int local = p.y - lay.listViewport.top + listScroll;
    if (local < 0) return -1;
    int idx = local / std::max(1, m.rowH);
    return idx < (int)entries.size() ? idx : -1;
}

int App::listVisibleRows() const {
    return std::max(1, (int)(lay.listViewport.bottom - lay.listViewport.top) / std::max(1, m.rowH));
}

int App::gridCellW() const { return (int)((mode == ViewMode::Icons ? 94 : 220) * scale + 0.5); }
int App::gridCellH() const { return (int)((mode == ViewMode::Icons ? 94 : 19) * scale + 0.5); }

int App::gridCols() const {
    int w = (int)(lay.gridViewport.right - lay.gridViewport.left);
    return std::max(1, (int)((w - (int)(8 * scale + 0.5)) / std::max(1, gridCellW())));
}

int App::gridCellCount() const { return (int)entries.size(); }

RECT App::gridCellRect(int i) const {
    const int cols = gridCols();
    const int cw = gridCellW(), ch = gridCellH();
    int col = cols > 0 ? i % cols : 0;
    int row = cols > 0 ? i / cols : 0;
    int x = lay.gridViewport.left + (int)(4 * scale + 0.5) + col * cw;
    int y = lay.gridViewport.top + (int)(4 * scale + 0.5) + row * ch - gridScroll;
    return RECT{x, y, x + cw, y + ch};
}

int App::gridIndexAt(POINT p) const {
    if (p.x < lay.gridViewport.left || p.x >= lay.gridViewport.right) return -1;
    if (p.y < lay.gridViewport.top || p.y >= lay.gridViewport.bottom) return -1;
    const int cols = gridCols();
    const int cw = gridCellW(), ch = gridCellH();
    int lx = p.x - lay.gridViewport.left - (int)(4 * scale + 0.5);
    int ly = p.y - lay.gridViewport.top - (int)(4 * scale + 0.5) + gridScroll;
    if (lx < 0 || ly < 0) return -1;
    int col = lx / cw;
    int row = ly / ch;
    if (col >= cols) return -1;
    int idx = row * cols + col;
    return idx < (int)entries.size() ? idx : -1;
}

int App::gridContentHeight() const {
    int n = (int)entries.size();
    int cols = gridCols();
    int rows = (n + cols - 1) / cols;
    return rows * gridCellH() + (int)(8 * scale + 0.5);
}

int App::listContentHeight() const {
    return (int)entries.size() * m.rowH + (int)(4 * scale + 0.5);
}

int App::treeContentHeight() const {
    return (int)treeFlat.size() * m.rowH + (int)(4 * scale + 0.5);
}

void App::clampScroll() {
    auto clamp = [](int& v, int content, int view) {
        int maxV = std::max(0, content - view);
        if (v > maxV) v = maxV;
        if (v < 0) v = 0;
    };
    int listView = std::max(0, (int)(lay.listViewport.bottom - lay.listViewport.top));
    int gridView = std::max(0, (int)(lay.gridViewport.bottom - lay.gridViewport.top));
    int treeView = std::max(0, (int)(lay.treeViewport.bottom - lay.treeViewport.top));
    clamp(listScroll, listContentHeight(), listView);
    clamp(gridScroll, gridContentHeight(), gridView);
    clamp(treeScroll, treeContentHeight(), treeView);
}

void App::scrollListBy(int delta) {
    listScroll += delta;
    clampScroll();
}
void App::scrollGridBy(int delta) {
    gridScroll += delta;
    clampScroll();
}
void App::scrollTreeBy(int delta) {
    treeScroll += delta;
    clampScroll();
}

std::wstring App::statusLeftText() const {
    wchar_t buf[128];
    int sel = (int)selection.size();
    if (sel > 0)
        swprintf_s(buf, L"%d 个对象，已选定 %d 个对象", (int)entries.size(), sel);
    else
        swprintf_s(buf, L"%d 个对象", (int)entries.size());
    return buf;
}

// ---------------------------------------------------------------- menus

std::vector<std::wstring> App::menuFavorites() const {
    std::vector<std::wstring> out;
    for (auto& f : settings.favorites)
        if (fsx::exists(f)) out.push_back(f);
    return out;
}

std::wstring App::menuTitle(int index) const {
    static const wchar_t* titles[6] = {L"文件", L"编辑", L"查看", L"收藏", L"工具", L"帮助"};
    if (index < 0 || index >= 6) return L"";
    return titles[index];
}

std::vector<MenuItem> App::menuFor(const std::wstring& name) const {
    std::vector<MenuItem> m;
    const bool hasSel = !selection.empty();
    if (name == L"文件") {
        m.push_back({L"打开", ID_OPEN, hasSel});
        m.push_back({L"快速查看", ID_PREVIEW, hasSel, false, false, L"Space"});
        m.push_back({L"新建文件夹", ID_NEW_FOLDER, true});
        m.push_back({L"", 0, false, false, true});
        m.push_back({L"重命名", ID_RENAME, hasSel});
        m.push_back({L"删除", ID_DELETE, hasSel});
        m.push_back({L"", 0, false, false, true});
        m.push_back({L"关闭", ID_CLOSE, true, false, false, L"Alt+F4"});
    } else if (name == L"编辑") {
        m.push_back({L"剪切", ID_CUT, hasSel, false, false, L"Ctrl+X"});
        m.push_back({L"复制", ID_COPY, hasSel, false, false, L"Ctrl+C"});
        m.push_back({L"粘贴", ID_PASTE, true, false, false, L"Ctrl+V"});
        m.push_back({L"", 0, false, false, true});
        m.push_back({L"全选", ID_SELECT_ALL, true, false, false, L"Ctrl+A"});
        m.push_back({L"复制路径", ID_COPY_PATH, hasSel});
        m.push_back({L"", 0, false, false, true});
        m.push_back({L"在资源管理器中显示", ID_REVEAL, hasSel});
    } else if (name == L"查看") {
        m.push_back({L"刷新", ID_REFRESH, true, false, false, L"F5"});
        m.push_back({L"", 0, false, false, true});
        m.push_back({L"显示隐藏文件", ID_TOGGLE_HIDDEN, true, showHidden});
        m.push_back({L"使用系统图标", ID_TOGGLE_SYSICONS, true, useSystemIcons});
        m.push_back({L"", 0, false, false, true});
        m.push_back({L"大图标", ID_VIEW_BIG, true, mode == ViewMode::Icons});
        m.push_back({L"列表", ID_VIEW_LIST, true, mode == ViewMode::List});
        m.push_back({L"详细信息", ID_VIEW_DETAILS, true, mode == ViewMode::Details});
    } else if (name == L"收藏") {
        m.push_back({L"添加当前文件夹到收藏夹", ID_ADD_FAV_CURRENT, true});
        auto favs = menuFavorites();
        if (!favs.empty()) {
            m.push_back({L"", 0, false, false, true});
            for (size_t i = 0; i < favs.size(); ++i)
                m.push_back({fsx::fileName(favs[i]), (int)(ID_FAV_BASE + i), true});
        }
    } else if (name == L"工具") {
        m.push_back({L"在命令提示符中打开", ID_OPEN_TERMINAL, true});
        m.push_back({L"", 0, false, false, true});
        m.push_back({L"文件夹选项", 0, false});
    } else if (name == L"帮助") {
        m.push_back({L"关于 My Old Computer", ID_ABOUT, true});
    }
    return m;
}

std::vector<MenuItem> App::contextMenu() const {
    std::vector<MenuItem> m;
    const bool hasSel = !selection.empty();
    m.push_back({L"打开", ID_OPEN, hasSel});
    m.push_back({L"快速查看", ID_PREVIEW, hasSel});
    m.push_back({L"在资源管理器中显示", ID_REVEAL, hasSel});
    m.push_back({L"", 0, false, false, true});
    m.push_back({L"剪切", ID_CUT, hasSel});
    m.push_back({L"复制", ID_COPY, hasSel});
    m.push_back({L"粘贴", ID_PASTE, true});
    m.push_back({L"", 0, false, false, true});
    m.push_back({L"新建文件夹", ID_NEW_FOLDER, true});
    m.push_back({L"重命名", ID_RENAME, hasSel});
    m.push_back({L"删除", ID_DELETE, hasSel});
    m.push_back({L"", 0, false, false, true});
    m.push_back({L"添加到收藏夹", ID_ADD_FAV, true});
    if (contextIsFavorite) m.push_back({L"从收藏夹中移除", ID_REMOVE_FAV, true});
    m.push_back({L"在命令提示符中打开", ID_OPEN_TERMINAL, true});
    return m;
}

std::vector<MenuItem> App::treeContextMenu(const std::wstring& nodePath, bool isFavorite) const {
    std::vector<MenuItem> m;
    m.push_back({L"打开", ID_OPEN, !nodePath.empty()});
    m.push_back({L"在资源管理器中显示", ID_REVEAL, !nodePath.empty()});
    m.push_back({L"", 0, false, false, true});
    m.push_back({L"新建文件夹", ID_NEW_FOLDER, true});
    m.push_back({L"粘贴", ID_PASTE, true});
    m.push_back({L"", 0, false, false, true});
    m.push_back({L"添加到收藏夹", ID_ADD_FAV, !nodePath.empty()});
    if (isFavorite) m.push_back({L"从收藏夹中移除", ID_REMOVE_FAV, true});
    m.push_back({L"在命令提示符中打开", ID_OPEN_TERMINAL, !nodePath.empty()});
    return m;
}
std::vector<MenuItem> App::placesMenu() const {
    std::vector<MenuItem> m;
    m.push_back({L"我的电脑", ID_FAV_BASE + 900, true});
    m.push_back({L"个人文件夹", ID_FAV_BASE + 901, true});
    m.push_back({L"", 0, false, false, true});
    struct Special { const wchar_t* title; REFKNOWNFOLDERID id; };
    const Special specials[] = {
        {L"桌面", FOLDERID_Desktop},       {L"我的文档", FOLDERID_Documents},
        {L"下载", FOLDERID_Downloads},     {L"我的图片", FOLDERID_Pictures},
        {L"我的音乐", FOLDERID_Music},     {L"我的影片", FOLDERID_Videos},
    };
    for (auto& s : specials) {
        std::wstring p = fsx::knownFolder(s.id);
        if (!p.empty()) m.push_back({s.title, ID_FAV_BASE + 910 + (int)(&s - specials), true});
    }
    return m;
}

void App::performMenuAction(int id, const std::wstring& arg) {
    (void)arg;
    switch (id) {
    case ID_OPEN:
        if (!selection.empty()) {
            openSelection();
        } else if (!contextFolder.empty()) {
            if (fsx::isDirectory(contextFolder)) loadDirectory(contextFolder);
            else if (!fsx::openWithShell(contextFolder))
                showError(hwnd, L"打开", L"无法打开该项。");
            contextFolder.clear();
        }
        break;
    case ID_PREVIEW:
        if (const Entry* e = firstSelected()) preview::show(hwnd, e->path);
        break;
    case ID_NEW_FOLDER: newFolder(); break;
    case ID_RENAME: renameSelection(); break;
    case ID_DELETE: deleteSelection(); break;
    case ID_CLOSE: PostMessageW(hwnd, WM_CLOSE, 0, 0); break;
    case ID_CUT: copySelection(true); break;
    case ID_COPY: copySelection(false); break;
    case ID_PASTE: paste(); break;
    case ID_SELECT_ALL: selectAll(); break;
    case ID_COPY_PATH: copyPaths(); break;
    case ID_REVEAL: revealSelection(); break;
    case ID_REFRESH: reload(); break;
    case ID_TOGGLE_HIDDEN:
        showHidden = !showHidden;
        settings.showHidden = showHidden;
        refreshTree();
        reload();
        break;
    case ID_TOGGLE_SYSICONS:
        useSystemIcons = !useSystemIcons;
        settings.useSystemIcons = useSystemIcons;
        preview::setSystemIcons(useSystemIcons);
        break;
    case ID_VIEW_BIG: setViewMode(ViewMode::Icons); break;
    case ID_VIEW_LIST: setViewMode(ViewMode::List); break;
    case ID_VIEW_DETAILS: setViewMode(ViewMode::Details); break;
    case ID_ADD_FAV: addFavorite(contextFolder.empty() ? current : contextFolder); break;
    case ID_ADD_FAV_CURRENT: addFavorite(current); break;
    case ID_REMOVE_FAV: removeFavorite(contextFolder.empty() ? current : contextFolder); break;
    case ID_OPEN_TERMINAL: openTerminal(); break;
    case ID_ABOUT: showAbout(hwnd); break;
    default:
        if (id >= ID_FAV_BASE) {
            int k = id - ID_FAV_BASE;
            if (k >= 900 && k < 910) {
                if (k == 900) loadDirectory(L"C:\\");
                if (k == 901) loadDirectory(fsx::homeDir());
                return;
            }
            if (k >= 910 && k < 916) {
                static const KNOWNFOLDERID* ids[] = {&FOLDERID_Desktop, &FOLDERID_Documents,
                                                     &FOLDERID_Downloads, &FOLDERID_Pictures,
                                                     &FOLDERID_Music, &FOLDERID_Videos};
                std::wstring p = fsx::knownFolder(*ids[k - 910]);
                if (!p.empty()) loadDirectory(p);
                return;
            }
            auto favs = menuFavorites();
            if (k >= 0 && k < (int)favs.size()) loadDirectory(favs[k]);
        }
        break;
    }
}

// ---------------------------------------------------------------- address bar

namespace {
App* g_addrOwner = nullptr;
WNDPROC g_addrOldProc = nullptr;

LRESULT CALLBACK addrEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && g_addrOwner) {
        if (wp == VK_RETURN) {
            g_addrOwner->endAddressEdit(true);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            g_addrOwner->endAddressEdit(false);
            return 0;
        }
    }
    if (msg == WM_KILLFOCUS && g_addrOwner && g_addrOwner->addrEditing) {
        g_addrOwner->endAddressEdit(true);
    }
    return CallWindowProcW(g_addrOldProc, hwnd, msg, wp, lp);
}
}  // namespace

void App::beginAddressEdit() {
    if (!addrEdit) {
        addrEdit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL,
                                   lay.addrField.left + 2, lay.addrField.top + 2,
                                   lay.addrField.right - lay.addrField.left - 4,
                                   lay.addrField.bottom - lay.addrField.top - 4, hwnd, nullptr,
                                   GetModuleHandleW(nullptr), nullptr);
        SendMessageW(addrEdit, WM_SETFONT,
                     (WPARAM)CreateFontW(-(int)(14.6667 * scale + 0.5), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma"),
                     TRUE);
        SendMessageW(addrEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
        g_addrOwner = this;
        g_addrOldProc =
            (WNDPROC)SetWindowLongPtrW(addrEdit, GWLP_WNDPROC, (LONG_PTR)addrEditProc);
    }
    SetWindowPos(addrEdit, HWND_TOP, lay.addrField.left + 2, lay.addrField.top + 2,
                 lay.addrField.right - lay.addrField.left - 4,
                 lay.addrField.bottom - lay.addrField.top - 4, SWP_NOACTIVATE);
    SetWindowTextW(addrEdit, fsx::abbreviateHome(current).c_str());
    ShowWindow(addrEdit, SW_SHOW);
    SetFocus(addrEdit);
    SendMessageW(addrEdit, EM_SETSEL, 0, -1);
    addrEditing = true;
}

void App::endAddressEdit(bool commit) {
    if (!addrEditing) return;
    std::wstring text;
    wchar_t buf[32768]{};
    GetWindowTextW(addrEdit, buf, 32768);
    text = buf;
    addrEditing = false;
    ShowWindow(addrEdit, SW_HIDE);
    SetFocus(hwnd);
    if (commit) goToTypedPath(text);
}

void App::saveSettings() {
    settings.sidebarWidth = sidebarWidth;
    settings.viewMode = (int)mode;
    settings.showHidden = showHidden;
    settings.useSystemIcons = useSystemIcons;
    if (hwnd) {
        WINDOWPLACEMENT wp{};
        wp.length = sizeof(wp);
        if (GetWindowPlacement(hwnd, &wp)) {
            const RECT& nr = wp.rcNormalPosition;
            settings.x = nr.left;
            settings.y = nr.top;
            settings.w = (int)std::lround((nr.right - nr.left) / scale);
            settings.h = (int)std::lround((nr.bottom - nr.top) / scale);
        }
    }
    settings.save();
}
