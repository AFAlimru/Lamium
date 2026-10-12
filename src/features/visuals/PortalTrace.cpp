#include "features/visuals/PortalTrace.h"
#ifdef LAMIUM_PORTAL_TRACE
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Player.h"
#include <format>
#include <string>

namespace lamium::visuals::portalTrace {
namespace {
int lines = 0;
std::string lastScreen;
void log(std::string const& text) {
    if (lines >= 2000) return;
    ++lines;
    Runtime::instance().self().getLogger().info("L-148 {}", text);
}
std::string screen() {
    auto client = ll::service::getClientInstance();
    return client ? client->getTopScreenName() : std::string{};
}
std::string fields(LocalPlayer& player) {
    return std::format("effect {:.3f} prev {:.3f} enabled {} inside {}", static_cast<float>(player.mPortalEffectTime), static_cast<float>(player.mOPortalEffectTime),
        static_cast<bool>(player.mEnablePortalEffect), player.isInsidePortal());
}
}
// Called while the player touches a portal: what changes inside it.
LL_TYPE_INSTANCE_HOOK(PortalInside, ll::memory::HookPriority::Normal, LocalPlayer,
    &LocalPlayer::$handleInsidePortal, void, BlockPos const& portal) {
    std::string before;
    try { before = fields(*this) + " screen " + screen(); } catch (...) {}
    origin(portal);
    try { log(std::format("handleInsidePortal {},{},{}: {} -> {} screen {}", portal.x, portal.y, portal.z, before, fields(*this), screen())); }
    catch (...) {}
}
// Every client tick while the effect runs or a screen changes near a portal.
LL_TYPE_INSTANCE_HOOK(PortalTick, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::$normalTick, void) {
    std::string before;
    try { before = screen(); } catch (...) {}
    origin();
    // Experiment (trace builds only): the effect time back to 0 after each
    // tick. Does the purple overlay go, does the inventory stay open, does
    // the transfer still happen?
    bool zeroed = false;
    if (static_cast<float>(mPortalEffectTime) > 0 || static_cast<float>(mOPortalEffectTime) > 0) {
        mPortalEffectTime = 0.f;
        mOPortalEffectTime = 0.f;
        zeroed = true;
    }
    try {
        if (zeroed) log("effect time zeroed after the tick");
        auto now = screen();
        bool active = static_cast<float>(mPortalEffectTime) > 0 || static_cast<float>(mOPortalEffectTime) > 0 || isInsidePortal();
        if (active || now != before) log(std::format("tick: {} screen {} -> {}", fields(*this), before, now));
        if (now != lastScreen) { log(std::format("screen changed {} -> {} ({})", lastScreen, now, fields(*this))); lastScreen = now; }
    } catch (...) {}
}
// Experiment 3 (trace builds only): who refuses container screens near a
// portal? Logged when it says no; answered yes for the local player.
LL_TYPE_INSTANCE_HOOK(PortalCanOpen, ll::memory::HookPriority::Normal, Player, &Player::canOpenContainerScreen, bool) {
    bool result = origin();
    try {
        auto client = ll::service::getClientInstance();
        auto* local = client ? client->getLocalPlayer() : nullptr;
        if (!result && local && static_cast<Player*>(local) == this) {
            log(std::format("canOpenContainerScreen -> false ({}, screen {}); answering true", fields(*local), screen()));
            return true;
        }
    } catch (...) {}
    return result;
}
void start() {
    PortalCanOpen::hook();
    PortalInside::hook();
    PortalTick::hook();
    Runtime::instance().self().getLogger().warn("Portal diagnostics enabled (L-148/L-149)");
}
void stop() {
    PortalCanOpen::unhook(true);
    PortalInside::unhook(true);
    PortalTick::unhook(true);
}
}
#else
namespace lamium::visuals::portalTrace { void start() {} void stop() {} }
#endif
