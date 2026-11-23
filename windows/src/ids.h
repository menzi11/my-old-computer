#pragma once

// Command ids shared by the menu bar, context menus and the keyboard layer.

// Toolbar index of the "show hidden files" toggle.
constexpr int kToolbarHiddenIndex = 6;

enum MenuId {
    ID_NONE = 0,
    ID_OPEN = 1,
    ID_PREVIEW,
    ID_NEW_FOLDER,
    ID_RENAME,
    ID_DELETE,
    ID_CLOSE,
    ID_CUT,
    ID_COPY,
    ID_PASTE,
    ID_SELECT_ALL,
    ID_COPY_PATH,
    ID_REVEAL,
    ID_REFRESH,
    ID_TOGGLE_HIDDEN,
    ID_TOGGLE_SYSICONS,
    ID_VIEW_BIG,
    ID_VIEW_LIST,
    ID_VIEW_DETAILS,
    ID_ADD_FAV,
    ID_ADD_FAV_CURRENT,
    ID_REMOVE_FAV,
    ID_OPEN_TERMINAL,
    ID_ABOUT,
    ID_OPEN_WITH,
    ID_OPEN_NEW_WINDOW,
    ID_CREATE_SHORTCUT,
    ID_PROPERTIES,
    ID_SORT_NAME,
    ID_SORT_SIZE,
    ID_SORT_TYPE,
    ID_SORT_DATE,
    ID_SETTINGS,
    ID_FAV_BASE = 1000,
};
