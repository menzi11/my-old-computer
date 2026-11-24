// Application state shared by the renderer and the window procedure.
#pragma once

#include <windows.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "fs.h"
#include "settings.h"
#include "theme.h"

// The timer that waits for the window of a new instance to come up, so that
// it can be brought to the front (App::openPathInNewWindow, raiseNewWindows).
constexpr UINT_PTR kNewWindowTimer = 0x4D4E;

// `s` as one argument of a command line, in quotes.  A backslash before the
// closing quote would escape it - and the root of a drive, C:\, ends in one -
// so the backslashes at the end are doubled.
std::wstring commandArgument(const std::wstring& s);

enum class ViewMode { Details = 0, Icons = 1, List = 2 };

struct TreeNode {
    enum Kind { Machine, Volume, Folder, Special, Favorite, Favorites } kind = Folder;
    std::wstring path;
    std::wstring title;
    int depth = 0;
    bool expanded = false;
    bool loaded = false;
    // Whether the row gets a +/-.  Known from the start for a drive, a special
    // folder, a favorite and a folder that was listed; for the folders inside
    // one that was just listed it is looked up the first time the row is drawn
    // or clicked (App::treeRowHasChildren) - hasChildren is not to be read
    // where that may be the case.
    bool hasChildren = true;
    bool hasChildrenKnown = true;
    bool missing = false;  // a favorite whose folder is gone: shown, but it leads nowhere
    std::vector<TreeNode> children;
};

struct Layout {
    RECT client{};
    RECT title{}, menu{}, tool{}, addr{}, status{};
    RECT sidebar{}, splitter{}, content{};
    RECT treeHeader{}, treeViewport{}, treeBar{};
    RECT listHeader{}, listViewport{}, listBar{};
    RECT gridViewport{}, gridBar{};
    RECT addrField{}, addrGo{}, addrArrow{};
    RECT addrMore{};   // the ... that stands for the crumbs out of view; empty when all fit
    RECT addrBlank{};  // the rest of the field: a click there types a path
    RECT statusLeft{}, statusRight{};
    int colX[4]{};
    int colW[4]{};
    int nameW = 0;
    // Measured once per relayout: hit tests and paints must not re-measure.
    int menuItemW[6]{};
    int toolItemW[7]{};
};

struct MenuItem {
    std::wstring title;
    int id = 0;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
    std::wstring shortcut;
};

// A name being edited in place (see App::renameField).
struct RenameField {
    RECT frame{};   // the white field and its 1 px frame, over the name's selection box
    RECT edit{};    // the edit control: exactly the line the row paints its name on
    int inset = 0;  // white between the frame and the name, so the name does not move
};

// One step of the way to the current folder, a button on the address bar.
struct Crumb {
    std::wstring path;   // the folder the button goes to; empty for the machine, the first
    std::wstring title;
    // What the button shows: the title; nothing for the machine when the row
    // is too narrow (its icon alone), or the title cut with an ellipsis when
    // even the current folder's name alone does not fit.
    std::wstring text;
    RECT box{};          // the button; empty for one that is out of view
    RECT more{};         // the v after it, which lists the folder's subfolders
};

// A row of one of the address bar's lists (see folderlist.h).
struct FolderRow {
    std::wstring path;
    std::wstring title;
    TreeNode::Kind kind = TreeNode::Folder;  // Machine, Volume or Folder: the row's icon
};

// What is under the pointer on the address bar.
enum class AddressHit { None, Blank, Crumb, CrumbMore, More };

class App {
public:
    HWND hwnd = nullptr;
    double scale = 1.0;
    Metrics m{};
    Settings settings;
    Layout lay;
    int width = 0;
    int height = 0;
    // Logical (96 dpi) frame size, used to keep the size stable across DPI changes.
    int logicalW = 1000;
    int logicalH = 640;
    bool windowActive = true;

