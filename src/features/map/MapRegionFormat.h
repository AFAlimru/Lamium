#pragma once
#include "features/map/MapRegion.h"
#include "features/map/MapSave.h"
#include <memory>
#include <bit>
#include <unordered_map>
#include <stdexcept>
#include <zstd.h>

namespace lamium::map {
inline constexpr size_t maxRegionBytes = 8 * 1024 * 1024;
inline std::uint32_t regionCrc(std::string_view bytes) {
    static constexpr auto table = [] {
        std::array<std::uint32_t, 256> values{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            auto c = i;
            for (int j = 0; j < 8; ++j) c = (c >> 1) ^ (c & 1 ? 0xEDB88320u : 0);
            values[i] = c;
        }
        return values;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (unsigned char c : bytes) crc = (crc >> 8) ^ table[(crc ^ c) & 255];
    return crc ^ 0xFFFFFFFFu;
}
namespace regionFormat {
inline void put(std::string& out, std::uint64_t value, int count) {
    for (int i = 0; i < count; ++i) out.push_back(static_cast<char>(value >> (8 * i)));
}
inline std::uint64_t get(std::string_view bytes, size_t at, int count) {
    std::uint64_t value = 0;
    for (int i = 0; i < count; ++i) value |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    return value;
}
inline bool known(std::string_view tag) { return tag == "CHNK" || tag == "COLR" || tag == "HGHT" || tag == "OLDF"; }
inline bool rawSize(std::string_view tag, size_t size) {
    return tag == "CHNK" ? size == 2560 : tag == "HGHT" ? size == 131072 : tag == "OLDF" ? size == 20
        : tag == "COLR" ? size >= 65544 && size <= 393220 : true;
}
}
// A worker owns and reuses these; no global codec state or game dependencies.
class RegionCodec {
    std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> compressor{ZSTD_createCCtx(), ZSTD_freeCCtx};
    std::unique_ptr<ZSTD_DCtx, decltype(&ZSTD_freeDCtx)> decompressor{ZSTD_createDCtx(), ZSTD_freeDCtx};
public:
    std::string compress(std::string_view raw) {
        if (!compressor) throw std::bad_alloc();
        std::string bytes(ZSTD_compressBound(raw.size()), '\0');
        auto n = ZSTD_compressCCtx(compressor.get(), bytes.data(), bytes.size(), raw.data(), raw.size(), 3);
        if (ZSTD_isError(n)) throw std::runtime_error(ZSTD_getErrorName(n));
        bytes.resize(n);
        return bytes;
    }
    std::optional<std::string> decode(std::string_view bytes, std::uint8_t codec, size_t size) {
        if (!codec) return bytes.size() == size ? std::optional<std::string>(bytes) : std::nullopt;
        if (codec != 1 || bytes.size() < 4 || regionFormat::get(bytes, 0, 4) != ZSTD_MAGICNUMBER
            || ZSTD_getDictID_fromFrame(bytes.data(), bytes.size()) != 0) return std::nullopt;
        auto n = ZSTD_findFrameCompressedSize(bytes.data(), bytes.size());
        auto content = ZSTD_getFrameContentSize(bytes.data(), bytes.size());
        if (ZSTD_isError(n) || n != bytes.size() || content == ZSTD_CONTENTSIZE_ERROR
            || (content != ZSTD_CONTENTSIZE_UNKNOWN && content != size)) return std::nullopt;
        if (!decompressor) throw std::bad_alloc();
        std::string raw(size, '\0');
        n = ZSTD_decompressDCtx(decompressor.get(), raw.data(), raw.size(), bytes.data(), bytes.size());
        if (ZSTD_isError(n) || n != size) return std::nullopt;
        return raw;
    }
};
enum class RegionStatus { Loaded, Missing, Damaged, Refused };
struct RegionRead {
    RegionStatus status = RegionStatus::Missing;
    RegionStatus legacyStatus = RegionStatus::Missing;
    std::optional<RegionData> data;
    bool needsSave = false;
    bool damaged = false;
    RegionRead(RegionStatus value = RegionStatus::Missing, std::optional<RegionData> region = std::nullopt)
        : status(value), data(std::move(region)) {}
};
namespace regionFormat {
struct Entry { std::string_view tag; std::uint8_t codec, flags; size_t raw, stored; std::uint32_t crc; };
struct Table {
    RegionStatus status;
    std::vector<Entry> entries;
    Table(RegionStatus value = RegionStatus::Loaded) : status(value) {}
};
inline Table inspect(std::string_view bytes, size_t fileSize, RegionKey key) {
    using namespace regionFormat;
    auto result = [](RegionStatus status) { return Table{status}; };
    if (fileSize > maxRegionBytes) return result(RegionStatus::Refused);
    if (bytes.size() < 4 || bytes.substr(0, 4) != "LMR2")
        return result(bytes.starts_with("LMR") ? RegionStatus::Refused : RegionStatus::Damaged);
    if (bytes.size() < 6 || !get(bytes, 4, 2)) return result(RegionStatus::Damaged);
    if (get(bytes, 4, 2) > 1) return result(RegionStatus::Refused);
    if (bytes.size() < 18) return result(RegionStatus::Damaged);
    if (get(bytes, 6, 2)) return result(RegionStatus::Refused);
    auto count = static_cast<size_t>(get(bytes, 16, 2));
    if (count > 64) return result(RegionStatus::Refused);
    if (bytes.size() < 18 + count * 18) return result(RegionStatus::Damaged);
    Table table;
    size_t total = 18 + count * 18, kept = 0;
    bool damaged = false;
    std::array<bool, 4> tags{};
    for (size_t i = 0; i < count; ++i) {
        size_t at = 18 + i * 18;
        Entry e{bytes.substr(at, 4), static_cast<std::uint8_t>(bytes[at + 4]), static_cast<std::uint8_t>(bytes[at + 5]),
                static_cast<size_t>(get(bytes, at + 6, 4)), static_cast<size_t>(get(bytes, at + 10, 4)),
                static_cast<std::uint32_t>(get(bytes, at + 14, 4))};
        bool isKnown = known(e.tag);
        if ((e.flags & 0xFC) || ((!isKnown || e.codec > 1) && (e.flags & 1))) return result(RegionStatus::Refused);
        if (!isKnown && (e.flags & 2)) {
            kept += e.stored;
            if (e.stored > 1024 * 1024 || kept > 2 * 1024 * 1024) return result(RegionStatus::Refused);
        }
        if (isKnown) {
            size_t tag = e.tag == "CHNK" ? 0 : e.tag == "COLR" ? 1 : e.tag == "HGHT" ? 2 : 3;
            damaged = damaged || tags[tag] || !rawSize(e.tag, e.raw);
            tags[tag] = true;
        }
        total += e.stored;
        table.entries.push_back(e);
    }
    if (damaged || total != fileSize
        || std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(bytes, 8, 4))) != key.x
        || std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(get(bytes, 12, 4))) != key.z)
        return result(RegionStatus::Damaged);
    return table;
}
}
inline RegionRead decodeRegion2(std::string_view bytes, RegionKey key, RegionCodec& codec) {
    using namespace regionFormat;
    auto table = inspect(bytes, bytes.size(), key);
    if (table.status != RegionStatus::Loaded) return {table.status};
    RegionData region;
    bool chunks = false, colors = false, heights = false;
    size_t at = 18 + table.entries.size() * 18;
    for (auto const& e : table.entries) {
        auto body = bytes.substr(at, e.stored);
        at += e.stored;
        if (regionCrc(body) != e.crc) continue;
        if (!known(e.tag)) {
            if (e.flags & 2) region.kept.push_back({std::string(e.tag), e.codec, e.flags,
                                                  static_cast<std::uint32_t>(e.raw), e.crc, std::string(body)});
            continue;
        }
        if (!e.stored) continue;
        auto decoded = codec.decode(body, e.codec, e.raw);
        if (!decoded) continue;
        std::string_view raw = *decoded;
        if (e.tag == "CHNK") {
            for (size_t i = 0; i < 256; ++i) region.chunks[i] = {get(raw, i * 10, 8), static_cast<std::uint16_t>(get(raw, i * 10 + 8, 2))};
            chunks = true;
        } else if (e.tag == "COLR") {
            size_t p = static_cast<size_t>(get(raw, 0, 4));
            if (!p || p > 65536 || raw.size() != 4 + 4 * p + 65536 * (p <= 256 ? 1 : 2)) continue;
            std::vector<std::uint32_t> palette;
            bool valid = true;
            for (size_t i = 0; i < p; ++i) {
                auto c = static_cast<std::uint32_t>(get(raw, 4 + 4 * i, 4));
                if (c && c >> 24 != 255) valid = false;
                palette.push_back(c);
            }
            if (!valid) continue;
            for (size_t i = 0; i < regionColumns; ++i) {
                size_t index = static_cast<unsigned char>(raw[4 + 4 * p + i]);
                if (p > 256) index |= size_t(static_cast<unsigned char>(raw[4 + 4 * p + 65536 + i])) << 8;
                if (index >= p) { valid = false; break; }
                region.colors[i] = palette[index];
            }
            colors = valid;
        } else if (e.tag == "HGHT") {
            for (size_t i = 0; i < regionColumns; ++i) {
                auto v = static_cast<std::uint16_t>(static_cast<unsigned char>(raw[i])
                    | (std::uint16_t(static_cast<unsigned char>(raw[65536 + i])) << 8));
                auto delta = static_cast<std::uint16_t>((v >> 1) ^ (v & 1 ? 0xFFFF : 0));
                auto prev = i % 256 ? region.heights[i - 1] : i >= 256 ? region.heights[i - 256] : 0;
                region.heights[i] = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(prev + delta));
            }
            heights = true;
        } else region.oldFile = OldFile{get(raw, 0, 8), get(raw, 8, 8), static_cast<std::uint32_t>(get(raw, 16, 4))};
    }
    if (!colors || !heights) return {RegionStatus::Damaged};
    if (!chunks) region.kept.clear();
    return {RegionStatus::Loaded, std::move(region)};
}
inline std::string encodeRegion2(RegionData const& region, RegionKey key, RegionCodec& codec) {
    using namespace regionFormat;
    auto count = 3 + (region.oldFile ? 1 : 0)
        + std::count_if(region.kept.begin(), region.kept.end(), [](auto const& s) { return s.flags & 2; });
    if (count > 64) throw RegionSaveError("Cannot preserve kept map sections within the 64-section limit");
    if (region.colors.size() != regionColumns || region.heights.size() != regionColumns)
        throw RegionSaveError("Invalid map region dimensions");
    std::vector<RegionSection> sections;
    auto add = [&](std::string tag, std::string raw, bool compressed = true) {
        auto bytes = compressed ? codec.compress(raw) : raw;
        sections.push_back({tag, static_cast<std::uint8_t>(compressed), static_cast<std::uint8_t>(tag == "OLDF" ? 0 : 1),
                            static_cast<std::uint32_t>(raw.size()), regionCrc(bytes), std::move(bytes)});
    };
    std::string raw;
    for (auto stamp : region.chunks) { put(raw, stamp.time, 8); put(raw, stamp.revision, 2); }
    add("CHNK", std::move(raw));
    std::unordered_map<std::uint32_t, std::uint16_t> lookup;
    std::vector<std::uint32_t> palette;
    std::vector<std::uint16_t> indices;
    for (auto color : region.colors) {
        if (color && color >> 24 != 255) throw RegionSaveError("Invalid map color");
        auto [it, added] = lookup.try_emplace(color, static_cast<std::uint16_t>(palette.size()));
        if (added) palette.push_back(color);
        indices.push_back(it->second);
    }
    raw.clear();
    put(raw, palette.size(), 4);
    for (auto c : palette) put(raw, c, 4);
    for (auto index : indices) put(raw, index & 255, 1);
    if (palette.size() > 256) for (auto index : indices) put(raw, index >> 8, 1);
    add("COLR", std::move(raw));
    raw.assign(131072, '\0');
    auto height = [&](size_t i) { return region.colors[i] ? static_cast<std::uint16_t>(region.heights[i]) : std::uint16_t(0); };
    for (size_t i = 0; i < regionColumns; ++i) {
        auto prev = i % 256 ? height(i - 1) : i >= 256 ? height(i - 256) : 0;
        auto delta = static_cast<std::uint16_t>(height(i) - prev);
        auto zigzag = static_cast<std::uint16_t>((delta << 1) ^ (delta & 0x8000 ? 0xFFFF : 0));
        raw[i] = static_cast<char>(zigzag & 255);
        raw[65536 + i] = static_cast<char>(zigzag >> 8);
    }
    add("HGHT", std::move(raw));
    if (region.oldFile) {
        raw.clear();
        put(raw, region.oldFile->size, 8); put(raw, region.oldFile->time, 8); put(raw, region.oldFile->crc, 4);
        add("OLDF", std::move(raw), false);
    }
    for (auto const& s : region.kept) if (s.flags & 2) sections.push_back(s);
    std::string out = "LMR2";
    put(out, 1, 2); put(out, 0, 2); put(out, static_cast<std::uint32_t>(key.x), 4);
    put(out, static_cast<std::uint32_t>(key.z), 4); put(out, sections.size(), 2);
    for (auto const& s : sections) {
        out += s.tag; put(out, s.codec, 1); put(out, s.flags, 1); put(out, s.rawSize, 4);
        put(out, s.bytes.size(), 4); put(out, s.crc, 4);
    }
    for (auto const& s : sections) out += s.bytes;
    if (out.size() > maxRegionBytes) throw RegionSaveError("Map region exceeds the 8 MiB file limit");
    return out;
}
}
