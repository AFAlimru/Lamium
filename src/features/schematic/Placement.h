#pragma once
#include "features/schematic/Structure.h"
#include <algorithm>
#include <cstdint>
#include <optional>

// Where a schematic sits in the world and which part of it is shown
// (BACKLOG L-93). Positions only; block states are turned by the game's own
// transform in the render/verify glue.
namespace lamium::schematic {
struct Point {
    int x = 0, y = 0, z = 0;
    bool operator==(Point const&) const = default;
};
// X flips east-west, Z flips north-south. Applied before rotation.
enum class Mirror : std::uint8_t { None, X, Z };
struct Placement {
    Point origin;      // lower north-west corner of the placed (turned) box
    int rotation = 0;  // clockwise quarter turns seen from above, 0-3
    Mirror mirror = Mirror::None;
};

inline int quarterTurns(int rotation) { return ((rotation % 4) + 4) % 4; }
inline Size placedSize(Size size, int rotation) {
    return quarterTurns(rotation) % 2 ? Size{size.z, size.y, size.x} : size;
}
// The placed box seen from above, in world blocks: [x0, x1) by [z0, z1).
struct Footprint {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;
    bool operator==(Footprint const&) const = default;
};
inline Footprint footprint(Size size, Placement const& placement) {
    auto placed = placedSize(size, placement.rotation);
    return {placement.origin.x, placement.origin.z, placement.origin.x + placed.x, placement.origin.z + placed.z};
}
// Structure-local cell -> world cell.
inline Point toWorld(Size size, Placement const& placement, Point local) {
    int x = local.x, z = local.z;
    if (placement.mirror == Mirror::X) x = size.x - 1 - x;
    if (placement.mirror == Mirror::Z) z = size.z - 1 - z;
    // One clockwise turn takes east to south: (x, z) -> (depth-1-z, x).
    int width = size.x, depth = size.z;
    for (int i = 0; i < quarterTurns(placement.rotation); ++i) {
        int turnedX = depth - 1 - z, turnedZ = x;
        x = turnedX; z = turnedZ;
        std::swap(width, depth);
    }
    return {placement.origin.x + x, placement.origin.y + local.y, placement.origin.z + z};
}
// A free position in structure space (an entity's) -> world, turned and
// mirrored like cells: a cell's center lands on the center of its world cell.
struct Position {
    double x = 0, y = 0, z = 0;
    bool operator==(Position const&) const = default;
};
inline Position toWorldPosition(Size size, Placement const& placement, Position local) {
    double x = local.x, z = local.z;
    if (placement.mirror == Mirror::X) x = size.x - x;
    if (placement.mirror == Mirror::Z) z = size.z - z;
    double width = size.x, depth = size.z;
    for (int i = 0; i < quarterTurns(placement.rotation); ++i) {
        double turnedX = depth - z, turnedZ = x;
        x = turnedX; z = turnedZ;
        std::swap(width, depth);
    }
    return {placement.origin.x + x, placement.origin.y + local.y, placement.origin.z + z};
}
// An entity's facing (yaw in degrees: 0 faces south, 90 west) after the same
// mirror and turns as its position. Mirrored as the game's structure block
// does: on the other axis, which is a true mirror turned half round (seen
// in game: a south-facing armor stand faces north under the X mirror).
inline float toWorldYaw(float yaw, Placement const& placement) {
    if (placement.mirror == Mirror::X) yaw = 180 - yaw;
    if (placement.mirror == Mirror::Z) yaw = -yaw;
    yaw += 90.f * quarterTurns(placement.rotation);
    while (yaw >= 180) yaw -= 360;
    while (yaw < -180) yaw += 360;
    return yaw;
}
// A block face (the game's order: down, up, north, south, west, east) after
// the same mirror and turns as cells, for a direction kept in block entity
// data rather than in block states (a shulker box's opening, L-179).
inline int toWorldFace(int face, Placement const& placement) {
    if (face < 2 || face > 5) return face;
    if (placement.mirror == Mirror::X && face >= 4) face = 9 - face;
    if (placement.mirror == Mirror::Z && face <= 3) face = 5 - face;
    constexpr int clockwise[6] = {0, 1, 5, 4, 2, 3}; // north -> east -> south -> west
    for (int i = 0; i < quarterTurns(placement.rotation); ++i) face = clockwise[face];
    return face;
}
// World cell -> structure-local cell, or nullopt outside the placed box.
inline std::optional<Point> toLocal(Size size, Placement const& placement, Point world) {
    Size placed = placedSize(size, placement.rotation);
    int x = world.x - placement.origin.x, y = world.y - placement.origin.y, z = world.z - placement.origin.z;
    if (x < 0 || y < 0 || z < 0 || x >= placed.x || y >= placed.y || z >= placed.z) return std::nullopt;
    int width = placed.x, depth = placed.z;
    for (int i = 0; i < quarterTurns(placement.rotation); ++i) {
        // Inverse of one clockwise turn: (x, z) -> (z, width-1-x).
        int localX = z, localZ = width - 1 - x;
        x = localX; z = localZ;
        std::swap(width, depth);
    }
    if (placement.mirror == Mirror::X) x = size.x - 1 - x;
    if (placement.mirror == Mirror::Z) z = size.z - 1 - z;
    return Point{x, y, z};
}

// Which layers are shown: a cut plane along one placed-box axis. `cut` is a
// local offset from the low corner, so moving or rotating the placement keeps
// the same physical slice. The six-direction fields stay for older readers and
// for files written before `layerView` existed (L-234).
enum class LayerAxis : std::uint8_t { Y, X, Z };
enum class LayerKeep : std::uint8_t { Low, High };
enum class LayerMode : std::uint8_t { All, Only, UpTo };
// The old model's direction: which side the index counted from.
enum class LegacyLayerAxis : std::uint8_t {
    UpFromBottom, DownFromTop, EastFromWest, WestFromEast, SouthFromNorth, NorthFromSouth
};
struct Layers {
    LayerAxis axis = LayerAxis::Y;
    LayerMode mode = LayerMode::All;
    LayerKeep keep = LayerKeep::Low;
    int cut = 0;          // 0 = the low layer along the axis
    bool legacy = false;  // still the old six-direction form; needs the placed size
    LegacyLayerAxis legacyAxis = LegacyLayerAxis::UpFromBottom;
    LayerMode legacyMode = LayerMode::All;
    int legacyIndex = 0;
    bool operator==(Layers const&) const = default;
};
inline int layerCount(Size placed, LayerAxis axis) {
    switch (axis) {
    case LayerAxis::Y: return placed.y;
    case LayerAxis::X: return placed.x;
    default: return placed.z;
    }
}
// `offset` is the cell's position inside the placed box.
inline int layerOf(Size, LayerAxis axis, Point offset) {
    switch (axis) {
    case LayerAxis::Y: return offset.y;
    case LayerAxis::X: return offset.x;
    default: return offset.z;
    }
}
inline LayerAxis layerAxisOf(LegacyLayerAxis axis) {
    switch (axis) {
    case LegacyLayerAxis::UpFromBottom: case LegacyLayerAxis::DownFromTop: return LayerAxis::Y;
    case LegacyLayerAxis::EastFromWest: case LegacyLayerAxis::WestFromEast: return LayerAxis::X;
    default: return LayerAxis::Z;
    }
}
inline bool legacyHighSide(LegacyLayerAxis axis) {
    return axis == LegacyLayerAxis::DownFromTop || axis == LegacyLayerAxis::WestFromEast
        || axis == LegacyLayerAxis::NorthFromSouth;
}
inline LegacyLayerAxis lowLegacyAxis(LayerAxis axis) {
    return axis == LayerAxis::Y ? LegacyLayerAxis::UpFromBottom
        : axis == LayerAxis::X ? LegacyLayerAxis::EastFromWest : LegacyLayerAxis::SouthFromNorth;
}
inline LegacyLayerAxis highLegacyAxis(LayerAxis axis) {
    return axis == LayerAxis::Y ? LegacyLayerAxis::DownFromTop
        : axis == LayerAxis::X ? LegacyLayerAxis::WestFromEast : LegacyLayerAxis::NorthFromSouth;
}
// The old reader's fields for the current cut plane. Call with the placed size
// so `layer` can be expressed from the side the old direction names.
inline void dualWrite(Layers& layers, Size placed) {
    int count = std::max(1, layerCount(placed, layers.axis));
    layers.cut = std::clamp(layers.cut, 0, count - 1);
    bool high = layers.keep == LayerKeep::High;
    layers.legacyAxis = high ? highLegacyAxis(layers.axis) : lowLegacyAxis(layers.axis);
    layers.legacyMode = layers.mode;
    layers.legacyIndex = high ? count - 1 - layers.cut : layers.cut;
}
// The old six-direction view as a cut plane. The cut is local to the placed
// box, so the conversion happens only where the placed size is known.
inline Layers fromLegacy(Layers const& old, Size placed) {
    Layers out;
    out.legacy = false;
    out.legacyAxis = old.legacyAxis;
    out.legacyMode = old.legacyMode;
    out.legacyIndex = old.legacyIndex;
    out.axis = layerAxisOf(old.legacyAxis);
    out.mode = old.legacyMode;
    int count = std::max(1, layerCount(placed, out.axis));
    int index = std::clamp(old.legacyIndex, 0, count - 1);
    bool high = legacyHighSide(old.legacyAxis);
    out.keep = high ? LayerKeep::High : LayerKeep::Low;
    out.cut = high ? count - 1 - index : index;
    dualWrite(out, placed);
    return out;
}
// The drawn view's dominant axis: largest |x|, |y| or |z|; ties keep Y, then X.
inline LayerAxis dominantAxis(double dx, double dy, double dz) {
    double ax = std::abs(dx), ay = std::abs(dy), az = std::abs(dz);
    if (ay >= ax && ay >= az) return LayerAxis::Y;
    if (ax >= az) return LayerAxis::X;
    return LayerAxis::Z;
}
// A world position's layer along the axis, clamped to the placed box.
inline int cutAlong(Size placed, Point origin, LayerAxis axis, Point world) {
    int value = axis == LayerAxis::Y ? world.y - origin.y : axis == LayerAxis::X ? world.x - origin.x : world.z - origin.z;
    return std::clamp(value, 0, std::max(0, layerCount(placed, axis) - 1));
}
inline Layers withAxis(Layers layers, Size placed, LayerAxis next) {
    layers.axis = next;
    layers.cut = std::clamp(layers.cut, 0, std::max(0, layerCount(placed, next) - 1));
    return layers;
}
inline bool layerShown(Layers const& layers, Size placed, Point offset) {
    if (layers.legacy) {
        int layer = legacyHighSide(layers.legacyAxis)
            ? layerCount(placed, layerAxisOf(layers.legacyAxis)) - 1 - layerOf(placed, layerAxisOf(layers.legacyAxis), offset)
            : layerOf(placed, layerAxisOf(layers.legacyAxis), offset);
        if (layers.legacyMode == LayerMode::All) return true;
        return layers.legacyMode == LayerMode::Only ? layer == layers.legacyIndex : layer <= layers.legacyIndex;
    }
    if (layers.mode == LayerMode::All) return true;
    int cut = std::clamp(layers.cut, 0, std::max(0, layerCount(placed, layers.axis) - 1));
    int at = layerOf(placed, layers.axis, offset);
    if (layers.mode == LayerMode::Only) return at == cut;
    return layers.keep == LayerKeep::Low ? at <= cut : at >= cut;
}
}

