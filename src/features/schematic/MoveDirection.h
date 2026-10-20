#pragma once
#include "features/schematic/Placement.h"
#include <cmath>

// One-block placement steps from where the camera looks (L-230). All six
// directions come from one camera frame, so if forward/back turn vertical
// when looking steeply, up/down take over the horizontal direction instead
// of disappearing. Per-step only: no gesture lock or hysteresis.
namespace lamium::schematic {
struct Axes {
    Point forward; // toward the view; vertical when looking 60 degrees or more
    Point right;   // horizontal perpendicular to the view's yaw
    Point up;      // right x forward: world Y unless forward is vertical
};
inline Point cross(Point a, Point b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
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
    Point right{-side.z, 0, side.x};
    // Screen up, as a world axis: world Y with a level-ish view, and the
    // horizontal direction the camera faces when forward has gone vertical.
    return {forward, right, cross(right, forward)};
}
inline Point scaled(Point p, int amount) { return {p.x * amount, p.y * amount, p.z * amount}; }
}
