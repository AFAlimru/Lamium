#pragma once
// Checking placements against the world (BACKLOG L-93): the selected
// placement in bounded passes (published as a Verification), the Placed
// list's progress for the others, and whether the schematic's entities
// stand at their spots. Run by the ghost pass on the render thread; it owns
// the scans and the published results.
#include "features/schematic/ResolvedPlacement.h"
#include "features/schematic/SchematicSession.h"
#include "features/schematic/Verification.h"
#include <optional>
#include <vector>
class BlockSource;
class LocalPlayer;
class Vec3;
namespace lamium::schematic::ghosts {
// Which of each placement's entities stand at their spots (fills
// Resolved::entityPlaced), from the entities the client knows around them.
void checkEntities(BlockSource& region, LocalPlayer& player, session::Snapshot const& snapshot, int dimension,
                   std::vector<Resolved>& resolved);
void stepScan(BlockSource& region, session::Snapshot const& snapshot, int dimension, Vec3 const& camera,
              std::vector<Resolved> const& resolved);
void stepProgress(BlockSource& region, session::Snapshot const& snapshot, int dimension, std::vector<Resolved> const& resolved);
// The checked placement's cell at `world` (mismatchAt in GhostRenderer.h).
std::optional<Mismatch> checkedCell(BlockSource& region, Point world, std::vector<Resolved> const& resolved);
// A shulker box's opening (block entity data, L-179): the file's turned with
// the placement, and the world's at `world`; -1 when not a shulker box or
// unreadable. The Check list and the in-world marks share them.
int expectedShulkerFace(Structure const& structure, std::int32_t cell, Placement const& placement);
int actualShulkerFace(BlockSource& region, Point world);
// Drops the running pass and publishes an empty result.
void resetVerification();
}
