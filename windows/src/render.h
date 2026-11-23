#pragma once

#include <windows.h>

class App;
class Canvas;

// Paints the entire window (or a snapshot bitmap) using only App state.
void renderApp(Canvas& canvas, App& app);

// Draws only the rubber-band rectangle; the rest of the window stays cached.
void renderMarquee(Canvas& canvas, App& app);
