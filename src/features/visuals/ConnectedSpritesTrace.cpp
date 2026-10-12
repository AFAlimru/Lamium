#ifdef LAMIUM_CTSPRITE_TRACE
// L-164 spike: generated sprites in the terrain atlas, so a connected face is
// drawn as vanilla's single quad. Sprites are baked like the split rules from
// the loaded texture. Round 3 (2026-10-12) worked for plain glass; round 4
// adds a bookshelf side joined left and right (Vibrant Visuals, L-156).
#include "features/visuals/ConnectedSpritesTrace.h"
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/texture/TextureAtlas.h"
#include "mc/client/renderer/texture/TextureAtlasResourceCallbacks.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core/resource/ResourceLocationPair.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/core_graphics/ImageDescription.h"
#include "mc/deps/core_graphics/TextureSetImageContainer.h"
#include "mc/deps/core_graphics/enums/TextureFormat.h"
#include "mc/deps/json/Value.h"
#include "mc/resources/ResourcePackManager.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace lamium::visuals::connected::sprites {
namespace {
// Border widths and the texel each refilled strip starts from, in 16ths.
struct Edges { int left = 0, right = 0, top = 0, bottom = 0, leftFrom = 0, rightFrom = 0, topFrom = 0, bottomFrom = 0; };
struct Sprite {
    char const* name;
    char const* base; // terrain texture_data key
    Edges edges;
};
// Glass: 2 texels on every side from texels 7-8. Bookshelf: the 1-texel side
// columns from the column next to them (as the split rules, L-96).
std::array<Sprite, 2> const table{{
    {"lamium_ct_glass_15", "glass", {2, 2, 2, 2, 7, 7, 7, 7}},
    {"lamium_ct_bookshelf_lr", "bookshelf", {1, 1, 0, 0, 1, 14, 0, 0}},
}};
std::string pathFor(Sprite const& sprite) { return std::string("textures/blocks/") + sprite.name; }

std::mutex mutex;
struct Made { std::string png; std::string basePath; };
std::array<Made, 2> made;                                 // guarded by mutex
std::atomic<int> generation{0};
int uvGeneration = -1;                                    // guarded by mutex
std::array<std::optional<TextureUVCoordinateSet>, 2> uvs; // guarded by mutex
std::atomic<int> packLogs{0};

void log(std::string const& text) {
    try { Runtime::instance().self().getLogger().info("CT sprite: {}", text); } catch (...) {}
}
std::string pathOf(ResourceLocation const& location) {
    try { return location.mPath->value; } catch (...) { return "?"; }
}
int sourceIndex(int i, int size, int low, int lowFrom, int high, int highFrom) {
    int a = size * low / 16, b = size * high / 16;
    if (i < a) return size * lowFrom / 16 + i;
    if (i >= size - b) return size * highFrom / 16 + (i - (size - b));
    return i;
}
std::string compose(cg::ImageBuffer const& source, Edges const& e, char const* name) {
    auto const& description = *source.mImageDescription;
    auto format = description.mTextureFormat;
    int width = static_cast<int>(description.mWidth), height = static_cast<int>(description.mHeight);
    auto const& storage = *source.mStorage;
    bool usable = (format == mce::TextureFormat::R8g8b8a8Unorm || format == mce::TextureFormat::R8g8b8a8UnormSrgb)
        && width >= 16 && height >= 16 && storage.size() >= static_cast<size_t>(width) * height * 4;
    log(std::format("{}: source format {} {}x{} -> {}", name, static_cast<unsigned>(format), width, height,
                    usable ? "composing" : "not usable"));
    if (!usable) return {};
    auto const* in = reinterpret_cast<std::uint32_t const*>(storage.data());
    std::vector<std::uint32_t> out(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            int sx = sourceIndex(x, width, e.left, e.leftFrom, e.right, e.rightFrom);
            int sy = sourceIndex(y, height, e.top, e.topFrom, e.bottom, e.bottomFrom);
            out[static_cast<size_t>(y) * width + x] = in[static_cast<size_t>(sy) * width + sx];
        }
    std::string png;
    stbi_write_png_to_func([](void* context, void* data, int size) { static_cast<std::string*>(context)->append(static_cast<char*>(data), size); },
                           &png, width, height, 4, out.data(), width * 4);
    return png;
}
std::string listOf(gsl::span<std::string const> extensions) {
    std::string out;
    for (auto const& e : extensions) out += (out.empty() ? "" : ",") + e;
    return out;
}
std::string const* pngExtension(gsl::span<std::string const> extensions) {
    for (auto const& e : extensions) if (e == ".png") return &e;
    return nullptr;
}
int spriteIndex(ResourceLocation const& location) {
    auto path = pathOf(location);
    for (size_t i = 0; i < table.size(); ++i) {
        auto mine = pathFor(table[i]);
        if (path == mine || path == mine + ".png") return static_cast<int>(i);
    }
    return -1;
}
std::string firstTexture(Json::Value const& entry) {
    auto const& textures = entry["textures"];
    if (textures.isString()) return textures.asString("");
    if (textures.isArray() && textures.size() > 0) {
        auto const& first = textures[0];
        if (first.isString()) return first.asString("");
        if (first.isObject()) return first["path"].asString("");
    }
    if (textures.isObject()) return textures["path"].asString("");
    return {};
}

LL_TYPE_INSTANCE_HOOK(SpriteAtlas, ll::memory::HookPriority::Normal, TextureAtlas, &TextureAtlas::_createAtlas,
    TextureAtlasResourceCallbacks, Json::Value const& root, std::function<void(TextureAtlasStatus const&)> status,
    std::function<void()> const finished, std::shared_ptr<mce::TextureGroup> group, cg::MipMapSupport const mip,
    ResourcePackManager& packs, std::shared_ptr<cg::ImageBuffer> outFinal,
    std::shared_ptr<std::unordered_map<ResourceLocation, cg::TextureSetImageContainer>> outSources) {
    std::string name;
    try { name = root["texture_name"].asString(""); } catch (...) {}
    if (name != "atlas.terrain" || !group)
        return origin(root, std::move(status), finished, std::move(group), mip, packs, std::move(outFinal), std::move(outSources));
    Json::Value copy(root);
    try {
        for (size_t i = 0; i < table.size(); ++i) {
            auto const& sprite = table[i];
            auto basePath = firstTexture(root["texture_data"][sprite.base]);
            std::string png;
            if (!basePath.empty())
                if (auto* image = group->getCachedImageOrLoadSync(ResourceLocation(Core::PathView(basePath)), false))
                    png = compose(*image, sprite.edges, sprite.name);
            // Whether the base has a PBR texture set the sprite lacks.
            std::array<std::string, 1> setExt{".texture_set.json"};
            auto set = packs.getPackStackIndexOfResource(ResourceLocation(Core::PathView(basePath)), setExt);
            log(std::format("{}: base '{}' texture set in pack {} ; png {} bytes", sprite.name, basePath, set.first, png.size()));
            {
                std::lock_guard lock(mutex);
                made[i] = {png, basePath};
            }
            if (!png.empty()) copy["texture_data"][sprite.name]["textures"] = Json::Value(pathFor(sprite));
        }
    } catch (...) { log("preparing sprites threw; atlas built unchanged"); copy = root; }
    ++generation;
    packLogs = 0;
    return origin(copy, std::move(status), finished, std::move(group), mip, packs, std::move(outFinal), std::move(outSources));
}
// The atlas asks which pack holds an image and gives up on -1 (round 2), then
// loads it through the pair overload (round 3). Image lookups are answered
// with the base texture's pack; texture set lookups stay -1.
void packLog(std::string const& text) {
    if (packLogs++ < 40) log(text);
}
Made madeCopy(int index) { std::lock_guard lock(mutex); return made[static_cast<size_t>(index)]; }
using PackIndex = std::pair<int, std::string_view>;
LL_TYPE_INSTANCE_HOOK(SpritePackIndex, ll::memory::HookPriority::Normal, ResourcePackManager,
    &ResourcePackManager::$getPackStackIndexOfResource, PackIndex, ResourceLocation const& location,
    gsl::span<std::string const> extensions) {
    auto result = origin(location, extensions);
    int index = spriteIndex(location);
    if (index < 0) return result;
    auto const* png = pngExtension(extensions);
    auto copy = madeCopy(index);
    if (result.first < 0 && png && !copy.png.empty()) {
        auto base = origin(ResourceLocation(Core::PathView(copy.basePath)), extensions);
        // The view must outlive the call: point it at the caller's own entry.
        if (base.first >= 0) result = {base.first, std::string_view(*png)};
    }
    packLog(std::format("{} lookup[{}] = {}", table[static_cast<size_t>(index)].name, listOf(extensions), result.first));
    return result;
}
using LoadPair = bool (ResourcePackManager::*)(ResourceLocationPair const&, std::string&, gsl::span<std::string const>) const;
LL_TYPE_INSTANCE_HOOK(SpritePackLoadPair, ll::memory::HookPriority::Normal, ResourcePackManager,
    static_cast<LoadPair>(&ResourcePackManager::$load), bool, ResourceLocationPair const& pair, std::string& stream,
    gsl::span<std::string const> extensions) {
    int index = spriteIndex(*pair.mResourceLocation);
    if (index >= 0 && pngExtension(extensions)) {
        auto copy = madeCopy(index);
        packLog(std::format("{} load[{}] -> {}", table[static_cast<size_t>(index)].name, listOf(extensions),
                            copy.png.empty() ? "nothing" : "png"));
        if (!copy.png.empty()) { stream = std::move(copy.png); return true; }
    }
    return origin(pair, stream, extensions);
}
bool installed = false;
}

