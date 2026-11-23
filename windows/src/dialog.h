// Custom Win98-style popup menus and modal dialogs.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "app.h"

int showPopupMenu(HWND owner, const std::vector<MenuItem>& items, int x, int y, bool alignRight);

bool promptText(HWND owner, const std::wstring& title, const std::wstring& label,
                const std::wstring& initial, std::wstring* result);
void showAbout(HWND owner);
void showError(HWND owner, const std::wstring& title, const std::wstring& text);

// Lets the user choose which entries the context menus contain.  `shellItems`
// lists the labels the shell contributes, `hidden` receives the hidden ones.
bool showMenuOptions(HWND owner, unsigned* mask, const std::vector<std::wstring>& shellItems,
                     std::vector<std::wstring>& hidden);
