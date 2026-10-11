#pragma once
// Where the ghosts of two placements share cells (BACKLOG L-141), the one
// earlier in the list draws them and the later one leaves them out: two
// ghosts on the same planes flickered. Pure; used by the ghost pass.
#include "features/schematic/Placement.h"
#include "features/schematic/PlacementStore.h"
#include "features/schematic/SchematicSession.h"
#include <span>
#include <string>

namespace lamium::schematic::overlap {
// Whether a placement shown in this dimension has a ghost block (not void,
// not air, within its shown layers) at a world cell.
inline bool drawsCell(session::Shown const& shown, int dimension, Point cell) {
    if (!shown.structure || !shown.placement.visible || shown.placement.dimension != dimension) return false;
    auto const& structure = *shown.structure;
    auto const& placement = shown.placement.placement;
    auto local = toLocal(structure.size, placement, cell);
    if (!local) return false;
    Size placed = placedSize(structure.size, placement.rotation);
    Point offset{cell.x - placement.origin.x, cell.y - placement.origin.y, cell.z - placement.origin.z};
    if (!layerShown(shown.placement.layers, placed, offset)) return false;
    auto at = static_cast<size_t>(structure.cell(local->x, local->y, local->z));
    if (at >= structure.blocks.size()) return false;
    auto index = structure.blocks[at];
    return index != voidCell && static_cast<size_t>(index) < structure.palette.size()
        && !structure.palette[static_cast<size_t>(index)].isAir();
}
inline bool ownedEarlier(std::span<session::Shown const> placements, size_t index, int dimension, Point cell) {
    for (size_t j = 0; j < index && j < placements.size(); ++j)
        if (drawsCell(placements[j], dimension, cell)) return true;
    return false;
}
inline bool boxesMeet(session::Shown const& a, session::Shown const& b) {
    if (!a.structure || !b.structure || a.placement.dimension != b.placement.dimension) return false;
    Size sa = placedSize(a.structure->size, a.placement.placement.rotation);
    Size sb = placedSize(b.structure->size, b.placement.placement.rotation);
    Point oa = a.placement.placement.origin, ob = b.placement.placement.origin;
    return oa.x < ob.x + sb.x && ob.x < oa.x + sa.x && oa.y < ob.y + sb.y && ob.y < oa.y + sa.y
        && oa.z < ob.z + sb.z && ob.z < oa.z + sa.z;
}
// The earlier shown placements whose boxes meet this one's, as draw keys:
// part of its built sections' key, so a change to one rebuilds the overlap.
inline std::string earlierKey(std::span<session::Shown const> placements, size_t index) {
    std::string key;
    for (size_t j = 0; j < index && j < placements.size(); ++j)
        if (placements[j].placement.visible && boxesMeet(placements[j], placements[index]))
            key += "#" + drawKey(placements[j].placement);
    return key;
}
}
