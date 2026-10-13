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
void start() noexcept;
void stop();
// A sample in the editor; only owned geometry from play is remembered.
std::optional<ui::hud_editor::Box> preview(MinecraftUIRenderContext&, float width, float height,
                                         ui::HudElement const&);
}
