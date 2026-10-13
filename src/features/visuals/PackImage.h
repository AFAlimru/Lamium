#pragma once
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>
class ResourcePackManager;
namespace lamium::visuals {
// A texture file decoded from the current pack stack, RGBA8 rows.
struct PackImage {
    std::vector<std::uint8_t> rgba;
    int width = 0, height = 0;
};
// Read straight from the packs, never through the TextureGroup's image cache:
// an image loaded there by path is reused when the pack stack changes, so the
// terrain atlas kept the old pack's texture for every block Lamium had
// sampled (L-167, 2026-10-14).
std::optional<PackImage> readPackImage(ResourcePackManager& packs, std::string_view path);
}
