#pragma once
#include "features/map/MapView.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <unordered_map>
#include <vector>

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

// What a scan path last saw of a chunk: nothing yet, no chunk on the client
// (and nothing saved), a chunk with blocks not received yet or only saved
// colors, or the whole chunk.
enum class Seen : std::uint8_t { Never, Missing, Partial, Complete };
inline Seen seenOf(Tile const& tile) {
    if (!tile.loaded) return Seen::Missing;
    return tile.partial || tile.stored ? Seen::Partial : Seen::Complete;
}
// When a chunk should be scanned again: chunks near the player change most
// (the player builds and digs there); incomplete chunks are retried soon,
// since the client may send the rest any moment, and back off with each
// incomplete result in a row (`tries`). Some never complete - the edge of
// what the client loads, or columns whose biome tint the renderer never
// readies - and each retry of a partial chunk scans all of it. In game
// (L-210) 700 edge chunks retried every 3 s, and then 81 near ones every
// 0.5 s, held the whole recording budget.
inline double rescanAfter(int ring, Seen seen, int tries = 0) {
    if (seen == Seen::Complete) return ring <= 1 ? 1.0 : ring <= 4 ? 4.0 : 15.0;
    int doubled = 1 << std::clamp(tries, 0, 3);
    if (ring <= 4) return 0.5 * doubled;
    return 3.0 * doubled;
}
inline constexpr int nearScanRadius = 4;
struct ScanCell {
    Seen seen = Seen::Never;
    std::uint8_t tries = 0; // Incomplete results in a row.
    int layer = 0;
    double at = 0;
};
// How a due chunk is picked: Near ones (due within four chunks) and Fresh
// ones (never seen, or scanned at another cave layer) take turns, each
// nearest first, so neither the area around the player nor the rest of the
// range can hold the budget; then Retry ones, incomplete far chunks, longest
// waiting first; then Stale ones, complete chunks out to `staleRadius`,
// nearest first. `next` lowers to when a waiting chunk becomes due.
enum class Due { Not, Near, Fresh, Retry, Stale };
inline Due dueOf(ScanCell const& cell, int ring, double now, int layer, int tolerance, int staleRadius, double& next) {
    if (cell.seen == Seen::Never || std::abs(cell.layer - layer) > tolerance) return Due::Fresh;
    bool complete = cell.seen == Seen::Complete, near = ring <= nearScanRadius;
    if (complete && !near && ring > staleRadius) return Due::Not;
    double due = cell.at + rescanAfter(ring, cell.seen, cell.tries);
    if (now < due) {
        next = std::min(next, due);
        return Due::Not;
    }
    return near ? Due::Near : complete ? Due::Stale : Due::Retry;
}
// Per scan path (display surface, display caves, recording), the state of
// every chunk in range around the player, in a grid that moves with the
// player (L-210). Every call picks the nearest due chunks from scratch, so
// moving never leaves a chunk behind the way a resumable ring walk did
// (one-chunk arcs and wedges left unrecorded inside the view distance), and
// what the path has seen does not depend on the display cache, which evicts
// far tiles (L-206 review). The grid is dense, so a pass over even the
// widest recording range (97 chunks) costs array reads, not tile lookups.
class ScanGrid {
public:
    // Points the grid at `center`; chunks still in range keep their state.
    // `round`: a disc (what the client loads around the player) instead of
    // the square a map view shows.
    void follow(ChunkKey center, int radius, bool round) {
        radius = std::max(0, radius);
        if (center == center_ && radius == radius_ && round == round_) return;
        int side = 2 * radius + 1;
        std::vector<ScanCell> cells(static_cast<size_t>(side) * side);
        if (radius_ >= 0)
            for (int dz = -radius; dz <= radius; ++dz)
                for (int dx = -radius; dx <= radius; ++dx)
                    if (auto const* old = find({center.x + dx, center.z + dz}))
                        cells[static_cast<size_t>(dz + radius) * side + (dx + radius)] = *old;
        if (radius != radius_ || round != round_) {
            order_.clear();
            for (int dz = -radius; dz <= radius; ++dz)
                for (int dx = -radius; dx <= radius; ++dx)
                    if (!round || dx * dx + dz * dz <= radius * radius + radius) order_.push_back({dx, dz});
            std::stable_sort(order_.begin(), order_.end(), [](ChunkKey a, ChunkKey b) {
                return a.x * a.x + a.z * a.z < b.x * b.x + b.z * b.z;
            });
        }
        cells_ = std::move(cells);
        center_ = center;
        radius_ = radius;
        round_ = round;
        quietUntil_ = -1e300;
    }
    ScanCell const* find(ChunkKey key) const {
        auto i = index(key);
        return i ? &cells_[*i] : nullptr;
    }
    void saw(ChunkKey key, Seen seen, int layer, double now) {
        if (auto i = index(key)) {
            auto& cell = cells_[*i];
            int tries = seen != Seen::Complete && cell.seen == seen ? std::min(cell.tries + 1, 8) : 0;
            cell = {seen, static_cast<std::uint8_t>(tries), layer, now};
        }
        quietUntil_ = -1e300;
    }
    void clear() { *this = {}; }
    // Up to `limit` due chunks in the order dueOf describes. A pass that
    // finds nothing remembers when the next chunk becomes due and skips
    // until then, unless the grid moves, a chunk is seen or the layer changes.
    std::vector<ChunkKey> due(double now, int layer, int tolerance, int staleRadius, size_t limit) {
        if (layer != layer_) {
            layer_ = layer;
            quietUntil_ = -1e300;
        }
        std::vector<ChunkKey> out, near, fresh, stale;
        if (now < quietUntil_) return out;
        std::vector<std::pair<double, ChunkKey>> retry;
        double next = 1e300;
        for (auto offset : order_) {
            ChunkKey key{center_.x + offset.x, center_.z + offset.z};
            auto const& cell = *find(key);
            switch (dueOf(cell, std::max(std::abs(offset.x), std::abs(offset.z)), now, layer, tolerance, staleRadius, next)) {
            case Due::Near: near.push_back(key); break;
            case Due::Fresh:
                if (fresh.size() < limit) fresh.push_back(key);
                break;
            case Due::Retry: retry.push_back({cell.at, key}); break;
            case Due::Stale:
                if (stale.size() < limit) stale.push_back(key);
                break;
            case Due::Not: break;
            }
            // Near cells all come early in the order; the rest only fills.
            if (fresh.size() >= limit && std::max(std::abs(offset.x), std::abs(offset.z)) > nearScanRadius + 1) break;
        }
        for (size_t i = 0; out.size() < limit && (i < near.size() || i < fresh.size()); ++i) {
            if (i < near.size()) out.push_back(near[i]);
            if (i < fresh.size() && out.size() < limit) out.push_back(fresh[i]);
        }
        if (out.empty() && retry.empty() && stale.empty()) quietUntil_ = next;
        size_t room = std::min(limit - out.size(), retry.size());
        std::partial_sort(retry.begin(), retry.begin() + static_cast<std::ptrdiff_t>(room), retry.end(),
                          [](auto const& a, auto const& b) { return a.first < b.first; });
        for (size_t i = 0; i < room; ++i) out.push_back(retry[i].second);
        for (auto key : stale)
            if (out.size() < limit) out.push_back(key);
        return out;
    }
    ChunkKey center() const { return center_; }
    // Chunks in range by what was seen (Never, Missing, Partial, Complete),
    // and the farthest chunk the client had, in chunks from the center.
    struct Counts {
        std::array<int, 4> seen{};
        int reach = 0;
    };
    Counts counts() const {
        Counts out;
        for (auto offset : order_) {
            auto seen = find({center_.x + offset.x, center_.z + offset.z})->seen;
            ++out.seen[static_cast<size_t>(seen)];
            if (seen == Seen::Partial || seen == Seen::Complete)
                out.reach = std::max(out.reach, static_cast<int>(std::sqrt(offset.x * offset.x + offset.z * offset.z)));
        }
        return out;
    }

private:
    std::optional<size_t> index(ChunkKey key) const {
        int dx = key.x - center_.x, dz = key.z - center_.z;
        if (radius_ < 0 || std::abs(dx) > radius_ || std::abs(dz) > radius_) return std::nullopt;
        return static_cast<size_t>(dz + radius_) * (2 * radius_ + 1) + (dx + radius_);
    }
    ChunkKey center_{};
    int radius_ = -1, layer_ = 0;
    bool round_ = false;
    std::vector<ScanCell> cells_;
    std::vector<ChunkKey> order_; // Offsets in range, nearest first.
    double quietUntil_ = -1e300;
};
inline constexpr size_t scanChunksPerCall = 64;
// One call of a scan path: the due chunks, nearest first, until the time
// budget runs out. `scan` returns what it saw, or nothing while it waits for
// resources (that chunk stays due and comes first next call); `stop` checks
// the time budget.
template <class Scan, class Stop>
int scanBatch(ScanGrid& grid, ChunkKey center, int radius, bool round, double now, int layer, int tolerance,
              int staleRadius, Scan&& scan, Stop&& stop) {
    grid.follow(center, radius, round);
    int completed = 0;
    for (auto key : grid.due(now, layer, tolerance, staleRadius, scanChunksPerCall)) {
        std::optional<Seen> seen = scan(key);
        if (!seen) break;
        grid.saw(key, *seen, layer, now);
        ++completed;
        if (stop()) break;
    }
    return completed;
}
}
