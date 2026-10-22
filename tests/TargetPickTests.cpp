#include "features/information/TargetPick.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>
void check(bool, char const*);
namespace {
using lamium::schematic::Point;
// Where the ray from (x, y, z) along the unit (dx, dy, dz) enters the unit cube at `cell`.
std::optional<double> enterCube(Point cell, double x, double y, double z, double dx, double dy, double dz) {
    double near = 0, far = 1e9;
    auto axis = [&](double p, double d, int c) {
        if (d == 0) return p >= c && p <= c + 1;
        double a = (c - p) / d, b = (c + 1 - p) / d;
        near = std::max(near, std::min(a, b));
        far = std::min(far, std::max(a, b));
        return near <= far;
    };
    if (!axis(x, dx, cell.x) || !axis(y, dy, cell.y) || !axis(z, dz, cell.z)) return std::nullopt;
    return near;
}
}
void targetPickTests() {
    using namespace lamium::information;
    // A block at (0, 64, 0) with another above it; the eye in front, a little
    // below the lower block's top, looking up at its upper edge: the whole ray
    // goes on into the upper block's underside.
    std::set<std::tuple<int, int, int>> blocks{{0, 64, 0}, {0, 65, 0}};
    double x = -2, y = 64.5, z = .5;
    double dx = 2, dy = .4, dz = 0, length = std::sqrt(dx * dx + dy * dy);
    dx /= length; dy /= length;
    auto clip = [&](Point const& cell) -> std::optional<double> {
        if (!blocks.contains({cell.x, cell.y, cell.z})) return std::nullopt;
        return enterCube(cell, x, y, z, dx, dy, dz);
    };
    auto loaded = [](Point const&) { return false; };
    auto lower = enterCube({0, 64, 0}, x, y, z, dx, dy, dz);
    auto upper = enterCube({0, 65, 0}, x, y, z, dx, dy, dz);
    check(lower && upper && *lower < *upper, "the ray meets the lower block's face before the upper block");
    auto hit = nearestCellHit(x, y, z, dx, dy, dz, 6, CellHit{{0, 65, 0}, *upper}, clip, loaded);
    check(hit && hit->cell.y == 64 && hit->cell.x == 0, "a game pick of the block above gives way to the nearer face");
    hit = nearestCellHit(x, y, z, dx, dy, dz, 6, CellHit{{0, 65, 0}, *lower}, clip, loaded);
    check(hit && hit->cell.y == 64, "at the game's own distance the checked cell is named");
    hit = nearestCellHit(x, y, z, dx, dy, dz, 6, std::nullopt, clip, loaded);
    check(hit && hit->cell.y == 64 && std::abs(hit->distance - *lower) < 1e-9, "without a game pick the walk finds the block");
    hit = nearestCellHit(x, y, z, dx, dy, dz, 6, CellHit{{0, 64, 0}, *lower}, [](Point const&) { return std::optional<double>{}; },
        loaded);
    check(hit && hit->cell.y == 64, "a correct game pick stands when the cells report nothing");
    hit = nearestCellHit(x, y, z, dx, dy, dz, 6, std::nullopt, clip, [](Point const& cell) { return cell.x >= 0; });
    check(!hit, "the walk ends at a cell that is not loaded");
    hit = nearestCellHit(x, y, z, dx, dy, dz, 1.5, std::nullopt, clip, loaded);
    check(!hit, "nothing past the reach");
    auto fixed = [](double distance) {
        return [distance](Point const&) -> std::optional<double> { return distance; };
    };
    auto open = [](Point const&) { return false; };
    auto atTolerance = nearestCellHit(.5, .5, .5, 1, 0, 0, 6, CellHit{{0, 0, 0}, 2}, fixed(2.0005), open);
    check(atTolerance && std::abs(atTolerance->distance - 2.0005) < 1e-9,
          "a walked hit at the game distance's tolerance wins");
    auto outside = nearestCellHit(.5, .5, .5, 1, 0, 0, 6, CellHit{{0, 0, 0}, 2}, fixed(2.002), open);
    check(outside && std::abs(outside->distance - 2) < 1e-9,
          "a walked hit beyond the tolerance keeps the game's pick");
}
