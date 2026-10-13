#include "features/information/ChatPosition.h"
#include "settings/SettingsStore.h"
#include "input/ToggleAction.h"
#include "app/PausedFeatures.h"
#include <limits>
#include <chrono>
void check(bool, char const*);
void chatPositionTests() {
    using namespace lamium;
    using namespace ui;
    using information::chatPosition::placed;
    auto defaults = defaultHudElement(HudElementId::Chat);
    auto first = placed(640, 360, 256, 27, defaults);
    auto taller = placed(640, 360, 256, 72, defaults);
    check(first && taller && first->x == hudInset && first->y + first->h == taller->y + taller->h,
          "chat grows upward without moving its bottom edge");
    auto moved = placeAt(defaults, 190, 90, 256, 27, 640, 360);
    auto next = placed(640, 360, 256, 27, moved);
    check(next && next->x == 190 && next->y == 90, "a chat drop uses the shared HUD placement exactly");
    auto right = snapTo(defaults, Anchor::BottomRight);
    next = placed(800, 450, 320, 45, right);
    check(next && next->x + next->w == 800 - hudInset && next->y + next->h == 450 - hudInset,
          "chat edge anchors follow a resized screen and the game's new measured width");
    right.dx = 999; right.dy = -999;
    next = placed(640, 360, 256, 27, right);
    check(next && next->x == 384 && next->y == 0, "chat stays inside the screen after a stale offset");
    check(!placed(640, 360, 641, 20, defaults) && !placed(640, 360, 20, 361, defaults)
          && !placed(640, 360, 20, 0, defaults) && !placed(0, 360, 20, 20, defaults)
          && !placed(640, 360, std::numeric_limits<float>::quiet_NaN(), 20, defaults),
          "unmeasured, oversized or invalid chat geometry leaves vanilla placement intact");
    check(!decodeSettings("{}").information.chatPosition && input::defaultChord(input::Action::ToggleChatPosition).empty(),
          "older settings leave chat vanilla and its new key unbound");
    Settings value;
    value.hud.chat = moved;
    value.hud.info.dx = 19;
    check(input::toggleAction(value, input::Action::ToggleChatPosition), "the chat switch has a toggle action");
    auto path = std::filesystem::temp_directory_path()
        / ("lamium-chat-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    writeSettings(path, value);
    auto restored = readSettings(path);
    std::filesystem::remove(path);
    check(restored.information.chatPosition && restored.hud.chat.anchor == moved.anchor
          && restored.hud.chat.dx == moved.dx && restored.hud.chat.dy == moved.dy && restored.hud.info.dx == 19,
          "chat enable and placement persist without changing another HUD element");
    auto active = paused::inUse(value);
    check(paused::onUnverified("chatPosition") == paused::Level::Fully
          && std::ranges::find(active, "chatPosition") != active.end(),
          "chat position is reported paused on an unverified executable");
}
