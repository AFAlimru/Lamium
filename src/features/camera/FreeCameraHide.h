#pragma once
// What FreeCamera hides while it flies (BACKLOG L-191): nothing, the hand and
// held item, also the hotbar, or the whole HUD as F1 does. Vanilla returns as
// soon as FreeCamera ends; nothing is saved in the game.
namespace lamium::camera::freeCameraHide {
// The setting's steps, from Settings::Camera::freeCameraHide.
enum class Hide { Nothing = 0, Hand = 1, Hotbar = 2, Hud = 3 };
inline bool hidesHand(int setting) { return setting >= static_cast<int>(Hide::Hand); }
inline bool hidesHotbar(int setting) { return setting == static_cast<int>(Hide::Hotbar); }
inline bool hidesHud(int setting) { return setting >= static_cast<int>(Hide::Hud); }
// The whole HUD is hidden now: Lamium's own HUD treats it like F1.
bool hudHidden() noexcept;
void start();
void stop();
}
