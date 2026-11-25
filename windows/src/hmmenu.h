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

// (The tests.)  What the root menu on screen says, from the top; the menu that
// has the keyboard (a submenu that is open, else the root; null when none is
// up) and how many rows it has; and the row, in that menu's own coordinates,
// of the item whose text has `text` in it.
std::vector<std::wstring> menuRootTexts();
HWND menuActiveWindow();
int menuRowsUp();
// When the menu on screen came up (diagMillis's clock, ms).  Not the moment the
// window's thread is free again: the menu takes the foreground there, and that
// takes as long as the application that had it takes to let go.
double menuShownAt();
bool menuRowOf(const wchar_t* text, RECT* rect);
