#pragma once
#include <cmath>

namespace lamium::interaction {
// Update only the outgoing raw vector, after the packet agrees with the
// guarded live calculation. Physical device input remains independent.
template <class Vector>
bool alignGuardedPacketRaw(Vector& raw, Vector const& move, Vector const& analog,
                           Vector const& liveMove, Vector const& liveAnalog, bool directions, bool mayLeave) {
    auto finite = [](Vector const& v) { return std::isfinite(v.x) && std::isfinite(v.z); };
    auto same = [](Vector const& a, Vector const& b) {
        return std::abs(a.x - b.x) <= 1e-5 && std::abs(a.z - b.z) <= 1e-5;
    };
    if (directions || mayLeave || !finite(raw) || !finite(move) || !finite(analog)
        || !finite(liveMove) || !finite(liveAnalog) || !same(move, liveMove)
        || !same(analog, liveAnalog) || same(raw, analog)) return false;
    raw = analog;
    return true;
}
}
