#pragma once
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

// Line breaks for a row of key caps (L-235). Values are measured text widths;
// each cap adds its 6 px padding and caps that are not first on their line get
// a 6 px "+" gap. Pure so the wrap is tested without a renderer.
namespace lamium::ui {
inline std::vector<std::pair<std::size_t, std::size_t>> keycapLines(std::span<float const> textWidths, float width) {
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    std::size_t first = 0;
    while (first < textWidths.size()) {
        std::size_t end = first;
        float used = 0;
        while (end < textWidths.size()) {
            float need = textWidths[end] + 6 + (end == first ? 0 : 6);
            if (end == first) {
                used = need;
                ++end;
                // A cap wider than the line still gets its own line so the
                // caller advances instead of looping.
                if (used > width) break;
                continue;
            }
            if (used + need > width) break;
            used += need;
            ++end;
        }
        lines.emplace_back(first, end);
        first = end;
    }
    return lines;
}
}
