#pragma once
#include <cstdint>

namespace lamium::camera {
// Observed on 1.26.51.01: normal terrain visibility is 3, spectator is 5.
// Keep shadow/capture and unknown renderer requests intact.
inline constexpr std::uint8_t terrainCuller(std::uint8_t requested, bool freeCamera) noexcept {
    return freeCamera && requested == 3 ? 5 : requested;
}
// The spectator culler draws terrain the camera is inside, but it culls less
// than the normal type and halves the frame rate while it stays selected
// (L-207). Only a camera below its column's surface benefits: above ground
// the normal culler already shows everything the free camera can see.
// `eyeY` is the camera eye's height, `surfaceY` the heightmap position of
// the top blocking block in its column.
inline constexpr bool freeCameraUnderground(double eyeY, int surfaceY) noexcept {
    return eyeY < static_cast<double>(surfaceY) + 1.0;
}
void startTerrainCulling() noexcept;
void stopTerrainCulling() noexcept;
}