    std::wstring current;
    std::vector<Entry> entries;
    std::vector<int> nameWidths;
    // Raw name text widths (nameWidths minus the icon lead-in), reused by the
    // list view's hit test so no view measures text on mouse move.
    std::vector<int> textWidths;
    std::vector<std::wstring> back;
    std::vector<std::wstring> forward;
    int sortColumn = 0;
    bool sortAsc = true;
    ViewMode mode = ViewMode::Details;
    // This folder is mostly pictures: its big-icon view shows the pictures
    // themselves, and the folder opens in that view (updateThumbnails).
    bool thumbnails = false;
    // The view is this folder's own: a folder of pictures opened as
    // thumbnails, whatever view is the usual one.  A view chosen here is for
    // here; the usual one is back on leaving.
    bool viewAutomatic = false;
    // Per entry: its thumbnail, if it has one, is drawn in the frame.
    std::vector<bool> thumbDrawn;
    // The entries the render in progress drew with their thumbnail.
    std::vector<int> thumbRendered;
    bool sidebarVisible = true;
    bool showHidden = false;
    bool useSystemIcons = true;
    int sidebarWidth = 200;

    std::set<std::wstring> selection;
    int selAnchor = -1;  // row index used for Shift+click ranges
    std::wstring contextFolder;
    bool contextIsFavorite = false;
    std::vector<std::wstring> clip;
    bool clipCut = false;

    int listScroll = 0;
    int gridScroll = 0;
    int treeScroll = 0;

    std::vector<TreeNode> treeRoots;
    std::vector<TreeNode*> treeFlat;
    // +/- answers per folder, invalidated when the disk or the hidden filter
    // changes (a process-wide cache used to go stale after every file op).
    std::map<std::wstring, bool> subdirCache;
    // Folders the tree has had to ask the disk about since this was last
    // zeroed: the tests and the log read it.
    int treeProbes = 0;
    // Row that was clicked in the tree.  Tree clicks only select and navigate;
    // they must not expand the tree or scroll it out from under the pointer.
    int treeClickRow = -1;

    // The tree row whose context menu is up: it is the tree's selected row
    // meanwhile, the way Explorer's tree shows it.
    int contextTreeRow = -1;

    // Hover / press feedback for the custom chrome.
    int hoverTree = -1;
    int hoverTool = -1;
    int hoverMenu = -1;
    int hoverTitle = -1;
    int pressTool = -1;
    int pressTitle = -1;
    int pressMenu = -1;

    // Marquee selection and internal drag.
    bool marquee = false;
    POINT marqueeStart{};
    POINT marqueeEnd{};
    // Plain click on one of several selected items: the multi-selection is
    // kept while the button is down (a drag may still start) and collapsed to
    // this row on release if it never did.
    int pressSelectedRow = -1;
    // The frame on screen is out of date and has to be rendered again (see
    // invalidate()).  Set whenever anything but the rubber band changes, so
    // the window can reuse its cached frame while a selection drag goes on.
    bool paintDirty = true;
    bool dragArmed = false;
    bool dragging = false;
    POINT dragPoint{};
    std::wstring dragPath;  // item the drag started on, used when nothing is selected
    std::wstring dragTargetFolder;
    bool dragTargetIsTree = false;
    int dragTargetRow = -1;
    bool dragTargetFavorites = false;  // the drag is over the Favorites group

    // A press on a tree row: a click that navigates when the button comes up
    // where it went down, or a drag when the pointer moves first.
    int treePressRow = -1;
    std::wstring treePressPath;
    POINT treePressPoint{};
    // A favorite pressed in the tree is dragged only up and down inside its
    // group: this is that drag, the place the favorite would land, and the
    // line to draw there.
    bool favDragArmed = false;
    bool favDragging = false;
    std::wstring favDragPath;
    int favInsert = -1;
    RECT favLine{};

    // Scrollbar dragging: 0 none, 1 list, 2 grid, 3 tree.
    int scrollDrag = 0;
    int scrollDragStartY = 0;
    int scrollDragStartPos = 0;

    // Sidebar splitter dragging.
    bool splitterDrag = false;
    int splitterStartW = 0;
    int splitterStartX = 0;

    // Demo mode: replace the sidebar tree with the current folder, for clean
    // screenshots that do not leak real folder names.
    bool demo = false;

    // Address bar edit control.
    HWND addrEdit = nullptr;
    bool addrEditing = false;
    HFONT addrFont = nullptr;  // rebuilt on DPI change, deleted then

