#pragma once

#include <windows.h>

#include <string>

// A Win98 "Quick View" style preview window: images are decoded with WIC,
// text files show their first screen, everything else shows its properties.
namespace preview {

void show(HWND owner, const std::wstring& path);
void hide();
bool visible();
void setSystemIcons(bool on);

}  // namespace preview
