#pragma once
#include "features/map/MapImage.h"
#include "features/map/MapTiles.h"
#include "features/map/MapView.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lamium::map {
// The world map's saved unit (BACKLOG L-60 world map): 256x256 blocks (16x16
// chunks) of surface color and height. Pure; MapStore.cpp reads, writes and
// bakes them on its worker thread.
inline constexpr int regionBlocks = 256, regionSide = 16;
inline constexpr int regionColumns = regionBlocks * regionBlocks;
struct RegionKey {
    int x = 0, z = 0;
    bool operator==(RegionKey const&) const = default;
};
inline constexpr RegionKey regionOfChunk(ChunkKey chunk) { return {floorDiv(chunk.x, regionSide), floorDiv(chunk.z, regionSide)}; }
inline constexpr RegionKey regionOfBlock(int x, int z) { return {floorDiv(x, regionBlocks), floorDiv(z, regionBlocks)}; }
inline constexpr int regionIndex(int blockX, int blockZ) {
    return floorMod(blockZ, regionBlocks) * regionBlocks + floorMod(blockX, regionBlocks);
}

struct RegionData {
    std::vector<std::uint32_t> colors = std::vector<std::uint32_t>(regionColumns, 0); // Alpha 0: not recorded.
    std::vector<std::int16_t> heights = std::vector<std::int16_t>(regionColumns, 0);
    bool empty() const {
        for (auto c : colors) if (c >> 24) return false;
        return true;
    }
};

// Copies a chunk's known columns in. Unknown columns keep what was recorded
// before: a chunk seen half-loaded must not erase it. True when anything changed.
inline bool mergeChunk(RegionData& region, ChunkKey chunk, std::array<Column, 256> const& columns,
                       std::vector<bool>* recorded = nullptr) {
    bool changed = false;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            auto const& c = columns[static_cast<size_t>(z * 16 + x)];
            if (!(c.color >> 24)) continue;
            auto i = static_cast<size_t>(regionIndex(chunk.x * 16 + x, chunk.z * 16 + z));
            if (recorded) (*recorded)[i] = true;
            if (region.colors[i] == c.color && region.heights[i] == c.height) continue;
            region.colors[i] = c.color;
            region.heights[i] = c.height;
            changed = true;
        }
    return changed;
}
// Pure black is no block's color: only the renderer-tint bug recorded it
// (2026-10-07), so it counts as nothing saved and is replaced when rescanned.
inline bool savedUsable(Column const& c) { return (c.color >> 24) && c.color != packColor(0, 0, 0); }
// A chunk scanned before everything was ready keeps what the saved map knows
// for its columns not received yet (unknown) and those colored with the
// stand-in map tint (provisional), instead of showing them that way.
inline void fillFromSaved(std::array<Column, 256>& columns, std::array<bool, 256> const& provisional,
                          std::array<Column, 256> const& saved) {
    for (size_t i = 0; i < columns.size(); ++i)
        if ((!(columns[i].color >> 24) || provisional[i]) && savedUsable(saved[i])) columns[i] = saved[i];
}
// A chunk's columns from a region; false when none is known.
inline bool copyChunk(RegionData const& region, ChunkKey chunk, std::array<Column, 256>& out) {
    bool any = false;
    for (int z = 0; z < 16; ++z)
        for (int x = 0; x < 16; ++x) {
            auto i = static_cast<size_t>(regionIndex(chunk.x * 16 + x, chunk.z * 16 + z));
            out[static_cast<size_t>(z * 16 + x)] = {region.colors[i], region.heights[i]};
            any = any || (region.colors[i] >> 24);
        }
    return any;
}
// Fills columns that `recorded` does not mark from `disk`: what was saved
// before this session under what the scan found since.
inline void underlay(RegionData& region, std::vector<bool> const& recorded, RegionData const& disk) {
    for (size_t i = 0; i < static_cast<size_t>(regionColumns); ++i)
        if (!recorded[i]) {
            region.colors[i] = disk.colors[i];
            region.heights[i] = disk.heights[i];
        }
}

