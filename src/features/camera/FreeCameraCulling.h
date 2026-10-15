#pragma once
#include <cstdint>

namespace lamium::camera {
inline constexpr bool terrainLoaderSupported(int major, int minor, int patch) noexcept {
    return major == 26 && minor == 51 && (patch == 5 || patch == 6 || patch == 7);
}
// Observed on 1.26.51.01: normal terrain visibility is 3, spectator is 5.
// Keep shadow/capture and unknown renderer requests intact.
inline constexpr std::uint8_t terrainCuller(std::uint8_t requested, bool freeCamera) noexcept {
    return freeCamera && requested == 3 ? 5 : requested;
}
void startTerrainCulling() noexcept;
void stopTerrainCulling() noexcept;
}
