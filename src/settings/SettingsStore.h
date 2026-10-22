#pragma once
#include "settings/Settings.h"
#include <filesystem>
#include <string_view>

namespace lamium {
Settings decodeSettings(std::string_view text);
// A file decodeSettings rejects is moved to a sibling .broken file before
// defaults are saved; a read error still throws.
Settings readSettings(std::filesystem::path const& path, bool* recovered = nullptr);
// Writes a complete sibling temporary file before replacing the destination.
// On error the existing file stays intact and an exception describes the error.
void writeSettings(std::filesystem::path const& path, Settings const& settings);
}
