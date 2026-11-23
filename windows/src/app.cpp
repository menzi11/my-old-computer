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
#include "options.h"
#include "preview.h"
#include "shellmenu.h"

// Defined further down in this file; relayout() needs them for the cached
// menu/toolbar item widths.
extern const wchar_t* const kMenuTitles[6];
extern const wchar_t* const kToolTitles[7];
int measureUiText(const std::wstring& s, int fontPx);

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
    refreshTree();
}

void App::setScale(double s) {
    if (renaming) endRename(false);  // the edit is laid out for the old DPI
    scale = s;
    m = metricsFor(s);
    rebuildNameWidths();
    relayout(width, height);
    if (addrEdit) {
        // Keep the embedded edit control's font and geometry on the new DPI.
        HFONT nf = CreateFontW(-(int)(14.6667 * scale + 0.5), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
        if (nf) {
            SendMessageW(addrEdit, WM_SETFONT, (WPARAM)nf, TRUE);
            if (addrFont) DeleteObject(addrFont);
            addrFont = nf;
        }
        SetWindowPos(addrEdit, HWND_TOP, lay.addrField.left + 2, lay.addrField.top + 2,
                     lay.addrField.right - lay.addrField.left - 4,
                     lay.addrField.bottom - lay.addrField.top - 4, SWP_NOACTIVATE);
    }
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

    // Menu/toolbar widths depend only on the scale; cache them here so hit
    // tests and paints stop re-measuring text on every mouse move.
    const int fontPx = (int)(14.6667 * scale + 0.5);
    for (int i = 0; i < 6; ++i) lay.menuItemW[i] = measureUiText(kMenuTitles[i], fontPx) + px(16);
    for (int i = 0; i < 7; ++i)
        lay.toolItemW[i] = px(6 + 16 + 4) + measureUiText(kToolTitles[i], fontPx) + px(6);

    clampScroll();
}

// ---------------------------------------------------------------- navigation

void App::loadDirectory(const std::wstring& path, bool recordHistory, bool fromTreeClick) {
    if (renaming) endRename(false);
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
    selAnchor = -1;
    entries = fsx::listDirectory(current, showHidden);
    fsx::sortEntries(entries, sortColumn, sortAsc);
    rebuildNameWidths();
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
    if (addrEdit && addrEditing) SetWindowTextW(addrEdit, current.c_str());
}

void App::reload() {
    entries = fsx::listDirectory(current, showHidden);
    fsx::sortEntries(entries, sortColumn, sortAsc);
    rebuildNameWidths();
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
    if (renaming) endRename(false);  // the edit belongs to the old view's geometry
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

// The tree can open the very same context menu for a folder that is not part
// of the current listing: those commands then work on contextFolder.
std::vector<std::wstring> App::selectedPaths() const {
    std::vector<std::wstring> out;
    for (auto& e : entries)
        if (selection.count(e.path)) out.push_back(e.path);
    if (out.empty() && !contextFolder.empty() && fsx::exists(contextFolder))
        out.push_back(contextFolder);
    return out;
}

std::wstring App::firstSelectedPath() const {
    auto paths = selectedPaths();
    return paths.empty() ? std::wstring() : paths.front();
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
    std::wstring path = firstSelectedPath();
    if (path.empty()) return;
    if (fsx::isDirectory(path)) loadDirectory(path);
    else if (!fsx::openWithShell(path)) showError(hwnd, L"打开", L"无法打开该项。");
}

void App::openWithSelection() {
    std::wstring path = firstSelectedPath();
    if (path.empty()) return;
    if (!fsx::openWithDialog(hwnd, path)) showError(hwnd, L"打开方式", L"无法打开“打开方式”对话框。");
}

void App::openInNewWindow() {
    auto paths = selectedPaths();
    if (paths.empty() || !fsx::isDirectory(paths.front())) return;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring args = L"--path \"" + paths.front() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(exe, args.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void App::createShortcutForSelection() {
    auto paths = selectedPaths();
    if (paths.empty()) return;
    std::wstring dir = fsx::parentPath(paths.front());
    if (dir.empty()) dir = current;
    std::wstring created;
    if (!fsx::createShortcut(paths.front(), dir, &created)) {
        showError(hwnd, L"创建快捷方式", L"无法创建快捷方式。");
        return;
    }
    if (_wcsicmp(dir.c_str(), current.c_str()) == 0) refreshAfterChange();
    else loadDirectory(dir);
    selection.clear();
    selection.insert(created);
}

void App::showSelectionProperties() {
    std::wstring path = firstSelectedPath();
    if (path.empty()) path = current;
    if (!fsx::showProperties(hwnd, path)) showError(hwnd, L"属性", L"无法显示属性。");
}

void App::sortBy(int column) {
    sortColumn = std::clamp(column, 0, 3);
    sortAsc = true;
    fsx::sortEntries(entries, sortColumn, sortAsc);
    rebuildNameWidths();
    clampScroll();
}

std::vector<std::wstring> App::hiddenFor(const std::vector<std::wstring>& paths) const {
    if (paths.empty()) return settings.hiddenBg;
    if (fsx::isDirectory(paths.front())) return settings.hiddenFolder;
    std::vector<std::wstring> out = settings.hiddenFile;
    const std::wstring ext = fsx::extensionLower(fsx::fileName(paths.front()));
    if (!ext.empty()) {
        auto it = settings.hiddenExt.find(ext);
        if (it != settings.hiddenExt.end())
            out.insert(out.end(), it->second.begin(), it->second.end());
    }
    return out;
}

void App::openMenuSettings() {
    // One section per Explorer context.  Labels we replace with working
    // equivalents (view/sort/refresh/new, rename) are left out: they are
    // governed by our own toggles in the dialog's left column.
    std::vector<MenuSection> sections;
    auto filter = [](std::vector<std::wstring> items, bool background) {
        std::vector<std::wstring> out;
        for (auto& t : items) {
            if (t.empty()) continue;
            if (background) {
                if (t == L"查看" || t == L"排序方式" || t == L"分组依据" || t == L"刷新" ||
                    t == L"新建" || t == L"新建文件夹" || t == L"新建文本文档")
                    continue;
            } else if (t == L"重命名") {
                continue;
            }
            if (std::find(out.begin(), out.end(), t) == out.end()) out.push_back(t);
        }
        return out;
    };

    MenuSection bg;
    bg.key = L"bg";
    bg.title = L"空白处（当前文件夹）";
    bg.items = filter(shellmenu::shellLabels(current, {}, true), true);
    bg.hidden = settings.hiddenBg;
    sections.push_back(std::move(bg));

    MenuSection dir;
    dir.key = L"dir";
    dir.title = L"文件夹";
    dir.items = filter(shellmenu::probeFolderLabels(), false);
    dir.hidden = settings.hiddenFolder;
    sections.push_back(std::move(dir));

    MenuSection plain;
    plain.key = L"file";
    plain.title = L"文件（无扩展名）";
    plain.items = filter(shellmenu::probeFileLabels(L""), false);
    plain.hidden = settings.hiddenFile;
    sections.push_back(std::move(plain));

    // File types: everything the user already configured comes first (so it
    // can be undone), then the current folder's types up to a cap - probing
    // each type builds a shell menu, and dozens of those would stall the
    // dialog.  Anything else can be added by hand inside the dialog.
    std::vector<std::wstring> exts;
    auto addExt = [&exts](const std::wstring& e) {
        if (e.empty()) return;
        if (std::find(exts.begin(), exts.end(), e) == exts.end()) exts.push_back(e);
    };
    for (auto& saved : settings.hiddenExt) addExt(saved.first);
    for (auto& e : entries)
        if (!e.isDir) addExt(fsx::extensionLower(e.name));
    if (exts.size() > 16) exts.resize(16);
    for (auto& ext : exts) {
        MenuSection s;
        s.key = ext;
        s.title = L"." + ext + L" 文件";
        s.items = filter(shellmenu::probeFileLabels(ext), false);
        if (s.items.empty()) continue;
        auto it = settings.hiddenExt.find(ext);
        if (it != settings.hiddenExt.end()) s.hidden = it->second;
        sections.push_back(std::move(s));
    }

    if (!showMenuOptions(hwnd, &settings.menuMask, sections)) return;
    settings.hiddenBg.clear();
    settings.hiddenFolder.clear();
    settings.hiddenFile.clear();
    settings.hiddenExt.clear();
    for (auto& s : sections) {
        if (s.key == L"bg") settings.hiddenBg = s.hidden;
        else if (s.key == L"dir") settings.hiddenFolder = s.hidden;
        else if (s.key == L"file") settings.hiddenFile = s.hidden;
        else if (!s.hidden.empty()) settings.hiddenExt[s.key] = s.hidden;
    }
    settings.save();
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
    else refreshAfterChange();
    selection.clear();
    selection.insert(created);
}

void App::newTextFile() {
    std::wstring base = contextFolder.empty() ? current : contextFolder;
    contextFolder.clear();
    std::wstring path = fsx::joinPath(base, L"新建文本文档.txt");
    for (int n = 2; fsx::exists(path) && n < 1000; ++n)
        path = fsx::joinPath(base, L"新建文本文档 (" + std::to_wstring(n) + L").txt");
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        showError(hwnd, L"新建文本文档", fsx::lastErrorText());
        return;
    }
    CloseHandle(h);
    if (_wcsicmp(base.c_str(), current.c_str()) == 0) refreshAfterChange();
    else loadDirectory(base);
    selection.clear();
    selection.insert(path);
}

void App::renameSelection() {
    auto paths = selectedPaths();
    if (paths.size() != 1) return;
    // In the current listing: rename in place, over the row's name.
    for (int i = 0; i < (int)entries.size(); ++i)
        if (_wcsicmp(entries[i].path.c_str(), paths.front().c_str()) == 0) {
            beginRename(i);
            return;
        }
    // Not visible here (e.g. a tree-only folder): fall back to a prompt.
    std::wstring full = paths.front();
    std::wstring name = fsx::fileName(full);
    if (!promptText(hwnd, L"重命名", L"输入新名称：", name, &name)) return;
    if (name.empty() || name == fsx::fileName(full)) return;
    std::wstring error;
    if (!fsx::renameItem(full, name, &error)) {
        showError(hwnd, L"重命名", error);
        return;
    }
    std::wstring renamed = fsx::joinPath(fsx::parentPath(full), name);
    refreshAfterChange();
    selection.clear();
    selection.insert(renamed);
}

void App::deleteSelection() {
    auto paths = selectedPaths();
    if (paths.empty()) return;
    if (!fsx::recycle(paths)) showError(hwnd, L"删除", L"无法将选定的项目移入回收站。");
    selection.clear();
    refreshAfterChange();
}

void App::copySelection(bool cut) {
    auto sel = selectedPaths();
    if (sel.empty()) return;
    clip.clear();
    for (auto& p : sel) clip.push_back(p);
    clipCut = cut;

    // Publish the file list so Explorer and other apps can paste it.
    HGLOBAL mem = fsx::makeHdropData(clip);
    if (!mem) return;
    if (!OpenClipboard(hwnd)) {
        GlobalFree(mem);
        return;
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_HDROP, mem)) GlobalFree(mem);  // on success the system owns it
    HGLOBAL fx = GlobalAlloc(GMEM_MOVEABLE, sizeof(DWORD));
    if (fx) {
        auto* effect = (DWORD*)GlobalLock(fx);
        if (effect) {
            *effect = cut ? DROPEFFECT_MOVE : DROPEFFECT_COPY;
            GlobalUnlock(fx);
            if (!SetClipboardData(fsx::preferredDropEffectFormat(), fx)) GlobalFree(fx);
        } else {
            GlobalFree(fx);
        }
    }
    CloseClipboard();
}

void App::paste() {
    // Prefer the real system clipboard so a later copy in another app is not
    // overridden by our stale mirror; honour the cut marker Explorer leaves.
    std::vector<std::wstring> urls;
    bool cut = false;
    if (OpenClipboard(hwnd)) {
        if (HANDLE h = GetClipboardData(CF_HDROP)) urls = fsx::pathsFromHdrop((HDROP)h);
        if (HANDLE eh = GetClipboardData(fsx::preferredDropEffectFormat())) {
            if (auto* effect = (DWORD*)GlobalLock(eh)) {
                if (*effect & DROPEFFECT_MOVE) cut = true;
                GlobalUnlock(eh);
            }
        }
        CloseClipboard();
    }
    if (urls.empty()) {
        urls = clip;
        cut = clipCut;
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
    refreshAfterChange();
}

void App::copyPaths() {
    std::wstring text;
    for (auto& p : selectedPaths()) {
        if (!text.empty()) text += L"\r\n";
        text += p;
    }
    if (text.empty()) return;
    if (OpenClipboard(hwnd)) {
        EmptyClipboard();
        size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
        if (mem) {
            memcpy(GlobalLock(mem), text.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        }
        CloseClipboard();
    }
}

void App::revealSelection() {
    std::wstring path = firstSelectedPath();
    if (!path.empty()) fsx::revealInExplorer(path);
}

void App::openTerminalForSelection() {
    std::wstring path = firstSelectedPath();
    std::wstring dir = current;
    if (!path.empty())
        dir = fsx::isDirectory(path) ? path : fsx::parentPath(path);
    fsx::openTerminalAt(dir);
}

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
    bool ok = fsx::dropShouldMove(items, dest) ? fsx::moveItems(items, dest)
                                               : fsx::copyItems(items, dest);
    if (!ok) MessageBeep(MB_ICONASTERISK);
    refreshAfterChange();
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
    auto it = subdirCache.find(path);
    if (it != subdirCache.end()) return it->second;
    bool v = fsx::hasAnySubdirectory(path, showHidden);
    subdirCache[path] = v;
    return v;
}

void App::noteChildren(const std::wstring& dir, bool hasSubdir) {
    subdirCache[dir] = hasSubdir;
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
        group.path = L"";  // a header, not a folder: no menu or navigation of its own
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

    for (auto& r : treeRoots) walk(r, current);
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

// Drops tree nodes whose folder no longer exists; keeps the expansion state.
void App::pruneTree() {
    std::function<void(std::vector<TreeNode>&)> walk = [&](std::vector<TreeNode>& nodes) {
        for (auto it = nodes.begin(); it != nodes.end();) {
            const bool check = it->kind != TreeNode::Machine && it->kind != TreeNode::Favorites &&
                               !it->path.empty();
            if (check && !fsx::isDirectory(it->path)) {
                it = nodes.erase(it);
                continue;
            }
            walk(it->children);
            ++it;
        }
    };
    walk(treeRoots);
    flattenTree();
}

void App::reloadTreeChildrenAt(const std::wstring& dir) {
    std::function<bool(TreeNode&)> visit = [&](TreeNode& node) -> bool {
        if (node.kind != TreeNode::Favorites && !node.path.empty() &&
            _wcsicmp(node.path.c_str(), dir.c_str()) == 0) {
            node.loaded = false;
            node.children.clear();
            loadTreeChildren(node);
            return true;
        }
        for (auto& child : node.children)
            if (visit(child)) return true;
        return false;
    };
    for (auto& root : treeRoots)
        if (visit(root)) break;
    flattenTree();
}

void App::refreshAfterChange() {
    // A deleted folder must leave the tree, and if it was the current folder
    // (or an ancestor of it) move up to whatever still exists.
    std::wstring dir = current;
    while (!dir.empty() && !fsx::isDirectory(dir)) {
        const std::wstring parent = fsx::parentPath(dir);
        if (_wcsicmp(parent.c_str(), dir.c_str()) == 0) break;
        dir = parent;
    }
    if (_wcsicmp(dir.c_str(), current.c_str()) != 0) {
        loadDirectory(dir, false);
        pruneTree();
        subdirCache.clear();
        return;
    }
    pruneTree();
    // Folders may have gained or lost children: forget the +/- answers.
    subdirCache.clear();
    reloadTreeChildrenAt(current);
    reload();
}

// ---------------------------------------------------------------- geometry

// Text measurement for the custom chrome.  Geometry and hit testing stay
// identical to what the renderer draws; the measurement DC and font are kept
// for the process lifetime so hit tests cost one GetTextExtentPoint32 call.
int measureUiText(const std::wstring& s, int fontPx) {
    static HDC dc = nullptr;
    static HFONT font = nullptr;
    static int px = -1;
    static bool selected = false;
    if (px != fontPx) {
        if (!dc) dc = CreateCompatibleDC(nullptr);
        HFONT nf = dc ? CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                    CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma")
                      : nullptr;
        if (nf) {
            HGDIOBJ prev = SelectObject(dc, nf);  // prev is our old font, or the stock default
            if (selected && prev) DeleteObject(prev);
            font = nf;
            selected = true;
            px = fontPx;
        }
    }
    if (!dc || !font) return (int)s.size() * fontPx / 2;
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
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
    int x = lay.tool.left + (int)(3 * scale + 0.5);
    const int y = lay.tool.top + (int)(2 * scale + 0.5);
    const int h = (lay.tool.bottom - lay.tool.top) - (int)(4 * scale + 0.5);
    for (int j = 0; j < 7; ++j) {
        const int w = lay.toolItemW[j];
        if (j == i) return RECT{x, y, x + w, y + h};
        x += w + (int)(1 * scale + 0.5);
    }
    return RECT{0, 0, 0, 0};
}

int App::toolbarIndexAt(POINT p) const {
    for (int i = 0; i < 7; ++i) {
        RECT r = toolbarItemRect(i);
        if (PtInRect(&r, p)) return i;
    }
    return -1;
}

RECT App::menuItemRect(int i) const {
    int x = lay.menu.left + (int)(2 * scale + 0.5);
    for (int j = 0; j < 6; ++j) {
        const int w = lay.menuItemW[j];
        if (j == i)
            return RECT{x, lay.menu.top + (int)(1 * scale + 0.5), x + w,
                        lay.menu.top + m.menuH - (int)(1 * scale + 0.5)};
        x += w;
    }
    return RECT{0, 0, 0, 0};
}

int App::menuItemAt(POINT p) const {
    for (int i = 0; i < 6; ++i) {
        RECT r = menuItemRect(i);
        if (PtInRect(&r, p)) return i;
    }
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

int App::nameExtent(int row) const {
    if (row < 0 || row >= (int)nameWidths.size()) return 0;
    return nameWidths[row];
}

void App::rebuildNameWidths() {
    const int fontPx = (int)(14.6667 * scale + 0.5);
    const int lead = (int)(22 * scale + 0.5) + (int)(6 * scale + 0.5);
    const size_t n = entries.size();
    nameWidths.resize(n);
    textWidths.resize(n);
    for (size_t i = 0; i < n; ++i) {
        textWidths[i] = measureUiText(entries[i].name, fontPx);
        nameWidths[i] = lead + textWidths[i];
    }
}

// An item owns its full row height - the visual gap between two names is
// still one of those rows, clicking it picks that row, never blank canvas
// (there is no seam to click *between* two items).  Horizontally only the
// icon and the name text count: the rest of a row stays blank so the
// rubber band can start there.
bool App::hitTestItem(POINT p, int* index) const {
    if (mode == ViewMode::Details) {
        int row = listRowAt(p);
        if (row < 0) return false;
        const int limit = std::min(lay.nameW, nameExtent(row));
        if (p.x - lay.listViewport.left >= limit) return false;
        if (index) *index = row;
        return true;
    }
    int idx = gridIndexAt(p);
    if (idx < 0) return false;
    RECT r = gridCellRect(idx);
    const int s16 = (int)(16 * scale + 0.5);
    if (mode == ViewMode::List) {
        const int tw = idx < (int)textWidths.size() ? textWidths[idx] : 0;
        const int right = r.left + (int)(2 * scale + 0.5) + s16 + (int)(4 * scale + 0.5) + tw +
                          (int)(2 * scale + 0.5);
        if (p.x > right) return false;
        if (index) *index = idx;
        return true;
    }
    const int iconSize = (int)(48 * scale + 0.5);
    RECT icon{(r.left + r.right) / 2 - iconSize / 2, r.top + (int)(4 * scale + 0.5),
              (r.left + r.right) / 2 + iconSize / 2, r.top + (int)(4 * scale + 0.5) + iconSize};
    RECT label{r.left + (int)(2 * scale + 0.5), r.top + (int)(54 * scale + 0.5),
               r.right - (int)(2 * scale + 0.5), r.bottom};
    if (!PtInRect(&icon, p) && !PtInRect(&label, p)) return false;
    if (index) *index = idx;
    return true;
}

int App::gridCellW() const { return (int)((mode == ViewMode::Icons ? 94 : 220) * scale + 0.5); }
int App::gridCellH() const { return (int)((mode == ViewMode::Icons ? 94 : 19) * scale + 0.5); }

int App::gridCols() const {
    int w = (int)(lay.gridViewport.right - lay.gridViewport.left);
    return std::max(1, (int)((w - (int)(8 * scale + 0.5)) / std::max(1, gridCellW())));
}

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
    // Scrolling moves the row; keep the in-place editor on top of its name.
    if (renaming && renameEdit) {
        RECT r = renameRect(renameRow);
        SetWindowPos(renameEdit, HWND_TOP, r.left, r.top, std::max(60L, r.right - r.left),
                     r.bottom - r.top, SWP_NOACTIVATE);
    }
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
    std::vector<MenuItem> items;
    const bool hasSel = !selection.empty();
    if (name == L"文件") {
        items.push_back({L"打开", ID_OPEN, hasSel});
        items.push_back({L"快速查看", ID_PREVIEW, hasSel, false, false, L"Space"});
        items.push_back({L"新建文件夹", ID_NEW_FOLDER, true});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"重命名", ID_RENAME, hasSel});
        items.push_back({L"删除", ID_DELETE, hasSel});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"关闭", ID_CLOSE, true, false, false, L"Alt+F4"});
    } else if (name == L"编辑") {
        items.push_back({L"剪切", ID_CUT, hasSel, false, false, L"Ctrl+X"});
        items.push_back({L"复制", ID_COPY, hasSel, false, false, L"Ctrl+C"});
        items.push_back({L"粘贴", ID_PASTE, true, false, false, L"Ctrl+V"});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"全选", ID_SELECT_ALL, true, false, false, L"Ctrl+A"});
        items.push_back({L"复制路径", ID_COPY_PATH, hasSel});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"在资源管理器中显示", ID_REVEAL, hasSel});
    } else if (name == L"查看") {
        items.push_back({L"刷新", ID_REFRESH, true, false, false, L"F5"});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"显示隐藏文件", ID_TOGGLE_HIDDEN, true, showHidden});
        items.push_back({L"使用系统图标", ID_TOGGLE_SYSICONS, true, useSystemIcons});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"大图标", ID_VIEW_BIG, true, mode == ViewMode::Icons});
        items.push_back({L"列表", ID_VIEW_LIST, true, mode == ViewMode::List});
        items.push_back({L"详细信息", ID_VIEW_DETAILS, true, mode == ViewMode::Details});
    } else if (name == L"收藏") {
        items.push_back({L"添加当前文件夹到收藏夹", ID_ADD_FAV_CURRENT, true});
        auto favs = menuFavorites();
        if (!favs.empty()) {
            items.push_back({L"", 0, false, false, true});
            for (size_t i = 0; i < favs.size(); ++i)
                items.push_back({fsx::fileName(favs[i]), (int)(ID_FAV_BASE + i), true});
        }
    } else if (name == L"工具") {
        items.push_back({L"在命令提示符中打开", ID_OPEN_TERMINAL, true});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"右键菜单设置…", ID_SETTINGS, true});
    } else if (name == L"帮助") {
        items.push_back({L"关于 My Old Computer", ID_ABOUT, true});
    }
    return items;
}

