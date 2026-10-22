#pragma once
#include "features/map/MapRegionFormat.h"
#include "app/AtomicFile.h"
#include <chrono>
#include <filesystem>
#include <fstream>

namespace lamium::map {
// Shared by resident loads, image reads and idle migration.
inline std::filesystem::path legacyRegionPath(std::filesystem::path path) { return path.replace_extension(".lmr"); }
struct RegionFileBytes {
    RegionStatus status = RegionStatus::Missing;
    std::string bytes;
    std::optional<OldFile> facts;
    RegionFileBytes(RegionStatus value = RegionStatus::Missing, std::string contents = {}, std::optional<OldFile> fingerprint = {})
        : status(value), bytes(std::move(contents)), facts(fingerprint) {}
};
inline RegionStatus inspectRegionFile(std::filesystem::path const& path, RegionKey key) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) && !error) return RegionStatus::Missing;
    if (error || !std::filesystem::is_regular_file(path, error)) return RegionStatus::Refused;
    auto size = std::filesystem::file_size(path, error);
    if (error || size > maxRegionBytes) return RegionStatus::Refused;
    std::ifstream file(path, std::ios::binary);
    std::array<char, 18 + 64 * 18> header{};
    auto n = static_cast<std::streamsize>(std::min<std::uint64_t>(header.size(), size));
    if (!file.read(header.data(), n)) return RegionStatus::Refused;
    return regionFormat::inspect(std::string_view(header.data(), static_cast<size_t>(n)), static_cast<size_t>(size), key).status;
}
inline RegionFileBytes readRegionBytes(std::filesystem::path const& path, std::optional<RegionKey> key = {}) {
    std::error_code error;
    bool exists = std::filesystem::exists(path, error);
    if (!exists && !error) return {};
    // Unreadable is Refused: a permission error must not authorize replacement.
    if (error || !std::filesystem::is_regular_file(path, error)) return {RegionStatus::Refused};
    auto size = std::filesystem::file_size(path, error);
    if (error || size > maxRegionBytes) return {RegionStatus::Refused};
    auto time = std::filesystem::last_write_time(path, error);
    if (error) return {RegionStatus::Refused};
    std::ifstream file(path, std::ios::binary);
    if (!file) return {RegionStatus::Refused};
    if (key) {
        // Validate the complete table on the stack before allocating any body.
        std::array<char, 18 + 64 * 18> header{};
        auto n = static_cast<std::streamsize>(std::min<std::uint64_t>(header.size(), size));
        if (!file.read(header.data(), n)) return {RegionStatus::Refused};
        auto table = regionFormat::inspect(std::string_view(header.data(), static_cast<size_t>(n)), static_cast<size_t>(size), *key);
        if (table.status != RegionStatus::Loaded) return {table.status};
        file.seekg(0);
    }
    std::string bytes(static_cast<size_t>(size), '\0');
    if (!file.read(bytes.data(), static_cast<std::streamsize>(size)) || file.peek() != std::char_traits<char>::eof())
        return {RegionStatus::Refused};
    // Do not absorb a legacy file while another process is replacing it.
    if (std::filesystem::file_size(path, error) != size || error
        || std::filesystem::last_write_time(path, error) != time || error) return {RegionStatus::Refused};
    if (key) return {RegionStatus::Loaded, std::move(bytes)};
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::clock_cast<std::chrono::system_clock>(time).time_since_epoch()).count();
    OldFile facts{size, static_cast<std::uint64_t>(ms), regionCrc(bytes)};
    return {RegionStatus::Loaded, std::move(bytes), facts};
}
inline RegionRead readRegionFiles(std::filesystem::path const& path, RegionKey key, RegionCodec& codec) {
    auto file = readRegionBytes(path, key);
    RegionRead result{file.status};
    if (file.status == RegionStatus::Loaded) result = decodeRegion2(file.bytes, key, codec);
    if (result.status == RegionStatus::Refused) return result;
    result.damaged = result.status == RegionStatus::Damaged;
    auto old = readRegionBytes(legacyRegionPath(path));
    result.legacyStatus = old.status;
    if (old.status != RegionStatus::Loaded) return result;
    // A matching OLDF can be cleaned up even if its contents are not decoded.
    if (result.data && result.data->oldFile == old.facts) {
        result.needsSave = true;
        return result;
    }
    auto legacy = decodeRegion(old.bytes);
    if (!legacy) { result.legacyStatus = RegionStatus::Damaged; return result; }
    if (result.data) fillFromLegacy(*result.data, *legacy);
    else result.data = std::move(legacy);
    result.data->oldFile = old.facts;
    result.status = RegionStatus::Loaded;
    result.needsSave = true;
    return result;
}
inline bool deleteAbsorbedLegacy(std::filesystem::path const& path, std::optional<OldFile> const& absorbed) {
    if (!absorbed) return false;
    auto legacy = legacyRegionPath(path);
    auto current = readRegionBytes(legacy);
    if (current.status != RegionStatus::Loaded || current.facts != absorbed) return false;
    std::error_code error;
    return std::filesystem::remove(legacy, error);
}
inline void writeRegionFiles(std::filesystem::path const& path, RegionKey key, RegionData const& data, RegionCodec& codec) {
    // Recheck at the write path too, in case a future file arrived since load.
    if (inspectRegionFile(path, key) == RegionStatus::Refused)
        throw RegionSaveError("Refused map region cannot be overwritten");
    writeFileReplacing(path, encodeRegion2(data, key, codec), "map region");
    deleteAbsorbedLegacy(path, data.oldFile);
}
inline bool idleRegionEligible(bool resident, bool hasLegacy, RegionStatus status) {
    return !resident && hasLegacy && status != RegionStatus::Refused;
}
}
