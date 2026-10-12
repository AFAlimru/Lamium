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
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/screens/controllers/CraftingScreenController.h"
#include "mc/world/containers/managers/models/CraftingContainerManagerModel.h"
#include <chrono>
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
// Experiment 5 (trace builds only): the client tick of the last
// handleInsidePortal, to tell "in a portal" by recent contact.
uint64_t ticks = 0, lastPortalTick = 0;
bool nearPortal() { return lastPortalTick && ticks - lastPortalTick <= 2; }
// The player closing the screen: Esc, or E once the screen has been open a
// moment (the press that opened it is still down when the first close comes).
std::chrono::steady_clock::time_point forcedOpen{};
bool playerClosing() {
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x8000) || (GetAsyncKeyState(VK_ESCAPE) & 1)) return true;
    bool e = (GetAsyncKeyState('E') & 0x8000) || (GetAsyncKeyState('E') & 1);
    return e && std::chrono::steady_clock::now() - forcedOpen > std::chrono::milliseconds(500);
}
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
    lastPortalTick = ticks;
    try { log(std::format("handleInsidePortal {},{},{}: {} -> {} screen {}", portal.x, portal.y, portal.z, before, fields(*this), screen())); }
    catch (...) {}
}
// Every client tick while the effect runs or a screen changes near a portal.
LL_TYPE_INSTANCE_HOOK(PortalTick, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::$normalTick, void) {
    std::string before;
    try { before = screen(); } catch (...) {}
    ++ticks;
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
// Follow jump stubs to the function they lead to: jmp [rip+d], jmp rel32,
// mov rax, imm64 + jmp rax.
uintptr_t follow(void* p, std::string* bytes = nullptr) {
    auto* b = static_cast<unsigned char*>(p);
    for (int i = 0; i < 6 && b; ++i) {
        if (bytes && i == 0) for (int k = 0; k < 14; ++k) *bytes += std::format("{:02x}", b[k]);
        if (b[0] == 0xFF && b[1] == 0x25) b = *reinterpret_cast<unsigned char**>(b + 6 + *reinterpret_cast<int32_t*>(b + 2));
        else if (b[0] == 0x48 && b[1] == 0xFF && b[2] == 0x25) b = *reinterpret_cast<unsigned char**>(b + 7 + *reinterpret_cast<int32_t*>(b + 3));
        else if (b[0] == 0xE9) b = b + 5 + *reinterpret_cast<int32_t*>(b + 1);
        else if (b[0] == 0x48 && b[1] == 0xB8 && b[10] == 0xFF && b[11] == 0xE0) b = *reinterpret_cast<unsigned char**>(b + 2);
        else if (b[0] == 0x49 && b[1] == 0xBB && b[10] == 0x41 && b[11] == 0xFF && b[12] == 0xE3) b = *reinterpret_cast<unsigned char**>(b + 2);
        else break;
    }
    return reinterpret_cast<uintptr_t>(b);
}
struct Candidate { std::string name; uintptr_t address; };
std::vector<Candidate> candidates;
// Game functions by mangled name; the stubs Lamium calls through are filled
// at load, so their pointers do not give the addresses.
void loadCandidates() {
    for (char const* name : {LAMIUM_PORTAL_CANDIDATES})
        if (auto* p = ll::memory::SymbolView{name}.resolve(true)) candidates.push_back({name, follow(p)});
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
    try {
        auto top = screen();
        log(std::format("schedulePopScreen {} screen {} stack{}", total, top, stack()));
        if (nearPortal() && top.starts_with("inventory_screen") && !playerClosing()) {
            log("  pop blocked: inventory in a portal, no Esc/E");
            return;
        }
    } catch (...) {}
    origin(total, std::move(route), expected);
}
LL_TYPE_STATIC_HOOK(PortalInventoryKey, ll::memory::HookPriority::Normal, ClientInputCallbacks,
    &ClientInputCallbacks::handleInventoryButtonPress, void, IClientInstance& client, CraftingType craft) {
    try { log(std::format("handleInventoryButtonPress screen {} stack{}", screen(), stack())); } catch (...) {}
    std::string before;
    try { before = screen(); } catch (...) {}
    origin(client, craft);
    try {
        auto after = screen();
        log(std::format("after handleInventoryButtonPress screen {}", after));
        auto* player = client.getLocalPlayer();
        if (player && nearPortal() && after == before && after.starts_with("hud_screen")) {
            log("  refused in a portal: calling openInventory");
            forcedOpen = std::chrono::steady_clock::now();
            player->openInventory();
        }
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(PortalOpenInventory, ll::memory::HookPriority::Normal, LocalPlayer, &LocalPlayer::$openInventory, void) {
    try { log(std::format("openInventory ({}) stack{}", fields(*this), stack())); } catch (...) {}
    origin();
}
// Experiment 6 (trace builds only): does the inventory's container model
// call itself invalid near a portal? Answered valid there.
LL_TYPE_INSTANCE_HOOK(PortalModelValid, ll::memory::HookPriority::Normal, ContainerManagerModel,
    &ContainerManagerModel::$isValid, bool, float range) {
    bool result = origin(range);
    if (!result && nearPortal()) {
        try { log("ContainerManagerModel::isValid -> false near a portal; answering true"); } catch (...) {}
        return true;
    }
    return result;
}
LL_TYPE_INSTANCE_HOOK(PortalCraftingValid, ll::memory::HookPriority::Normal, CraftingContainerManagerModel,
    &CraftingContainerManagerModel::$isValid, bool, float range) {
    bool result = origin(range);
    if (!result && nearPortal()) {
        try { log("CraftingContainerManagerModel::isValid -> false near a portal; answering true"); } catch (...) {}
        return true;
    }
    return result;
}
// Experiment 7 (trace builds only): the inventory screen's own validity
// check, which a container screen polls to close itself and to refuse moves.
LL_TYPE_INSTANCE_HOOK(PortalScreenValid, ll::memory::HookPriority::Normal, ContainerScreenController,
    &ContainerScreenController::$_isStillValid, bool) {
    bool result = origin();
    if (!result && nearPortal()) {
        try { log("ContainerScreenController::_isStillValid -> false near a portal; answering true"); } catch (...) {}
        return true;
    }
    return result;
}
LL_TYPE_INSTANCE_HOOK(PortalCraftingScreenValid, ll::memory::HookPriority::Normal, CraftingScreenController,
    &CraftingScreenController::$_isStillValid, bool) {
    bool result = origin();
    if (!result && nearPortal()) {
        try { log("CraftingScreenController::_isStillValid -> false near a portal; answering true"); } catch (...) {}
        return true;
    }
    return result;
}
void start() {
    PortalScreenValid::hook();
    PortalCraftingScreenValid::hook();
    PortalModelValid::hook();
    PortalCraftingValid::hook();
    try {
        loadCandidates();
        auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        log(std::format("{} candidate functions resolved", candidates.size()));
        if (auto* p = ll::memory::SymbolView{"?$handleInsidePortal@LocalPlayer@@QEAAXAEBVBlockPos@@@Z"}.resolve(true)) {
            std::string bytes;
            auto real = follow(p, &bytes);
            log(std::format("handleInsidePortal symbol {:x} first bytes {} -> {:x} (game base {:x})", reinterpret_cast<uintptr_t>(p), bytes,
                real, base));
        }
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
    PortalScreenValid::unhook(true);
    PortalCraftingScreenValid::unhook(true);
    PortalModelValid::unhook(true);
    PortalCraftingValid::unhook(true);
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
