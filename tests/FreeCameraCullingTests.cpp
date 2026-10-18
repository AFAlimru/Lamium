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
    using lamium::camera::spectatorTerrainWanted;
    check(spectatorTerrainWanted(true, true), "the spectator culler is wanted while the camera is inside terrain");
    check(!spectatorTerrainWanted(true, false) && !spectatorTerrainWanted(false, true),
        "open space and other cameras stay on the normal culler");
    check(terrainCuller(3, spectatorTerrainWanted(true, true)) == 5
              && terrainCuller(3, spectatorTerrainWanted(true, false)) == 3,
        "the native request follows the camera being inside terrain");
    lamium::camera::SpectatorHold hold;
    bool entered = hold.wanted(true, true, 10);
    bool held = hold.wanted(true, false, 11) && hold.wanted(true, false, 12.9);
    bool released = !hold.wanted(true, false, 13.1);
    check(entered && held && released, "the spectator culler stays for a few seconds after leaving terrain");
    hold.wanted(true, true, 20);
    bool reentered = hold.wanted(true, false, 22) && hold.wanted(true, true, 22.5) && hold.wanted(true, false, 25);
    check(reentered, "going back in within the hold keeps it without switching");
    check(!hold.wanted(false, true, 26) && !hold.wanted(true, false, 26.1),
          "ending FreeCamera drops the spectator culler at once and clears the hold");
    lamium::camera::CullerCheck culler;
    bool transient = false;
    for (int i = 0; i < 5; ++i) transient = transient || culler.failed(false);
    transient = transient || culler.failed(true);
    for (int i = 0; i < lamium::camera::CullerCheck::limit - 1; ++i) transient = transient || culler.failed(false);
    check(!transient, "a few updates that keep the previous type do not fail the check");
    check(culler.failed(false), "a run of updates that never keep the type fails it");
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