    // The way to the current folder as buttons: one crumb per folder, built
    // when the folder changes (buildCrumbs) and placed when the window is
    // (layoutCrumbs).
    std::vector<Crumb> crumbs;
    // The first of the last crumbs, the ones after the ...; 0 when the whole
    // way fits.  The machine is in view before the ... either way.
    int crumbFirst = 0;
    int hoverCrumb = -1;         // the crumb under the pointer, -1 none
    int hoverCrumbMore = -1;     // ... its v
    bool hoverAddrMore = false;  // ... the ... that stands for the crumbs out of view
    int pressCrumb = -1;         // a crumb pressed and not released yet
    int pressCrumbMore = -1;     // the v whose list is up
    bool pressAddrMore = false;  // the ... of the crumbs out of view, while its list is up

    // In-place rename, Explorer style: an edit control laid over the row's
    // name.  Enter commits, Escape cancels, losing focus commits.
    bool renaming = false;
    int renameRow = -1;
    HWND renameEdit = nullptr;
    HFONT renameFont = nullptr;

    void init(HWND h);
    void setScale(double s);

    // Layout must be recomputed before painting or hit testing.
    void relayout(int w, int h);

    // Says that the frame on screen no longer shows this state.  The window
    // keeps its last frame and draws into it for hovering and scrolling, so
    // it only renders again when told - and a change nobody announced stayed
    // on screen as it was, old folder and all, until the next click.
    void invalidate();

    // Navigation
    void loadDirectory(const std::wstring& path, bool recordHistory = true,
                       bool fromTreeClick = false);
    void reload();
    void goBack();
    void goForward();
    void goUp();
    bool goToTypedPath(const std::wstring& text);
    void setViewMode(ViewMode v);
    void cycleViewMode();
    // Once a folder is listed: whether it is mostly pictures (thumbnails),
    // and the view it opens in.
    void updateThumbnails();

    // Selection
    void selectAll();
    void clearSelection();
    // Paths the context menu and the file commands act on: the selected items,
    // or the folder a context menu was opened on in the tree.
    std::vector<std::wstring> selectedPaths() const;
    std::wstring firstSelectedPath() const;

    // File operations
    void openEntry(const Entry& e);
    void openSelection();
    void newFolder();
    void newTextFile();
    void renameSelection();
    void deleteSelection();
    void copySelection(bool cut);
    void paste();
    void copyPaths();
    void revealSelection();
    void openTerminalForSelection();
    void openWithSelection();
    // The selected folder in a new window.
    void openInNewWindow();
    // A folder in a new window: another instance of this program, started in
    // it and placed like this window (see cascadePosition).  Returns that
    // instance's id, 0 when there is nothing to open or it could not start.
    // The window of that instance is to be in front: its id is kept until the
    // window is there and has been brought to the front (raiseNewWindows).
    DWORD openPathInNewWindow(const std::wstring& path);
    std::vector<DWORD> newWindowPids;  // the instances whose window is not up yet
    int newWindowTicks = 0;            // how long the last of them has been waited for
    void createShortcutForSelection();
    void showSelectionProperties();
    void sortBy(int column);
    void openMenuSettings();
    // `toFavorites`: dropped on the Favorites group - every folder among the
    // paths becomes a favorite instead of being moved or copied.
    bool performDrop(const std::vector<std::wstring>& paths, const std::wstring& folder,
                     bool toFavorites = false);

    // Explorer-style rename: an edit control over the row's name, the base
    // name pre-selected.  commit=false restores the old name.
    void beginRename(int row);
    void endRename(bool commit);
    RenameField renameField(int row) const;
    void placeRenameEdit();

    // Favorites
    void addFavorite(const std::wstring& path);
    void removeFavorite(const std::wstring& path);
    void renameFavorite(const std::wstring& path, const std::wstring& name);  // empty: the folder's own name
    void promptRenameFavorite(const std::wstring& path);
    void moveFavorite(int from, int to);  // `to`: its index in the list afterwards
    int favoriteIndex(const std::wstring& path) const;
    std::wstring favoriteTitle(const Favorite& f) const;
    int dropOnFavorites(const std::vector<std::wstring>& paths);  // returns how many were new
    void refreshFavorites();  // rebuilds the Favorites group, and nothing else
    // Where the favorite being dragged lands if the pointer lets go at p: its
    // index in the list, and the line to draw.  -1 and an empty rect when p
    // is not over the group.
    void favoriteDropPosition(POINT p, int* insert, RECT* line) const;

