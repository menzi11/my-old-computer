#include "scrollbar.h"

#include <algorithm>

namespace scrollbar {

RECT trackRect(const RECT& bar, const Metrics& m) {
    const int arrow = std::min(m.arrowH, (int)(bar.bottom - bar.top) / 2);
    return RECT{bar.left, bar.top + arrow, bar.right, bar.bottom - arrow};
}

RECT knobRect(const RECT& bar, const Metrics& m, int content, int view, int pos) {
    RECT track = trackRect(bar, m);
    if (content <= view || track.bottom <= track.top) return RECT{0, 0, 0, 0};
    const int trackH = track.bottom - track.top;
    double ratio = (double)view / (double)content;
    int h = (int)(trackH * ratio);
    int minH = (int)(18 * (m.rowH / 19.0));
    if (minH < 8) minH = 8;
    h = std::max(minH, h);
    if (h > trackH) h = trackH;
    int maxScroll = std::max(1, content - view);
    double p = std::min(1.0, (double)pos / (double)maxScroll);
    int y = track.top + (int)((trackH - h) * p);
    return RECT{track.left + 1, y, track.right - 1, y + h};
}

Part hitTest(const RECT& bar, POINT p, const Metrics& m, int content, int view, int pos) {
    if (!PtInRect(&bar, p)) return Part::None;
    const int arrow = std::min(m.arrowH, (int)(bar.bottom - bar.top) / 2);
    RECT dec{bar.left, bar.top, bar.right, bar.top + arrow};
    RECT inc{bar.left, bar.bottom - arrow, bar.right, bar.bottom};
    if (PtInRect(&dec, p)) return Part::Decrement;
    if (PtInRect(&inc, p)) return Part::Increment;
    RECT knob = knobRect(bar, m, content, view, pos);
    if (knob.bottom > knob.top && PtInRect(&knob, p)) return Part::Knob;
    if (content > view && p.y < knob.top) return Part::PageUp;
    if (content > view && p.y >= knob.bottom) return Part::PageDown;
    return Part::None;
}

int positionFromDrag(const RECT& bar, const Metrics& m, int content, int view, int startPos,
                     int deltaY) {
    RECT track = trackRect(bar, m);
    RECT knob = knobRect(bar, m, content, view, startPos);
    int knobH = knob.bottom - knob.top;
    int travel = (track.bottom - track.top) - knobH;
    if (travel <= 0) return startPos;
    int maxScroll = std::max(0, content - view);
    int pos = startPos + (int)((double)deltaY * maxScroll / travel);
    return std::clamp(pos, 0, maxScroll);
}

void draw(Canvas& c, const RECT& bar, const Metrics& m, int content, int view, int pos) {
    c.fill(bar, Theme::face);
    const int arrow = std::min(m.arrowH, (int)(bar.bottom - bar.top) / 2);
    RECT dec{bar.left, bar.top, bar.right, bar.top + arrow};
    RECT inc{bar.left, bar.bottom - arrow, bar.right, bar.bottom};
    for (int k = 0; k < 2; ++k) {
        RECT r = k == 0 ? dec : inc;
        if (r.bottom <= r.top) continue;
        c.fill(r, Theme::face);
        c.bevelRaised(r);
        const int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2;
        POINT tri[3];
        if (k == 0) {
            tri[0] = {cx - c.px(4), cy + c.px(2)};
            tri[1] = {cx + c.px(4), cy + c.px(2)};
            tri[2] = {cx, cy - c.px(3)};
        } else {
            tri[0] = {cx - c.px(4), cy - c.px(2)};
            tri[1] = {cx + c.px(4), cy - c.px(2)};
            tri[2] = {cx, cy + c.px(3)};
        }
        c.polygonFill(tri, 3, Theme::dark);
    }
    RECT track = trackRect(bar, m);
    if (track.bottom > track.top) {
        c.fill(track, Theme::faceHi);
        c.vline(track.left, track.top, track.bottom - track.top, Theme::shadow);
        c.vline(track.left + 1, track.top, track.bottom - track.top, Theme::white);
        c.vline(track.right - 1, track.top, track.bottom - track.top, Theme::shadow);
    }
    RECT knob = knobRect(bar, m, content, view, pos);
    if (knob.bottom > knob.top) {
        c.fill(knob, Theme::face);
        c.bevelRaised(knob);
    }
}

}  // namespace scrollbar
