#pragma once

#include <windows.h>

#include <string>

// Icons exactly as Windows Explorer shows them, taken from the shell image
// lists.  Keyed by directory, file extension or, for executables and
// shortcuts, the file itself.  The size picks the closest shell image list so
// the icon is never blown up more than necessary.
namespace shellicons {

HICON icon(const std::wstring& path, bool isDir, bool isDrive, int px);
void clear();

}  // namespace shellicons
