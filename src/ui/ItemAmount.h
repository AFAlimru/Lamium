#pragma once
#include <cstdint>

// An item count as a player stores it (L-93 screen review, 2026-10-08):
// chests of 27 stacks, stacks, items. Shared by Schematic's materials and
// the calculator (L-171). Pure.
namespace lamium::ui {
struct ItemAmount {
    std::uint64_t chests = 0, stacks = 0, items = 0;
    bool operator==(ItemAmount const&) const = default;
};
inline constexpr int chestSlots = 27;
// Items that stack to less than 64 use their own stack size.
inline ItemAmount amountOf(std::uint64_t count, int maxStack) {
    std::uint64_t stack = maxStack > 0 ? static_cast<std::uint64_t>(maxStack) : 64;
    std::uint64_t stacks = count / stack;
    return {stacks / chestSlots, stacks % chestSlots, count % stack};
}
}
