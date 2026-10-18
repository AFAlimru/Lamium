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
// since the client may send the rest any moment, and back off a little with
// each incomplete result in a row (`tries`). A retry first probes a few
// columns (probeFirst) instead of scanning the whole chunk, so retries stay
// cheap enough to be frequent: with whole scans they had to back off to
// 24 s, and terrain that had arrived showed up as specks, the chunks inside
// the range last (L-210, in game 2026-10-18).
inline double rescanAfter(int ring, Seen seen, int tries = 0) {
    if (seen == Seen::Complete) return ring <= 1 ? 1.0 : ring <= 4 ? 4.0 : 15.0;
    int doubled = 1 << std::clamp(tries, 0, 3);
    if (ring <= 4) return 0.5 * doubled;
    return std::min(4.0, 1.0 * doubled);
}
inline constexpr int nearScanRadius = 4;
struct ScanCell {
    Seen seen = Seen::Never;
    std::uint8_t tries = 0; // Incomplete results in a row.
    int layer = 0;
    double at = 0;
};
// Whether a retry may probe first: incomplete chunks, except every fourth
// retry in a row, which scans the whole chunk in case the probed columns
// missed what arrived.
inline bool probeFirst(ScanCell const& cell) {
    return (cell.seen == Seen::Missing || cell.seen == Seen::Partial) && cell.tries % 4 != 3;
}
// The count after one more incomplete result in a row. Past the backoff's
// last step (rescanAfter clamps at 3) it cycles 4..7 instead of stopping, so
// every fourth retry stays a whole scan (L-225: a cap at 8 made every later
// retry a probe, and a chunk whose probed columns never arrived stayed a hole).
inline constexpr int nextTries(int tries) { return tries < 7 ? tries + 1 : 4; }
// What a due chunk is: Near (due within four chunks), Fresh (never seen, or
// scanned at another cave layer), Retry (incomplete beyond four chunks) or
// Stale (complete, out to `staleRadius`). `next` lowers to when a waiting
// chunk becomes due.
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
// player (L-210), so moving never leaves a chunk behind the way a resumable
// ring walk did (one-chunk arcs and wedges unrecorded inside the view
// distance), and what the path has seen does not depend on the display
// cache, which evicts far tiles (L-206 review).
//
// Each call looks at a bounded number of cells, so the widest recording
// range (97 chunks, 30,000 cells) does not eat the frame budget before any
// chunk is scanned: the near cells, never-seen cells from the first one not
// yet seen (nearest first), and a window of the rest that moves on each call
// for retries and stale rescans. The kinds take turns - never-seen chunks
// every other one - and the turn carries over between calls, so even a
// one-chunk budget gives each a share (in game, near chunks that kept coming
// due held the whole budget).
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
            nearCount_ = 0;
            while (nearCount_ < order_.size() && ringOf(order_[nearCount_]) <= nearScanRadius + 1) ++nearCount_;
            window_ = 0;
        }
        cells_ = std::move(cells);
        center_ = center;
        radius_ = radius;
        round_ = round;
        freshFrom_ = 0;
    }
    ScanCell const* find(ChunkKey key) const {
        auto i = index(key);
        return i ? &cells_[*i] : nullptr;
    }
    // A chunk seen complete makes its incomplete neighbors due at once: the
    // client sends terrain in patches, and each chunk's own retry time
    // differed, so a patch that had arrived showed up chunk by chunk as
    // scattered specks (maintainer, 2026-10-18).
    void saw(ChunkKey key, Seen seen, int layer, double now) {
        auto i = index(key);
        if (!i) return;
        auto& cell = cells_[*i];
        bool arrived = seen == Seen::Complete && cell.seen != Seen::Complete;
        int tries = seen != Seen::Complete && cell.seen == seen ? nextTries(cell.tries) : 0;
        cell = {seen, static_cast<std::uint8_t>(tries), layer, now};
        if (!arrived) return;
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                ChunkKey next{key.x + dx, key.z + dz};
                auto j = index(next);
                if (!j || (dx == 0 && dz == 0)) continue;
                auto& neighbor = cells_[*j];
                if (neighbor.seen != Seen::Missing && neighbor.seen != Seen::Partial) continue;
                neighbor.at = -1e9;
                neighbor.tries = 0;
                if (hints_.size() < maxHints) hints_.push_back(next);
            }
    }
    void clear() { *this = {}; }
    // Up to `limit` due chunks, the kinds (near, never seen, the window's
    // retries and stale rescans) taking turns.
    std::vector<ChunkKey> due(double now, int layer, int tolerance, int staleRadius, size_t limit) {
        if (layer != layer_) {
            layer_ = layer;
            freshFrom_ = 0;
        }
        std::array<std::vector<ChunkKey>, 3> lists; // Near, fresh, rest.
        double next = 1e300;
        auto key = [&](size_t i) { return ChunkKey{center_.x + order_[i].x, center_.z + order_[i].z}; };
        auto kind = [&](size_t i) { return dueOf(cells_[*index(key(i))], ringOf(order_[i]), now, layer, tolerance, staleRadius, next); };
        for (size_t i = 0; i < nearCount_; ++i)
            if (kind(i) == Due::Near && lists[0].size() < limit) lists[0].push_back(key(i));
        // Cells before freshFrom_ have all been seen; it only moves back when
        // the grid moves or the layer changes.
        while (freshFrom_ < order_.size() && kind(freshFrom_) != Due::Fresh) ++freshFrom_;
        for (size_t i = freshFrom_, looked = 0; i < order_.size() && lists[1].size() < limit && looked < scanWindowCells; ++i, ++looked)
            if (kind(i) == Due::Fresh) lists[1].push_back(key(i));
        // Neighbors of chunks that just arrived go first among the rest.
        size_t used = 0;
        for (; used < hints_.size() && lists[2].size() < limit; ++used) {
            auto j = index(hints_[used]);
            if (!j) continue;
            ChunkKey offset{hints_[used].x - center_.x, hints_[used].z - center_.z};
            auto k = dueOf(cells_[*j], ringOf(offset), now, layer, tolerance, staleRadius, next);
            if (k == Due::Retry || k == Due::Stale) lists[2].push_back(hints_[used]);
        }
        hints_.erase(hints_.begin(), hints_.begin() + static_cast<std::ptrdiff_t>(used));
        for (size_t looked = 0; looked < std::min(scanWindowCells, order_.size()) && lists[2].size() < limit; ++looked) {
            window_ = (window_ + 1) % order_.size();
            auto k = kind(window_);
            if ((k == Due::Retry || k == Due::Stale)
                && std::find(lists[2].begin(), lists[2].end(), key(window_)) == lists[2].end())
                lists[2].push_back(key(window_));
        }
        // Never-seen chunks get every other turn, near and the rest share the
        // others; a kind with nothing due passes its turn on.
        static constexpr std::array<size_t, 4> pattern{1, 0, 1, 2};
        std::vector<ChunkKey> out;
        std::array<size_t, 3> taken{};
        for (size_t step = turn_++, idle = 0; out.size() < limit && idle < pattern.size(); ++step) {
            size_t kind = pattern[step % pattern.size()];
            if (taken[kind] < lists[kind].size()) {
                out.push_back(lists[kind][taken[kind]++]);
                idle = 0;
            } else ++idle;
        }
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
    // Cells a call looks at for never-seen chunks and for its moving window.
    static constexpr size_t scanWindowCells = 2048;

private:
    static int ringOf(ChunkKey offset) { return std::max(std::abs(offset.x), std::abs(offset.z)); }
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
    size_t nearCount_ = 0;        // The leading offsets that can be near.
    size_t freshFrom_ = 0;        // No never-seen cell before this.
    size_t window_ = 0;           // Where the retry window stopped.
    size_t turn_ = 0;             // Where the turn pattern starts this call.
    std::vector<ChunkKey> hints_; // Neighbors of chunks that just arrived.
    static constexpr size_t maxHints = 512;
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
