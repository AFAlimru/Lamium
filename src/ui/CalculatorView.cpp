#include "ui/CalculatorView.h"
#include "app/Desktop.h"
#include "ui/Calculator.h"
#include "ui/ListViewWidgets.h"
#include "ui/Localization.h"
#include "ui/SearchQuery.h"
#include "ui/Widgets.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/math/Vec3.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace lamium::ui::calculator_view {
namespace {
// Variant A (maintainer 2026-10-12): centered near the top; result, one
// restatement line, three history lines, hints.
struct Layout {
    static constexpr float width = 216, pad = 6, fieldHeight = 13, rowHeight = 11, copyWidth = 40;
    static constexpr int historyRows = 3;
    float left = 0, top = 0;
    static Layout at(glm::vec2 size) { return {std::max(0.f, (size.x - width) / 2), std::min(40.f, std::max(0.f, size.y / 8))}; }
    static constexpr float height() { return 152; }
    float inner() const { return width - 2 * pad; }
    float titleY() const { return top + 5; }
    float fieldY() const { return top + 19; }
    float resultY() const { return top + 37; }
    float restateY() const { return top + 55; }
    float historyLabelY() const { return top + 69; }
    float historyY(int row) const { return top + 79 + row * rowHeight; }
    float hintY() const { return top + 118; }
    float copyX() const { return left + width - pad - copyWidth; }
    bool inCopy(float x, float y) const { return x >= copyX() && x <= copyX() + copyWidth && y >= resultY() && y <= resultY() + rowHeight; }
    int historyAt(float x, float y) const {
        if (x < left + pad || x > left + width - pad) return -1;
        for (int row = 0; row < historyRows; ++row)
            if (y >= historyY(row) && y < historyY(row) + rowHeight) return row;
        return -1;
    }
};
SearchQuery line;
std::vector<calculator::Line> history; // the session's lines, newest first
int recalled = -1;                     // the history line shown in the field (Up/Down), -1 none
std::string draft;                     // the line typed before recalling
calculator::Variables variables;
calculator::Result result;

void recalculate() {
    variables.ans = history.empty() ? std::nullopt : std::optional(history.front().value);
    result = calculator::evaluate(line.value(), variables);
}
void show(std::string const& text) {
    line.set(text);
    recalculate();
}
std::string errorText() {
    switch (result.error) {
    case calculator::Error::Incomplete: return translated("calc.incomplete");
    case calculator::Error::UnknownName: return translated("calc.unknown", result.name);
    case calculator::Error::Undefined: return translated("calc.undefined");
    default: return {};
    }
}
}

