#pragma once
#include "features/schematic/Placement.h"
#include <cmath>

// One-block placement steps from where the camera looks (L-230). Forward and
// back follow the view, so looking past 60 degrees up or down moves on Y;
// left and right stay horizontal, and explicit up/down keeps world Y.
// Per-step only: no gesture lock or hysteresis, so turning the view changes
// the next step at once.
namespace lamium::schematic {
struct Axes {
    Point forward; // one block toward the view, vertical when looking steeply
    Point right;   // one block to the right of the horizontal view; horizontal
};
// `view` is the direction the camera looks toward; any length.
inline Axes moveAxes(double vx, double vy, double vz) {
    constexpr double tan60 = 1.7320508075688772;
    double horizontal = std::sqrt(vx * vx + vz * vz);
    // The horizontal axis is needed even when looking straight up or down:
    // the view's tiny horizontal remainder still follows the yaw.
    Point side{0, 0, 1};
    if (horizontal >= 1e-9) {
        if (std::abs(vx) >= std::abs(vz)) side = {vx > 0 ? 1 : -1, 0, 0};
        else side = {0, 0, vz > 0 ? 1 : -1};
    }
    bool steep = std::abs(vy) > tan60 * horizontal;
    Point forward = steep ? Point{0, vy > 0 ? 1 : -1, 0} : side;
    return {forward, {-side.z, 0, side.x}};
}
inline Point scaled(Point p, int amount) { return {p.x * amount, p.y * amount, p.z * amount}; }
}
