#pragma once
#include "ui/HudEditorLayout.h"
#include <optional>
class MinecraftUIRenderContext;

namespace lamium::information::chatPosition {
// Translation only: retain the game's measured size and all of its styling.
inline std::optional<ui::hud_editor::Box> placed(float width, float height, float chatW, float chatH,
                                               ui::HudElement const& element) {
    for (float v : {width, height, chatW, chatH}) if (!std::isfinite(v)) return std::nullopt;
    if (width <= 0 || height <= 0 || chatW <= 0 || chatH <= 0 || chatW > width || chatH > height)
        return std::nullopt;
    auto at = ui::placeElement(width, height, chatW, chatH, element);
    return ui::hud_editor::Box{at.x, at.y, chatW, chatH};
}
inline std::optional<ui::hud_editor::Point> translated(float width, float height, ui::hud_editor::Box panel,
                                                     ui::hud_editor::Point position, ui::HudElement const& element) {
    for (float v : {panel.x, panel.y, position.x, position.y}) if (!std::isfinite(v)) return std::nullopt;
    auto box = placed(width, height, panel.w, panel.h, element);
    if (!box) return std::nullopt;
    ui::hud_editor::Point point{position.x + box->x - panel.x, position.y + box->y - panel.y};
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) return std::nullopt;
    return point;
}
void start() noexcept;
void stop();
// A sample in the editor; only owned geometry from play is remembered.
std::optional<ui::hud_editor::Box> preview(MinecraftUIRenderContext&, float width, float height,
                                         ui::HudElement const&);
}
