#include "features/visuals/PortalTrace.h"
#ifdef LAMIUM_PORTAL_TRACE
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/world/actor/player/Player.h"
#include "mc/client/game/ClientInputCallbacks.h"
#include "mc/client/gui/screens/SceneStack.h"
#include <Windows.h>
#include <algorithm>
#include <vector>
#include "features/visuals/PortalCandidates.inc"
#include "ll/api/memory/Symbol.h"
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
// Experiment 4 (trace builds only): who closes the inventory in a portal,
// and does the inventory key reach the player? Caller stacks as offsets from
// the game module, with the offsets of candidate functions logged at start.
struct Candidate { std::string name; uintptr_t address; };
std::vector<Candidate> candidates;
// Game functions by mangled name; the stubs Lamium calls through are filled
// at load, so their pointers do not give the addresses.
void loadCandidates() {
    for (char const* name : {LAMIUM_PORTAL_CANDIDATES})
        if (auto* p = ll::memory::SymbolView{name}.resolve(true)) candidates.push_back({name, reinterpret_cast<uintptr_t>(p)});
    std::sort(candidates.begin(), candidates.end(), [](auto const& a, auto const& b) { return a.address < b.address; });
}
// The nearest candidate function starting at or below an address, if close.
std::string nameOf(uintptr_t address) {
    Candidate const* best = nullptr;
    for (auto const& c : candidates) if (c.address <= address) best = &c;
    if (!best || address - best->address > 0x3000) return {};
    return std::format("={}+{:x}", best->name, address - best->address);
}
std::string stack() {
    void* frames[12]{};
    auto count = CaptureStackBackTrace(2, 12, frames, nullptr);
    auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    std::string out;
    for (USHORT i = 0; i < count; ++i) {
        auto a = reinterpret_cast<uintptr_t>(frames[i]);
        out += std::format(" +{:x}{}", a - base, nameOf(a));
    }
    return out;
}
LL_TYPE_INSTANCE_HOOK(PortalSchedulePop, ll::memory::HookPriority::Normal, SceneStack, &SceneStack::_schedulePopScreen, void,
    int total, std::optional<OreUI::RouteAction> route, std::vector<std::string> const& expected) {
    try { log(std::format("schedulePopScreen {} screen {} stack{}", total, screen(), stack())); } catch (...) {}
    origin(total, std::move(route), expected);
}
LL_TYPE_STATIC_HOOK(PortalInventoryKey, ll::memory::HookPriority::Normal, ClientInputCallbacks,
    &ClientInputCallbacks::handleInventoryButtonPress, void, IClientInstance& client, CraftingType craft) {
    try { log(std::format("handleInventoryButtonPress screen {} stack{}", screen(), stack())); } catch (...) {}
    origin(client, craft);
    try { log(std::format("after handleInventoryButtonPress screen {}", screen())); } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(PortalOpenInventory, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::$openInventory, void) {
    try { log(std::format("openInventory ({}) stack{}", fields(*this), stack())); } catch (...) {}
    origin();
}
void start() {
    try {
        loadCandidates();
        auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        log(std::format("{} candidate functions resolved", candidates.size()));
        for (auto const& c : candidates) log(std::format("candidate +{:x} {}", c.address - base, c.name));
    } catch (...) {}
    PortalSchedulePop::hook();
    PortalInventoryKey::hook();
    PortalOpenInventory::hook();
    PortalCanOpen::hook();
    PortalInside::hook();
    PortalTick::hook();
    Runtime::instance().self().getLogger().warn("Portal diagnostics enabled (L-148/L-149)");
}
void stop() {
    PortalSchedulePop::unhook(true);
    PortalInventoryKey::unhook(true);
    PortalOpenInventory::unhook(true);
    PortalCanOpen::unhook(true);
    PortalInside::unhook(true);
    PortalTick::unhook(true);
}
}
#else
namespace lamium::visuals::portalTrace { void start() {} void stop() {} }
#endif
