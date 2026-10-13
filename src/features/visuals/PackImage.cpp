#include "features/visuals/PackImage.h"
#include "mc/deps/core/file/PathView.h"
#include "mc/deps/core/resource/ResourceLocation.h"
#include "mc/platform/UUID.h"
#include "mc/resources/ResourcePackManager.h"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <array>
#include <cstring>
#include <string>

namespace lamium::visuals {
namespace {
std::array<std::string, 4> const imageExtensions{".png", ".tga", ".jpg", ".jpeg"};
}
std::optional<PackImage> readPackImage(ResourcePackManager& packs, std::string_view path) {
    std::string bytes;
    if (!packs.load(ResourceLocation(Core::PathView(std::string(path))), bytes, imageExtensions) || bytes.empty())
        return std::nullopt;
    int width = 0, height = 0, channels = 0;
    auto* decoded = stbi_load_from_memory(reinterpret_cast<stbi_uc const*>(bytes.data()), static_cast<int>(bytes.size()), &width,
                                          &height, &channels, 4);
    if (!decoded) return std::nullopt;
    PackImage out{std::vector<std::uint8_t>(static_cast<size_t>(width) * height * 4), width, height};
    std::memcpy(out.rgba.data(), decoded, out.rgba.size());
    stbi_image_free(decoded);
    return out;
}
}