// File format: "LMR1", then runs of equal columns, each a little-endian
// u16 (length - 1), u32 color, i16 height, covering all 65536 columns in
// row-major order (z rows of x).
inline constexpr std::string_view regionMagic = "LMR1";
inline std::string encodeRegion(RegionData const& region) {
    std::string out(regionMagic);
    auto put = [&](std::uint64_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    };
    for (size_t i = 0; i < static_cast<size_t>(regionColumns);) {
        size_t run = 1;
        while (i + run < static_cast<size_t>(regionColumns) && run < 65536 && region.colors[i + run] == region.colors[i]
               && region.heights[i + run] == region.heights[i])
            ++run;
        put(run - 1, 2);
        put(region.colors[i], 4);
        put(static_cast<std::uint16_t>(region.heights[i]), 2);
        i += run;
    }
    return out;
}
// Nullopt for anything that is not a complete region.
inline std::optional<RegionData> decodeRegion(std::string_view bytes) {
    if (bytes.size() < regionMagic.size() || bytes.substr(0, regionMagic.size()) != regionMagic) return std::nullopt;
    if ((bytes.size() - regionMagic.size()) % 8) return std::nullopt;
    RegionData region;
    auto get = [&](size_t at, int count) {
        std::uint64_t value = 0;
        for (int i = 0; i < count; ++i) value |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
        return value;
    };
    size_t filled = 0;
    for (size_t at = regionMagic.size(); at < bytes.size(); at += 8) {
        size_t run = get(at, 2) + 1;
        if (filled + run > static_cast<size_t>(regionColumns)) return std::nullopt;
        auto color = static_cast<std::uint32_t>(get(at + 2, 4));
        auto height = static_cast<std::int16_t>(static_cast<std::uint16_t>(get(at + 6, 2)));
        std::fill_n(region.colors.begin() + static_cast<std::ptrdiff_t>(filled), run, color);
        std::fill_n(region.heights.begin() + static_cast<std::ptrdiff_t>(filled), run, height);
        filled += run;
    }
    if (filled != static_cast<size_t>(regionColumns)) return std::nullopt;
    return region;
}

// A region's RGBA pixels (one per column) with height shading as on the
// minimap. Columns on the north and west edge shade against level ground.
inline std::vector<std::uint32_t> shadeRegion(RegionData const& region) {
    std::vector<std::uint32_t> out(static_cast<size_t>(regionColumns), 0);
    for (int z = 0; z < regionBlocks; ++z)
        for (int x = 0; x < regionBlocks; ++x) {
            auto i = static_cast<size_t>(z * regionBlocks + x);
            auto color = region.colors[i];
            if (!(color >> 24)) continue;
            int h = region.heights[i];
            auto neighbor = [&](size_t j) { return (region.colors[j] >> 24) ? int(region.heights[j]) : h; };
            int north = z ? neighbor(i - regionBlocks) : h, west = x ? neighbor(i - 1) : h;
            out[i] = shade(color, shadeFactor(h, north, west));
        }
    return out;
}
inline void clearQuadrant(std::vector<std::uint32_t>& parent, int quadrant) {
    int half = regionBlocks / 2, ox = (quadrant & 1) * half, oz = (quadrant >> 1) * half;
    for (int z = 0; z < half; ++z)
        std::fill_n(parent.begin() + (oz + z) * regionBlocks + ox, half, 0u);
}
// One quarter of a coarser image from a finer one: each 2x2 block of the
// child averages its known pixels into one. `quadrant` 0-3: x then z.
inline void downsampleInto(std::vector<std::uint32_t>& parent, std::vector<std::uint32_t> const& child, int quadrant) {
    int half = regionBlocks / 2, ox = (quadrant & 1) * half, oz = (quadrant >> 1) * half;
    for (int z = 0; z < half; ++z)
        for (int x = 0; x < half; ++x) {
            int r = 0, g = 0, b = 0, n = 0;
            for (int dz = 0; dz < 2; ++dz)
                for (int dx = 0; dx < 2; ++dx) {
                    auto c = child[static_cast<size_t>((2 * z + dz) * regionBlocks + 2 * x + dx)];
                    if (!(c >> 24)) continue;
                    r += channel(c, 0); g += channel(c, 1); b += channel(c, 2); ++n;
                }
            parent[static_cast<size_t>((oz + z) * regionBlocks + ox + x)] = n ? packColor(r / n, g / n, b / n) : 0;
        }
}

