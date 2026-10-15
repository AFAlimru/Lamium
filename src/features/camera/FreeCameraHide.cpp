#include "features/camera/FreeCameraHide.h"
#include "features/camera/CameraSessions.h"
#include "app/Runtime.h"
#include "app/Versions.h"
#include "app/CapabilityHooks.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/VisibilityFlag.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/game/ItemInHandRenderer.h"
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace lamium::camera::freeCameraHide {
namespace {
bool handInstalled = false, hudInstalled = false;
// The setting while FreeCamera flies; -1 otherwise (nothing hidden).
int hiding() noexcept {
    try {
        auto& runtime = Runtime::instance();
        if (!runtime.enabled() || !CameraSessions::instance().blocksPerspective()) return -1;
        return runtime.snapshot()->camera.freeCameraHide;
    } catch (...) { return -1; }
}
// FreeCamera always flies first person, so the hand is the first-person pass.
LL_TYPE_INSTANCE_HOOK(FreeCameraHand, ll::memory::HookPriority::Normal, ItemInHandRenderer,
    &ItemInHandRenderer::renderFirstPerson, void, BaseActorRenderContext& context, Matrix const& previous, ItemContextFlags flags) {
    if (hidesHand(hiding()) && capabilities::registry().status(capabilities::Id::FreeCameraHand).available()) return;
    origin(context, previous, flags);
}
bool hudScreen(ScreenView const& view) {
    auto const& tree = view.mVisualTree;
    return tree && std::string_view(*tree->mRootControlName).ends_with(".hud_screen");
}
// The whole HUD is not drawn; for the hotbar only, its control is invisible
// for this one draw and restored right after, so no game state outlives it.
LL_TYPE_INSTANCE_HOOK(FreeCameraHud, ll::memory::HookPriority::Normal, ScreenView, &ScreenView::render, void,
    UIRenderContext& context) {
    int setting = hiding();
    if (setting < 0 || !hudScreen(*this) || !capabilities::registry().status(capabilities::Id::FreeCameraHud).available()) return origin(context);
    if (hidesHud(setting)) return;
    if (!hidesHotbar(setting) || !versionSensitiveAllowed(capabilities::Id::FreeCameraHotbar)) return origin(context);
    std::vector<std::pair<std::shared_ptr<UIControl>, ::ui::VisibilityFlag>> hidden;
    for (auto name : {"desktop_hotbar", "pocket_hotbar", "hotbar_panel"})
        if (auto control = mVisualTree->getControlByName(name, true)) {
            hidden.push_back({control, static_cast<::ui::VisibilityFlag>(control->mVisible)});
            control->mVisible = ::ui::VisibilityFlag::None;
        }
    struct Restore {
        std::vector<std::pair<std::shared_ptr<UIControl>, ::ui::VisibilityFlag>>& list;
        ~Restore() { for (auto& [control, flag] : list) control->mVisible = flag; }
    } restore{hidden};
    origin(context);
}
}
bool hudHidden() noexcept {
    return hidesHud(hiding()) && capabilities::registry().status(capabilities::Id::FreeCameraHud).available();
}
void start() {
    for (auto id : {capabilities::Id::FreeCameraHand,capabilities::Id::FreeCameraHud,capabilities::Id::FreeCameraHotbar}) capabilities::begin(id);
    capabilities::attachHook<FreeCameraHand>(handInstalled,{capabilities::Id::FreeCameraHand});
    capabilities::attachHook<FreeCameraHud>(hudInstalled,{capabilities::Id::FreeCameraHud,capabilities::Id::FreeCameraHotbar});
    capabilities::hooks(capabilities::Id::FreeCameraHand,handInstalled);
    capabilities::hooks(capabilities::Id::FreeCameraHud,hudInstalled);
    capabilities::hooks(capabilities::Id::FreeCameraHotbar,hudInstalled);
    if (!handInstalled || !hudInstalled)
        Runtime::instance().self().getLogger().warn("FreeCamera cannot hide the hand or HUD; they stay drawn");
}
void stop() {
    for (auto id : {capabilities::Id::FreeCameraHand,capabilities::Id::FreeCameraHud,capabilities::Id::FreeCameraHotbar}) capabilities::stop(id);
    capabilities::detachHook<FreeCameraHud>(hudInstalled,{capabilities::Id::FreeCameraHud,capabilities::Id::FreeCameraHotbar});
    capabilities::detachHook<FreeCameraHand>(handInstalled,{capabilities::Id::FreeCameraHand});
}
}
