#pragma once
#include "ui/SettingsTable.h"

namespace lamium::ui {
struct DescriptionPanel {
    float left{}, top{}, width{}, height{}, textTop{}, textWidth{}, closeX{};
    int visible{};
    static constexpr float lineHeight = 12, pad = SettingsTable::pad;
    static DescriptionPanel fit(SettingsTable const& table) {
        DescriptionPanel p;
        p.left = table.left + pad;
        p.top = table.top + pad;
        p.width = table.width - 2 * pad;
        p.height = table.height - 2 * pad;
        p.textTop = p.top + SettingsTable::headerHeight + pad;
        p.textWidth = p.width - 2 * pad - SettingsTable::gap;
        p.closeX = p.left + p.width - pad - SettingsTable::closeWidth;
        p.visible = std::max(1, static_cast<int>((p.top + p.height - 2 * pad - lineHeight - p.textTop) / lineHeight));
        return p;
    }
    bool contains(float x, float y) const {
        return x >= left && x < left + width && y >= top && y < top + height;
    }
    bool close(float x, float y) const {
        return x >= closeX && x < closeX + SettingsTable::closeWidth && y >= top + 4 && y < top + 16;
    }
    int scroll(int first, int lines) const { return SettingsTable::clampFirst(first, lines, visible); }
};
}
