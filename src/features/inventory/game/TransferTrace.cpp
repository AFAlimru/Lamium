#include "features/inventory/game/TransferTrace.h"
#ifdef LAMIUM_TRANSFER_TRACE
#include "app/TraceLog.h"
#include "app/Versions.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/screens/controllers/ContainerScreenController.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/world/containers/controllers/AutoPlaceItem.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/item/ItemStack.h"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <format>
#include <iterator>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace lamium::inventory::game::transferTrace {
namespace {
TraceBudget viewBudget, screenBudget, routeBudget, inputBudget, callBudget;
std::atomic<int64_t> screenAt{0};
std::atomic<bool> altEvent{false};
ll::event::ListenerPtr mouseListener, keyListener;
std::set<std::string> seenViews;
int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool allowed() { return versionSensitiveAllowed("Inventory transfer diagnostics"); }
bool recent() { auto at = screenAt.load(); return at != 0 && now() - at < 1000; }
std::string modifiers() {
    constexpr int down = 0x8000;
    return std::format("shift={} ctrl={} alt={} alt-event={}", bool(GetAsyncKeyState(VK_SHIFT) & down),
        bool(GetAsyncKeyState(VK_CONTROL) & down), bool(GetAsyncKeyState(VK_MENU) & down), altEvent.load());
}
std::vector<std::string> collections(ContainerManagerController& manager) {
    std::vector<std::string> names;
    size_t visited = 0;
    for (auto const& [name, container] : manager.mContainers.get()) {
        if (++visited > 32) break;
        if (container) names.push_back(name);
    }
    std::ranges::sort(names);
    return names;
}
std::string snapshot(ContainerScreenController& controller) {
    auto manager = controller.mContainerManagerController;
    if (!manager || manager->mContainersClosed) return "closed";
    std::string result = std::format("type={} cursor={}", int(manager->getContainerType()),
        controller._isCursorSelectedActive());
    int budget = 256;
    for (auto const& name : collections(*manager)) {
        int size = manager->getContainerSize(name);
        result += std::format(" {}[{}]={{", name, size);
        for (int i = 0; i < size && budget > 0; ++i, --budget) {
            auto const& item = manager->getItemStack(name, i);
            if (!item.isNull() && item.mCount > 0)
                result += std::format("{}:{}x{},", i, item.getTypeName(), int(item.mCount));
        }
        result += "}";
        if (budget == 0) { result += " truncated"; break; }
    }
    return result;
}
void observe(ContainerScreenController& controller, char const* operation, char const* stage,
             std::string const& collection, int index, int amount) noexcept {
    try {
        if (!recent() || !allowed() || !callBudget.take(160)) return;
        Runtime::instance().self().getLogger().info("L-169 vanilla {} {} {}:{} amount={} {} {}",
            operation, stage, collection, index, amount, modifiers(), snapshot(controller));
    } catch (...) {}
}
// Observe ordinary player operations only; no diagnostic invokes a handler.
LL_TYPE_INSTANCE_HOOK(TransferTakeAllTrace, ll::memory::HookPriority::Normal, ContainerScreenController,
    &ContainerScreenController::_handleTakeAll, void, std::string const& collection, int index) {
    observe(*this, "take-all", "before", collection, index, -1);
    origin(collection, index);
    observe(*this, "take-all", "predicted-after", collection, index, -1);
}
LL_TYPE_INSTANCE_HOOK(TransferPlaceAllTrace, ll::memory::HookPriority::Normal, ContainerScreenController,
    &ContainerScreenController::$_handlePlaceAll, void, std::string const& collection, int index) {
    observe(*this, "place-all", "before", collection, index, -1);
    origin(collection, index);
    observe(*this, "place-all", "predicted-after", collection, index, -1);
}
LL_TYPE_INSTANCE_HOOK(TransferAutoPlaceTrace, ll::memory::HookPriority::Normal, ContainerScreenController,
    &ContainerScreenController::_handleAutoPlace, void, int amount, std::string const& collection, int index) {
    observe(*this, "auto-place", "before", collection, index, amount);
    origin(amount, collection, index);
    observe(*this, "auto-place", "predicted-after", collection, index, amount);
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[]{
    {TransferTakeAllTrace::hook, TransferTakeAllTrace::unhook},
    {TransferPlaceAllTrace::hook, TransferPlaceAllTrace::unhook},
    {TransferAutoPlaceTrace::hook, TransferAutoPlaceTrace::unhook}
};
}
void inspectView(ScreenView& view) noexcept {
    try {
        if (!view.mHasFocus || !view.mController || !allowed() || seenViews.size() >= 24) return;
        auto name = view.getScreenName();
        if (!seenViews.insert(name).second) return;
        traceLog(viewBudget, 24, "L-169 focused view={} is-container={}", name,
            view.mController->_isContainerScreen());
    } catch (...) {}
}
void inspect(ContainerScreenController& controller, bool changed) noexcept {
    try {
        if (!allowed()) return;
        screenAt = now();
        // Input callbacks only see the owned time, never the controller.
        if (!changed) return;
        if (!screenBudget.take(24)) return;
        auto manager = controller.mContainerManagerController;
        if (!manager) return;
        Runtime::instance().self().getLogger().info("L-169 layout {}", snapshot(controller));
        for (auto const& name : collections(*manager)) {
            std::string order;
            if (auto* targets = controller.tryGetAutoPlaceOrder(name)) {
                size_t count = 0;
                for (auto const& target : *targets) {
                    if (++count > 32) { order += " truncated"; break; }
                    order += std::format(" {}(stop={})", target.collection.get(), bool(target.stopWhenPlaced));
                }
            }
            traceLog(routeBudget, 200, "L-169 route {} ->{}", name, order.empty() ? " none" : order);
        }
    } catch (...) {}
}
void start() {
    if (!allowed()) return;
    if (mouseListener && keyListener) return;
    try {
        for (auto& hook : hooks) if (!hook.installed) {
            if (hook.install(true) != 0) throw std::runtime_error("Could not install transfer diagnostics");
            hook.installed = true;
        }
        auto& bus = ll::event::EventBus::getInstance();
        mouseListener = bus.emplaceListener<ll::event::input::MouseInputEvent>([](auto& event) {
            if (!recent() || event.actionButtonId() == MouseAction::ActionMove
                || event.actionButtonId() == MouseAction::ActionMoveRelative) return;
            try { traceLog(inputBudget, 240, "L-169 mouse button={} data={} cancelled={} {}",
                int(event.actionButtonId()), int(event.buttonData()), event.isCancelled(), modifiers()); } catch (...) {}
        });
        keyListener = bus.emplaceListener<ll::event::input::KeyInputEvent>([](auto& event) {
            if (event.keyCode() != VK_MENU && event.keyCode() != VK_LMENU && event.keyCode() != VK_RMENU) return;
            altEvent = event.isDown();
            if (recent()) traceLog(inputBudget, 240, "L-169 Alt key code={} down={} cancelled={}",
                event.keyCode(), event.isDown(), event.isCancelled());
        });
        if (!mouseListener || !keyListener) throw std::runtime_error("Could not subscribe transfer diagnostics");
        Runtime::instance().self().getLogger().warn("L-169 read-only transfer diagnostics enabled; disable Item Transfer to observe vanilla");
    } catch (...) {
        stop();
        Runtime::instance().self().getLogger().error("L-169 transfer diagnostics unavailable; inventory features remain enabled");
    }
}
void stop() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto* listener : {&mouseListener, &keyListener}) {
        if (*listener) bus.removeListener(*listener);
        listener->reset();
    }
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && !it->remove(true))
            Runtime::instance().self().getLogger().error("Could not remove transfer diagnostic hook");
        else it->installed = false;
    screenAt = 0;
    altEvent = false;
    seenViews.clear();
}
}
#else
namespace lamium::inventory::game::transferTrace {
void start() {}
void stop() {}
void inspectView(ScreenView&) noexcept {}
void inspect(ContainerScreenController&, bool) noexcept {}
}
#endif
