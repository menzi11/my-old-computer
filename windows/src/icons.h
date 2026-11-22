// Code-drawn Win98 style icons: no bitmap resources anywhere in the project.
#pragma once

#include <windows.h>

#include <string>

#include "canvas.h"

namespace Icon {
void folder(Canvas& c, const RECT& r, bool open = false);
void document(Canvas& c, const RECT& r);
void computer(Canvas& c, const RECT& r);
void views(Canvas& c, const RECT& r);
void file(Canvas& c, const RECT& r, const std::wstring& extensionLower);
void drive(Canvas& c, const RECT& r);
void arrowLeft(Canvas& c, const RECT& r);
void arrowRight(Canvas& c, const RECT& r);
void arrowUp(Canvas& c, const RECT& r);
void refresh(Canvas& c, const RECT& r);
void sortArrow(Canvas& c, const RECT& r, bool up);
void shortcut(Canvas& c, const RECT& r);
}  // namespace Icon
