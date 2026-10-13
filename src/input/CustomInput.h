#pragma once
namespace lamium::input {
void startCustomInput();
void stopCustomInput();
void resetCustomInput();
// A press from Lamium's own controls (the title screen's entry), run like a key.
void queuePress(int action);
}
