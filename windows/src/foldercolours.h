// The colours a folder can be given, and which folder has which.  Chosen from
// the folder's own context menu (文件夹颜色); the folder's icon is drawn in
// that colour wherever this program shows the folder - in the list, as a big
// icon, in the tree.
//
// Kept in a file of this program's own, next to settings.ini: nothing is
// written into the folders, and Explorer shows them as it always did.  Every
// window of this program keeps a copy in memory and reads the file again when
// it comes to the front or opens a menu (reload), so what one window coloured,
// the others show too.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace foldercolours {

struct Colour {
    const wchar_t* name;
    COLORREF rgb;
};

constexpr int kCount = 8;     // colours to choose from
constexpr int kDefault = -1;  // no colour: the folder as it is

const Colour& colour(int i);  // 0 .. kCount - 1

// The file they are kept in.
std::wstring path();
// The colour of a folder: kDefault when it has none.
int of(const std::wstring& path);
// Gives every folder in `paths` its colour (kDefault: none) and keeps it.
void set(const std::vector<std::wstring>& paths, int colour);
// Reads the file again: another window may have coloured folders since.
void reload();

}  // namespace foldercolours
