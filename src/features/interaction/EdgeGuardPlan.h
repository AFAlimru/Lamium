#pragma once
#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace lamium::interaction {
// Edge Guard (BACKLOG L-40): shorten a horizontal move until the player's
// feet stay supported, the way sneaking stops at an edge. `supported(dx, dz)`
// answers whether ground lies within the step height under the feet shifted
// by (dx, dz). Each axis is tried alone first so sliding along an edge keeps
// the unaffected component.
template <class Supported>
std::pair<double, double> guardEdge(double dx, double dz, Supported&& supported, double increment = 0.05) {
    auto shrink = [increment](double v) { return std::abs(v) <= increment ? 0.0 : v - std::copysign(increment, v); };
    if (!std::isfinite(dx) || !std::isfinite(dz) || !(increment > 0)) return {dx, dz};
    while (dx != 0 && !supported(dx, 0.0)) dx = shrink(dx);
    while (dz != 0 && !supported(0.0, dz)) dz = shrink(dz);
    while (dx != 0 && dz != 0 && !supported(dx, dz)) {
        dx = shrink(dx);
        dz = shrink(dz);
    }
    return {dx, dz};
}

// The move input is (strafe, forward), strafe positive to the left; yaw in
// degrees, 0 facing +z, turning toward -x. World and input directions convert
// with one rotation.
inline std::pair<double, double> worldMove(double strafe, double forward, double yawDegrees) {
    double yaw = yawDegrees * std::numbers::pi / 180, c = std::cos(yaw), s = std::sin(yaw);
    return {strafe * c - forward * s, strafe * s + forward * c};
}
inline std::pair<double, double> inputMove(double x, double z, double yawDegrees) {
    double yaw = yawDegrees * std::numbers::pi / 180, c = std::cos(yaw), s = std::sin(yaw);
    return {x * c + z * s, -x * s + z * c};
}

// Edge Guard on a server (BACKLOG L-160): the server moves the player from
// the move input, so the input itself must not lead over an edge. (x, z) is
// the input's world direction, (vx, vz) the current horizontal velocity per
// tick, `accel` the ground acceleration per tick of full input and `friction`
// the velocity kept per tick. On each axis the input keeps as much of itself
// as leaves the feet supported along the whole slide that follows when it
// stops next tick; when even the slide alone would leave, the input brakes,
// only as hard as needed (a full reverse pushed the player back off the edge).
template <class Supported>
std::pair<double, double> guardInput(double x, double z, double vx, double vz, double accel, double friction,
                                     Supported&& supported, double step = 0.1) {
    if (!std::isfinite(x) || !std::isfinite(z) || !std::isfinite(vx) || !std::isfinite(vz)
        || !(friction > 0 && friction < 1) || !(accel >= 0) || !(step > 0)) return {x, z};
    double slide = 1 / (1 - friction);
    auto clear = [&](double dx, double dz) {
        int steps = std::max(1, static_cast<int>(std::ceil(std::max(std::abs(dx), std::abs(dz)) / step)));
        for (int i = 1; i <= steps; ++i)
            if (!supported(dx * i / steps, dz * i / steps)) return false;
        return true;
    };
    auto axis = [&](double input, double velocity, bool alongX) {
        auto reach = [&](double in) {
            double d = (velocity * friction + in * accel) * slide;
            return alongX ? clear(d, 0) : clear(0, d);
        };
        if (reach(input)) return input;
        // The largest share of `to` that still reaches safely, given that
        // `from` does (or `to` if none is found).
        auto most = [&](double from, double to) {
            double good = from, bad = to;
            for (int i = 0; i < 8; ++i) {
                double mid = (good + bad) / 2;
                (reach(mid) ? good : bad) = mid;
            }
            return good;
        };
        if (reach(0)) return input == 0 ? 0.0 : most(0, input);
        if (velocity == 0) return 0.0;
        double brake = -std::copysign(1.0, velocity);
        return reach(brake) ? most(brake, 0) : brake;
    };
    double gx = axis(x, vx, true), gz = axis(z, vz, false);
    if (gx != 0 && gz != 0 && !clear((vx * friction + gx * accel) * slide, (vz * friction + gz * accel) * slide)) {
        gx = axis(0, vx, true);
        gz = axis(0, vz, false);
    }
    double length = std::hypot(gx, gz);
    if (length > 1) { gx /= length; gz /= length; }
    return {gx, gz};
}
}