std::optional<TextureUVCoordinateSet> uv(Kind kind) {
    std::lock_guard lock(mutex);
    int now = generation.load();
    if (uvGeneration != now) {
        uvGeneration = now;
        for (size_t i = 0; i < table.size(); ++i) {
            uvs[i].reset();
            try {
                auto sprite = BlockGraphics::getTextureUVCoordinateSet(table[i].name, 0, 0);
                auto base = BlockGraphics::getTextureUVCoordinateSet(table[i].base, 0, 0);
                bool distinct = sprite._u1 != sprite._u0 && (sprite._u0 != base._u0 || sprite._v0 != base._v0);
                log(std::format("{} uv ({:.5f},{:.5f}) base ({:.5f},{:.5f}) -> {}", table[i].name, sprite._u0, sprite._v0,
                                base._u0, base._v0, distinct ? "used" : "not in the atlas"));
                if (distinct) uvs[i] = sprite;
            } catch (...) { log("uv lookup threw"); }
        }
    }
    return uvs[static_cast<size_t>(kind)];
}

bool start() {
    if (installed) return true;
    if (SpriteAtlas::hook() != 0 || SpritePackIndex::hook() != 0 || SpritePackLoadPair::hook() != 0) {
        SpriteAtlas::unhook(); SpritePackIndex::unhook(); SpritePackLoadPair::unhook();
        log("hooks failed");
        return false;
    }
    installed = true;
    log("installed");
    return true;
}
}
#endif
