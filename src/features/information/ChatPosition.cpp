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
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "ll/api/service/TargetedBedrock.h"
#include <Windows.h>
#include <memory>
#include <mutex>
#include <vector>

namespace lamium::information::chatPosition {
namespace {
bool installed = false;
struct Seen { float width, height, screenW, screenH; };
std::optional<Seen> seen;
std::mutex seenMutex;
// All references and writes live inside one render call, including on early exit.
struct Translation {
    struct Node { std::shared_ptr<UIControl> control; glm::vec2 position; };
    static constexpr size_t limit = 512;
    std::vector<Node> nodes;
    glm::vec2 relative{};
    bool moved = false;
    ~Translation() {
        if (!moved) return;
        nodes[0].control->mParentRelativePosition = relative;
        for (size_t i = 0; i < nodes.size(); ++i) {
            nodes[i].control->mCachedPosition = nodes[i].position;
            // Re-layout or animations during rendering may have changed a child.
            nodes[i].control->mCachedPositionDirty = true;
        }
    }
    void prepare(ScreenView& view) {
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
        auto panel = tree->getControlByName("chat_panel", true);
        if (!panel) return;
        auto parent = panel->mParent.lock();
        if (!parent || *parent->mName != "chat_stack" || parent->mClipsChildren || panel->mClipsChildren) return;
        // Gather completely before writing; an unexpected/oversized pack stays vanilla.
        nodes.reserve(limit);
        nodes.push_back({std::move(panel), {}});
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto const& children = nodes[i].control->mChildren.get();
            if (children.size() > limit - nodes.size()) return;
            for (auto const& child : children) {
                if (!child) return;
                nodes.push_back({child, {}});
            }
        }
        for (size_t i = 0; i < nodes.size(); ++i) {
            auto& control = *nodes[i].control;
            if (control.mCachedPositionDirty) control._setCachedPosition();
            auto position = *control.mCachedPosition;
            if (!std::isfinite(position.x) || !std::isfinite(position.y)) return;
            nodes[i].position = position;
        }
        auto& root = *nodes[0].control;
        glm::vec2 size = *root.mSize, screenSize = *view.mSize;
        auto box = placed(screenSize.x, screenSize.y, size.x, size.y, settings->hud.chat);
        if (!box) return;
        {
            std::lock_guard lock(seenMutex);
            seen = Seen{size.x, size.y, screenSize.x, screenSize.y};
        }
        glm::vec2 delta{box->x - nodes[0].position.x, box->y - nodes[0].position.y};
        relative = *root.mParentRelativePosition;
        moved = true;
        root.mParentRelativePosition = relative + delta;
        for (size_t i = 0; i < nodes.size(); ++i) {
            nodes[i].control->mCachedPosition = nodes[i].position + delta;
            nodes[i].control->mCachedPositionDirty = false;
        }
    }
};
}
LL_TYPE_INSTANCE_HOOK(ChatHudRender, ll::memory::HookPriority::Normal, ScreenView,
    &ScreenView::render, void, UIRenderContext& context) {
    Translation translation;
    try { translation.prepare(*this); } catch (...) {}
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
        if (!installed) Runtime::instance().self().getLogger().warn("Chat position cannot hook the HUD; chat stays vanilla");
    } catch (...) {}
}
void stop() {
    if (installed && ChatHudRender::unhook(true)) installed = false;
    std::lock_guard lock(seenMutex);
    seen.reset();
}
}
