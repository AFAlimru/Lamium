#include "features/information/DebugVanillaHud.h"
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/gui/controls/SpriteComponent.h"
#include "mc/client/gui/controls/TextComponent.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/renderers/HudPlayerRenderer.h"
#include "settings/Settings.h"
#include <string>

namespace lamium::information::debugVanilla {
namespace {
bool installedSprite = false, installedText = false, installedDoll = false;
bool hiding() noexcept {
    try {
        auto& runtime = Runtime::instance();
        if (!runtime.enabled()) return false;
        auto settings = runtime.snapshot();
        return settings->information.debug && settings->information.debugHideVanilla;
    } catch (...) { return false; }
}
bool position(UIControl& owner) noexcept {
    try {
        std::string const& name = *owner.mName;
        return name.size() <= 32 && positionControl(name);
    } catch (...) { return false; }
}
}
LL_TYPE_INSTANCE_HOOK(DebugPositionSprite, ll::memory::HookPriority::Normal, SpriteComponent,
    &SpriteComponent::render, void, UIRenderContext& context) {
    if (position(mOwner) && hiding()) return;
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(DebugPositionText, ll::memory::HookPriority::Normal, TextComponent,
    &TextComponent::$render, void, UIRenderContext& context) {
    if (position(mOwner) && hiding()) return;
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(DebugPaperDoll, ll::memory::HookPriority::Normal, HudPlayerRenderer,
    &HudPlayerRenderer::$render, void, MinecraftUIRenderContext& context, IClientInstance& client, UIControl& owner,
    int pass) {
    if (hiding()) return;
    origin(context, client, owner, pass);
}
void start() noexcept {
    try {
        if (!installedSprite) installedSprite = DebugPositionSprite::hook(true) == 0;
        if (!installedText) installedText = DebugPositionText::hook(true) == 0;
        if (!installedDoll) installedDoll = DebugPaperDoll::hook(true) == 0;
        if (!installedSprite || !installedText || !installedDoll)
            Runtime::instance().self().getLogger().warn("Debug View cannot hide the game's position or paper doll; they stay drawn");
    } catch (...) {}
}
void stop() {
    if (installedDoll && DebugPaperDoll::unhook(true)) installedDoll = false;
    if (installedText && DebugPositionText::unhook(true)) installedText = false;
    if (installedSprite && DebugPositionSprite::unhook(true)) installedSprite = false;
}
}
