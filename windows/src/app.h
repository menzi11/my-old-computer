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

enum class ViewMode { Details = 0, Icons = 1, List = 2 };

struct TreeNode {
    enum Kind { Machine, Volume, Folder, Special, Favorite, Favorites } kind = Folder;
    std::wstring path;
    std::wstring title;
    int depth = 0;
    bool expanded = false;
    bool loaded = false;
    bool hasChildren = true;
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
    // Set whenever anything but the rubber band changes; lets the window reuse
    // its cached frame while a selection drag is in progress.
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
    void openInNewWindow();
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

    // Address bar
    void beginAddressEdit();
    void endAddressEdit(bool commit);

private:
    bool isEditingText() const;

public:
    bool qlOpen = false;
};
