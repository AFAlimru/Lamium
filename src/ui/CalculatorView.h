#pragma once
// The calculator panel (BACKLOG L-171, docs/demos/calculator.html variant A):
// a small window near the top over gameplay. The settings screen's scene owns
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
// Enter keeps the line in the history; Up/Down recall it; Esc closes.
Result key(int key);
// Ctrl+C and the button: the result on the clipboard.
void copyResult();
void click(float x, float y, glm::vec2 size);
void render(MinecraftUIRenderContext& context, glm::vec2 size, glm::vec2 pointer);
// Where the native keyboard's text box goes.
glm::vec2 fieldPosition(glm::vec2 size);
}
