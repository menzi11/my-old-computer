// Filesystem model: directory listings, sorting, path helpers, file operations.
#pragma once

#include <windows.h>
#include <shellapi.h>  // HDROP, DragQueryFileW
#include <shlobj.h>

#include <string>
#include <vector>

struct Entry {
    std::wstring path;
    std::wstring name;
    bool isDir = false;
    bool hidden = false;  // the hidden or the system attribute: in the list only while those are shown, and drawn faint
    unsigned long long size = 0;
    bool hasSize = false;
    std::wstring type;
    FILETIME mtime{};
    bool hasTime = false;
};

namespace fsx {

std::wstring homeDir();
std::wstring knownFolder(REFKNOWNFOLDERID id);
std::vector<Entry> listDirectory(const std::wstring& dir, bool showHidden);
// Names of the folders directly inside dir, in the order the tree lists them -
// and nothing else: no sizes, no types.  For a list read on a click.
std::vector<std::wstring> listSubfolders(const std::wstring& dir, bool showHidden);
std::wstring typeName(const std::wstring& path, bool isDir);
std::wstring extensionLower(const std::wstring& name);
std::wstring formatSize(unsigned long long bytes);
std::wstring formatDate(const FILETIME& ft);

void sortEntries(std::vector<Entry>& entries, int column, bool ascending);

std::wstring parentPath(const std::wstring& path);
std::wstring joinPath(const std::wstring& dir, const std::wstring& name);
std::wstring fileName(const std::wstring& path);
std::wstring rootOf(const std::wstring& path);
bool isRoot(const std::wstring& path);
bool isInside(const std::wstring& child, const std::wstring& parent);
bool exists(const std::wstring& path);
bool isDirectory(const std::wstring& path);
bool sameVolume(const std::wstring& a, const std::wstring& b);
std::wstring uniqueTarget(const std::wstring& src, const std::wstring& destDir);

// File operations.  Return true when at least one item was handled.
bool recycle(const std::vector<std::wstring>& paths);
bool copyItems(const std::vector<std::wstring>& paths, const std::wstring& destDir);
bool moveItems(const std::vector<std::wstring>& paths, const std::wstring& destDir);
bool makeFolder(const std::wstring& dir, const std::wstring& name, std::wstring* created);
bool renameItem(const std::wstring& path, const std::wstring& newName, std::wstring* error);
bool openWithShell(const std::wstring& path);
bool openWithDialog(HWND owner, const std::wstring& path);
bool showProperties(HWND owner, const std::wstring& path);
bool createShortcut(const std::wstring& target, const std::wstring& dir, std::wstring* created);
bool clipboardHasFiles();
bool revealInExplorer(const std::wstring& path);
bool openTerminalAt(const std::wstring& dir);
bool hasAnySubdirectory(const std::wstring& dir, bool showHidden);

// Drag/drop + clipboard helpers shared by the clipboard code and the OLE drop
// target, so the copy-or-move decision is made in exactly one place.
// Explorer's rule: same volume moves, different volumes copy.
bool dropShouldMove(const std::vector<std::wstring>& paths, const std::wstring& dest);
// Builds a CF_HDROP payload (double-null terminated, wide); caller owns the
// HGLOBAL and must free it unless SetClipboardData/STGMEDIUM takes ownership.
HGLOBAL makeHdropData(const std::vector<std::wstring>& paths);
std::vector<std::wstring> pathsFromHdrop(HDROP drop);
UINT preferredDropEffectFormat();

std::wstring lastErrorText(DWORD code = 0);

}  // namespace fsx
