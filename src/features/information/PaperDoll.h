#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

namespace lamium::information::paperDoll {
// Whether Lamium's Info HUD covers the game's paper doll (L-145). Pure.
struct Box { float x, y, w, h; };
// The doll is drawn larger than its control (15x15 units on desktop): about
// one control width to each side, and from the top of the screen down to
// where the chat's room for it ends (50 units for a 15-unit control).
inline std::optional<Box> area(Box control) {
    for (float v : {control.x, control.y, control.w, control.h})
        if (!std::isfinite(v)) return std::nullopt;
    if (control.w <= 0 || control.h <= 0) return std::nullopt;
    float side = control.w;
    return Box{control.x - side, 0, 3 * side, control.y + side * 35 / 15};
}
inline bool overlaps(Box a, Box b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
inline bool covered(std::optional<Box> control, std::optional<Box> hud) {
    if (!control || !hud) return false;
    auto doll = area(*control);
    return doll && overlaps(*doll, *hud);
}
}
