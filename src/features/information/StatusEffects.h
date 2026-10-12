#pragma once
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

// Status Effects HUD (BACKLOG L-147, docs/demos/status-effects-hud.html).
// Pure ordering and formatting; InfoHud reads the local player's effects.
namespace lamium::information::statusEffects {
struct Effect {
    unsigned id = 0;
    int ticks = 0;      // Remaining; negative for an infinite effect
    int amplifier = 0;  // 0 is level I
    bool ambient = false; // From a beacon or conduit: renewed every few seconds
    bool harmful = false;
    std::string name;
    std::string icon; // The effect's icon name or texture path
};
inline bool infinite(Effect const& effect) { return effect.ticks < 0; }
// Under 10 s, colored only (no blinking). Beacon effects are refreshed with
// short times and would always look like they are running out.
inline constexpr int runningOutTicks = 200;
inline bool runningOut(Effect const& effect) {
    return !effect.ambient && !infinite(effect) && effect.ticks < runningOutTicks;
}
// Shortest first so the next to run out is on top; infinite ones last.
inline void order(std::vector<Effect>& effects) {
    std::stable_sort(effects.begin(), effects.end(), [](Effect const& a, Effect const& b) {
        if (infinite(a) != infinite(b)) return infinite(b);
        if (a.ticks != b.ticks) return a.ticks < b.ticks;
        return a.id < b.id;
    });
}
// M:SS, H:MM:SS from an hour, rounded up so an active effect never reads 0:00.
inline std::string timeText(int ticks) {
    if (ticks < 0) return "\xE2\x88\x9E"; // ∞
    int seconds = (ticks + 19) / 20, minutes = seconds / 60, hours = minutes / 60;
    auto two = [](int value) { return (value < 10 ? "0" : "") + std::to_string(value); };
    if (hours > 0) return std::to_string(hours) + ":" + two(minutes % 60) + ":" + two(seconds % 60);
    return std::to_string(minutes) + ":" + two(seconds % 60);
}
// Level I is not written, as in the game; II to X in Roman numerals.
inline std::string levelText(int amplifier) {
    static constexpr char const* roman[]{"", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X"};
    if (amplifier <= 0) return {};
    if (amplifier < 10) return roman[amplifier];
    return std::to_string(amplifier + 1);
}
inline std::string label(Effect const& effect) {
    auto level = levelText(effect.amplifier);
    return level.empty() ? effect.name : effect.name + " " + level;
}
// Effect icons live in textures/ui as <name>_effect; a full path is kept.
inline std::string iconPath(std::string_view icon) {
    if (icon.empty()) return {};
    std::string path(icon);
    if (path.size() > 4 && path.ends_with(".png")) path.resize(path.size() - 4);
    if (path.find('/') == std::string::npos) path = "textures/ui/" + path;
    return path;
}
}
