#pragma once
namespace lamium::visuals::serverPacks {
// Do not apply server resource packs (L-167). The client still downloads and
// accepts them as the server asks; only the stack the server then asks it to
// apply is left without them, so the world shows vanilla textures (a
// required server pack also turns off the player's global packs, as vanilla
// does). Remote servers only; local worlds keep their packs. Takes effect on
// the next join.
bool start();
void stop();
// Packs left out on the last join, once; 0 when none.
int takeSkipped();
}
