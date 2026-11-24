#pragma once

#include <windows.h>

#include <string>

class App;

// Renders the whole UI into a 32-bit DIB and writes it as a PNG.
bool renderSnapshot(App& app, const std::wstring& path, int width, int height);

// Writes pixels as a PNG: 32 bits each, top-down, in the order GetDIBits
// gives them (blue, green, red, unused).
bool writePng(const std::wstring& path, const void* bgrx, int w, int h);
