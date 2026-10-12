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
// Empty for an infinite effect: the caller writes a translated word (the
// game font has no infinity sign in Japanese, checked 2026-10-12).
inline std::string timeText(int ticks) {
    if (ticks < 0) return {};
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
// The vanilla pack's effect icons, textures/ui/<name>_effect, by effect id
// (MobEffectIds). The effect's own icon name drew nothing (2026-10-12).
// Instant health and damage and saturation have no icon.
inline std::string iconPath(unsigned id) {
    static constexpr std::string_view names[]{
        "", "speed", "slowness", "haste", "mining_fatigue", "strength", "", "", "jump_boost", "nausea",
        "regeneration", "resistance", "fire_resistance", "water_breathing", "invisibility", "blindness",
        "night_vision", "hunger", "weakness", "poison", "wither", "health_boost", "absorption", "",
        "levitation", "poison", "conduit_power", "slow_falling", "bad_omen", "village_hero", "darkness",
        "trial_omen", "wind_charged", "weaving", "oozing", "infested", "raid_omen", "breath_of_the_nautilus"};
    if (id >= std::size(names) || names[id].empty()) return {};
    return "textures/ui/" + std::string(names[id]) + "_effect";
}
}
