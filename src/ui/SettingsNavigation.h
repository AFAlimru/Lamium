#pragma once
#include "ui/SettingsRows.h"

namespace lamium::ui {
struct SettingsNavigation {
    int current = 0;
    int remembered = 0;

    void reopenNormal() { current = remembered; }
    void select(int index, bool temporary = false) {
        current = index;
        if (!temporary) remembered = index;
    }
};
// Navigation items: All, each section, then the tools pinned to the
// sidebar's bottom (Hotkeys, Shapes, Waypoints, Schematics, the world map,
// the HUD layout, the calculator).
namespace nav {
inline constexpr int count = static_cast<int>(sections.size()) + 8;
inline constexpr int hotkeys = count - 7;
inline constexpr int shapes = count - 6;
inline constexpr int waypoints = count - 5;
inline constexpr int schematics = count - 4;
// The world map is never the current item: choosing it opens the map over
// the panel, and closing the map returns to where the settings were.
inline constexpr int worldMap = count - 3;
// The HUD layout editor replaces the whole panel.
inline constexpr int hud = count - 2;
// Like the world map: opens the calculator panel, Esc returns (L-171).
inline constexpr int calculator = count - 1;
// Views made of one world's data; outside a world they do not open (L-175).
inline constexpr bool needsWorld(int index) {
    return index == shapes || index == waypoints || index == schematics || index == worldMap || index == hud;
}
}
}
