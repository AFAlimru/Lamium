#pragma once
namespace lamium { struct Settings; }
namespace lamium::ui::paused_notice {
// Called every HUD frame. Requested affected features are explained on join
// and when shared failure state changes; unchanged results remain quiet.
void frame(Settings const&);
}