// Saved folders per map layer: one per dimension, and in the Nether one per
// 16-block layer, since its cave view depends on the height.
struct MapLayer {
    int dimension = 0;
    int band = 0; // Nether only: floor(y / 16).
    bool operator==(MapLayer const&) const = default;
};
inline constexpr int bandHeight = 16;
inline MapLayer mapLayer(int dimension, int caveLayer) {
    return dimension == 1 ? MapLayer{1, floorDiv(caveLayer, bandHeight)} : MapLayer{dimension, 0};
}
inline std::string layerFolder(MapLayer layer) {
    if (layer.dimension == 0) return "overworld";
    if (layer.dimension == 1) return "nether/y" + std::to_string(layer.band * bandHeight);
    if (layer.dimension == 2) return "end";
    return "dimension" + std::to_string(layer.dimension);
}
inline std::string regionFileName(RegionKey key) {
    return "r." + std::to_string(key.x) + "." + std::to_string(key.z) + ".lmr";
}
namespace detail {
inline std::optional<int> parseInt(std::string_view text) {
    if (text.empty() || text.size() > 11) return std::nullopt;
    bool negative = text.front() == '-';
    if (negative) text.remove_prefix(1);
    if (text.empty()) return std::nullopt;
    long long value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    value = negative ? -value : value;
    if (value < -(1LL << 31) || value >= (1LL << 31)) return std::nullopt;
    return static_cast<int>(value);
}
}
// The inverses, for listing what is saved; anything else is not a region.
inline std::optional<RegionKey> regionOfFileName(std::string_view name) {
    if (!name.starts_with("r.") || !name.ends_with(".lmr")) return std::nullopt;
    name = name.substr(2, name.size() - 6);
    auto dot = name.find('.');
    if (dot == std::string_view::npos) return std::nullopt;
    auto x = detail::parseInt(name.substr(0, dot)), z = detail::parseInt(name.substr(dot + 1));
    if (!x || !z) return std::nullopt;
    return RegionKey{*x, *z};
}
// `folder` relative to the world's map folder, with '/' separators.
inline std::optional<MapLayer> layerOfFolder(std::string_view folder) {
    if (folder == "overworld") return MapLayer{0, 0};
    if (folder == "end") return MapLayer{2, 0};
    if (folder.starts_with("nether/y")) {
        auto y = detail::parseInt(folder.substr(8));
        if (!y || floorMod(*y, bandHeight)) return std::nullopt;
        return MapLayer{1, floorDiv(*y, bandHeight)};
    }
    if (folder.starts_with("dimension")) {
        auto d = detail::parseInt(folder.substr(9));
        if (!d || *d < 3) return std::nullopt;
        return MapLayer{*d, 0};
    }
    return std::nullopt;
}
// The next wanted image the worker builds. Moving while the map is open
// keeps the images around the player stale - every recorded chunk makes its
// region's images, at every scale, due for a rebuild - and rebuilding those
// first left images never built, shown black, waiting until the player
// stopped; building never-built ones first left the area by the player out
// of date at coarse scales instead (L-215, in game 2026-10-18). So the two
// take turns: `missingTurn` says whose turn it is, and the other kind goes
// when that one has nothing due.
template <class Id, class Missing, class Buildable>
typename std::vector<Id>::const_iterator nextBuild(std::vector<Id> const& wanted, Missing&& missing, Buildable&& buildable,
                                                   bool missingTurn = true) {
    auto never = std::find_if(wanted.begin(), wanted.end(), missing);
    auto stale = std::find_if(wanted.begin(), wanted.end(), [&](Id const& id) { return !missing(id) && buildable(id); });
    if (missingTurn) return never != wanted.end() ? never : stale;
    return stale != wanted.end() ? stale : never;
}
// What a coarser image does with one quarter while it is rebuilt. A child
// image that could not be made right now - a region still loading, a file
// not saved yet - used to clear the quarter, and the cleared image counted
// as fresh, so recorded land stayed black at that scale once the player had
// moved on (L-215). Now a quarter with recorded data keeps its last pixels
// and the image is rebuilt again later; only a quarter with nothing recorded
// is cleared.
enum class Quarter { Keep, Draw, Clear, KeepAndRetry };
// Whether a coarser image keeps a quarter as it is: the child image it was
// made from (`madeFrom`, a version, 0 none) is still the current, fresh one.
// A dirty mark alone was not enough: a child rebuilt on its own first lost
// it, and the old quarter stayed (L-215).
inline constexpr bool quarterUnchanged(bool hadPixels, bool recorded, unsigned madeFrom, unsigned current, bool currentFresh) {
    return hadPixels && recorded && madeFrom != 0 && currentFresh && current == madeFrom;
}
inline constexpr Quarter quarterAction(bool unchanged, bool built, bool recorded, bool hadPixels) {
    if (unchanged) return Quarter::Keep;
    if (built) return Quarter::Draw;
    if (recorded && hadPixels) return Quarter::KeepAndRetry;
    return Quarter::Clear;
}
}
