#pragma once
#include <functional>
namespace lamium::clientTasks {
// Runs `task` on the client thread, in order: LeviLamina's client thread
// executor inside a world, the game's own update before a world is joined,
// where that executor does not run (L-175, 2026-10-14).
void post(std::function<void()> task);
bool start();
void stop();
}
