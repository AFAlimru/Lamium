#pragma once
#include <cstdint>

// The schematic move target (L-231): what the six direction keys and the
// radial menu's Move category act on. None means no valid target exists.
namespace lamium::schematic {
enum class MoveTarget : std::uint8_t { Placement, Corner1, Corner2, Area, None };

// Which targets exist right now: a selected placement in this dimension, and
// corners or the whole area set in this dimension.
struct TargetValidity {
    bool placement = false;
    bool corner1 = false;
    bool corner2 = false;
    bool area = false;
};

inline bool valid(MoveTarget target, TargetValidity const& v) {
    switch (target) {
    case MoveTarget::Placement: return v.placement;
    case MoveTarget::Corner1: return v.corner1;
    case MoveTarget::Corner2: return v.corner2;
    case MoveTarget::Area: return v.area;
    // No target is the state only while no candidate exists; once one does,
    // a move resolves it instead of keeping "none" forever.
    case MoveTarget::None: return !v.placement && !v.corner1 && !v.corner2 && !v.area;
    }
    return false;
}
// The first valid target in cycle order, or none.
inline MoveTarget fallback(TargetValidity const& v) {
    if (v.placement) return MoveTarget::Placement;
    if (v.corner1) return MoveTarget::Corner1;
    if (v.corner2) return MoveTarget::Corner2;
    if (v.area) return MoveTarget::Area;
    return MoveTarget::None;
}
// The next valid target after `current` in cycle order (placement, corner 1,
// corner 2, area), or none. Starting from none gives the first valid one.
inline MoveTarget nextValid(MoveTarget current, TargetValidity const& v) {
    constexpr MoveTarget order[]{MoveTarget::Placement, MoveTarget::Corner1, MoveTarget::Corner2, MoveTarget::Area};
    int start = 0;
    for (int i = 0; i < 4; ++i)
        if (order[i] == current) { start = i + 1; break; }
    for (int i = 0; i < 4; ++i) {
        MoveTarget candidate = order[(start + i) % 4];
        if (valid(candidate, v)) return candidate;
    }
    return MoveTarget::None;
}
// The previous valid target, for a wheel turn in the other direction.
inline MoveTarget previousValid(MoveTarget current, TargetValidity const& v) {
    constexpr MoveTarget order[]{MoveTarget::Placement, MoveTarget::Corner1, MoveTarget::Corner2, MoveTarget::Area};
    int start = 3;
    for (int i = 0; i < 4; ++i)
        if (order[i] == current) { start = i - 1; break; }
    for (int i = 0; i < 4; ++i) {
        MoveTarget candidate = order[((start - i) % 4 + 4) % 4];
        if (valid(candidate, v)) return candidate;
    }
    return MoveTarget::None;
}
}
