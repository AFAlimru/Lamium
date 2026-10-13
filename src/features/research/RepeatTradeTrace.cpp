#include "features/research/RepeatTradeTrace.h"
#ifdef LAMIUM_REPEAT_TRADE_TRACE
#include "app/Runtime.h"
#include "app/TraceLog.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/KeyInputEvent.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
// ScreenButtonMapping must be complete before InputComponent's vector member.
#include "mc/client/gui/controls/ScreenButtonMapping.h"
#include "mc/client/gui/controls/InputComponent.h"
#include "mc/client/gui/controls/UIPropertyBag.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/gui/screens/ScreenController.h"
#include "mc/client/gui/screens/ScreenEvent.h"
#include "mc/client/gui/screens/controllers/Trade2ScreenController.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/client/network/LegacyClientNetworkHandler.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/deps/json/Value.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/deps/nbt/IntTag.h"
#include "mc/deps/nbt/ListTag.h"
#include "mc/network/packet/InventoryContentPacket.h"
#include "mc/network/packet/InventorySlotPacket.h"
#include "mc/network/packet/ItemStackResponseContainerInfo.h"
#include "mc/network/packet/ItemStackResponseInfo.h"
#include "mc/network/packet/ItemStackResponsePacket.h"
#include "mc/network/packet/ItemStackRequestPacketPayload.h"
#include "mc/network/packet/UpdateTradePacket.h"
#include "mc/network/packet/ActorEventPacket.h"
#include "mc/entity/components_json_legacy/EconomyTradeableComponent.h"
#include "mc/world/containers/managers/controllers/ContainerManagerController.h"
#include "mc/world/containers/managers/models/ContainerManagerModel.h"
#include "mc/world/inventory/network/ItemStackRequestBatch.h"
#include "mc/world/inventory/network/ItemStackRequestAction.h"
#include "mc/world/inventory/network/ItemStackRequestData.h"
#include "mc/world/inventory/network/crafting/ItemStackRequestActionCraftBase.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/trading/MerchantRecipeList.h"
#include "mc/world/level/Level.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <format>
#include <iterator>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>

