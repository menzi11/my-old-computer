// Custom Win98 scrollbar geometry shared by drawing and hit testing.
#pragma once

#include <windows.h>

#include "canvas.h"
#include "theme.h"

namespace scrollbar {

RECT trackRect(const RECT& bar, const Metrics& m);
RECT knobRect(const RECT& bar, const Metrics& m, int content, int view, int pos);

enum class Part { None, Decrement, Increment, Knob, PageUp, PageDown };

Part hitTest(const RECT& bar, POINT p, const Metrics& m, int content, int view, int pos);

// Converts a knob drag (delta pixels) into a new scroll position.
int positionFromDrag(const RECT& bar, const Metrics& m, int content, int view, int startPos,
                     int deltaY);

// Draws a complete scrollbar; geometry and pixels in one place so the main
// window and the settings dialog share the exact same look.
void draw(Canvas& c, const RECT& bar, const Metrics& m, int content, int view, int pos);

}  // namespace scrollbar
