#pragma once
#include "features/map/MapRegion.h"
#include "features/map/WorldMapView.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

namespace lamium::map::store {
// The world map's saved regions (BACKLOG L-60 world map). Regions near the
// player stay in memory: the scan records into them and the minimap reads
// them for chunks the client has not loaded. A worker thread reads and saves
// region files and builds the world map's images; it runs while a world is
// joined and is joined on world exit and mod stop. Every call is safe from
// any thread; the game calls come from the client thread.
void start();
void stop();

// Each frame while recording: follows world changes (`world` counts joins,
// `folder` is where this world's map is saved, none for session only),
// keeps regions around the player loaded, saves changed ones, forgets far ones.
void frame(unsigned world, std::optional<std::filesystem::path> const& folder, MapLayer layer, double x, double z,
           double now);
void record(MapLayer layer, ChunkKey chunk, std::array<Column, 256> const& columns,
            ColumnMask const& produced, ColumnMask const& scanned);
bool needsRevision(MapLayer layer, ChunkKey chunk);
// The caller marks these as partial in each scan grid; reports are consumed once.
std::vector<ChunkKey> takeRevisionReports(MapLayer layer);
// A chunk from loaded regions; false when nothing is known there.
bool cached(MapLayer layer, ChunkKey chunk, std::array<Column, 256>& out);
std::optional<Column> column(MapLayer layer, int x, int z);

// Built images: pixels null when nothing is recorded there. Version changes
// when the image is rebuilt.
struct Image {
    std::shared_ptr<std::vector<std::uint32_t> const> pixels;
    unsigned version = 0;
};
std::optional<Image> image(MapLayer layer, TileKey tile, std::optional<int> focus = std::nullopt);
// The tiles the world map shows now, in build order; replaces the last list.
// `focus`: the cave layer view's selected height; none builds the broad view.
void want(MapLayer layer, std::vector<TileKey> tiles, std::optional<int> focus = std::nullopt);
// Wanted tiles not built yet (or being rebuilt).
size_t pending();

// How far the saved and recorded map of a layer reaches; none before the
// saved folder is listed or when nothing is recorded.
std::optional<ExploredIndex::Bounds> exploredBounds(MapLayer layer);

// Bytes saved for this world, once counted; none outside a world or for a
// world kept only for the session.
std::optional<std::uint64_t> usage();
// Deletes this world's saved map. False when there is none to delete.
bool clear();
// Counts clears and world changes; the minimap forgets its scanned chunks
// when it changes so they are recorded again.
unsigned epoch();
}
