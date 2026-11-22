// Custom Win98 scrollbar geometry shared by drawing and hit testing.
#pragma once

#include <windows.h>

#include "theme.h"

namespace scrollbar {

RECT trackRect(const RECT& bar, const Metrics& m);
RECT knobRect(const RECT& bar, const Metrics& m, int content, int view, int pos);

enum class Part { None, Decrement, Increment, Knob, PageUp, PageDown };

Part hitTest(const RECT& bar, POINT p, const Metrics& m, int content, int view, int pos);

// Converts a knob drag (delta pixels) into a new scroll position.
int positionFromDrag(const RECT& bar, const Metrics& m, int content, int view, int startPos,
                     int deltaY);

}  // namespace scrollbar
