#pragma once
#include "features/schematic/MenuModel.h"
#include "features/schematic/MoveTarget.h"
#include "input/Binding.h"
#include <optional>
#include <string>
class IClientInstance;
class MinecraftUIRenderContext;
// Keys that act on the selected schematic placement (BACKLOG L-93). Called on
// the client thread from the action dispatch, in gameplay only.
namespace lamium::schematic::actions {
bool handles(input::Action action);
void press(IClientInstance& client, input::Action action);

// The schematic menu's steppers and its commands that act in the world
// (screen commands and settings switches are the screen's). Client thread.
MoveTarget target();
void setTarget(menu::Target);
// A placement was selected or placed: movement goes back to it.
void placementSelected();
void step(IClientInstance& client, menu::Stepper stepper, int amount);
void run(IClientInstance& client, menu::Command command);
// The value an item shows under its name; empty when it has none.
std::string value(menu::Stepper);

// The adjust key: held, the wheel repeats the stepper used last in the menu.
void setAdjustHeld(bool held);
std::optional<menu::Stepper> lastStepper();
void startAdjust();
void stopAdjust();
// Applies wheel turns made while the adjust key is held and draws the hint
// under the crosshair. Called from the HUD each frame.
void adjustFrame(MinecraftUIRenderContext& context, float width, float height);
}
