#pragma once

// Command ids shared by the menu bar, context menus and the keyboard layer.
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
    ID_VIEW_BIG,
    ID_VIEW_LIST,
    ID_VIEW_DETAILS,
    ID_ADD_FAV,
    ID_ADD_FAV_CURRENT,
    ID_REMOVE_FAV,
    ID_OPEN_TERMINAL,
    ID_ABOUT,
    ID_FAV_BASE = 1000,
};
