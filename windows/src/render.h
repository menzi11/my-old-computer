#pragma once

#include <windows.h>

class App;
class Canvas;

// Paints the entire window (or a snapshot bitmap) using only App state.
void renderApp(Canvas& canvas, App& app);