std::vector<MenuItem> App::contextMenu() const {
    std::vector<MenuItem> items;
    const auto paths = selectedPaths();
    if (paths.empty()) {
        // Empty area of the current folder: view, sort, paste and folder tools.
        items.push_back({L"大图标", ID_VIEW_BIG, true, mode == ViewMode::Icons});
        items.push_back({L"列表", ID_VIEW_LIST, true, mode == ViewMode::List});
        items.push_back({L"详细信息", ID_VIEW_DETAILS, true, mode == ViewMode::Details});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"按名称排序", ID_SORT_NAME, true, sortColumn == 0});
        items.push_back({L"按大小排序", ID_SORT_SIZE, true, sortColumn == 1});
        items.push_back({L"按类型排序", ID_SORT_TYPE, true, sortColumn == 2});
        items.push_back({L"按修改日期排序", ID_SORT_DATE, true, sortColumn == 3});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"刷新", ID_REFRESH, true, false, false, L"F5"});
        items.push_back({L"粘贴", ID_PASTE, fsx::clipboardHasFiles(), false, false, L"Ctrl+V"});
        items.push_back({L"新建文件夹", ID_NEW_FOLDER, true});
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"显示隐藏文件", ID_TOGGLE_HIDDEN, true, showHidden});
        items.push_back({L"在命令提示符中打开", ID_OPEN_TERMINAL, true});
        items.push_back({L"属性", ID_PROPERTIES, true});
        return items;
    }

    const bool single = paths.size() == 1;
    const bool isDir = fsx::isDirectory(paths.front());
    items.push_back({L"打开", ID_OPEN, true});
    if (single && !isDir) items.push_back({L"打开方式…", ID_OPEN_WITH, true});
    if (single && isDir) items.push_back({L"在新窗口中打开", ID_OPEN_NEW_WINDOW, true});
    items.push_back({L"快速查看", ID_PREVIEW, single, false, false, L"Space"});
    items.push_back({L"", 0, false, false, true});
    items.push_back({L"剪切", ID_CUT, true, false, false, L"Ctrl+X"});
    items.push_back({L"复制", ID_COPY, true, false, false, L"Ctrl+C"});
    if (isDir) items.push_back({L"粘贴", ID_PASTE, fsx::clipboardHasFiles(), false, false, L"Ctrl+V"});
    items.push_back({L"创建快捷方式", ID_CREATE_SHORTCUT, single});
    items.push_back({L"", 0, false, false, true});
    items.push_back({L"删除", ID_DELETE, true, false, false, L"Del"});
    items.push_back({L"重命名", ID_RENAME, single, false, false, L"F2"});
    items.push_back({L"", 0, false, false, true});
    items.push_back({L"属性", ID_PROPERTIES, true});
    items.push_back({L"在资源管理器中显示", ID_REVEAL, true});
    items.push_back({L"复制路径", ID_COPY_PATH, true});
    if (isDir) {
        items.push_back({L"", 0, false, false, true});
        items.push_back({L"添加到收藏夹", ID_ADD_FAV, true});
        if (contextIsFavorite) items.push_back({L"从收藏夹中移除", ID_REMOVE_FAV, true});
        items.push_back({L"在命令提示符中打开", ID_OPEN_TERMINAL, true});
    }
    return items;
}

