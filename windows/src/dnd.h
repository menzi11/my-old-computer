#pragma once

#include <windows.h>

#include <string>
#include <vector>

class App;

// OLE drag and drop: a drop target for the window (so files can be dropped
// from Explorer or moved inside the app) and a drag source so files can be
// dragged out to Explorer, the desktop or another application.
namespace dnd {

void registerTarget(HWND hwnd, App* app);
void revokeTarget(HWND hwnd);

// Blocks until the drag finishes.  *effect receives the performed effect.
bool dragOut(HWND hwnd, const std::vector<std::wstring>& paths, DWORD* effect);

}  // namespace dnd
