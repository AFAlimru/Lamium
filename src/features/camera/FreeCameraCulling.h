#pragma once
#include <cstdint>

namespace lamium::camera {
// Observed on 1.26.51.01: normal terrain visibility is 3, spectator is 5.
// Keep shadow/capture and unknown renderer requests intact.
inline constexpr std::uint8_t terrainCuller(std::uint8_t requested, bool freeCamera) noexcept {
    return freeCamera && requested == 3 ? 5 : requested;
}
void startTerrainCulling() noexcept;
void stopTerrainCulling() noexcept;
}