    // Tree
    void buildTree();
    void buildFavoriteNode(TreeNode& fav, const Favorite& f);
    void loadTreeChildren(TreeNode& node);
    void refreshTree();
    // Keeps the tree in sync after a file operation: deleted folders leave the
    // tree, renamed/new ones are re-read, and a deleted current folder is left.
    void pruneTree();
    void reloadTreeChildrenAt(const std::wstring& dir);
    void refreshAfterChange();
    void toggleTreeNode(TreeNode& node);
    void revealInTree();
    void flattenTree();
    int treeSelectedRow() const;
    void scrollTreeToSelection();
    int rowOfPath(const std::wstring& path) const;
    void revealRow(int row);  // scrolls the file view just enough to show the row
    bool treeHasSubdir(const std::wstring& path);
    // Whether a tree row gets a +/-, looked up now if nobody has asked yet.
    bool treeRowHasChildren(TreeNode& n);
    void noteChildren(const std::wstring& dir, bool hasSubdir);

    // Geometry helpers (all return screen-client coordinates)
    RECT titleButtonRect(int i) const;    RECT menuItemRect(int i) const;
    int menuItemAt(POINT p) const;
    RECT toolbarItemRect(int i) const;
    int toolbarItemCount() const;
    int toolbarIndexAt(POINT p) const;
    RECT treeRowRect(int i) const;
    int treeRowAt(POINT p) const;
    int treeRowCount() const;
    RECT listRowRect(int i) const;
    int listRowAt(POINT p) const;
    // Right edge of the icon plus the name text, relative to the row start.
    // Clicks beyond it must not select the row (no full-row select).
    int nameExtent(int row) const;
    // Name widths are measured once per listing (and on DPI change): measuring
    // them inside hit tests and paint made dragging in large folders crawl.
    void rebuildNameWidths();
    // True when p is on an item's icon or its name (not on the empty part of a
    // row or a grid cell) - only there may a click select and start a drag.
    bool hitTestItem(POINT p, int* index) const;
    int gridCols() const;
    int gridCellW() const;
    int gridCellH() const;
    // The big-icon view: the side of the box an icon (or a thumbnail) is
    // drawn in, and where the name under it begins.
    int iconBox() const;
    int labelTop() const;
    RECT gridCellRect(int i) const;
    int gridIndexAt(POINT p) const;
    int gridContentHeight() const;
    int listContentHeight() const;
    int treeContentHeight() const;

    // Scroll positions
    void clampScroll();
    void scrollListBy(int delta);
    void scrollGridBy(int delta);
    void scrollTreeBy(int delta);

    // Status
    std::wstring statusLeftText() const;

    // Menus
    std::vector<MenuItem> menuFor(const std::wstring& name) const;
    std::wstring menuTitle(int index) const;
    std::vector<MenuItem> contextMenu() const;
    std::vector<MenuItem> placesMenu() const;
    void performMenuAction(int id, const std::wstring& arg = L"");
    // Shell labels to hide for a context-menu invocation: the background,
    // folder items, or files (generic list plus the first item's extension).
    std::vector<std::wstring> hiddenFor(const std::vector<std::wstring>& paths) const;

    // Persistence
    void saveSettings();

    // Address bar.  onlyIfChanged: the box lost its focus with the path as it
    // was - nothing is gone to again then.
    void beginAddressEdit();
    void endAddressEdit(bool commit, bool onlyIfChanged = false);
    // The white inside the address field's frame: where the crumbs go.
    RECT addressInner() const;
    // The crumbs for the current folder, one per folder from the machine down.
    void buildCrumbs();
    // Places them in the field, from the left: all in a row while they fit,
    // else as many of the last ones as do, with a ... in front for the rest
    // (called by relayout).
    void layoutCrumbs();
    // What is under p on the address bar; `crumb` gets the one the point is on
    // the button or the v of.
    AddressHit addressAt(POINT p, int* crumb = nullptr) const;
    // The rows of the v after a crumb: the folder's subfolders, the machine's
    // drives.  `currentRow` gets the one the way to the current folder goes
    // through, -1 when the list does not have it.
    std::vector<FolderRow> crumbRows(int crumb, int* currentRow) const;
    // The crumbs the ... stands for, in order: the machine is not among them,
    // it is in view ahead of it.
    std::vector<FolderRow> hiddenCrumbRows() const;

private:
    bool isEditingText() const;

public:
    bool qlOpen = false;
};
