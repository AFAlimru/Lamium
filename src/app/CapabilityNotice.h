#pragma once
#include <string>
#include <utility>
namespace lamium::capabilities {
// Owned text only. Repeated frames and unrelated ready transitions stay quiet.
class Notice {
    unsigned world_ = 0, revision_ = 0;
    std::string previous;
public:
    bool poll(unsigned world, unsigned revision) {
        if (!world) { world_ = 0; previous.clear(); return false; }
        if (world == world_ && revision == revision_) return false;
        if (world != world_) previous.clear();
        world_ = world; revision_ = revision;
        return true;
    }
    bool changed(std::string text) {
        if (text == previous) return false;
        previous = std::move(text);
        return !previous.empty();
    }
};
}
