#pragma once
// What players see of the version gates (L-137, decided 2026-10-11): on a
// game executable Lamium was not verified on, features with a gated
// capability are marked paused, fully or partly, in their toggle toasts and
// settings rows, and a toast on joining a world names the ones in use.
// Keep in step with the capability table in docs/GAME-UPDATES.md. Pure.
#include "settings/Settings.h"
#include <array>
#include <string_view>
#include <vector>

namespace lamium::paused {
enum class Level { No, Partly, Fully };
// Feature ids as in ui::features.
inline constexpr std::array<std::string_view, 8> gatedFeatures{
    "nightVision", "edgeGuard", "connectedTextures", "freecamera", "hideEffects", "schematic", "repeatTrades", "gameLanguage"};
inline Level onUnverified(std::string_view feature) {
    if (feature == "nightVision" || feature == "edgeGuard" || feature == "connectedTextures" || feature == "repeatTrades" || feature == "gameLanguage") return Level::Fully;
    if (feature == "freecamera" || feature == "hideEffects" || feature == "schematic") return Level::Partly;
    return Level::No;
}
// Gated features switched on in these settings, for the join toast. FreeCamera
// has no saved switch; its toggle toast says it instead. Hide effects counts
// only with a gated selection on (fog, weather, nausea), not particles alone.
inline std::vector<std::string_view> inUse(Settings const& s) {
    std::vector<std::string_view> out;
    if (s.lighting.nightVision) out.push_back("nightVision");
    if (s.interaction.edgeGuard) out.push_back("edgeGuard");
    if (s.visuals.connectedTextures) out.push_back("connectedTextures");
    if (s.inventory.repeatTrades) out.push_back("repeatTrades");
    auto const& v = s.visuals;
    if (v.hideEffects && (v.hideWater || v.hideLava || v.hidePowderSnow || v.hideDistanceFog || v.hideWeather || v.hideNausea
                          || v.hidePortal))
        out.push_back("hideEffects");
    if (s.schematic.enabled) out.push_back("schematic");
    return out;
}
}