std::vector<MenuItem> App::placesMenu() const {
    std::vector<MenuItem> items;
    items.push_back({L"我的电脑", ID_FAV_BASE + 900, true});
    items.push_back({L"个人文件夹", ID_FAV_BASE + 901, true});
    items.push_back({L"", 0, false, false, true});
    struct Special { const wchar_t* title; REFKNOWNFOLDERID id; };
    const Special specials[] = {
        {L"桌面", FOLDERID_Desktop},       {L"我的文档", FOLDERID_Documents},
        {L"下载", FOLDERID_Downloads},     {L"我的图片", FOLDERID_Pictures},
        {L"我的音乐", FOLDERID_Music},     {L"我的影片", FOLDERID_Videos},
    };
    for (auto& s : specials) {
        std::wstring p = fsx::knownFolder(s.id);
        if (!p.empty()) items.push_back({s.title, ID_FAV_BASE + 910 + (int)(&s - specials), true});
    }
    return items;
}

void App::performMenuAction(int id, const std::wstring& arg) {
    (void)arg;
    switch (id) {
    case ID_OPEN:
        if (!selectedPaths().empty()) {
            openSelection();
        } else if (!contextFolder.empty()) {
            if (fsx::isDirectory(contextFolder)) loadDirectory(contextFolder);
            else if (!fsx::openWithShell(contextFolder))
                showError(hwnd, L"打开", L"无法打开该项。");
            contextFolder.clear();
        }
        break;
    case ID_OPEN_WITH: openWithSelection(); break;
    case ID_OPEN_NEW_WINDOW: openInNewWindow(); break;
    case ID_CREATE_SHORTCUT: createShortcutForSelection(); break;
    case ID_PROPERTIES: showSelectionProperties(); break;
    case ID_SORT_NAME: sortBy(0); break;
    case ID_SORT_SIZE: sortBy(1); break;
    case ID_SORT_TYPE: sortBy(2); break;
    case ID_SORT_DATE: sortBy(3); break;
    case ID_PREVIEW:
        if (std::wstring p = firstSelectedPath(); !p.empty()) preview::show(hwnd, p);
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
        subdirCache.clear();  // the +/- answers depend on the hidden filter
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
    case ID_OPEN_TERMINAL: openTerminalForSelection(); break;
    case ID_SETTINGS: openMenuSettings(); break;
    case ID_TREE_TOGGLE:
        // Sidebar header menu (我的电脑 / 收藏夹): expand or collapse that row.
        if (hoverTree >= 0 && hoverTree < (int)treeFlat.size())
            toggleTreeNode(*treeFlat[hoverTree]);
        break;
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
        SendMessageW(addrEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(2, 2));
        g_addrOwner = this;
        g_addrOldProc =
            (WNDPROC)SetWindowLongPtrW(addrEdit, GWLP_WNDPROC, (LONG_PTR)addrEditProc);
    }
    if (!addrFont) {
        addrFont = CreateFontW(-(int)(14.6667 * scale + 0.5), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    }
    if (addrFont) SendMessageW(addrEdit, WM_SETFONT, (WPARAM)addrFont, TRUE);
    SetWindowPos(addrEdit, HWND_TOP, lay.addrField.left + 2, lay.addrField.top + 2,
                 lay.addrField.right - lay.addrField.left - 4,
                 lay.addrField.bottom - lay.addrField.top - 4, SWP_NOACTIVATE);
    SetWindowTextW(addrEdit, current.c_str());
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

// ------------------------------------------------------------- in-place rename

namespace {
App* g_renameOwner = nullptr;
WNDPROC g_renameOldProc = nullptr;

LRESULT CALLBACK renameEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && g_renameOwner) {
        if (wp == VK_RETURN) {
            g_renameOwner->endRename(true);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            g_renameOwner->endRename(false);
            return 0;
        }
        if (wp == VK_F2) {  // pressing F2 again selects the whole name
            SendMessageW(hwnd, EM_SETSEL, 0, -1);
            return 0;
        }
    }
    if (msg == WM_KILLFOCUS && g_renameOwner && g_renameOwner->renaming)
        g_renameOwner->endRename(true);
    return CallWindowProcW(g_renameOldProc, hwnd, msg, wp, lp);
}
}  // namespace

