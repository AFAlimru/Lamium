#include "features/information/ChatPosition.h"
#include "app/Runtime.h"
#include "app/Versions.h"
#include "ui/Localization.h"
#include "ui/Widgets.h"
#include "input/Actions.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/SpriteComponent.h"
#include "mc/client/gui/controls/TextComponent.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "ll/api/service/TargetedBedrock.h"
#include <Windows.h>
#include <memory>
#include <mutex>
#include <utility>

namespace lamium::information::chatPosition {
namespace {
bool installed = false, installedText = false, installedSprite = false;
struct Seen { float width, height, screenW, screenH; };
std::optional<Seen> seen;
std::mutex seenMutex;
struct Frame { float width, height; ui::HudElement element; };
thread_local Frame const* active = nullptr;
// No game references or control changes: layout stays entirely vanilla.
struct RenderScope {
    Frame const* previous = std::exchange(active, nullptr);
    std::optional<Frame> frame;
    ~RenderScope() { active = previous; }
    void prepare(ScreenView const& view) {
        if (!installedText || !installedSprite) return;
        auto& runtime = Runtime::instance();
        auto settings = runtime.snapshot();
        if (!runtime.enabled() || !settings->information.chatPosition) return;
        auto* tree = view.mVisualTree.get();
        if (!tree || !std::string_view(*tree->mRootControlName).ends_with(".hud_screen")) return;
        auto client = ll::service::getClientInstance();
        if (!client || !client->getLocalPlayer()) return;
        auto screen = client->getScreenName();
        if (!gameplayScreen(screen)) return;
        DWORD foregroundProcess = 0;
        GetWindowThreadProcessId(GetForegroundWindow(), &foregroundProcess);
        if (foregroundProcess != GetCurrentProcessId()) return;
        if (!versionSensitiveAllowed("Chat position (HUD control positions)")) return;
        glm::vec2 size = *view.mSize;
        frame = Frame{size.x, size.y, settings->hud.chat};
        active = &*frame;
    }
};
// A component render occurs after native layout. Restore its cache immediately,
// without invalidating the tree or overwriting positions computed by re-layout.
struct Translation {
    UIControl* owner = nullptr;
    glm::vec2 position{};
    ~Translation() { if (owner) owner->mCachedPosition = position; }
    void prepare(UIControl& control) {
        if (!active) return;
        std::string const& name = *control.mName;
        if (name != "chat_text" && name != "chat_background") return;
        auto panel = control.mParent.lock();
        for (int depth = 0; panel && depth < 16; ++depth) {
            if (*panel->mName == "chat_panel") break;
            if (panel->mClipsChildren) return;
            panel = panel->mParent.lock();
        }
        if (!panel || *panel->mName != "chat_panel" || panel->mClipsChildren) return;
        auto parent = panel->mParent.lock();
        if (!parent || *parent->mName != "chat_stack" || parent->mClipsChildren) return;
        if (panel->mCachedPositionDirty) panel->_setCachedPosition();
        if (control.mCachedPositionDirty) control._setCachedPosition();
        glm::vec2 at = *panel->mCachedPosition, size = *panel->mSize;
        position = *control.mCachedPosition;
        auto next = translated(active->width, active->height, {at.x, at.y, size.x, size.y},
                               {position.x, position.y}, active->element);
        if (!next) return;
        {
            std::lock_guard lock(seenMutex);
            seen = Seen{size.x, size.y, active->width, active->height};
        }
        owner = &control;
        control.mCachedPosition = glm::vec2{next->x, next->y};
    }
};
}
LL_TYPE_INSTANCE_HOOK(ChatHudRender, ll::memory::HookPriority::Normal, ScreenView,
    &ScreenView::render, void, UIRenderContext& context) {
    RenderScope scope;
    try { scope.prepare(*this); } catch (...) {}
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(ChatTextRender, ll::memory::HookPriority::Normal, TextComponent,
    &TextComponent::$render, void, UIRenderContext& context) {
    Translation translation;
    try { translation.prepare(mOwner); } catch (...) {}
    origin(context);
}
LL_TYPE_INSTANCE_HOOK(ChatBackgroundRender, ll::memory::HookPriority::Normal, SpriteComponent,
    &SpriteComponent::render, void, UIRenderContext& context) {
    Translation translation;
    try { translation.prepare(mOwner); } catch (...) {}
    origin(context);
}
std::optional<ui::hud_editor::Box> preview(MinecraftUIRenderContext& context, float width, float height,
                                         ui::HudElement const& element) {
    float w = width * .4f, h = std::min(27.f, height);
    {
        std::lock_guard lock(seenMutex);
        if (seen && seen->screenW == width && seen->screenH == height) {
            w = seen->width;
            h = std::max(h, seen->height);
        }
    }
    auto box = placed(width, height, w, h, element);
    if (!box) return {};
    // Sample only; the gameplay chat is still rendered by vanilla.
    int lines = std::min(3, static_cast<int>(box->h / 9));
    for (int i = 0; i < lines; ++i) {
        float y = box->y + box->h - (lines - i) * 9;
        ui::fill(context, box->x, y, box->w, 9, ui::palette::panel, .5f);
        ui::labelScaled(context, box->x + 2, y, std::max(0.f, box->w - 5), ui::translated("chatPosition.sample"),
                        1.f, ui::palette::text, ui::Align::Left, true);
    }
    context.flushText(0, std::nullopt);
    return box;
}
void start() noexcept {
    try {
        if (!installed) installed = ChatHudRender::hook(true) == 0;
        if (!installedText) installedText = ChatTextRender::hook(true) == 0;
        if (!installedSprite) installedSprite = ChatBackgroundRender::hook(true) == 0;
        if (!installed || !installedText || !installedSprite)
            Runtime::instance().self().getLogger().warn("Chat position cannot hook the HUD components; chat stays vanilla");
    } catch (...) {}
}
void stop() {
    if (installed && ChatHudRender::unhook(true)) installed = false;
    if (installedText && ChatTextRender::unhook(true)) installedText = false;
    if (installedSprite && ChatBackgroundRender::unhook(true)) installedSprite = false;
    std::lock_guard lock(seenMutex);
    seen.reset();
}
}
