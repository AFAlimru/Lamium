#pragma once
#ifdef LAMIUM_CTSPRITE_TRACE
// L-164 spike (trace builds only): generated sprites added to the terrain
// atlas: plain glass joined on all four sides, a bookshelf side joined left
// and right.
#include <optional>
struct TextureUVCoordinateSet;
namespace lamium::visuals::connected::sprites {
bool start();
enum class Kind { Glass, BookshelfSides };
// The sprite's place in the atlas, once the atlas holds it.
std::optional<TextureUVCoordinateSet> uv(Kind kind);
}
#endif
