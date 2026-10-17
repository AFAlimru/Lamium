#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace lamium::map {
// Map geometry (BACKLOG L-60): block/chunk math, zoom steps and the
// map-pixel <-> world transforms. Pure; Minimap.cpp draws.
inline constexpr int floorDiv(int value, int divisor) {
    int q = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? q - 1 : q;
}
inline constexpr int floorMod(int value, int divisor) { return value - floorDiv(value, divisor) * divisor; }
struct ChunkKey {
    int x = 0, z = 0;
    bool operator==(ChunkKey const&) const = default;
};
inline constexpr ChunkKey chunkOf(int blockX, int blockZ) { return {floorDiv(blockX, 16), floorDiv(blockZ, 16)}; }
inline constexpr std::uint64_t packKey(ChunkKey key) {
    return (std::uint64_t(std::uint32_t(key.x)) << 32) | std::uint32_t(key.z);
}
// Index of a block inside its chunk's 16x16 column grid, row-major by z.
inline constexpr int columnIndex(int blockX, int blockZ) { return floorMod(blockZ, 16) * 16 + floorMod(blockX, 16); }
inline int blockFloor(double value) {
    if (!std::isfinite(value)) return 0;
    return static_cast<int>(std::floor(std::clamp(value, -3.0e7, 3.0e7)));
}

// Blocks shown across the map, from close to far.
inline constexpr std::array<int, 11> zoomSteps{16, 24, 32, 48, 64, 96, 128, 192, 256, 384, 512};
inline constexpr int defaultZoomIndex = 6;
inline constexpr int clampZoomIndex(int index) { return std::clamp(index, 0, static_cast<int>(zoomSteps.size()) - 1); }
// Zoom in (+1) shows fewer blocks.
inline constexpr int stepZoom(int index, int direction) { return clampZoomIndex(clampZoomIndex(index) - direction); }
inline constexpr int blocksAcross(int index) { return zoomSteps[static_cast<size_t>(clampZoomIndex(index))]; }
// The step nearest a saved width in blocks.
inline constexpr int zoomIndexFor(int blocks) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(zoomSteps.size()); ++i) {
        auto distance = [&](int k) { int d = zoomSteps[static_cast<size_t>(k)] - blocks; return d < 0 ? -d : d; };
        if (distance(i) < distance(best)) best = i;
    }
    return best;
}
// Bedrock yaw: 0 faces south (+z), 90 west, 180 north, -90 east.
struct Heading { double x, z; };
inline Heading heading(float yawDegrees) {
    double yaw = std::isfinite(yawDegrees) ? yawDegrees * 3.14159265358979323846 / 180.0 : 0;
    return {-std::sin(yaw), std::cos(yaw)};
}

// Map offsets (u right, v down, in blocks from the map center) to world
// offsets. North-up: right is east (+x), down is south (+z). Rotating: up is
// where the player faces.
struct ViewTransform {
    double rightX = 1, rightZ = 0, downX = 0, downZ = 1;
    static ViewTransform northUp() { return {}; }
    static ViewTransform headingUp(float yawDegrees) {
        auto f = heading(yawDegrees);
        // Facing north (0,-1) must give the north-up transform.
        return {-f.z, f.x, -f.x, -f.z};
    }
    struct Offset { double x, z; };
    Offset toWorld(double u, double v) const { return {u * rightX + v * downX, u * rightZ + v * downZ}; }
    // The inverse of an orthonormal basis is its transpose.
    Offset toMap(double dx, double dz) const { return {dx * rightX + dz * rightZ, dx * downX + dz * downZ}; }
};

// The map center moved onto the texture's pixel grid, along the map's own
// axes. Terrain and dots then step together by whole pixels; with a center
// between pixels each was rounded separately and dots wobbled against the
// ground while the player moved.
inline ViewTransform::Offset snapCenter(ViewTransform const& view, double x, double z, double blocksPerPixel) {
    if (!(blocksPerPixel > 0) || !std::isfinite(x) || !std::isfinite(z)) return {x, z};
    auto m = view.toMap(x, z);
    auto snap = [&](double v) { return std::floor(v / blocksPerPixel) * blocksPerPixel; };
    return view.toWorld(snap(m.x), snap(m.z));
}
// A world point on a square map of `pixels` across: where it lands, and
// whether it is inside after an edge margin.
struct MapPoint { double x, y; bool inside; };
inline MapPoint worldToPixel(ViewTransform const& view, double centerX, double centerZ, double worldX, double worldZ,
                             double blocks, int pixels, double margin = 0) {
    double scale = pixels / std::max(1.0, blocks);
    auto m = view.toMap(worldX - centerX, worldZ - centerZ);
    double x = pixels / 2.0 + m.x * scale, y = pixels / 2.0 + m.z * scale;
    bool inside = x >= margin && y >= margin && x <= pixels - margin && y <= pixels - margin;
    return {x, y, inside};
}

// Chunks that can appear on the map: half the width, plus the corner reach
// when the map turns, plus one chunk for blocks straddling the edge.
inline int chunkRadius(int blocks, bool rotating) {
    double half = std::max(1, blocks) / 2.0 * (rotating ? 1.4143 : 1.0);
    return static_cast<int>(std::ceil(half / 16.0)) + 1;
}

// Blocks across the range the world map records around the player (BACKLOG
// L-206): follows the view distance, in blocks, keeping the old fixed range
// as its floor and stopping at the game's own view-distance maximum (96
// chunks when the game reports none). Pure; Minimap.cpp reads the options.
inline constexpr int recordingBlocksFloor = 384;
inline int recordingBlocks(int viewDistanceChunks, int maximumChunks) {
    int ceiling = std::clamp(maximumChunks > 0 ? maximumChunks : 96, 1, 96);
    int chunks = std::clamp(viewDistanceChunks, 0, ceiling);
    return std::max(recordingBlocksFloor, chunks * 32);
}
}
