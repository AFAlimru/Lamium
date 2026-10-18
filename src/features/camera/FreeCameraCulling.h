#pragma once
#include <cstdint>

namespace lamium::camera {
// Observed on 1.26.51.01: normal terrain visibility is 3, spectator is 5.
// Keep shadow/capture and unknown renderer requests intact.
inline constexpr std::uint8_t terrainCuller(std::uint8_t requested, bool freeCamera) noexcept {
    return freeCamera && requested == 3 ? 5 : requested;
}
// The spectator culler draws terrain while the camera is inside it, where
// the normal culler clips at chunk borders; in open space the normal culler
// renders the view normally, and the spectator type only costs frame rate
// while it stays selected (L-207). `insideTerrain`: the camera eye's block
// is not air (an unloaded column or an out-of-range height reads as no).
inline constexpr bool spectatorTerrainWanted(bool freeCamera, bool insideTerrain) noexcept {
    return freeCamera && insideTerrain;
}
// The spectator culler stays selected for a while after the camera leaves
// terrain (L-214): every switch rebuilds the view, and flying through a cave
// or skimming the ground switched it each time it went in and out. Ending
// the FreeCamera session drops it at once.
inline constexpr double spectatorHoldSeconds = 3.0;
struct SpectatorHold {
    double lastInside = -1e300;
    constexpr bool wanted(bool freeCamera, bool insideTerrain, double now) noexcept {
        if (!freeCamera) {
            lastInside = -1e300;
            return false;
        }
        if (insideTerrain) lastInside = now;
        return now - lastInside < spectatorHoldSeconds;
    }
};
// Native updates that did not keep the requested type, counted in a row.
// Switching between the normal and spectator culler as the camera crosses
// terrain (L-207) left the previous type for a moment, and one such update
// turned the feature off for the session (L-213, in game 2026-10-18). Only a
// run of them says the field read is not the culler type.
struct CullerCheck {
    static constexpr int limit = 30;
    int misses = 0;
    constexpr bool failed(bool retained) noexcept {
        misses = retained ? 0 : misses + 1;
        return misses >= limit;
    }
};
void startTerrainCulling() noexcept;
void stopTerrainCulling() noexcept;
}