void begin() {
    line.clear();
    recalled = -1;
    draft.clear();
    recalculate();
}
TextEdit& field() { return line; }
void type(std::string const& text) {
    if (line.type(text)) edited();
}
void edited() {
    recalled = -1;
    recalculate();
}
Result key(int key) {
    switch (key) {
    case 0x1b: return Result::Close;
    case 0x0d:
        if (result.value) {
            calculator::remember(history, {line.value(), *result.value});
            recalled = -1;
            line.selectAll(); // the next number typed starts a new line; "ans" keeps this one
            recalculate();
        }
        return Result::None;
    case 0x26: // Up: older
        if (recalled + 1 < static_cast<int>(history.size())) {
            if (recalled < 0) draft = line.value();
            ++recalled;
            show(history[static_cast<size_t>(recalled)].text);
        }
        return Result::None;
    case 0x28: // Down: newer, then back to the line being typed
        if (recalled >= 0) {
            --recalled;
            show(recalled >= 0 ? history[static_cast<size_t>(recalled)].text : draft);
        }
        return Result::None;
    default: return Result::None;
    }
}
void copyResult() {
    if (result.value) copyText(calculator::format(*result.value));
}
void click(float x, float y, glm::vec2 size) {
    auto l = Layout::at(size);
    if (l.inCopy(x, y)) { copyResult(); return; }
    if (int row = l.historyAt(x, y); row >= 0 && row < static_cast<int>(history.size())) {
        recalled = -1;
        show(history[static_cast<size_t>(row)].text);
    }
}
glm::vec2 fieldPosition(glm::vec2 size) {
    auto l = Layout::at(size);
    return {l.left + Layout::pad + 3, l.fieldY()};
}
void render(MinecraftUIRenderContext& context, glm::vec2 size, glm::vec2 pointer) {
    // The player's block position for x, y and z.
    if (auto* player = context.mClient.getLocalPlayer()) {
        auto feet = player->getFeetPos();
        if (std::isfinite(feet.x) && std::isfinite(feet.y) && std::isfinite(feet.z)) {
            variables.x = std::floor(feet.x);
            variables.y = std::floor(feet.y);
            variables.z = std::floor(feet.z);
        }
    }
    recalculate();
    auto l = Layout::at(size);
    using L = Layout;
    float x = l.left + L::pad, right = l.left + L::width - L::pad;
    panel(context, l.left, l.top, L::width, L::height(), .86f);
    frame(context, l.left, l.top, L::width, L::height(), palette::white, .14f);
    label(context, x, l.titleY(), l.inner() / 2, translated("calc.title"));
    label(context, x + l.inner() / 2, l.titleY(), l.inner() / 2, translated("calc.close"), palette::faint, Align::Right);
    fill(context, l.left, l.titleY() + 11, L::width, 1, palette::white, .14f);
    fill(context, x, l.fieldY(), l.inner(), L::fieldHeight, Rgb{0, 0, 0}, .45f);
    frame(context, x, l.fieldY(), l.inner(), L::fieldHeight, palette::accent);
    drawEditField(context, x + 3, l.fieldY(), L::fieldHeight, l.inner() - 6, line);
    if (result.value) {
        labelScaled(context, x, l.resultY() - 1, l.inner() - L::copyWidth - 4, "= " + calculator::format(*result.value), 1.5f);
        bool over = l.inCopy(pointer.x, pointer.y);
        drawSmallButton(context, l.copyX(), l.resultY(), L::copyWidth, L::rowHeight, translated("calc.copy"), over);
        if (calculator::restatable(*result.value)) {
            auto count = static_cast<std::uint64_t>(std::llround(std::fabs(*result.value)));
            label(context, x, l.restateY(), l.inner(), (*result.value < 0 ? "-" : "") + amountText(count), palette::dim);
        }
    } else if (result.error != calculator::Error::Empty) {
        label(context, x, l.resultY() + 1, l.inner(), errorText(), palette::warning);
    }
    fill(context, l.left, l.historyLabelY() - 3, L::width, 1, palette::white, .14f);
    label(context, x, l.historyLabelY(), l.inner(), translated("calc.history"), palette::faint);
    int hovered = l.historyAt(pointer.x, pointer.y);
    for (int row = 0; row < L::historyRows && row < static_cast<int>(history.size()); ++row) {
        auto const& entry = history[static_cast<size_t>(row)];
        float y = l.historyY(row);
        if (row == hovered) fill(context, x - 2, y, l.inner() + 4, L::rowHeight, palette::white, .07f);
        auto value = calculator::format(entry.value);
        float valueWidth = std::min(textWidth(context, value), l.inner() / 2);
        label(context, x, y + 1, l.inner() - valueWidth - 6, entry.text, row == recalled ? palette::text : palette::dim);
        label(context, right - valueWidth, y + 1, valueWidth, value);
    }
    fill(context, l.left, l.hintY() - 3, L::width, 1, palette::white, .14f);
    labelScaled(context, x, l.hintY(), l.inner(), translated("calc.keys"), .75f, palette::faint);
    labelScaled(context, x, l.hintY() + 9, l.inner(), translated("calc.units"), .75f, palette::faint);
    labelScaled(context, x, l.hintY() + 18, l.inner(), translated("calc.functions"), .75f, palette::faint);
}
}
