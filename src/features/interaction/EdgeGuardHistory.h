#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

class LocalPlayer;
struct IReplayableActorInput;

namespace lamium::interaction::edgeGuard::history {
bool onClientThread() noexcept;
bool walking(LocalPlayer&);
std::optional<std::array<size_t, 3>> inputSlots();
std::optional<std::array<std::uintptr_t, 3>> inputTargets(IReplayableActorInput&);
void observe() noexcept;
bool stop() noexcept;
}
