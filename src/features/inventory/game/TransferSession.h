#pragma once

#include "features/inventory/transfer/TransferGesture.h"
#include <string>

class ContainerScreenController;

namespace lamium::inventory::game {
// Input callbacks only exchange owned values with this session. Container
// state and vanilla transfers are accessed from the UI/client thread in tick.
class TransferSession {
public:
    static bool mouseButton(int button, bool down, bool shift, bool control, bool alt, bool cancelled);
    static bool wheel(int direction, bool shift, bool control, bool cancelled);
    static void modifierReleased();
    static void cancel();
    static void slotHovered(ContainerScreenController& controller, std::string const& collection, int index);
    static void slotUnhovered(ContainerScreenController& controller, std::string const& collection, int index);
    static void tick(ContainerScreenController& controller);
};
}
