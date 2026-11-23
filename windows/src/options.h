#pragma once

#include <windows.h>

#include <string>
#include <vector>

// One Explorer context whose shell menu items can be switched off
// individually.  `key` maps the section back onto the settings: "bg", "dir",
// "file" for the three base contexts, or a file extension without the dot.
struct MenuSection {
    std::wstring key;
    std::wstring title;
    std::vector<std::wstring> items;  // shell item labels; checked = visible
    std::vector<std::wstring> hidden; // labels currently switched off
};

bool showMenuOptions(HWND owner, unsigned* mask, std::vector<MenuSection>& sections);

// Renders the dialog into any DC with the given sections; lets --uitest draw
// the real dialog layout offscreen (no window, no interaction).
class Canvas;
void drawOptionsPreview(Canvas& c, const RECT& rc, double scale, unsigned mask,
                        const std::vector<MenuSection>& sections);
