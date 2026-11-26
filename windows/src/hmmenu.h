// A Win98-style popup menu renderer for an HMENU.  The menu contents (shell
// verbs included) come from the shell; only the drawing is ours.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

// Shows `root` at screenPoint and returns the chosen command id, or 0.
int showHMenu(HWND owner, HMENU root, POINT screenPoint);

// While a menu is up: right-clicking elsewhere must close it and open the new
// one afterwards.
bool menuIsOpen();
void menuClose();
// A left press outside the menu, on a window of this program's, closes the
// menu - and the press is that window's own: it is put in the queue for the
// window as soon as the menu is down.  A button-up that comes to the window
// before that press does is kept for it (menuPressLetGo: true when it was),
// and the press carries it along (menuPressGiven: true when an up is to
// follow at once, and where it was let go).
bool menuPressLetGo(HWND hwnd, POINT where);
bool menuPressGiven(HWND hwnd, POINT* where);

// (The tests.)  What the root menu on screen says, from the top; the menu that
// has the keyboard (a submenu that is open, else the root; null when none is
// up) and how many rows it has; and the row, in that menu's own coordinates,
// of the item whose text has `text` in it.
std::vector<std::wstring> menuRootTexts();
HWND menuActiveWindow();
int menuRowsUp();
// Menus put up since the program started (the tests: once a right-click).
int menuPutUps();
// A submenu of the menu on screen is open.
bool menuSubmenuUp();
// The menu on screen is made to say what `menu` says - it has the same rows,
// in the same order, as the one shown (the caller saw to that).  The menu is
// not closed and put up again: its rows are redrawn, and the window is as
// wide as its longest row is now.  Gives back the menu it showed, which is
// the caller's from then on - and `menu` is not any more.  Nothing, and both
// as they were, when no menu is up or `menu` has not as many rows.  A
// submenu that is open is closed: its rows were the old menu's.
HMENU menuRestate(HMENU menu);
// When the menu on screen came up (diagMillis's clock, ms).  Not the moment the
// window's thread is free again: the menu takes the foreground there, and that
// takes as long as the application that had it takes to let go.  (An instance
// that stays in the background, a test's, takes none.)
double menuShownAt();
bool menuRowOf(const wchar_t* text, RECT* rect);
// The same in the menu itself, a submenu open or not; and the menu's own window.
bool menuRootRowOf(const wchar_t* text, RECT* rect);
HWND menuRootWindow();
