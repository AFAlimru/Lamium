#pragma once
#include <cstdint>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace lamium::schematic {
struct UnreadablePlacement {
    std::uint64_t id = 0;
    std::string name;
};

class UnreadableNotice {
    std::uint64_t generation = 0;
    std::set<std::uint64_t> announced;
public:
    std::vector<std::string> update(std::uint64_t world, bool enabled, std::span<UnreadablePlacement const> placements) {
        if (generation != world) {
            generation = world;
            announced.clear();
        }
        std::vector<std::string> names;
        if (enabled) for (auto const& p : placements)
            if (announced.insert(p.id).second) names.push_back(p.name);
        return names;
    }
};
}
