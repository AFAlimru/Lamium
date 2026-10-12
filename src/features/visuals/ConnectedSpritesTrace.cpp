#ifdef LAMIUM_CTSPRITE_TRACE
// L-164 spike: can Lamium add a generated sprite to the terrain atlas and draw
// a connected face as vanilla's single quad with it? One sprite only: plain
// glass joined on all four sides, baked like the split rule (2 texels on each
// side refilled from texels 7-8). Logs where the atlas asks for the image.
#include "features/visuals/ConnectedSpritesTrace.h"
#include "app/Runtime.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/renderer/TextureGroup.h"
#include "mc/client/renderer/block/BlockGraphics.h"
#include "mc/client/renderer/texture/TextureAtlas.h"
#include "mc/client/renderer/texture/TextureAtlasResourceCallbacks.h"
#include "mc/client/renderer/texture/TextureUVCoordinateSet.h"
#include "mc/deps/core/container/Blob.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/image/Image.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/deps/core_graphics/ImageBuffer.h"
#include "mc/deps/core_graphics/ImageDescription.h"
#include "mc/deps/core_graphics/TextureSetImageContainer.h"
#include "mc/deps/core_graphics/enums/TextureFormat.h"
#include "mc/deps/json/Value.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/deps/core/resource/ResourceLocationPair.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <atomic>
#include <cstdint>
#include <format>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace lamium::visuals::connected::sprites {
namespace {
std::string const spriteName = "lamium_ct_glass_15";
std::string const spritePath = "textures/blocks/lamium_ct_glass_15";
std::mutex mutex;
std::shared_ptr<cg::ImageBuffer> composed; // guarded by mutex
// The same pixels as RGBA rows and as a PNG file, for the pack loader.
std::vector<std::uint32_t> composedPixels; int composedWidth = 0, composedHeight = 0; std::string composedPng; // guarded by mutex
std::atomic<int> generation{0};
int uvGeneration = -1;                      // guarded by mutex
std::optional<TextureUVCoordinateSet> uvCache; // guarded by mutex
std::atomic<int> requestLogs{0};
std::atomic<int> packLogs{0};

void log(std::string const& text) {
    try { Runtime::instance().self().getLogger().info("CT sprite: {}", text); } catch (...) {}
}
std::string pathOf(ResourceLocation const& location) {
    try { return location.mPath->value; } catch (...) { return "?"; }
}
// The source column (row) a texel of the joined face shows: 2 texels at each
// edge come from texels 7-8; the rest stay. Scaled for larger textures.
int sourceIndex(int i, int size) {
    int edge = size * 2 / 16, from = size * 7 / 16;
    if (i < edge) return from + i;
    if (i >= size - edge) return from + (i - (size - edge));
    return i;
}
std::shared_ptr<cg::ImageBuffer> compose(cg::ImageBuffer const& source) {
    auto const& description = *source.mImageDescription;
    auto format = description.mTextureFormat;
    int width = static_cast<int>(description.mWidth), height = static_cast<int>(description.mHeight);
    auto const& storage = *source.mStorage;
    bool usable = (format == mce::TextureFormat::R8g8b8a8Unorm || format == mce::TextureFormat::R8g8b8a8UnormSrgb)
        && width >= 16 && height >= 16 && storage.size() >= static_cast<size_t>(width) * height * 4;
    log(std::format("glass image format {} {}x{} bytes {} -> {}", static_cast<unsigned>(format), width, height,
                    storage.size(), usable ? "composing" : "not usable"));
    if (!usable) return nullptr;
    auto const* in = reinterpret_cast<std::uint32_t const*>(storage.data());
    std::vector<std::uint32_t> out(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            out[static_cast<size_t>(y) * width + x] = in[static_cast<size_t>(sourceIndex(y, height)) * width + sourceIndex(x, width)];
    std::string png;
    stbi_write_png_to_func([](void* context, void* data, int size) { static_cast<std::string*>(context)->append(static_cast<char*>(data), size); },
                           &png, width, height, 4, out.data(), width * 4);
    mce::Image image(width, height, mce::ImageFormat::RGBA8Unorm, mce::ImageUsage::SRGB);
    image.mAlphaUsage = mce::AlphaUsage::Transparent;
    image.setRawImage(mce::Blob(reinterpret_cast<std::uint8_t const*>(out.data()), out.size() * sizeof(std::uint32_t)));
    {
        std::lock_guard lock(mutex);
        composedPixels = out;
        composedWidth = width;
        composedHeight = height;
        composedPng = png;
    }
    log(std::format("png {} bytes", png.size()));
    return std::make_shared<cg::ImageBuffer>(std::move(image));
}
std::string listOf(gsl::span<std::string const> extensions) {
    std::string out;
    for (auto const& e : extensions) out += (out.empty() ? "" : ",") + e;
    return out;
}
bool imageExtensions(gsl::span<std::string const> extensions) {
    for (auto const& e : extensions) if (e == ".png") return true;
    return false;
}
bool isSprite(ResourceLocation const& location) {
    auto path = pathOf(location);
    return path == spritePath || path == spritePath + ".png";
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
    if (name != "atlas.terrain" || !group) {
        log(std::format("atlas '{}' passed through", name));
        return origin(root, std::move(status), finished, std::move(group), mip, packs, std::move(outFinal), std::move(outSources));
    }
    Json::Value copy(root);
    try {
        auto glassPath = firstTexture(root["texture_data"]["glass"]);
        log(std::format("terrain atlas: glass texture '{}', {} entries", glassPath, root["texture_data"].size()));
        std::shared_ptr<cg::ImageBuffer> made;
        if (!glassPath.empty())
            if (auto* glass = group->getCachedImageOrLoadSync(ResourceLocation(Core::PathView(glassPath)), false)) made = compose(*glass);
        if (made) {
            {
                // An earlier image may still be held by the game; keep it alive.
                std::lock_guard lock(mutex);
                static std::vector<std::shared_ptr<cg::ImageBuffer>> retired;
                if (composed) retired.push_back(composed);
                composed = made;
            }
            copy["texture_data"][spriteName]["textures"] = Json::Value(spritePath);
            log(std::format("added '{}' -> '{}'; the image is served when the atlas asks for it", spriteName, spritePath));
        }
    } catch (...) { log("preparing the sprite threw; atlas built unchanged"); copy = root; }
    ++generation;
    requestLogs = 0;
    packLogs = 0;
    return origin(copy, std::move(status), finished, std::move(group), mip, packs, std::move(outFinal), std::move(outSources));
}
// Where the atlas build asks for images: logs the sprite's requests (and a few
// others for comparison) and serves the sprite if the cache lost it.
LL_TYPE_INSTANCE_HOOK(SpriteImageSync, ll::memory::HookPriority::Normal, mce::TextureGroup,
    &mce::TextureGroup::getCachedImageOrLoadSync, cg::ImageBuffer*, ResourceLocation const& location, bool forceReload) {
    auto path = pathOf(location);
    bool ours = path == spritePath;
    auto* result = origin(location, forceReload);
    // The cache map's key order is the game's own, so Lamium does not insert
    // into it; the composed image stays owned here and is handed out as is.
    if (ours && !result) {
        std::lock_guard lock(mutex);
        result = composed.get();
    }
    if ((ours || path.find("glass") != std::string::npos) && requestLogs++ < 40)
        log(std::format("getCachedImageOrLoadSync '{}' fs {} -> {}", path, static_cast<int>(location.mFileSystem),
                        result ? "image" : "null"));
    return result;
}
// The atlas reads its source images from the resource packs, not the texture
// group cache (round 1, 2026-10-12: the sprite got its place but showed the
// missing texture). Serve the composed image where the packs are asked.
void packLog(std::string const& what, ResourceLocation const& location, bool served) {
    if (packLogs++ < 60) log(std::format("{} '{}' -> {}", what, pathOf(location), served ? "served" : "not served"));
}
std::string pngCopy() { std::lock_guard lock(mutex); return composedPng; }
using LoadPlain = bool (ResourcePackManager::*)(ResourceLocation const&, std::string&) const;
LL_TYPE_INSTANCE_HOOK(SpritePackLoad, ll::memory::HookPriority::Normal, ResourcePackManager,
    static_cast<LoadPlain>(&ResourcePackManager::$load), bool, ResourceLocation const& location, std::string& stream) {
    if (isSprite(location)) {
        auto png = pngCopy();
        packLog("load", location, !png.empty());
        if (!png.empty()) { stream = std::move(png); return true; }
    }
    return origin(location, stream);
}
using LoadExt = bool (ResourcePackManager::*)(ResourceLocation const&, std::string&, gsl::span<std::string const>) const;
LL_TYPE_INSTANCE_HOOK(SpritePackLoadExt, ll::memory::HookPriority::Normal, ResourcePackManager,
    static_cast<LoadExt>(&ResourcePackManager::$load), bool, ResourceLocation const& location, std::string& stream,
    gsl::span<std::string const> extensions) {
    if (isSprite(location)) {
        auto png = imageExtensions(extensions) ? pngCopy() : std::string{};
        packLog(std::format("load+ext[{}]", listOf(extensions)), location, !png.empty());
        if (!png.empty()) { stream = std::move(png); return true; }
    }
    return origin(location, stream, extensions);
}
using LoadPair = bool (ResourcePackManager::*)(ResourceLocationPair const&, std::string&, gsl::span<std::string const>) const;
LL_TYPE_INSTANCE_HOOK(SpritePackLoadPair, ll::memory::HookPriority::Normal, ResourcePackManager,
    static_cast<LoadPair>(&ResourcePackManager::$load), bool, ResourceLocationPair const& pair, std::string& stream,
    gsl::span<std::string const> extensions) {
    auto const& location = *pair.mResourceLocation;
    if (isSprite(location)) {
        auto png = imageExtensions(extensions) ? pngCopy() : std::string{};
        packLog(std::format("load pair[{}]", listOf(extensions)), location, !png.empty());
        if (!png.empty()) { stream = std::move(png); return true; }
    }
    return origin(pair, stream, extensions);
}
LL_TYPE_INSTANCE_HOOK(SpritePackTexture, ll::memory::HookPriority::Normal, ResourcePackManager,
    &ResourcePackManager::$loadTexture, mce::Image, ResourceLocation const& location) {
    if (isSprite(location)) {
        std::lock_guard lock(mutex);
        packLog("loadTexture", location, !composedPixels.empty());
        if (!composedPixels.empty()) {
            mce::Image image(composedWidth, composedHeight, mce::ImageFormat::RGBA8Unorm, mce::ImageUsage::SRGB);
            image.mAlphaUsage = mce::AlphaUsage::Transparent;
            image.setRawImage(mce::Blob(reinterpret_cast<std::uint8_t const*>(composedPixels.data()),
                                        composedPixels.size() * sizeof(std::uint32_t)));
            return image;
        }
    }
    return origin(location);
}
using PackIndex = std::pair<int, std::string_view>;
LL_TYPE_INSTANCE_HOOK(SpritePackIndex, ll::memory::HookPriority::Normal, ResourcePackManager,
    &ResourcePackManager::$getPackStackIndexOfResource, PackIndex, ResourceLocation const& location,
    gsl::span<std::string const> extensions) {
    auto result = origin(location, extensions);
    // Round 2: the atlas asks where the image is first and gives up on -1.
    // Answer with plain glass's place for image lookups only (no PBR files).
    if (isSprite(location)) {
        bool answered = false;
        if (result.first < 0 && imageExtensions(extensions) && !pngCopy().empty()) {
            result = origin(ResourceLocation(Core::PathView("textures/blocks/glass")), extensions);
            answered = result.first >= 0;
        }
        packLog(std::format("packStackIndex[{}] = {} '{}'", listOf(extensions), result.first, result.second), location, answered);
    }
    return result;
}
using PathExt = Core::PathBuffer<std::string> (ResourcePackManager::*)(ResourceLocation const&, gsl::span<std::string const>) const;
LL_TYPE_INSTANCE_HOOK(SpritePackPath, ll::memory::HookPriority::Normal, ResourcePackManager,
    static_cast<PathExt>(&ResourcePackManager::$getPath), Core::PathBuffer<std::string>, ResourceLocation const& location,
    gsl::span<std::string const> extensions) {
    auto result = origin(location, extensions);
    if (isSprite(location)) packLog(std::format("getPath[{}] = '{}'", listOf(extensions), result.value), location, false);
    return result;
}
using StreamExt = bool (ResourcePackManager::*)(ResourceLocation const&, gsl::span<std::string const>) const;
LL_TYPE_INSTANCE_HOOK(SpritePackStream, ll::memory::HookPriority::Normal, ResourcePackManager,
    static_cast<StreamExt>(&ResourcePackManager::$isInStreamableLocation), bool, ResourceLocation const& location,
    gsl::span<std::string const> extensions) {
    auto result = origin(location, extensions);
    if (isSprite(location)) packLog(std::format("streamable[{}] = {}", listOf(extensions), result), location, false);
    return result;
}
bool installed = false;
}

std::optional<TextureUVCoordinateSet> joinedGlass() {
    std::lock_guard lock(mutex);
    int now = generation.load();
    if (uvGeneration != now) {
        uvGeneration = now;
        uvCache.reset();
        try {
            auto sprite = BlockGraphics::getTextureUVCoordinateSet(spriteName, 0, 0);
            auto glass = BlockGraphics::getTextureUVCoordinateSet("glass", 0, 0);
            log(std::format("uv sprite ({:.5f},{:.5f})-({:.5f},{:.5f}) {}x{}; glass ({:.5f},{:.5f})-({:.5f},{:.5f}) {}x{}",
                            sprite._u0, sprite._v0, sprite._u1, sprite._v1, sprite._sourceImageWidth, sprite._sourceImageHeight,
                            glass._u0, glass._v0, glass._u1, glass._v1, glass._sourceImageWidth, glass._sourceImageHeight));
            bool distinct = sprite._u1 != sprite._u0 && (sprite._u0 != glass._u0 || sprite._v0 != glass._v0);
            if (distinct) uvCache = sprite;
            else log("sprite has no own place in the atlas; faces fall back to the split rule");
        } catch (...) { log("uv lookup threw"); }
    }
    return uvCache;
}

bool start() {
    if (installed) return true;
    if (SpriteAtlas::hook() != 0 || SpriteImageSync::hook() != 0 || SpritePackLoad::hook() != 0 || SpritePackLoadExt::hook() != 0
        || SpritePackLoadPair::hook() != 0 || SpritePackTexture::hook() != 0 || SpritePackIndex::hook() != 0
        || SpritePackPath::hook() != 0 || SpritePackStream::hook() != 0) {
        SpritePackPath::unhook(); SpritePackStream::unhook();
        SpriteAtlas::unhook(); SpriteImageSync::unhook(); SpritePackLoad::unhook(); SpritePackLoadExt::unhook();
        SpritePackLoadPair::unhook(); SpritePackTexture::unhook(); SpritePackIndex::unhook();
        log("hooks failed");
        return false;
    }
    installed = true;
    log("installed; reload resource packs (or restart) so the terrain atlas is rebuilt with the sprite");
    return true;
}
}
#endif
