#pragma once

#include <windows.h>
#include <objidl.h>

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

// For --uitest: the window's drop target once it is registered, and a data
// object carrying paths the way a drag does (the caller releases it), so the
// receiving side of a drag can be driven without a mouse.
IDropTarget* dropTarget();
IDataObject* makeData(const std::vector<std::wstring>& paths);

}  // namespace dnd
