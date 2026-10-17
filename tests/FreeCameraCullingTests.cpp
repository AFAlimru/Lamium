#include "features/camera/FreeCameraCulling.h"
void check(bool, char const*);

void freeCameraCullingTests() {
    using lamium::camera::terrainCuller;
    for (unsigned type = 0; type < 256; ++type) {
        auto requested = static_cast<std::uint8_t>(type);
        check(terrainCuller(requested, false) == requested, "other cameras and inactive sessions keep every native culler");
        if (type != 3)
            check(terrainCuller(requested, true) == requested, "capture, shadow and unknown cullers are not replaced");
    }
    check(terrainCuller(3, true) == 5, "FreeCamera uses the observed spectator terrain visibility");
    using lamium::camera::freeCameraUnderground;
    check(freeCameraUnderground(70.0, 70) && freeCameraUnderground(40.5, 70),
        "an eye inside the top block or below it is underground");
    check(!freeCameraUnderground(71.0, 70) && !freeCameraUnderground(100.5, 63),
        "an eye at the surface or in the air above stays above ground");
    check(terrainCuller(3, freeCameraUnderground(60.0, 70)) == 5,
        "the spectator culler is only asked for under the surface");
    check(terrainCuller(3, freeCameraUnderground(90.0, 70)) == 3,
        "above ground the free camera keeps the normal culler");
    unsigned rebuilds = 0;
    std::uint8_t last = 255;
    auto frame = [&](bool free) {
        auto selected = terrainCuller(3, free);
        if (selected != last) { last = selected; ++rebuilds; }
    };
    frame(false);
    for (int i = 0; i < 100; ++i) frame(true);
    check(last == 5 && rebuilds == 2, "repeated FreeCamera frames reuse one culler instead of rebuilding it each frame");
    frame(false);
    check(last == 3 && rebuilds == 3, "ending the session restores the normal culler on the next native update");
    for (int i = 0; i < 100; ++i) frame(false);
    check(rebuilds == 3, "restored vanilla frames do not keep rebuilding the culler");
}
