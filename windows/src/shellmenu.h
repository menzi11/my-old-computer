// Hosts the real Windows shell context menu (IContextMenu), so every installed
// shell extension - 7-Zip, Git, editors, Send To, Previous Versions - appears
// exactly as it does in Explorer.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "settings.h"

namespace shellmenu {

enum class Command {
    None,
    Open,
    OpenNewWindow,
    ViewBig,
    ViewList,
    ViewDetails,
    SortName,
    SortSize,
    SortType,
    SortDate,
    Refresh,
    ToggleHidden,
    Terminal,
    Rename,
    AddFavorite,
    RemoveFavorite,
    RenameFavorite,
    NewFolder,
    NewTextFile,
    Settings,
    Preview,
    Reveal,
    CopyPath,
    FolderColour,  // `Result::colour` says which; foldercolours::kDefault: none
};

// State needed to draw our own working view/sort items in the background menu.
struct ViewState {
    int mode = 0;  // 0 details, 1 icons, 2 list
    int sortColumn = 0;
    bool showHidden = false;
    unsigned menuMask = kMenuDefaults;  // MenuOption flags from settings.h
    std::vector<std::wstring> hidden;   // top-level labels to leave out
    bool isFavorite = false;    // the folder is in the sidebar's Favorites
    bool favoriteNode = false;  // ... and the menu is opened on its row there, where it can be renamed too
    int folderColour = -1;      // the colour of the one folder the menu is for (foldercolours::kDefault: none)
};

struct Result {
    bool shown = false;      // the shell menu was displayed
    bool invokedShell = false;  // a shell verb ran
    Command command = Command::None;  // one of our own items was chosen
    int colour = -1;  // for FolderColour: the one chosen (foldercolours::kDefault: none)
};

// What the item with id `chosen` of the menu asks for (the ids are the menu's:
// kCustomBase and up).
Result decodeChoice(int chosen);

// Shows the shell menu for `paths` (empty = the folder background menu), building it and running what is picked on the thread that calls.
Result show(HWND owner, const std::wstring& folder, const std::vector<std::wstring>& paths,
            POINT screenPoint, const ViewState& state);

// ------------------------------------------------------ built ahead, on a thread of its own
//
// The shell and the installed extensions take a good part of a second to
// build a menu, the first of a window more - all of it on the thread that
// asks, which was the window's: the window stood still until the menu was
// there.  Now menus are built on a thread of the window's program that has
// nothing else to do, before anyone right-clicks: when a folder is opened
// (its background, a folder in it, a file of each common kind), while the
// pointer rests on something.  A right-click shows what was built; what was
// not is asked for and shown when it is in, the window free in between.
//
// And what is shown need not be the menu of the very thing: the shell's part
// of the menu of any one file of a kind in a folder is the same as that of
// the others (the extensions are asked about the kind, not the file).  So the
// menu of one stands for all until the thing's own is built - with this
// program's own items made for the thing as it is.  A few items are the
// file's own (an archive's name, say): when the thing's own menu is in and
// says anything else, it takes the stand-in's place on screen.  When a shell
// command is picked, it is looked up by where it lay and what it said in the
// menu built for the thing itself, and run from there only if both agree.

// What a menu is for: the things selected (all in `folder`), or with none the
// folder's background; and the shell's labels switched off for them.
struct Request {
    std::wstring folder;
    std::vector<std::wstring> paths;
    std::vector<std::wstring> hidden;
};

// The shell's part of a menu as plain data, that the window's thread can show.
struct Item {
    std::wstring text;  // as the shell wrote it ("打开(&O)")
    UINT id = 0;        // the shell's own (0 for a separator or a submenu)
    bool separator = false;
    bool popup = false;  // it opens a submenu (in `sub`, which may be empty)
    bool enabled = true;
    bool checked = false;
    bool isDefault = false;
    std::vector<Item> sub;
};

// An item of a menu shown: where it lay (its place in its menu, and that
// menu's place in its own, the root last) and what it said.
struct Where {
    std::vector<int> path;
    std::wstring text;
};

// A menu that was built: which one (0: none) and how long ago.
struct Found {
    ULONGLONG entry = 0;
    int ageMs = 0;
};

// Posted to the window that asked: the menu it asked for is built (wParam: the serial it asked under).
constexpr UINT kMenuReadyMessage = WM_APP + 8;
// Posted to the window that picked a shell command: how it went (wParam: the
// serial, lParam: one of kVerb...).
constexpr UINT kMenuDoneMessage = WM_APP + 9;
enum VerbResult {
    kVerbFailed = 0,     // the shell could not run it
    kVerbRan = 1,
    kVerbNotFound = 2,   // the menu of the thing itself has no such item any more
};

// Starts the thread (once); `owner` is the window the shell is shown to parent things to.
void start(HWND owner);
// The thread is up and takes work.
bool running();
// The window is going: nothing more is built, and what is being built (or
// run) is given a moment to be over - so the shell's code is not cut off by
// the process ending in the middle of it.
void stop();

// What a menu is a menu "of", for one to stand for others: the background of a
// folder, a folder in a folder, a kind of file in a folder; empty when there
// is no kind (several things selected).
std::wstring kindOf(const Request& what);

// Menus to build when the thread has time.  One batch at a time: every batch
// asked for before `newBatch` is not wanted any more (another folder was opened).
void newBatch();
void prefetch(const Request& what);
// The one the pointer rests on: before the batch, and only the last asked for.
void hover(const Request& what);

// Build this one next and say so (kMenuReadyMessage).  One built less than
// `freshMs` ago is taken as it is.
void ask(const Request& what, HWND notify, ULONGLONG serial, ULONGLONG freshMs);

// How long a menu built ahead stands for what is shown.
constexpr int kModelAgeMs = 5 * 60 * 1000;
// Its own older than this is built again the moment it is asked for.
constexpr int kAgainMs = 5000;

// The menu built for exactly this, and its age; or one of its kind.
Found find(const Request& what);
Found findKind(const std::wstring& kind);
// The menus of folders' backgrounds are not kept any longer - they say
// whether there is something to paste, and the clipboard has changed.  They
// are let go of on the thread that made them; what is asked for after is
// built anew.
void forgetBackgrounds();
// What it says (its items), copied.
bool itemsOf(ULONGLONG entry, std::vector<Item>* out);
// Whether two menus say the same: their items, what each says and how it
// stands, and what is in their submenus.  (Most of what a menu of a kind of
// file says is the same for every file of the kind; a few items are the
// file's own - an archive's name, say - and these say so.)
bool sameSay(const std::vector<Item>& a, const std::vector<Item>& b);

// Runs, from the menu built for `what` itself (built again if there is none
// worth taking), the item that lay at `path` and said `text`; kMenuDoneMessage
// says how it went.
void invoke(const Request& what, const std::vector<int>& path, const std::wstring& text,
            HWND notify, ULONGLONG serial);

// A menu to show, made of `items`.  As itself (`asModel` false) every item
// keeps the shell's id; as the model of another thing each gets a number of
// its own, and `wheres` (null: not asked) says of each where it lay and what
// it said - as it does for a menu shown as itself.  The caller destroys the menu.
HMENU materialize(const std::vector<Item>& items, bool asModel, std::vector<Where>* wheres);
// This program's own items, for the things as they are now (they depend on
// the view, on the folder's colour and on the Favorites: made when shown).
void addOwnItems(HMENU menu, const std::vector<std::wstring>& paths, const ViewState& state);

// Forwards WM_INITMENUPOPUP / WM_DRAWITEM / WM_MEASUREITEM to the open menu's
// shell extensions.  Returns true when it handled the message.
bool forwardMessage(UINT msg, WPARAM wp, LPARAM lp);

// The DLLs of the shell extensions a context menu of a file, a folder, a
// drive or of a folder's background is built from - the registry's
// ContextMenuHandlers under the keys that hold for all of them - each once,
// where COM will load each from.
std::vector<std::wstring> handlerDlls();
// Loads those of them that are not loaded yet.  Once: every call after the
// first loads nothing.  Returns how many it loaded.
//
// The first menu of a folder must not be the one that loads them.  Building
// it makes the shell list the libraries on one of its own threads, which
// reports back in a message sent to ours - and a DLL that makes a window as
// it is loaded (Baidu Netdisk's extension does) lets that message run while
// our thread still holds the loader lock.  What the message does needs a
// thread of its own, which waits for that lock: the two wait for each other
// for good, and the window never answers again.
int preloadHandlers();

// Used by --uitest: the final top-level labels of the menu that would be shown.
// With `subMenus`, also the entries of every submenu, as "submenu/entry".
std::vector<std::wstring> labels(const std::wstring& folder,
                                 const std::vector<std::wstring>& paths, const ViewState& state,
                                 std::vector<std::wstring>* subMenus = nullptr);

// Top-level labels the shell itself contributes, for the settings dialog.
std::vector<std::wstring> shellLabels(const std::wstring& folder,
                                      const std::vector<std::wstring>& paths, bool background);

// Enumerates a context's shell items by probing with a throwaway file in
// %TEMP%, so settings can be offered for any file type, not just whatever
// happens to sit in the current folder.  `ext` has no dot ("png"; empty =
// a file without an extension).
std::vector<std::wstring> probeFileLabels(const std::wstring& ext);
std::vector<std::wstring> probeFolderLabels();

// Used by --uitest: can the shell produce a menu for this path?
bool canBuildItemMenu(const std::wstring& path);
bool canBuildBackgroundMenu(const std::wstring& folder);

}  // namespace shellmenu
