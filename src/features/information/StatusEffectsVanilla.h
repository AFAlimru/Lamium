#pragma once

namespace lamium::information::statusEffects {
// While the Status Effects HUD shows the local player's effects, the game's
// own effect icons on the right are not drawn (L-147). Set each frame by the
// HUD draw; vanilla stays drawn whenever the HUD could not read the effects.
void covers(bool covered);
void start() noexcept;
void stop();
}
