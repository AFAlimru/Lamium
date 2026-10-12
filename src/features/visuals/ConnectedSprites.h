#pragma once
// Connected Textures sprites (BACKLOG L-164): when the terrain atlas is built,
// every texture a rule covers gets one sprite per joined combination, baked
// from the loaded image; the resource-pack loader serves them. Faces then
// shift their texture coordinates onto the sprite.
#include "features/visuals/ConnectedTextures.h"
#include <optional>
namespace lamium::visuals::connected::sprites {
bool start();
void stop();
// How far a face's texture coordinates move to show the sprite for `mask` in
// place of the tile whose low corner is (u, v); none when there is no sprite.
struct Shift { float u = 0, v = 0; };
std::optional<Shift> shift(float u, float v, Mask mask);
// Client thread: rebuild the lookup after a new atlas finished. True when
// it changed, so chunks should be rebuilt.
bool refresh();
}
