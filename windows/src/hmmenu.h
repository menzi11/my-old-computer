// A Win98-style popup menu renderer for an HMENU.  The menu contents (shell
// verbs included) come from the shell; only the drawing is ours.
#pragma once

#include <windows.h>

// Shows `root` at screenPoint and returns the chosen command id, or 0.
int showHMenu(HWND owner, HMENU root, POINT screenPoint);
