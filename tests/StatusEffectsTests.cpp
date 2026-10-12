#include "features/information/StatusEffects.h"
#include "settings/SettingsStore.h"
#include "ui/HudElement.h"
void check(bool, char const*);
void statusEffectsTests() {
    using namespace lamium::information::statusEffects;
    Effect speed{1, 3300, 1, false, false, "Speed"}, poison{19, 80, 0, false, true, "Poison"},
        vision{16, -1, 0, false, false, "Night Vision"}, beacon{3, 150, 1, true, false, "Haste"},
        regen{10, 160, 0, false, false, "Regeneration"};
    std::vector<Effect> effects{vision, speed, poison, beacon, regen};
    order(effects);
    std::vector<unsigned> ids;
    for (auto const& effect : effects) ids.push_back(effect.id);
    check(ids == std::vector<unsigned>{19, 3, 10, 1, 16}, "effects run shortest first with infinite ones last");
    check(timeText(3300) == "2:45" && timeText(80) == "0:04" && timeText(1) == "0:01" && timeText(0) == "0:00",
          "times read M:SS and round up while active");
    check(timeText(20 * 3725) == "1:02:05", "an hour or more reads H:MM:SS");
    check(timeText(-1).empty(), "an infinite effect has no time text (the HUD writes a word)");
    check(runningOut(poison) && runningOut(Effect{1, 199}) && !runningOut(Effect{1, 200}),
          "under 10 seconds is running out");
    check(!runningOut(beacon) && !runningOut(vision), "beacon and infinite effects never look like they run out");
    check(levelText(0).empty() && levelText(-1).empty() && levelText(1) == "2" && levelText(4) == "5"
              && levelText(10) == "11",
          "levels show from 2 in Arabic numerals; level 1 is blank");
    check(iconPath(1) == "textures/ui/speed_effect" && iconPath(25) == "textures/ui/poison_effect"
              && iconPath(37) == "textures/ui/breath_of_the_nautilus_effect" && iconPath(6).empty() && iconPath(0).empty()
              && iconPath(38).empty(),
          "effect ids resolve to the vanilla icons; instant effects and unknown ids have none");

    using namespace lamium;
    Settings defaults;
    check(!defaults.information.effectsHud && defaults.information.effectsNames && defaults.information.effectsWarning
              && defaults.information.effectsHideVanilla
              && defaults.hud.statusEffects.anchor == ui::Anchor::MiddleRight,
          "the effects HUD starts off at the right middle, with names, warning color and vanilla icons hidden");
    auto loaded = decodeSettings(R"({"information":{"effectsHud":true,"effectsNames":false,"effectsWarning":false,
        "effectsHideVanilla":false},"hud":{"statusEffects":{"anchor":0,"background":2}}})");
    check(loaded.information.effectsHud && !loaded.information.effectsNames && !loaded.information.effectsWarning
              && !loaded.information.effectsHideVanilla && loaded.hud.statusEffects.anchor == ui::Anchor::TopLeft
              && loaded.hud.statusEffects.background == ui::ElementBackground::Line,
          "effects HUD options and its per-line background load");
}
