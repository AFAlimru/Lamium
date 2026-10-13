#include "features/information/SaturationHud.h"
#include "features/information/Saturation.h"
#include "features/visuals/PackImage.h"
#include "app/Runtime.h"
#include "ui/Widgets.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/gui/controls/UIControl.h"
#include "mc/client/gui/controls/VisualTree.h"
#include "mc/client/gui/screens/ScreenView.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/core_graphics/ImageDescription.h"
#include "mc/deps/core_graphics/enums/TextureFormat.h"
#include "mc/world/actor/player/Player.h"
#include "mc/world/attribute/AttributeInstance.h"
#include "mc/world/attribute/AttributeInstanceConstRef.h"
#include "mc/world/item/Item.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/item/ItemStackBase.h"
#include "mc/world/item/components/IFoodItemComponent.h"
#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <string>

namespace lamium::information {
namespace {
namespace sat = saturation;
// The gold outlines are cut from the game's drumstick in memory and only ever
// uploaded as runtime textures; nothing is written to disk.
struct Outline {
    ResourceLocation const* location;
    sat::Part part;
    std::uint32_t color;
    bool uploaded = false;
};
ResourceLocation const& location(char const* path) {
    // Never destroyed: its destructor is game code, which must not run while
    // the process tears down after the game.
    return *new ResourceLocation(Core::PathView(path), ResourceFileSystem::Raw);
}
struct Outlines { Outline have, haveHalf, gain, gainRight, gainLeft; };
Outlines& outlines() {
    static Outlines value{{&location("lamium/saturation-full"), sat::Part::Whole, sat::gold},
                          {&location("lamium/saturation-half"), sat::Part::Right, sat::gold},
                          {&location("lamium/saturation-gain-full"), sat::Part::Whole, sat::paleGold},
                          {&location("lamium/saturation-gain-right"), sat::Part::Right, sat::paleGold},
                          {&location("lamium/saturation-gain-left"), sat::Part::Left, sat::paleGold}};
    return value;
}
bool failed = false;
void log(std::string const& text) {
    try { Runtime::instance().self().getLogger().info("Saturation: {}", text); } catch (...) {}
}
bool upload(IClientInstance& client, Outline& outline) {
    auto group = client.getTextureGroup();
    if (!group) return false;
    // The dark outline belongs to the background drawn under every drumstick;
    // hunger_full is only the filling on top of it.
    auto image = visuals::readPackImage(client.getResourcePackManager(), "textures/ui/hunger_background");
    if (!image || image->width <= 0 || image->height <= 0) {
        if (!failed) log("hunger_background could not be read from the packs; no outline");
        failed = true;
        return false;
    }
    int width = image->width, height = image->height;
    auto pixels = sat::outline(image->rgba.data(), width, height, outline.color, outline.part);
    mce::Image result(width, height, mce::ImageFormat::RGBA8Unorm, mce::ImageUsage::SRGB);
    result.mAlphaUsage = mce::AlphaUsage::Transparent;
    result.setRawImage(mce::Blob(reinterpret_cast<std::uint8_t const*>(pixels.data()), pixels.size() * sizeof(std::uint32_t)));
    group->uploadTexture(*outline.location, cg::ImageBuffer(std::move(result)));
    outline.uploaded = true;
    if (outline.part == sat::Part::Whole && outline.color == sat::gold)
        log(std::format("outline cut from a {}x{} drumstick", width, height));
    return true;
}
void drawOutline(MinecraftUIRenderContext& context, Outline& outline, ui::ImageRect rect, float opacity) {
    if (!outline.uploaded && (failed || !upload(context.mClient, outline))) return;
    // Resource reloads drop runtime textures; cut and upload again next time.
    if (!ui::runtimeImage(context, *outline.location, rect, opacity)) outline.uploaded = false;
}
std::optional<float> attribute(Player& player, Attribute const& which) {
    AttributeInstance const* instance = player.getAttribute(which).mPtr;
    if (!instance) return std::nullopt;
    float value = instance->mCurrentValue;
    return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
}
}
void drawSaturation(MinecraftUIRenderContext& context, ScreenView const& view, Settings::Information const& settings) {
    if (!settings.saturation) return;
    auto* player = context.mClient.getLocalPlayer();
    if (!player) return;
    auto hunger = attribute(*player, Player::HUNGER());
    auto saturation = attribute(*player, Player::SATURATION());
    if (!hunger || !saturation) return;
    // The game draws the bar from this control; without it (creative, a pack
    // that removed it) there is nothing to mark.
    VisualTree* tree = view.mVisualTree.get();
    if (!tree) return;
    auto control = tree->getControlByName("hunger_rend", true);
    if (!control || control->mCachedPositionDirty) return;
    // The control sits at the bar's right end; the rightmost drumstick starts
    // 8 units left of it and the rest follow 8 apart (one unit of overlap).
    glm::vec2 origin = *control->mCachedPosition;
    sat::Levels now{*hunger, *saturation};
    std::optional<sat::Levels> eaten;
    if (settings.saturationPreview) {
        ItemStack const& held = player->getSelectedItem();
        if (!held.isNull() && held.mItem)
            if (auto* food = held.mItem->getFood()) eaten = sat::afterEating(now, food->getNutrition(), food->getSaturationModifier());
    }
    auto& outline = outlines();
    // Half strength reads as "not eaten yet" without hiding the real icons
    // (compared at 30-70 % in game, 2026-10-02).
    constexpr float previewOpacity = .5f;
    // Hunger a held food would add: the game's own icons, translucent. The
    // saturation it would add is an opaque pale outline instead (below).
    if (eaten) {
        std::vector<ui::ImageRect> gainFull, gainHalf;
        for (int i = 0; i < sat::icons; ++i) {
            ui::ImageRect rect{origin.x - 8 - 8 * i, origin.y, 9, 9};
            auto before = sat::mark(i, now.hunger), after = sat::mark(i, eaten->hunger);
            if (after == sat::Mark::Full && before != sat::Mark::Full) gainFull.push_back(rect);
            else if (after == sat::Mark::Half && before == sat::Mark::None) gainHalf.push_back(rect);
        }
        ui::images(context, "textures/ui/hunger_full", gainFull, previewOpacity);
        ui::images(context, "textures/ui/hunger_half", gainHalf, previewOpacity);
    }
    for (int i = 0; i < sat::icons; ++i) {
        ui::ImageRect rect{origin.x - 8 - 8 * i, origin.y, 9, 9};
        auto have = sat::mark(i, now.saturation);
        if (have == sat::Mark::Full) drawOutline(context, outline.have, rect, 1);
        else if (have == sat::Mark::Half) drawOutline(context, outline.haveHalf, rect, 1);
        if (!eaten) continue;
        auto after = sat::mark(i, eaten->saturation);
        if (after == sat::Mark::Full && have == sat::Mark::None) drawOutline(context, outline.gain, rect, 1);
        else if (after == sat::Mark::Full && have == sat::Mark::Half) drawOutline(context, outline.gainLeft, rect, 1);
        else if (after == sat::Mark::Half && have == sat::Mark::None) drawOutline(context, outline.gainRight, rect, 1);
    }
}
}
namespace lamium::information {
void drawFoodIcons(MinecraftUIRenderContext& context, std::vector<saturation::FoodIcon> const& icons,
                   std::vector<ui::ImageRect> const& rects) {
    if (icons.size() != rects.size()) return;
    std::vector<ui::ImageRect> fulls, halves;
    for (size_t i = 0; i < icons.size(); ++i) {
        if (icons[i].hunger == sat::Mark::Full) fulls.push_back(rects[i]);
        else if (icons[i].hunger == sat::Mark::Half) halves.push_back(rects[i]);
    }
    ui::images(context, "textures/ui/hunger_background", rects);
    ui::images(context, "textures/ui/hunger_full", fulls);
    ui::images(context, "textures/ui/hunger_half", halves);
    auto& outline = outlines();
    for (size_t i = 0; i < icons.size(); ++i) {
        if (icons[i].saturation == sat::Mark::Full) drawOutline(context, outline.have, rects[i], 1);
        else if (icons[i].saturation == sat::Mark::Half) drawOutline(context, outline.haveHalf, rects[i], 1);
    }
}
}
