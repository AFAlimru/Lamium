#pragma once
#include "features/schematic/PlacementStore.h"
#include "features/schematic/Structure.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Schematic files and the current world's placements (BACKLOG L-93). Files
// live in mods/Lamium/schematics; placements follow the world like
// waypoints. Thread-safe: the screen changes placements, the world render
// reads snapshots.
namespace lamium::schematic::session {
void start();
void stop();
std::filesystem::path folder();

struct FileEntry {
    std::string relative; // forward slashes, inside the folder
    std::uintmax_t bytes = 0;
};
// Scans the folder (bounded); creates it when missing.
std::vector<FileEntry> files();
// Opens the folder in the system's file browser; false when that failed.
bool openFolder();
// Files larger than this ask before they are loaded: parsing and drawing
// them can stall the game (about a quarter million blocks; to be measured).
inline constexpr std::uintmax_t largeFileBytes = 2ull * 1024 * 1024;
// Loaded once per file and modification time; null with `error` set on failure.
std::shared_ptr<Structure const> structure(std::string const& relative, std::string* error = nullptr);
// Only what is already loaded; never reads the disk.
std::shared_ptr<Structure const> loaded(std::string const& relative);
// Cached failures only; these queries never load a file.
std::string loadError(std::string const& relative);
struct Unreadable {
    std::uint64_t id = 0;
    std::string name, file, error;
};
std::vector<Unreadable> unreadable();

PlacementSet current();
// Changes when the joined world changes (or the session stops), so view
// state such as the move target can be dropped without polling placements.
std::uint64_t generation();
// Applies a change to a copy, saves it and publishes it. False when the
// change declined (returned false) or saving failed.
bool change(std::function<bool(PlacementSet&)> const& mutation);
// Adds a placement of `relative` with its lower north-west corner at `feet`
// and selects it. False when the file cannot be loaded or saving failed.
bool place(std::string const& relative, Point feet, int dimension, std::string* error = nullptr);

// What the world render draws: placements with their loaded structures.
struct Shown {
    SavedPlacement placement;
    std::shared_ptr<Structure const> structure;
};
struct Snapshot {
    std::uint64_t revision = 0; // changes with every placement change and world join
    int selected = -1;
    std::vector<Shown> placements;
};
Snapshot snapshot();
}
