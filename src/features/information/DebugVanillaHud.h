#pragma once
#include <string_view>

namespace lamium::information::debugVanilla {
// While Debug View is open, the game's own position line and paper doll are
// not drawn, so they do not sit under it; the game's options stay as they
// are. Names from the vanilla hud_screen: the position panel, its label, and
// the paper doll's custom control (drawn by HudPlayerRenderer).
inline bool positionControl(std::string_view name) {
    return name == "player_position" || name == "player_position_text";
}
void start() noexcept;
void stop();
}
