#pragma once
#include "ui/ItemAmount.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The Materials tab's amounts (L-93 screen review, 2026-10-08). Pure.
namespace lamium::schematic {
// Amounts as a player stores them, shared with the calculator (ui/ItemAmount.h).
using Amount = ui::ItemAmount;
using ui::amountOf;
using ui::chestSlots;
// ResourceCalculator takes the wanted items in the URL fragment, named like
// "oakplanks" (checked 2026-10-08). Lamium only links to the site; it never
// reads its results or uses its data (GPL-3.0; see SCHEMATIC.md).
inline std::string calculatorName(std::string_view item) {
    if (item.starts_with("minecraft:")) item.remove_prefix(10);
    // Bedrock ids that the site names differently.
    static constexpr std::pair<std::string_view, std::string_view> renamed[]{
        {"planks", "oakplanks"}, {"log", "oaklog"}, {"stonebrick", "stonebricks"}, {"wool", "whitewool"},
        {"concrete", "whiteconcrete"}, {"carpet", "whitecarpet"}, {"stained_glass", "whitestainedglass"}};
    for (auto [bedrock, site] : renamed) if (item == bedrock) return std::string(site);
    std::string out;
    for (char c : item) if (c != '_') out += c;
    return out;
}
inline std::string calculatorUrl(std::vector<std::pair<std::string, std::uint64_t>> const& items) {
    std::string url = "https://resourcecalculator.com/minecraft/#";
    bool first = true;
    for (auto const& [item, count] : items) {
        if (item.empty() || !count) continue;
        if (!first) url += '&';
        url += calculatorName(item) + "=" + std::to_string(count);
        first = false;
    }
    return url;
}
}
