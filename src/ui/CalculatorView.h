#pragma once
// The calculator panel (BACKLOG L-171, docs/demos/calculator-v3.html "B"):
// two columns over the world, the field, result and a scientific key grid
// on the left, history cards on the right. The settings screen's scene owns
// focus and routes input here; nothing typed leaves the panel.
#include "ui/TextEdit.h"
#include <glm/vec2.hpp>
class MinecraftUIRenderContext;
namespace lamium::ui::calculator_view {
// A fresh line each time it opens; the history stays for the session.
void begin();
TextEdit& field();
// Native text, and what changed the field otherwise (L-172 keys).
void type(std::string const& text);
void edited();
enum class Result { None, Close };
// Enter keeps the line in the history; Up/Down go through it; Esc closes
// help first, then the panel.
Result key(int key);
// Ctrl+C: the value; Ctrl+Shift+C: "expression = value".
void copyValue();
void copyLine();
Result click(float x, float y, glm::vec2 size);
// The wheel over the history scrolls it. Wheel events carry no position, so
// the pointer as last drawn decides.
void wheel(int direction);
// The mouse button went up: a scrollbar drag ends.
void release();
void render(MinecraftUIRenderContext& context, glm::vec2 size, glm::vec2 pointer);
// Where the native keyboard's text box goes.
glm::vec2 fieldPosition(glm::vec2 size);
}
