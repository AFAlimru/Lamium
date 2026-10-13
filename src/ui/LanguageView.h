#pragma once
#include <glm/vec2.hpp>
class MinecraftUIRenderContext;
namespace lamium::ui::language_view {
bool begin();
bool opened();
void reset();
void click(float x, float y);
void key(int key);
void wheel(int amount);
void release();
void render(MinecraftUIRenderContext&, glm::vec2 size, glm::vec2 pointer);
}