namespace lamium::researchTrace::repeatTrade {
namespace {
using Clock = std::chrono::steady_clock;
TraceBudget inputBudget, screenBudget, requestBudget, responseBudget, offerBudget, rowBudget, notifyBudget,
    viewBudget, componentBudget;
std::atomic<int64_t> observedAt{0};
std::atomic<unsigned> sequence{0};
std::mutex seenMutex;
std::set<std::string> seen;
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count();
}
bool recent() {
    auto at = observedAt.load();
    return at != 0 && nowMs() - at < 10000;
}
bool tradeScreen() {
    auto client = ll::service::getClientInstance();
    return client && client->getScreenName().find("trade") != std::string::npos;
}
template <class... Args>
void log(TraceBudget& budget, unsigned limit, std::format_string<Args...> format, Args&&... args) noexcept {
    if (!budget.take(limit)) return;
    try {
        Runtime::instance().self().getLogger().info("L-166 seq={} ms={} {}", ++sequence, nowMs(),
            std::format(format, std::forward<Args>(args)...));
    } catch (...) {}
}
std::string bagText(UIPropertyBag const* bag) {
    if (!bag) return "(none)";
    auto text = bag->mJsonValue->toStyledString();
    for (char& c : text) if (c == '\n' || c == '\r') c = ' ';
    if (text.size() > 1200) text.resize(1200);
    return text;
}
int intTag(CompoundTag const& data, char const* key) {
    auto const* tag = data.get(key);
    return tag && tag->getId() == Tag::Int ? static_cast<IntTag const*>(tag)->data : -1;
}
// Packet state is logged separately from the screen's visual offer: the latter
// may be predicted locally and must not authorize another trade by itself.
LL_TYPE_INSTANCE_HOOK(RepeatOffer, ll::memory::HookPriority::Normal, ClientNetworkHandler,
    &ClientNetworkHandler::$handle, void, NetworkIdentifier const& source, UpdateTradePacket const& packet) {
    try {
        observedAt = nowMs();
        auto const& p = static_cast<UpdateTradePacketPayload const&>(packet);
        log(offerBudget, 200, "offer-received entity={} tier={} newScreen={} economy={}",
            p.mEntityUniqueID->rawID, static_cast<int>(p.mTraderTier), bool(p.mUseNewTradeScreen), bool(p.mUsingEconomyTrade));
        auto const* recipes = p.mData->get("Recipes");
        if (recipes && recipes->getId() == Tag::List) {
            unsigned index = 0;
            for (auto const& entry : static_cast<ListTag const&>(*recipes)) {
                if (index >= 64) break;
                if (entry && entry->getId() == Tag::Compound) {
                    auto const& recipe = static_cast<CompoundTag const&>(*entry);
                    log(rowBudget, 500, "offer-row={} recipe={} tier={} uses={}/{} buyA={} buyB={}", index,
                        intTag(recipe, "netId"), intTag(recipe, "tier"), intTag(recipe, "uses"),
                        intTag(recipe, "maxUses"), intTag(recipe, "buyCountA"), intTag(recipe, "buyCountB"));
                }
                ++index;
            }
        }
    } catch (...) {}
    origin(source, packet);
    log(offerBudget, 200, "offer-applied");
}
LL_TYPE_INSTANCE_HOOK(RepeatButton, ll::memory::HookPriority::Normal, ScreenController,
    &ScreenController::$handleEvent, ::ui::ViewRequest, ScreenEvent& event) {
    bool traced = false;
    try {
        if (event.type == ScreenEventType::ButtonEvent && tradeScreen()) {
            observedAt = nowMs();
            traced = true;
            auto const& button = event.data->button.get();
            log(screenBudget, 300, "button id={} from={} state={} previous={} input={} bag={}",
                uint(button.id), uint(button.fromButtonId), int(button.state), int(button.previousState),
                int(button.inputMode), bagText(button.properties));
        } else if (event.type == ScreenEventType::ToggleChangeEvent && tradeScreen()) {
            observedAt = nowMs();
            traced = true;
            auto const& toggle = event.data->toggle.get();
            log(screenBudget, 300, "toggle id={} index={} state={} clicked={} bag={}",
                uint(toggle.id), int(toggle.index), bool(toggle.state), bool(toggle.toggledByButtonClick), bagText(toggle.properties));
        }
    } catch (...) {}
    auto result = origin(event);
    if (traced) log(screenBudget, 300, "button-return result={}", int(result));
    return result;
}
LL_TYPE_INSTANCE_HOOK(RepeatSlotPressed, ll::memory::HookPriority::Normal, Trade2ScreenController,
    &Trade2ScreenController::$_onContainerSlotPressed, ::ui::ViewRequest, std::string const& collection, int index) {
    observedAt = nowMs();
    log(screenBudget, 300, "slot-pressed collection={} index={}", collection, index);
    auto result = origin(collection, index);
    log(screenBudget, 300, "slot-return result={}", int(result));
    return result;
}
LL_TYPE_INSTANCE_HOOK(RepeatTick, ll::memory::HookPriority::Normal, Trade2ScreenController,
    &Trade2ScreenController::$tick, ::ui::DirtyFlag) {
    auto result = origin();
    try {
        observedAt = nowMs();
        auto const& recipe = mVisualOffer.get();
        auto manager = mContainerManagerController.get();
        std::string slots;
        if (manager && !manager->mContainersClosed) {
            unsigned collections = 0;
            for (auto const& [name, container] : manager->mContainers.get()) {
                if (++collections > 16) break;
                int count = manager->getContainerSize(name);
                slots += std::format(" [{}:{}]", name, count);
                // Large collections are the player's inventory; identify them
                // without logging every carried item on every UI tick.
                if (count > 3) continue;
                for (int i = 0; i < count && i < 3; ++i) {
                    auto const& stack = manager->getItemStack(name, i);
                    slots += std::format(" {}:{}={}x{}", name, i, stack.isNull() ? "empty" : stack.getTypeName(), int(stack.mCount));
                }
            }
        }
        auto state = std::format("visual-offer show={} recipe={} tier={} uses={}/{} buyA={} buyB={}{}",
            bool(mShowVisualOffer), recipe.mRecipeNetId->mRawId, int(recipe.mTier), int(recipe.mUses),
            int(recipe.mMaxUses), int(recipe.mBuyCountA), int(recipe.mBuyCountB), slots);
        std::lock_guard lock{seenMutex};
        if (seen.size() < 200 && seen.insert(state).second) log(screenBudget, 300, "{}", state);
        auto client = ll::service::getClientInstance();
        auto* player = client ? client->getLocalPlayer() : nullptr;
        auto* actor = player ? player->getLevel().fetchEntity(mEntityUniqueID.get(), false) : nullptr;
        auto* offers = actor ? actor->getTradeOffers() : nullptr;
        if (offers) {
            std::string rows;
            unsigned count = 0;
            for (auto const& offer : offers->mRecipeList.get()) {
                if (++count > 32) break;
                rows += std::format(" {}={}/{}", offer.mRecipeNetId->mRawId, int(offer.mUses), int(offer.mMaxUses));
            }
            auto line = std::format("client-offers entity={}{}", mEntityUniqueID->rawID, rows);
            if (seen.size() < 200 && seen.insert(line).second) log(notifyBudget, 200, "{}", line);
        }
    } catch (...) {}
    return result;
}
LL_TYPE_INSTANCE_HOOK(RepeatLeave, ll::memory::HookPriority::Normal, Trade2ScreenController,
    &Trade2ScreenController::$onLeave, void) {
    log(screenBudget, 300, "screen-leave");
    // Keep a short observation window for a response arriving after close.
    observedAt = nowMs();
    { std::lock_guard lock{seenMutex}; seen.clear(); }
    origin();
}
LL_TYPE_INSTANCE_HOOK(RepeatRequestBatch, ll::memory::HookPriority::Normal, ItemStackRequestPacketPayload,
    &ItemStackRequestPacketPayload::$ctor, void*, ItemStackRequestBatch const& batch) {
    try {
        if (recent()) {
            unsigned requests = 0, actions = 0;
            for (auto const& request : batch.mRequests.get()) {
                if (++requests > 32) break;
                if (!request) continue;
                log(requestBudget, 200, "request-batch id={} actions={}",
                    request->mClientRequestId->mRawId, request->mActions->size());
                for (auto const& action : request->mActions.get()) {
                    if (++actions > 64) break;
                    if (!action) continue;
                    auto const* craft = action->getCraftAction();
                    log(requestBudget, 200, "request-action id={} type={} crafts={}",
                        request->mClientRequestId->mRawId, int(action->mActionType), craft ? int(craft->mNumCrafts) : -1);
                }
                if (actions > 64) break;
            }
        }
    } catch (...) {}
    return origin(batch);
}
LL_TYPE_INSTANCE_HOOK(RepeatResponse, ll::memory::HookPriority::Normal, ClientNetworkHandler,
    &ClientNetworkHandler::$handle, void, NetworkIdentifier const& source, ItemStackResponsePacket const& packet) {
    origin(source, packet);
    try {
        if (!recent()) return;
        unsigned count = 0;
        for (auto const& response : packet.mResponses.get()) {
            if (++count > 32) break;
            log(responseBudget, 300, "response-applied id={} result={} containers={}",
                response.mClientRequestId->mRawId, int(response.mResult), response.mContainers->size());
            unsigned slots = 0;
            for (auto const& container : response.mContainers.get()) {
                for (auto const& slot : container.mSlots.get()) {
                    if (++slots > 64) break;
                    log(responseBudget, 300, "response-slot id={} container={} slot={} amount={} netId={}",
                        response.mClientRequestId->mRawId, int(container.mFullContainerName->mName),
                        int(slot.mSlot), int(slot.mAmount), slot.mItemStackNetId->mRawId);
                }
                if (slots > 64) break;
            }
        }
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(RepeatInventorySlot, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void, NetworkIdentifier const& source, InventorySlotPacket const& packet) {
    origin(source, packet);
    try { if (recent()) log(responseBudget, 300, "inventory-slot-applied container={} slot={}",
        int(packet.mInventoryId), uint(packet.mSlot)); } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(RepeatInventoryContent, ll::memory::HookPriority::Normal, LegacyClientNetworkHandler,
    &LegacyClientNetworkHandler::$handle, void, NetworkIdentifier const& source, InventoryContentPacket const& packet) {
    origin(source, packet);
    try { if (recent()) log(responseBudget, 300, "inventory-content-applied container={}",
        int(packet.mInventoryId)); } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(RepeatNotify, ll::memory::HookPriority::Normal, EconomyTradeableComponent,
    &EconomyTradeableComponent::notifyTrade, void, int index, int trades) {
    try {
        if (recent()) {
            auto* offers = mOffers.get();
            auto const* recipe = offers && index >= 0 && size_t(index) < offers->mRecipeList->size()
                ? &offers->mRecipeList.get()[size_t(index)] : nullptr;
            log(notifyBudget, 200, "notify-before client={} entity={} index={} count={} uses={}",
                mOwner.getLevel().isClientSide(), mOwner.getOrCreateUniqueID().rawID, index, trades,
                recipe ? int(recipe->mUses) : -1);
        }
    } catch (...) {}
    origin(index, trades);
    try {
        if (recent()) {
            auto* offers = mOffers.get();
            auto const* recipe = offers && index >= 0 && size_t(index) < offers->mRecipeList->size()
                ? &offers->mRecipeList.get()[size_t(index)] : nullptr;
            log(notifyBudget, 200, "notify-after client={} entity={} index={} count={} uses={}",
                mOwner.getLevel().isClientSide(), mOwner.getOrCreateUniqueID().rawID, index, trades,
                recipe ? int(recipe->mUses) : -1);
        }
    } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(RepeatActorEvent, ll::memory::HookPriority::Normal, ClientNetworkHandler,
    &ClientNetworkHandler::$handle, void, NetworkIdentifier const& source, ActorEventPacket const& packet) {
    try { if (recent()) log(notifyBudget, 200, "actor-event-received runtime={} event={} data={}",
        packet.mRuntimeId->rawID, int(packet.mEventId), int(packet.mData)); } catch (...) {}
    origin(source, packet);
    try { if (recent()) log(notifyBudget, 200, "actor-event-applied runtime={} event={} data={}",
        packet.mRuntimeId->rawID, int(packet.mEventId), int(packet.mData)); } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(RepeatAutoPlace, ll::memory::HookPriority::Normal, ContainerScreenController,
    &ContainerScreenController::_handleAutoPlace, void, int amount, std::string const& collection, int index) {
    try {
        if (tradeScreen()) {
            observedAt = nowMs();
            log(screenBudget, 300, "auto-place collection={} index={} amount={}", collection, index, amount);
        }
    } catch (...) {}
    origin(amount, collection, index);
    try {
        if (tradeScreen()) log(screenBudget, 300, "auto-place-return collection={} index={} amount={}", collection, index, amount);
    } catch (...) {}
}
// Modified row clicks never reach ScreenController; these show whether they
// arrive as a different global button or are rejected by the row's mapping.
constexpr uint selectRow = 2196830738u, tradeRow = 4096534505u;
std::string modifiers() {
    constexpr int down = 0x8000;
    return std::format("shift={} ctrl={} alt={}", bool(GetAsyncKeyState(VK_SHIFT) & down),
        bool(GetAsyncKeyState(VK_CONTROL) & down), bool(GetAsyncKeyState(VK_MENU) & down));
}
bool rowComponent(InputComponent const& component) {
    for (auto const& mapping : component.mButtonMappings.get())
        if (mapping.toButtonId == tradeRow || mapping.toButtonId == selectRow) return true;
    return false;
}
std::string mappingText(ScreenButtonMapping const& mapping) {
    return std::format("{}->{} type={} scope={} consume={} select={} deselect={} alt={} ignore={} mode={}",
        uint(mapping.fromButtonId), uint(mapping.toButtonId), int(mapping.type), int(mapping.scope),
        bool(mapping.consumeEvent), bool(mapping.handleSelection), bool(mapping.handleDeselection),
        bool(mapping.alternateInputScope), bool(mapping.ignoreInputScope), int(mapping.inputModeCondition));
}
LL_TYPE_INSTANCE_HOOK(RepeatViewButton, ll::memory::HookPriority::Normal, ScreenView,
    &ScreenView::handleButtonEvent, void, uint id, ::ButtonState state, ::FocusImpact focus) {
    try {
        if (tradeScreen()) {
            observedAt = nowMs();
            log(viewBudget, 400, "view-button id={} state={} focus={} {}", id, int(state), int(focus), modifiers());
        }
    } catch (...) {}
    origin(id, state, focus);
}
LL_TYPE_INSTANCE_HOOK(RepeatComponentButton, ll::memory::HookPriority::Normal, InputComponent,
    &InputComponent::handleButtonEvent, ::ButtonHandleResult, ::VisualTree& tree, ::UIAnimationController& animation,
    ::ScreenInputContext& context, uint id, ::ButtonState state, ::glm::vec2 const& pointer, ::InputMode mode,
    bool tts) {
    bool row = false;
    try {
        row = tradeScreen() && rowComponent(*this);
        if (row) {
            observedAt = nowMs();
            std::string mappings;
            for (auto const& mapping : mButtonMappings.get()) mappings += " [" + mappingText(mapping) + "]";
            std::lock_guard lock{seenMutex};
            if (seen.size() < 200 && seen.insert("map" + mappings).second)
                log(componentBudget, 400, "row-mappings{}", mappings);
        }
    } catch (...) {}
    auto result = origin(tree, animation, context, id, state, pointer, mode, tts);
    try {
        if (row) log(componentBudget, 400, "row-button id={} state={} mode={} at={:.0f},{:.0f} result={} {}", id,
            int(state), int(mode), pointer.x, pointer.y, int(result), modifiers());
    } catch (...) {}
    return result;
}
LL_TYPE_INSTANCE_HOOK(RepeatPressedMapping, ll::memory::HookPriority::Normal, InputComponent,
    &InputComponent::_shouldHandlePressedMapping, bool, ::ScreenButtonMapping const& mapping, uint id,
    ::ButtonState state, ::glm::vec2 const& pointer, ::InputMode mode) {
    auto result = origin(mapping, id, state, pointer, mode);
    try {
        if ((mapping.toButtonId == tradeRow || mapping.toButtonId == selectRow) && tradeScreen())
            log(componentBudget, 400, "row-pressed-check id={} mapping=[{}] state={} result={} {}", id,
                mappingText(mapping), int(state), result, modifiers());
    } catch (...) {}
    return result;
}
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; bool optional = false; };
Hook hooks[] = {
    {RepeatOffer::hook, RepeatOffer::unhook}, {RepeatButton::hook, RepeatButton::unhook},
    {RepeatSlotPressed::hook, RepeatSlotPressed::unhook}, {RepeatTick::hook, RepeatTick::unhook},
    {RepeatLeave::hook, RepeatLeave::unhook}, {RepeatRequestBatch::hook, RepeatRequestBatch::unhook},
    {RepeatResponse::hook, RepeatResponse::unhook}, {RepeatInventorySlot::hook, RepeatInventorySlot::unhook},
    {RepeatInventoryContent::hook, RepeatInventoryContent::unhook},
    {RepeatNotify::hook, RepeatNotify::unhook}, {RepeatActorEvent::hook, RepeatActorEvent::unhook},
    {RepeatAutoPlace::hook, RepeatAutoPlace::unhook},
    {RepeatViewButton::hook, RepeatViewButton::unhook}, {RepeatComponentButton::hook, RepeatComponentButton::unhook},
    {RepeatPressedMapping::hook, RepeatPressedMapping::unhook, false, true}
};
ll::event::ListenerPtr mouseListener, keyListener;
}
void start() {
    try {
        for (auto& hook : hooks) if (!hook.installed) {
            if (hook.install(true) == 0) hook.installed = true;
            else if (hook.optional) Runtime::instance().self().getLogger().warn("L-166 optional trade diagnostic hook unavailable");
            else throw std::runtime_error("Could not install repeat trade diagnostics");
        }
        auto& bus = ll::event::EventBus::getInstance();
        if (!mouseListener) mouseListener = bus.emplaceListener<ll::event::input::MouseInputEvent>([](auto& event) {
            if (recent() && event.actionButtonId() != MouseAction::ActionMove
                && event.actionButtonId() != MouseAction::ActionMoveRelative)
                log(inputBudget, 150, "mouse button={} data={} cancelled={} {}",
                    int(event.actionButtonId()), int(event.buttonData()), event.isCancelled(), modifiers());
        });
        if (!keyListener) keyListener = bus.emplaceListener<ll::event::input::KeyInputEvent>([](auto& event) {
            if (recent() && (event.keyCode() == 0x10 || event.keyCode() == 0x11 || event.keyCode() == 0x1B
                || (event.keyCode() >= 0xA0 && event.keyCode() <= 0xA3)))
                log(inputBudget, 150, "key code={} down={} cancelled={}", event.keyCode(), event.isDown(), event.isCancelled());
        });
        if (!mouseListener || !keyListener) throw std::runtime_error("Could not subscribe repeat trade diagnostics");
        Runtime::instance().self().getLogger().warn("L-166 read-only trade diagnostics enabled");
    } catch (...) { stop(); throw; }
}
void stop() {
    auto& bus = ll::event::EventBus::getInstance();
    for (auto* listener : {&mouseListener, &keyListener}) {
        if (*listener) bus.removeListener(*listener);
        listener->reset();
    }
    for (auto it = std::rbegin(hooks); it != std::rend(hooks); ++it)
        if (it->installed && it->remove(true)) it->installed = false;
    for (auto const& hook : hooks) if (hook.installed) {
        Runtime::instance().self().getLogger().error("Could not remove repeat trade diagnostics");
        break;
    }
    observedAt = 0;
    std::lock_guard lock{seenMutex};
    seen.clear();
}
}
#else
namespace lamium::researchTrace::repeatTrade { void start() {} void stop() {} }
#endif
