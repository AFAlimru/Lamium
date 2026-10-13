#pragma once
#include <string>
#include <string_view>
#include "input/Binding.h"
class IClientInstance;
namespace lamium {
inline bool gameplayScreen(std::string_view name) { return name.starts_with("hud_screen"); }
// The title screen and the other menus before a world (OreUI pages): the
// settings open there too (L-175). Loading screens are not OreUI pages.
inline bool outsideWorldMenu(std::string_view name, bool hasPlayer) { return !hasPlayer && name.starts_with("ui://"); }
void executeAction(IClientInstance& client, input::Action action);
void releaseAction(input::Action action);
std::string actionBindingName(IClientInstance& client, input::Action action);
std::string bindingChordName(IClientInstance& client, input::Chord const& chord);
// Session features (Zoom, Freelook, FreeCamera, Permanent Sneak/Sprint)
// have no saved switch: their settings switch shows and flips whether
// they are wanted (BACKLOG L-47).
bool isSessionFeature(std::string_view feature);
bool sessionState(std::string_view feature);
void toggleSession(IClientInstance& client, std::string_view feature);
}
