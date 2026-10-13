#include "ui/LanguageView.h"
#include "ui/GameLanguage.h"
#include "ui/ListViewWidgets.h"
#include "ui/Localization.h"
#include "ui/ScreenParts.h"
#include "ui/SettingsTableView.h"
#include "ui/Widgets.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"

namespace lamium::ui::language_view {
namespace {
bool open = false, dragging = false;
LanguageSelection selection;
ShapesLayout last;
std::shared_ptr<game_language::Request> request;
std::string_view notice;
bool busy() { return request && request->result.load() == game_language::Result::Pending; }
void choose() {
    if (busy()) return;
    if (auto code = selection.request(game_language::currentCode(), game_language::savedCode())) {
        request = game_language::apply(*code);
        notice = {};
    }
}
void back() { reset(); table_view::rebuild(true); }
void reveal() {
    selection.first = SettingsTable::reveal(selection.first, selection.selected, last.listVisible);
    selection.first = SettingsTable::clampFirst(selection.first, last.listCount, last.listVisible);
}
void poll() {
    if (!request) return;
    using R = game_language::Result;
    auto result = request->result.load();
    if (result == R::Pending) return;
    if (result == R::Applied) notice = {};
    else if (result == R::Saving) notice = "language.saving";
    else if (result == R::Cancelled) notice = "language.cancelled";
    else if (result == R::Unavailable) notice = "language.unavailable";
    else notice = "language.failed";
    request.reset();
}
}
bool begin() {
    reset();
    selection = game_language::choices();
    if (selection.choices.empty()) { screen::setMessage(translated("language.unavailable")); return false; }
    table_view::endSearch();
    screen::finishEditing();
    open = true;
    return true;
}
bool opened() { return open; }
void reset() {
    if (request) request->cancelled.store(true);
    request.reset();
    selection = {}; last = {}; notice = {};
    open = false; dragging = false;
}
void click(float x, float y) {
    if (last.width <= 0) return;
    if (x >= last.closeX && x < last.closeX + ShapesLayout::closeWidth
        && y >= last.top && y < last.rowsTop) { back(); return; }
    if (last.onScrollbar(x, y)) { selection.first = last.firstAt(y); dragging = true; return; }
    if (x < last.listLeft || x >= last.listLeft + last.listWidth || y < last.rowsTop) return;
    int offset = static_cast<int>((y - last.rowsTop) / ShapesLayout::rowHeight);
    if (offset >= last.listVisible || offset + last.listFirst >= last.listCount || busy()) return;
    selection.select(offset + last.listFirst);
    choose();
}
void key(int key) {
    if (key == 0x1b) { back(); return; }
    if (busy()) return;
    switch (key) {
    case 0x26: selection.move(-1, last.listVisible); break;
    case 0x28: selection.move(1, last.listVisible); break;
    case 0x21: selection.move(-std::max(1, last.listVisible - 1), last.listVisible); break;
    case 0x22: selection.move(std::max(1, last.listVisible - 1), last.listVisible); break;
    case 0x24: selection.select(0); reveal(); break;
    case 0x23: selection.select(static_cast<int>(selection.choices.size()) - 1); reveal(); break;
    case 0x0d: case 0x20: choose(); break;
    }
}
void wheel(int amount) {
    selection.first = SettingsTable::clampFirst(selection.first + amount, static_cast<int>(selection.choices.size()), last.listVisible);
}
void release() { dragging = false; }
void render(MinecraftUIRenderContext& context, glm::vec2 size, glm::vec2 pointer) {
    poll();
    last = languageLayout(size.x, size.y, static_cast<int>(selection.choices.size()), selection.first);
    if (last.width <= 0) return;
    if (dragging) selection.first = last.firstAt(pointer.y);
    selection.first = last.listFirst = SettingsTable::clampFirst(selection.first, last.listCount, last.listVisible);
    auto current = game_language::currentCode();
    panel(context, last.left, last.top, last.width, last.height);
    label(context, last.left + ShapesLayout::pad, last.top + 5, last.closeX - last.left - 2 * ShapesLayout::pad,
        translated("feature.gameLanguage"));
    drawSmallButton(context, last.closeX, last.top + 4, ShapesLayout::closeWidth, capHeight,
        translated("cancelShort"), pointer.x >= last.closeX && pointer.y >= last.top && pointer.y < last.rowsTop);
    float codeWidth = SettingsTable::fit(size.x, size.y, 0, 0).keyWidth;
    for (int i = last.listFirst; i < last.listFirst + last.listVisible && i < last.listCount; ++i) {
        auto const& choice = selection.choices[i];
        float y = last.listRowY(i), x = last.listLeft + ShapesLayout::pad;
        bool hovered = pointer.x >= last.listLeft && pointer.x < last.listLeft + last.listWidth
            && pointer.y >= y && pointer.y < y + ShapesLayout::rowHeight;
        rowBackground(context, last.listLeft + 1, y, last.listWidth - 2, ShapesLayout::rowHeight, selection.selected == i, hovered);
        label(context, x, y + 3, last.listWidth - codeWidth - 3 * ShapesLayout::pad, choice.name,
            choice.code == current ? palette::accent : palette::text);
        label(context, last.listLeft + last.listWidth - codeWidth - 2 * ShapesLayout::pad, y + 3, codeWidth,
            choice.code, choice.code == current ? palette::accent : palette::faint, Align::Right);
    }
    drawListScrollbar(context, last);
    auto text = busy() ? translated("language.applying") : !notice.empty() ? translated(notice)
        : translated("language.current", game_language::currentName());
    paragraph(context, last.left + ShapesLayout::pad, last.footerTop + 3, last.width - 2 * ShapesLayout::pad,
        text, last.height < 230 ? 1 : 2, notice.empty() ? palette::dim : palette::warning);
    if (last.height >= 230) label(context, last.left + ShapesLayout::pad, last.footerTop + 29,
        last.width - 2 * ShapesLayout::pad, translated("language.hint"), palette::faint);
    context.flushText(0, std::nullopt);
}
}
