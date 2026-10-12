#include "ui/SettingsTable.h"
#include "app/Versions.h"
#include <limits>
void check(bool, char const*);
void settingsTableTests() {
    using lamium::ui::SettingsTable;
    using Zone = SettingsTable::Zone;
    using Column = SettingsTable::Column;
    constexpr int navItems = 12;

    // Typical 16:9 GUI sizes keep the sidebar and full columns.
    for (auto [w, h] : {std::pair{640.f, 360.f}, std::pair{480.f, 270.f}, std::pair{960.f, 540.f}}) {
        auto t = SettingsTable::fit(w, h, 60, 0);
        check(t.usable() && !t.compact && !t.shortFooter, "16:9 keeps sidebar and full footer");
        check(t.left >= 0 && t.left + t.width <= w && t.top >= 0 && t.top + t.height <= h, "panel inside screen");
        check(t.nameX < t.stateX && t.stateX + SettingsTable::stateWidth < t.keyX
            && t.keyX + t.keyWidth <= t.rowsRight(), "columns are ordered inside the table");
        check(t.rowY(t.first + t.visible - 1) + SettingsTable::rowHeight <= t.footerTop, "rows end above the footer");
        check(t.footerButtonX(2) + SettingsTable::footerButtonWidth <= t.left + t.width, "footer buttons fit");
    }
    auto wide = SettingsTable::fit(640, 360, 60, 0);
    check(wide.keyWidth == 88 && SettingsTable::fit(500, 360, 60, 0).keyWidth == 70 && !SettingsTable::fit(500, 360, 60, 0).compact,
        "a narrower table narrows the key column first");
    auto narrow = SettingsTable::fit(400, 360, 60, 0);
    check(narrow.compact && narrow.tableLeft == narrow.left && narrow.rowsTop > wide.rowsTop - 1,
        "narrow windows move categories into tabs");
    auto shortWindow = SettingsTable::fit(640, 220, 60, 0);
    check(shortWindow.shortFooter && shortWindow.usable(), "short windows keep a one-line footer");
    check(!SettingsTable::fit(200, 300, 60, 0).usable() && !SettingsTable::fit(640, 120, 60, 0).usable(),
        "too-small windows are reported unusable");
    // L-101: the version after the title is its own zone and clears the search
    // field even in the narrowest usable panel ("Lamium" ~30 units, "0.1.6" ~22).
    for (float w : {300.f, 400.f, 640.f}) {
        auto t = SettingsTable::fit(w, 360, 60, 0);
        t.placeVersion(31, 22);
        float y = t.top + 6;
        check(t.versionWidth == 22 && t.versionX + t.versionWidth < t.searchX, "the version ends before the search field");
        check(t.hit(t.versionX + 1, y, navItems).zone == Zone::Version
            && t.hit(t.versionX + t.versionWidth + 1, y, navItems).zone != Zone::Version
            && t.hit(t.versionX + 1, t.rowsTop + 1, navItems).zone != Zone::Version,
            "only the version text in the header is the version zone");
    }
    check(SettingsTable::fit(640, 360, 60, 0).hit(40, 30, navItems).zone != Zone::Version,
        "without a placed version there is no version zone");
    auto tiny = SettingsTable::fit(240, 360, 60, 0);
    tiny.placeVersion(31, 40);
    check(tiny.versionWidth == 0 && tiny.hit(tiny.versionX + 1, tiny.top + 6, navItems).zone != Zone::Version,
        "a version that would reach the search field is hidden");
    // The versionLine format is what bug reports paste.
    check(lamium::versionLine("0.1.6", "1.26.51", "26.51.5") == "Lamium 0.1.6 · Minecraft 1.26.51 · LeviLamina 26.51.5",
        "the version line names Lamium, Minecraft and LeviLamina in English");
    for (auto const& t : {wide, narrow}) {
        for (bool hotkeys : {false, true}) {
            float x = t.headActionX(hotkeys), y = t.theadTop + 2;
            check(x > t.nameX + 40 && x + SettingsTable::headActionWidth <= (hotkeys ? t.keyX : t.stateX - 6),
                "the reset button sits between the name heading and the next column heading");
            check(t.headAction(x + 1, y, hotkeys) && !t.headAction(x + 1, t.rowsTop + 1, hotkeys)
                && t.hit(x + 1, y, 9).zone == SettingsTable::Zone::None,
                "the reset button is hit only on the heading line, which no other zone claims");
        }
        float kx = t.keyFindX(), ky = t.theadTop + 2;
        check(t.keyFindShown() && kx > t.nameX + 40 && kx + SettingsTable::keyFindWidth < t.headActionX(true)
              && t.keyFind(kx + 1, ky) && !t.headAction(kx + 1, ky, true) && !t.keyFind(kx + 1, t.rowsTop + 1),
              "Find by key sits left of the Hotkeys reset button on the heading line");
    }

    // Scrolling is owned by the caller and clamped.
    check(SettingsTable::fit(640, 360, 60, 1000).first == 60 - wide.visible, "scroll clamps to the last page");
    // Fourteen sidebar items (L-93 added Schematics twice) in a short window.
    auto squeezed = SettingsTable::fit(640, 250, 60, 0, 14), tabbed = SettingsTable::fit(640, 190, 60, 0, 14);
    check(!squeezed.compact && squeezed.navStep >= SettingsTable::minNavStep
          && squeezed.navItemY(14 - SettingsTable::pinnedItems - 1) + squeezed.navStep + 4 <= squeezed.pinnedItemY(0),
          "a short window narrows sidebar items so the top ones and the pinned ones do not meet");
    check(tabbed.compact && SettingsTable::fit(640, 360, 60, 0, 14).navStep == SettingsTable::navItemHeight,
          "a window too short for the sidebar uses tabs; a tall one keeps full items");
    check(SettingsTable::fit(640, 360, 5, 3).first == 0, "short lists do not scroll");
    check(SettingsTable::reveal(10, 5, 8) == 5 && SettingsTable::reveal(10, 20, 8) == 13 && SettingsTable::reveal(10, 12, 8) == 10,
        "reveal scrolls minimally");

    // Hit testing: header controls, sidebar, rows by column, footer.
    auto t = SettingsTable::fit(640, 360, 60, 4);
    auto y = t.rowY(6) + 3;
    check(t.hit(t.nameX + 2, y, navItems).zone == Zone::Row && t.hit(t.nameX + 2, y, navItems).index == 6, "row hit follows scroll");
    check(t.hit(t.nameX + 2, y, navItems).column == Column::Name, "name column");
    check(t.hit(t.stateX + 2, y, navItems).column == Column::State, "state column");
    check(t.hit(t.keyX + 2, y, navItems).column == Column::Key, "key column");
    check(t.hit(t.closeX + 2, t.top + 8, navItems).zone == Zone::Close, "close button");
    check(t.hit(t.searchX + 2, t.top + 8, navItems).zone == Zone::Search, "search field");
    auto nav = t.hit(t.left + 10, t.navItemY(2) + 3, navItems);
    check(nav.zone == Zone::Nav && nav.index == 2, "sidebar item");
    auto pinned = t.hit(t.left + 10, t.pinnedItemY(SettingsTable::pinnedItems - 1) + 3, navItems);
    check(pinned.zone == Zone::Nav && pinned.index == navItems - 1, "HUD layout item is pinned at the sidebar bottom");
    pinned = t.hit(t.left + 10, t.pinnedItemY(SettingsTable::pinnedItems - 2) + 3, navItems);
    check(pinned.zone == Zone::Nav && pinned.index == navItems - 2, "world map item is pinned above it");
    pinned = t.hit(t.left + 10, t.pinnedItemY(3) + 3, navItems);
    check(pinned.zone == Zone::Nav && pinned.index == navItems - 3, "schematics item is pinned above the world map");
    pinned = t.hit(t.left + 10, t.pinnedItemY(2) + 3, navItems);
    check(pinned.zone == Zone::Nav && pinned.index == navItems - 4, "waypoints item is pinned above schematics");
    pinned = t.hit(t.left + 10, t.pinnedItemY(1) + 3, navItems);
    check(pinned.zone == Zone::Nav && pinned.index == navItems - 5, "shapes item is pinned above waypoints");
    pinned = t.hit(t.left + 10, t.pinnedItemY(0) + 3, navItems);
    check(pinned.zone == Zone::Nav && pinned.index == navItems - 6, "hotkeys item is pinned above shapes");
    check(t.hit(t.left + 10, t.footerTop + 5, navItems).zone == Zone::Footer, "footer spans the panel");
    check(t.footerButton(t.footerButtonX(1) + 3, t.footerButtonY() + 3) == 1, "footer button index");
    check(t.hit(t.left - 1, y, navItems).zone == Zone::None, "outside the panel");
    check(t.hit(std::numeric_limits<float>::quiet_NaN(), y, navItems).zone == Zone::None, "invalid pointer");
    auto tabs = SettingsTable::fit(400, 360, 60, 0);
    float tabWidth = (tabs.width - 4) / navItems;
    auto tab = tabs.hit(tabs.left + 2 + tabWidth * 3 + 2, tabs.navTop + 5, navItems, tabWidth);
    check(tab.zone == Zone::Nav && tab.index == 3, "tab hit");

    // Stepper parts are right-aligned in the value columns.
    check(t.stepperPart(t.stepperX() + 2) == -1 && t.stepperPart(t.stepperX() + t.stepperWidth() - 2) == 1
        && t.stepperPart(t.stepperX() + t.stepperWidth() / 2) == 0 && t.stepperPart(t.nameX) == 2, "stepper parts");
    check(t.stepperX() >= t.stateX, "stepper stays in the value columns");
    check(t.stepperX(true) + t.stepperWidth(true) <= t.keyX - SettingsTable::gap && t.stepperX(true) > t.nameX
        && t.stepperPart(t.keyX + 2, true) == 2 && t.stepperPart(t.stepperX(true) + 2, true) == -1,
        "a keyed stepper ends before the key column, which stays the key cell");
    {
        auto slider = SettingsTable::fit(640, 360, 20, 0);
        check(slider.sliderFraction(slider.sliderX() + 3) == 0 && slider.sliderFraction(slider.sliderX() + slider.sliderWidth() - 3) == 1,
              "the slider track maps its ends to 0 and 1");
        check(slider.sliderFraction(slider.sliderValueX() + 1) == -1, "the value text is not part of the track");
        check(SettingsTable::sliderValue(.5f, 2, 64, 1) == 33 && SettingsTable::sliderValue(.52f, 1, 10, .5f) == 5.5f,
              "slider values snap to the step");
        check(SettingsTable::sliderValue(2, 2, 64, 1) == 64 && SettingsTable::sliderPosition(6, 2, 64) > 0,
              "slider values and positions clamp to the range");
    }
}
