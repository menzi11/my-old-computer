#pragma once

#include <windows.h>

#include <string>

class App;

// Renders the whole UI into a 32-bit DIB and writes it as a PNG.
bool renderSnapshot(App& app, const std::wstring& path, int width, int height);
