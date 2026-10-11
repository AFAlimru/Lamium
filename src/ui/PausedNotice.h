#pragma once
namespace lamium { struct Settings; }
namespace lamium::ui::paused_notice {
// Called every HUD frame: on the first one after joining a world on an
// unverified game, a longer toast names the paused features in use (L-137).
void frame(Settings const&);
}
