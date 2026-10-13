#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace lamium::interaction {
// clang-cl's local virtual-call thunks load the entry before jumping to it.
// Decode only the measured x64 forms; never assume an interface slot number.
inline std::optional<size_t> replayInputSlot(std::span<uint8_t const> code) {
    if (code.size() < 10 || code[0] != 0x48 || code[1] != 0x8b || code[2] != 0x01
        || code[3] != 0x48 || code[4] != 0x8b) return std::nullopt;
    uint32_t displacement = 0;
    size_t jump = 0;
    if (code[5] == 0x40) {
        displacement = code[6];
        jump = 7;
    } else if (code[5] == 0x80 && code.size() >= 13) {
        for (size_t i = 0; i < 4; ++i) displacement |= uint32_t{code[6 + i]} << (i * 8);
        jump = 10;
    } else return std::nullopt;
    if (code[jump] != 0x48 || code[jump + 1] != 0xff || code[jump + 2] != 0xe0
        || displacement % 8 || displacement == 0 || displacement > 32 * 8) return std::nullopt;
    return displacement / 8;
}
}
