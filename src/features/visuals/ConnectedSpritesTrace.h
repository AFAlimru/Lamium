#pragma once
#ifdef LAMIUM_CTSPRITE_TRACE
// L-164 spike (trace builds only): a generated sprite for plain glass joined
// on all four sides, added to the terrain atlas.
#include <optional>
struct TextureUVCoordinateSet;
namespace lamium::visuals::connected::sprites {
bool start();
// The sprite's place in the atlas, once the atlas holds it.
std::optional<TextureUVCoordinateSet> joinedGlass();
}
#endif