// Where the edit control sits: exactly over the row's name, in every view.
RECT App::renameRect(int row) const {
    if (mode == ViewMode::Details) {
        RECT r = listRowRect(row);
        return RECT{r.left + (int)(22 * scale + 0.5), r.top + 1,
                    std::min(r.left + lay.nameW - 4, r.right - 2), r.bottom - 1};
    }
    RECT r = gridCellRect(row);
    if (mode == ViewMode::List) {
        const int s16 = (int)(16 * scale + 0.5);
        const int left = r.left + (int)(2 * scale + 0.5) + s16 + (int)(4 * scale + 0.5);
        const int tw = row < (int)textWidths.size() ? textWidths[row] : 0;
        return RECT{left, r.top + 1, left + tw + (int)(40 * scale + 0.5), r.bottom - 1};
    }
    // Icons: the label band under the icon.
    return RECT{r.left + (int)(2 * scale + 0.5), r.top + (int)(54 * scale + 0.5),
                r.right - (int)(2 * scale + 0.5), r.bottom - 1};
}

void App::beginRename(int row) {
    if (renaming) endRename(false);
    if (row < 0 || row >= (int)entries.size()) return;
    renaming = true;
    renameRow = row;
    // Keep the row highlighted while its name is edited.
    selection.clear();
    selection.insert(entries[row].path);
    selAnchor = row;
    RECT r = renameRect(row);
    renameEdit = CreateWindowExW(
        0, L"EDIT", entries[row].name.c_str(),
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL, r.left, r.top,
        std::max(60L, r.right - r.left), r.bottom - r.top, hwnd, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!renameFont)
        renameFont = CreateFontW(-(int)(14.6667 * scale + 0.5), 0, 0, 0, FW_NORMAL, 0, 0, 0,
                                 DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Tahoma");
    if (renameEdit) {
        if (renameFont) SendMessageW(renameEdit, WM_SETFONT, (WPARAM)renameFont, TRUE);
        SendMessageW(renameEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(1, 1));
        g_renameOwner = this;
        g_renameOldProc =
            (WNDPROC)SetWindowLongPtrW(renameEdit, GWLP_WNDPROC, (LONG_PTR)renameEditProc);
        SetFocus(renameEdit);
        // Explorer selects the base name; folders are selected in full.
        const std::wstring& name = entries[row].name;
        int selEnd = (int)name.size();
        if (!entries[row].isDir) {
            size_t dot = name.find_last_of(L'.');
            if (dot != std::wstring::npos && dot > 0) selEnd = (int)dot;
        }
        SendMessageW(renameEdit, EM_SETSEL, 0, selEnd);
    }
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
}

void App::endRename(bool commit) {
    if (!renaming) return;
    renaming = false;
    std::wstring text;
    wchar_t buf[1024]{};
    if (renameEdit) GetWindowTextW(renameEdit, buf, 1024);
    text = buf;
    if (renameEdit) {
        SetWindowLongPtrW(renameEdit, GWLP_WNDPROC, (LONG_PTR)g_renameOldProc);
        DestroyWindow(renameEdit);
        renameEdit = nullptr;
        g_renameOwner = nullptr;
    }
    const int row = renameRow;
    renameRow = -1;
    if (hwnd) SetFocus(hwnd);
    if (commit && row >= 0 && row < (int)entries.size() && !text.empty() &&
        text != entries[row].name) {
        std::wstring error;
        if (fsx::renameItem(entries[row].path, text, &error)) {
            std::wstring renamed = fsx::joinPath(fsx::parentPath(entries[row].path), text);
            refreshAfterChange();
            selection.clear();
            selection.insert(renamed);
            return;
        }
        showError(hwnd, L"重命名", error);
    }
    if (hwnd) InvalidateRect(hwnd, nullptr, FALSE);
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
