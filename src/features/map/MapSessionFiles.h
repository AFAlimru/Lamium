#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace lamium::map {
// Temporary region files for a world without a map folder: far regions can
// leave memory and come back. Errors never throw; without a folder the
// world stays in memory as before.
class SessionMapFiles {
    std::filesystem::path base;
    std::optional<std::filesystem::path> active;
    unsigned next = 0;
public:
    void start(std::filesystem::path const& data) {
        base = data / "maps" / ".session";
        std::error_code ignored;
        std::filesystem::remove_all(base, ignored);
    }
    std::optional<std::filesystem::path> create() {
        if (base.empty()) return std::nullopt;
        auto path = base / std::to_string(++next);
        std::error_code error;
        std::filesystem::create_directories(path, error);
        if (error) return std::nullopt;
        active = path;
        return path;
    }
    std::error_code discard() {
        std::error_code error;
        if (active) std::filesystem::remove_all(*active, error);
        active.reset();
        return error;
    }
};
}
