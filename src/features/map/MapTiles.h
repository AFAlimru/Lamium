#pragma once
#include "features/map/MapView.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_map>

namespace lamium::map {
// Owned per-column map data (BACKLOG L-60): the surface color and height of
// every column the scan has seen, by chunk. No game types; the scan in
// Minimap.cpp fills it, MapImage composes from it.
struct Column {
    std::uint32_t color = 0; // RGBA bytes, little-endian (r lowest); alpha 0 = unknown.
    std::int16_t height = 0;
};
inline constexpr std::uint32_t packColor(int r, int g, int b, int a = 255) {
    auto c = [](int v) { return std::uint32_t(std::clamp(v, 0, 255)); };
    return c(r) | c(g) << 8 | c(b) << 16 | c(a) << 24;
}
inline constexpr int channel(std::uint32_t color, int index) { return int((color >> (8 * index)) & 0xFF); }

struct Tile {
    std::array<Column, 256> columns{};
    bool loaded = false;  // False: the client had no chunk there when last tried.
    double scannedAt = 0; // Seconds, the caller's clock.
    int layer = 0;        // Cave view: the height the floors were found around.
    bool partial = false; // Some blocks had not arrived; scan again soon.
    bool stored = false;  // Filled from the saved map while the chunk is not loaded.
    // Colors with height shading applied, rebuilt when this chunk or its
    // north or west neighbor changes; composing reads only these.
    std::array<std::uint32_t, 256> shaded{};
    bool shadedValid = false;
};

class TileCache {
    std::unordered_map<std::uint64_t, Tile> tiles;
public:
    void clear() { tiles.clear(); }
    size_t size() const { return tiles.size(); }
    Tile const* find(ChunkKey key) const {
        auto at = tiles.find(packKey(key));
        return at == tiles.end() ? nullptr : &at->second;
    }
    Tile* find(ChunkKey key) {
        auto at = tiles.find(packKey(key));
        return at == tiles.end() ? nullptr : &at->second;
    }
    Tile& put(ChunkKey key) { return tiles[packKey(key)]; }
    // A chunk's columns changed: its shading and that of the chunks south and
    // east of it (which shade against it) must be rebuilt.
    void changed(ChunkKey key) {
        for (auto k : {key, ChunkKey{key.x + 1, key.z}, ChunkKey{key.x, key.z + 1}})
            if (auto* tile = find(k)) tile->shadedValid = false;
    }
    std::optional<Column> column(int blockX, int blockZ) const {
        auto const* tile = find(chunkOf(blockX, blockZ));
        if (!tile || !tile->loaded) return std::nullopt;
        auto const& c = tile->columns[static_cast<size_t>(columnIndex(blockX, blockZ))];
        if (!(c.color >> 24)) return std::nullopt;
        return c;
    }
    // Bound memory: forget chunks farther than `keep` chunks from the center.
    void evict(ChunkKey center, int keep) {
        std::erase_if(tiles, [&](auto const& entry) {
            int x = int(std::int32_t(entry.first >> 32)), z = int(std::int32_t(entry.first & 0xFFFFFFFFu));
            return std::abs(x - center.x) > keep || std::abs(z - center.z) > keep;
        });
    }
};

// When a chunk should be scanned again: chunks near the player change most
// (the player builds and digs there); missing chunks are retried soon, since
// the client may load them any moment.
inline double rescanAfter(int chebyshev, bool loaded) {
    if (!loaded) return 0.5;
    if (chebyshev <= 1) return 1.0;
    if (chebyshev <= 4) return 4.0;
    return 15.0;
}
// Scans walk the rings around the player from the middle out (L-206 review).
// The walk is a cursor: it remembers the cell its last call stopped on and
// resumes there, so a call looks at a bounded number of cells instead of
// re-walking every ring from the center (the old order cost up to about
// 38,000 tile lookups per frame once the recorded area reached 96 chunks),
// and the progress is the walk's own, not the tiles': the display cache
// evicts the rings beyond `keepChunks`, and eviction made those chunks look
// unseen again, so the nearest of them were rescanned over and over and the
// recording never reached the far edge while the player stood still. The
// walk is per scan path (display, recording), so the two cannot rewind each
// other's place.
struct ScanWalk {
    ChunkKey center{};
    int ring = 0, step = 0; // The cell walked within the ring; see walkCell.
};
// The cell the walk is on now: ring 0 is the center, ring r has 8r cells in
// the old order (corners first, then edges).
inline ChunkKey walkCell(ScanWalk const& walk) {
    if (walk.ring == 0) return walk.center;
    int i = walk.step / 4 - walk.ring;
    switch (walk.step % 4) {
    case 0: return {walk.center.x + i, walk.center.z - walk.ring};
    case 1: return {walk.center.x + walk.ring, walk.center.z + i};
    case 2: return {walk.center.x - i, walk.center.z + walk.ring};
    default: return {walk.center.x - walk.ring, walk.center.z - i};
    }
}
// Steps to the next cell; false when the pass is over and the walk is back
// at the center. The caller scans the cell's chunk before stepping past it
// (a scan that has to wait leaves the walk on the cell for a retry), so a
// chunk that needs scanning is never skipped over, and nearer rings come
// first.
inline bool walkStep(ScanWalk& walk, int radius) {
    if (walk.ring == 0) {
        if (radius < 1) return false;
        walk.ring = 1, walk.step = 0;
        return true;
    }
    if (++walk.step < 8 * walk.ring) return true;
    if (walk.ring >= radius) {
        walk.ring = 0, walk.step = 0;
        return false;
    }
    ++walk.ring, walk.step = 0;
    return true;
}
// Points the walk at the player. Cells are relative to the center, so
// ordinary walking keeps the walk's ring and step (the sweep shifts with the
// player); a jump farther than this (a teleport, a fast ride) or a radius
// that no longer holds the walk starts it over at the center, covering the
// new area from the middle out.
inline constexpr int walkRestartChunks = 32;
inline void walkAt(ScanWalk& walk, ChunkKey center, int radius) {
    if (std::abs(center.x - walk.center.x) > walkRestartChunks
        || std::abs(center.z - walk.center.z) > walkRestartChunks || walk.ring > radius)
        walk.ring = walk.step = 0;
    walk.center = center;
}
// Whether the walk should stop at this cell: the chunk is unseen, scanned
// around another height than `layer` (more than `tolerance` away), or its
// rescan window passed. Only cells that fail this are skipped: skipping is
// the bounded part, scanning is bounded by the caller's time budget.
inline bool scanDue(Tile const* tile, int ring, double now, int layer = 0, int tolerance = 1 << 20) {
    if (!tile || std::abs(tile->layer - layer) > tolerance) return true;
    return now - tile->scannedAt >= rescanAfter(ring, tile->loaded && !tile->partial && !tile->stored);
}

struct ScanSchedule {
    ScanWalk sweep;
    bool nearNext = true;
};
inline constexpr int nearScanRadius = 4, scanCellsPerCall = 512;
// Start the near search at the player every call, but keep the wide sweep's
// place. Alternating due chunks lets both progress even if one scan uses the
// whole frame's budget. The shared cell cap also bounds fresh-tile lookups.
// scan returns false while waiting for resources; stop checks the time budget.
template <class Scan, class Stop>
int scanBatch(TileCache const& cache, ScanSchedule& schedule, ChunkKey center, int radius, double now,
              int layer, int tolerance, Scan&& scan, Stop&& stop) {
    walkAt(schedule.sweep, center, radius);
    ScanWalk near{center};
    int const nearRadius = std::min(radius, nearScanRadius);
    bool nearDone = false, sweepDone = false;
    int examined = 0, completed = 0;
    auto next = [&](bool local) -> std::optional<ChunkKey> {
        auto& walk = local ? near : schedule.sweep;
        auto& done = local ? nearDone : sweepDone;
        int limit = local ? nearRadius : radius;
        while (!done && examined < scanCellsPerCall) {
            ++examined;
            auto key = walkCell(walk);
            if (scanDue(cache.find(key), walk.ring, now, layer, tolerance)) return key;
            if (!walkStep(walk, limit)) done = true;
        }
        return std::nullopt;
    };
    while (examined < scanCellsPerCall) {
        bool local = schedule.nearNext;
        auto key = next(local);
        if (!key) {
            local = !local;
            key = next(local);
        }
        if (!key) break;
        schedule.nearNext = !local;
        if (!scan(*key)) break;
        ++completed;
        if (local) {
            if (!walkStep(near, nearRadius)) nearDone = true;
        } else if (!walkStep(schedule.sweep, radius)) sweepDone = true;
        if (stop()) break;
    }
    return completed;
}
}
