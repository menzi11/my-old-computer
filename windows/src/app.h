// Application state shared by the renderer and the window procedure.
#pragma once

#include <windows.h>

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
};

struct MenuItem {
    std::wstring title;
    int id = 0;
    bool enabled = true;
    bool checked = false;
    bool separator = false;
    std::wstring shortcut;
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
    // Row that was clicked in the tree.  Tree clicks only select and navigate;
    // they must not expand the tree or scroll it out from under the pointer.
    int treeClickRow = -1;

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
    bool dragArmed = false;
    bool dragging = false;
    POINT dragPoint{};
    std::wstring dragTargetFolder;
    bool dragTargetIsTree = false;
    int dragTargetRow = -1;

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
    std::vector<const Entry*> selectedEntries() const;
    const Entry* firstSelected() const;

    // File operations
    void openEntry(const Entry& e);
    void openSelection();
    void newFolder();
    void renameSelection();
    void deleteSelection();
    void copySelection(bool cut);
    void paste();
    void copyPaths();
    void revealSelection();
    void openTerminal();
    bool performDrop(const std::vector<std::wstring>& paths, const std::wstring& folder);

    // Favorites
    void addFavorite(const std::wstring& path);
    void removeFavorite(const std::wstring& path);

    // Tree
    void buildTree();
    void loadTreeChildren(TreeNode& node);
    void refreshTree();
    void toggleTreeNode(TreeNode& node);
    void revealInTree();
    void flattenTree();
    int treeSelectedRow() const;
    void scrollTreeToSelection();
    bool treeHasSubdir(const std::wstring& path);
    void noteChildren(const std::wstring& dir, bool hasSubdir);

    // Geometry helpers (all return screen-client coordinates)
    RECT titleButtonRect(int i) const;
    RECT menuItemRect(int i) const;
    int menuItemAt(POINT p) const;
    RECT toolbarItemRect(int i) const;
    int toolbarItemCount() const;
    int toolbarIndexAt(POINT p) const;
    RECT treeRowRect(int i) const;
    int treeRowAt(POINT p) const;
    int treeRowCount() const;
    RECT listRowRect(int i) const;
    int listRowAt(POINT p) const;
    int listVisibleRows() const;
    int gridCellCount() const;
    int gridCols() const;
    int gridCellW() const;
    int gridCellH() const;
    RECT gridCellRect(int i) const;
    int gridIndexAt(POINT p) const;
    int gridContentHeight() const;
    int listContentHeight() const;
    int treeContentHeight() const;
    std::wstring nameExtentPath() const;

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
    std::vector<MenuItem> treeContextMenu(const std::wstring& nodePath, bool isFavorite) const;
    std::vector<MenuItem> placesMenu() const;
    void performMenuAction(int id, const std::wstring& arg = L"");

    // Persistence
    void saveSettings();

    // Address bar
    void beginAddressEdit();
    void endAddressEdit(bool commit);

private:
    std::vector<std::wstring> menuFavorites() const;
    bool isEditingText() const;

public:
    bool qlOpen = false;
};
