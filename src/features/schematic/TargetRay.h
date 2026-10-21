#pragma once
#include <algorithm>
#include <cmath>
#include <optional>

// Where the drawn view ray enters the volumes the move target uses (BACKLOG
// L-231): a corner's block cell or the save area's box. Pure, so the air and
// occlusion rules are testable without the game.
namespace lamium::schematic {
struct TargetRay {
    double x = 0, y = 0, z = 0;
    double dx = 0, dy = 1, dz = 0;
    double reach = 256;
};
// Entry distance into an axis-aligned box; none when it misses, is behind, or
// is past the reach. 0 when the origin is inside.
inline std::optional<double> boxEntry(TargetRay const& ray, double lx, double ly, double lz, double hx, double hy, double hz) {
    double tmin = 0, tmax = ray.reach;
    auto axis = [&](double o, double d, double lo, double hi) {
        if (std::abs(d) < 1e-9) return o >= lo && o <= hi;
        double a = (lo - o) / d, b = (hi - o) / d;
        if (a > b) std::swap(a, b);
        tmin = std::max(tmin, a);
        tmax = std::min(tmax, b);
        return tmin <= tmax;
    };
    if (!axis(ray.x, ray.dx, lx, hx) || !axis(ray.y, ray.dy, ly, hy) || !axis(ray.z, ray.dz, lz, hz)) return std::nullopt;
    return tmin;
}
// A corner's block cell: the cell is a unit box, so a corner in the air is
// still pickable by looking through it.
inline std::optional<double> cellEntry(TargetRay const& ray, int x, int y, int z) {
    return boxEntry(ray, x, y, z, x + 1.0, y + 1.0, z + 1.0);
}
}
