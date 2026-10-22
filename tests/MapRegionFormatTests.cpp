#include "features/map/MapRegionFiles.h"
#include "features/map/MapColors.h"
#include "features/map/MapCave.h"
#include "features/map/MapSessionFiles.h"
#include <chrono>
#include <fstream>
void check(bool, char const*);
namespace {
using namespace lamium::map;
using namespace lamium::map::regionFormat;
constexpr RegionKey key{-2, 3};
std::string read(std::filesystem::path const& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write(std::filesystem::path const& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    check(bool(out), "write test region");
}
std::vector<RegionSection> sections(std::string_view bytes) {
    std::vector<RegionSection> out;
    auto count = get(bytes, 16, 2);
    size_t body = 18 + static_cast<size_t>(count) * 18;
    for (size_t i = 0; i < count; ++i) {
        size_t at = 18 + i * 18, stored = static_cast<size_t>(get(bytes, at + 10, 4));
        out.push_back({std::string(bytes.substr(at, 4)), static_cast<std::uint8_t>(bytes[at + 4]),
                       static_cast<std::uint8_t>(bytes[at + 5]), static_cast<std::uint32_t>(get(bytes, at + 6, 4)),
                       static_cast<std::uint32_t>(get(bytes, at + 14, 4)), std::string(bytes.substr(body, stored))});
        body += stored;
    }
    return out;
}
std::string container(std::vector<RegionSection> const& sections) {
    std::string out = "LMR2";
    put(out, 1, 2); put(out, 0, 2); put(out, static_cast<std::uint32_t>(key.x), 4);
    put(out, key.z, 4); put(out, sections.size(), 2);
    for (auto const& s : sections) {
        out += s.tag; put(out, s.codec, 1); put(out, s.flags, 1); put(out, s.rawSize, 4);
        put(out, s.bytes.size(), 4); put(out, s.crc, 4);
    }
    for (auto const& s : sections) out += s.bytes;
    return out;
}
void setRaw(RegionSection& section, std::string bytes, int codec, RegionCodec& context) {
    section.rawSize = static_cast<std::uint32_t>(bytes.size());
    section.codec = static_cast<std::uint8_t>(codec);
    section.bytes = codec ? context.compress(bytes) : bytes;
    section.crc = regionCrc(section.bytes);
}
RegionData fixtureData() {
    RegionData data;
    for (size_t i = 0; i < regionColumns; ++i) {
        data.colors[i] = i % 17 ? 0xFF000000u | static_cast<std::uint32_t>((i * 7919) & 0xFFFFFF) : 0;
        data.heights[i] = data.colors[i] ? static_cast<std::int16_t>(int((i * 32767 + (i / 256) * 123) & 65535) - 32768) : 0;
    }
    for (size_t i = 0; i < 256; ++i) data.chunks[i] = {1700000000000ull + i * 12345, static_cast<std::uint16_t>(i % 4)};
    return data;
}
void roundTrips(RegionCodec& codec) {
    check(regionCrc("123456789") == 0xCBF43926, "IEEE CRC-32 check value");
    for (int count : {1, 256, 257, 65536}) {
        RegionData data;
        for (size_t i = 0; i < regionColumns; ++i) {
            data.colors[i] = 0xFF000000u | static_cast<std::uint32_t>(i % count);
            data.heights[i] = std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(i * 32767));
        }
        data.heights[0] = -32768; data.heights[1] = 32767; data.heights[2] = -32768;
        data.chunks[17] = {12345678901234, 42};
        data.oldFile = OldFile{555, 123456789, 0x12345678};
        auto bytes = encodeRegion2(data, key, codec);
        auto back = decodeRegion2(bytes, key, codec);
        check(back.status == RegionStatus::Loaded && back.data && back.data->colors == data.colors
                  && back.data->heights == data.heights && back.data->chunks == data.chunks && back.data->oldFile == data.oldFile,
              "LMR2 survives palette boundaries and every wrapping height delta");
        check(decodeRegion2(bytes, {0, 0}, codec).status == RegionStatus::Damaged, "region coordinates match the filename");
        auto parts = sections(bytes);
        auto rawHeight = codec.decode(parts[2].bytes, 1, 131072);
        auto h = static_cast<std::uint16_t>(data.heights[256]), north = static_cast<std::uint16_t>(data.heights[0]);
        auto delta = static_cast<std::uint16_t>(h - north);
        auto zigzag = static_cast<std::uint16_t>((delta << 1) ^ (delta & 32768 ? 65535 : 0));
        check(static_cast<unsigned char>((*rawHeight)[256]) == (zigzag & 255)
                  && static_cast<unsigned char>((*rawHeight)[65536 + 256]) == (zigzag >> 8), "row starts predict from north, in byte planes");
        auto oldRaw = codec.decode(parts[3].bytes, 0, 20);
        setRaw(parts[3], *oldRaw, 1, codec);
        back = decodeRegion2(container(parts), key, codec);
        check(back.data && back.data->oldFile == data.oldFile, "OLDF also accepts zstd");
    }
    RegionData blank;
    blank.heights[300] = 32767;
    auto back = decodeRegion2(encodeRegion2(blank, key, codec), key, codec);
    check(back.data && back.data->heights[300] == 0, "unrecorded heights are written as zero");
    auto fixture = fixtureData();
    back = decodeRegion2(read("tests/fixtures/lmr2/r.-2.3.lm2"), key, codec);
    check(back.data.has_value(), "independent Python golden file is readable in C++");
    check(back.data->colors == fixture.colors, "independent Python golden colors decode in C++");
    check(back.data->heights == fixture.heights, "independent Python golden heights decode in C++");
    check(back.data->chunks == fixture.chunks, "independent Python golden stamps decode in C++");
    write("build/lmr2-cpp.lm2", encodeRegion2(fixture, key, codec));
}
void malformed(RegionCodec& codec) {
    RegionData blank;
    blank.chunks[0] = {123456, 7};
    auto good = encodeRegion2(blank, key, codec);
    auto original = sections(good);
    auto status = [&](std::string_view bytes) { return decodeRegion2(bytes, key, codec).status; };
    check(status("LMR3") == RegionStatus::Refused && status("LMR") == RegionStatus::Refused
              && status("JUNK") == RegionStatus::Damaged && status("") == RegionStatus::Damaged, "magic classification");
    check(status(std::string("LMR2\2\0", 6)) == RegionStatus::Refused
              && status(std::string("LMR2\0\0", 6)) == RegionStatus::Damaged && status("LMR2") == RegionStatus::Damaged,
          "future version is refused before checking header length");
    auto bad = good; bad[6] = 1;
    check(status(bad) == RegionStatus::Refused, "unknown header flags are refused");
    bad = good; bad[16] = 65;
    check(status(bad) == RegionStatus::Refused, "section count bounded before table reads");
    check(status(good + "x") == RegionStatus::Damaged && status(good.substr(0, 22)) == RegionStatus::Damaged,
          "trailing bytes and truncated tables are damaged");
    check(status(std::string(maxRegionBytes + 1, 'x')) == RegionStatus::Refused, "oversized file is refused before reading magic");
    auto parts = original; parts.push_back(parts[0]);
    check(status(container(parts)) == RegionStatus::Damaged, "duplicate known sections are damaged");
    parts = original; parts[1].rawSize = 0xFFFFFFFF;
    check(status(container(parts)) == RegionStatus::Damaged, "known raw sizes bounded before allocation");
    bad = good; for (int i = 0; i < 4; ++i) bad[28 + i] = '\xFF';
    check(status(bad) == RegionStatus::Damaged, "huge stored size cannot read beyond the file");
    parts = original; parts[0].flags = 4;
    check(status(container(parts)) == RegionStatus::Refused, "unknown section flag bits are refused");
    parts = original; parts[1].codec = 99;
    check(status(container(parts)) == RegionStatus::Refused, "required section with an unknown codec is refused");
    parts = original; parts[1].crc ^= 1;
    check(status(container(parts)) == RegionStatus::Damaged, "COLR CRC failure is damaged");
    parts = original; parts[2].crc ^= 1;
    check(status(container(parts)) == RegionStatus::Damaged, "HGHT CRC failure is damaged");
    parts = original; parts[0].crc ^= 1;
    auto back = decodeRegion2(container(parts), key, codec);
    check(back.data && back.data->chunks[0] == ChunkStamp{}, "unreadable CHNK falls back to revision and time zero");
    parts = original; parts.erase(parts.begin());
    check(status(container(parts)) == RegionStatus::Loaded, "CHNK may be absent");
    for (bool trailing : {false, true}) {
        parts = original;
        parts[1].bytes += trailing ? "x" : parts[1].bytes;
        parts[1].crc = regionCrc(parts[1].bytes);
        check(status(container(parts)) == RegionStatus::Damaged, "zstd must be exactly one frame without trailing data");
    }
    parts = original; parts[1].bytes = codec.compress("wrong size"); parts[1].crc = regionCrc(parts[1].bytes);
    check(status(container(parts)) == RegionStatus::Damaged, "zstd content size must match raw size");
    auto colorRaw = *codec.decode(original[1].bytes, 1, original[1].rawSize);
    for (auto p : {0u, 65537u}) {
        auto raw = colorRaw;
        for (int j = 0; j < 4; ++j) raw[j] = static_cast<char>(p >> (8 * j));
        parts = original; setRaw(parts[1], raw, 0, codec);
        check(status(container(parts)) == RegionStatus::Damaged, "invalid palette size");
    }
    auto raw = colorRaw; raw[8] = 1;
    parts = original; setRaw(parts[1], raw, 0, codec);
    check(status(container(parts)) == RegionStatus::Damaged, "palette indices are checked");
    raw = colorRaw; raw[7] = 40;
    parts = original; setRaw(parts[1], raw, 0, codec);
    check(status(container(parts)) == RegionStatus::Damaged, "partial alpha is invalid");
    raw = colorRaw; raw[4] = 1;
    parts = original; setRaw(parts[1], raw, 0, codec);
    check(status(container(parts)) == RegionStatus::Damaged, "transparent colors must be exactly zero");
    for (auto tag : {std::string("test"), std::string("\xFF\0ab", 4)}) {
        RegionSection unknown{tag, 77, 2, 0xFFFFFFFF, regionCrc("opaque"), "opaque"};
        parts = original; parts.push_back(unknown); parts.push_back(unknown);
        back = decodeRegion2(container(parts), key, codec);
        check(back.data && back.data->kept.size() == 2, "unknown tags may repeat and are never decompressed");
        auto resaved = decodeRegion2(encodeRegion2(*back.data, key, codec), key, codec);
        check(resaved.data && resaved.data->kept[0].bytes == "opaque" && resaved.data->kept[0].codec == 77
                  && resaved.data->kept[0].rawSize == 0xFFFFFFFF, "kept sections survive unchanged");
        parts.back().flags = 1;
        check(status(container(parts)) == RegionStatus::Refused, "required unknown raw-byte tag is refused");
        parts.pop_back(); parts.back().flags = 0;
        check(decodeRegion2(container(parts), key, codec).data->kept.empty(), "unknown optional sections without keep are dropped");
        parts.back().flags = 2; parts.back().crc ^= 1;
        check(decodeRegion2(container(parts), key, codec).data->kept.empty(), "bad unknown CRC is dropped");
        parts.back().crc ^= 1; parts.erase(parts.begin());
        check(decodeRegion2(container(parts), key, codec).data->kept.empty(), "unknown kept sections require readable CHNK");
    }
    parts = original; parts.push_back({"TEST", 0, 2, 0, 0, std::string(1536 * 1024, 'x')});
    check(status(container(parts)) == RegionStatus::Refused, "kept section size is limited");
    parts = original;
    for (int i = 0; i < 3; ++i) parts.push_back({"TEST", 0, 2, 0, 0, std::string(900 * 1024, 'x')});
    check(status(container(parts)) == RegionStatus::Refused, "total kept section size is limited");
}
void revisions() {
    RegionData data;
    std::array<Column, 256> columns{};
    for (auto& c : columns) c = {packColor(10, 20, 30), 64};
    ColumnMask all{}, none{}, partial{};
    all.fill(true); partial[0] = true;
    auto change = recordChunk(data, {0, 0}, columns, all, all, 2, 100);
    check(change.pixels && change.metadata && data.chunks[0] == ChunkStamp{100, 2}, "full changed scan sets build revision and time");
    change = recordChunk(data, {0, 0}, columns, all, all, 1, 200);
    check(!change.pixels && !change.metadata && data.chunks[0] == ChunkStamp{100, 2}, "unchanged full scan keeps higher revision and time");
    change = recordChunk(data, {0, 0}, columns, partial, all, 1, 250);
    check(!change.pixels && change.metadata && data.chunks[0] == ChunkStamp{100, 1}, "partial older build lowers revision even without pixel changes");
    change = recordChunk(data, {0, 0}, columns, all, all, 2, 300);
    check(!change.pixels && change.metadata && data.chunks[0] == ChunkStamp{100, 2}, "unchanged full current scan raises revision without time change");
    columns[0].height = 65;
    recordChunk(data, {0, 0}, columns, none, partial, 2, 400);
    check(data.chunks[0] == ChunkStamp{400, 2}, "provisional-only change updates time and keeps min revision");
    recordChunk(data, {0, 0}, columns, none, none, 1, 500);
    check(data.chunks[0] == ChunkStamp{400, 2}, "no column scanned keeps metadata");
    RegionData fresh;
    recordChunk(fresh, {0, 0}, columns, partial, all, 2, 600);
    check(fresh.chunks[0] == ChunkStamp{600, 0}, "a new partial chunk has unknown revision");
    auto filled = columns;
    filled[0] = {};
    ColumnMask provisional{};
    auto produced = all; produced[0] = false;
    fillFromSaved(filled, provisional, columns);
    recordChunk(data, {0, 0}, filled, produced, produced, 1, 700);
    check(data.chunks[0].revision == 1, "saved fill of an unknown column cannot certify a full chunk");
    RegionData disk, live;
    recordChunk(disk, {0, 0}, columns, all, all, 1, 800);
    recordChunk(disk, {1, 0}, columns, all, all, 3, 810);
    std::vector<bool> recorded(regionColumns, false);
    std::array<Column, 256> one{}; one[0] = {packColor(90, 80, 70), 70};
    recordChunk(live, {0, 0}, one, partial, partial, 2, 900, &recorded);
    underlay(live, recorded, disk);
    check(live.colors[0] == one[0].color && live.colors[1] == columns[1].color
              && live.chunks[0] == ChunkStamp{900, 1} && live.chunks[1] == ChunkStamp{810, 3},
          "underlay lowers mixed chunk revision, keeps scanned columns and untouched disk stamps");
    recordChunk(live, {0, 0}, columns, all, all, 2, 1000);
    check(live.chunks[0] == ChunkStamp{1000, 2}, "full scan after underlay certifies the chunk");
    RegionData cave;
    std::array<Column, 256> caveColumns{};
    caveColumns.fill(caveColumn({CaveHit::Kind::Floor, 31}, 32, packColor(30, 60, 90)));
    recordChunk(cave, {0, 0}, caveColumns, all, all, 1, 1100);
    caveColumns[0] = caveColumn({CaveHit::Kind::Floor, 31}, 32, packColor(40, 70, 100));
    auto caveProduced = all;
    caveProduced[0] = false;
    recordChunk(cave, {0, 0}, caveColumns, caveProduced, all, columnRevision, 1200);
    check(cave.chunks[0].revision < columnRevision && cave.chunks[0].time == 1200,
          "cave fallback tint updates time while an older falsely certified chunk remains due");
    recordChunk(cave, {0, 0}, caveColumns, all, all, columnRevision, 1300);
    check(cave.chunks[0] == ChunkStamp{1200, columnRevision},
          "a ready unchanged cave scan certifies the current revision without changing time");
    ScanGrid grid; grid.follow({0, 0}, 30, false); grid.saw({25, 0}, Seen::Complete, 0, 0);
    grid.outdated({25, 0});
    auto due = grid.due(1, 0, 100, 16, 64);
    check(grid.find({25, 0})->seen == Seen::Partial && std::find(due.begin(), due.end(), ChunkKey{25, 0}) != due.end(),
          "old revisions become due even beyond the ordinary stale radius");
}
void migration(RegionCodec& codec) {
    auto folder = std::filesystem::temp_directory_path() / ("lamium-lmr2-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    struct Cleanup { std::filesystem::path folder; ~Cleanup() { std::error_code ignored; std::filesystem::remove_all(folder, ignored); } } cleanup{folder};
    auto path = folder / regionFileName(key), old = legacyRegionPath(path);
    RegionData legacy;
    legacy.colors[0] = packColor(10, 20, 30); legacy.heights[0] = 55;
    legacy.colors[1] = packColor(11, 22, 33); legacy.heights[1] = 56;
    write(old, encodeRegion(legacy));
    auto loaded = readRegionFiles(path, key, codec);
    check(loaded.status == RegionStatus::Loaded && loaded.needsSave && loaded.data->chunks[0] == ChunkStamp{}, "LMR1-only regions load once for migration");
    writeRegionFiles(path, key, *loaded.data, codec);
    check(!std::filesystem::exists(old) && std::filesystem::exists(path), "successful migration deletes only the absorbed legacy file");
    auto back = readRegionFiles(path, key, codec);
    check(back.data->oldFile && back.data->colors == legacy.colors && !back.needsSave, "migrated LMR2 stores OLDF and loads without another save");
    write(old, encodeRegion(legacy));
    auto facts = readRegionBytes(old).facts;
    auto current = *back.data;
    current.oldFile = facts; current.colors[0] = packColor(90, 80, 70); current.chunks[0] = {100, 2};
    write(path, encodeRegion2(current, key, codec));
    loaded = readRegionFiles(path, key, codec);
    check(loaded.data->colors[0] == current.colors[0] && loaded.data->chunks[0].revision == 2, "matching OLDF leftover is not merged");
    writeRegionFiles(path, key, *loaded.data, codec);
    check(!std::filesystem::exists(old), "interrupted-save leftover is removed");
    current.colors[1] = 0;
    current.chunks[1] = {150, 3}; current.colors[16] = packColor(50, 60, 70);
    legacy.colors[16] = current.colors[16];
    current.colors[32] = packColor(10, 11, 12); current.chunks[2] = {160, 4};
    write(path, encodeRegion2(current, key, codec));
    write(old, encodeRegion(legacy));
    loaded = readRegionFiles(path, key, codec);
    check(loaded.data->colors[0] == current.colors[0] && loaded.data->colors[1] == legacy.colors[1]
              && loaded.data->chunks[0] == ChunkStamp{100, 0} && loaded.data->chunks[1] == ChunkStamp{150, 3}
              && loaded.data->chunks[2] == ChunkStamp{160, 4}, "legacy merge fills only holes; only taken or conflicting chunks lose revision");
    auto absorbed = loaded.data->oldFile;
    legacy.colors[2] = packColor(99, 88, 77);
    write(old, encodeRegion(legacy));
    writeRegionFiles(path, key, *loaded.data, codec);
    check(std::filesystem::exists(old) && readRegionFiles(path, key, codec).needsSave, "legacy file changed after load survives the save");
    check(!deleteAbsorbedLegacy(path, absorbed), "deletion rechecks size time and CRC");
    write(path, std::string("LMR2\2\0", 6));
    auto future = read(path), oldBytes = read(old);
    loaded = readRegionFiles(path, key, codec);
    check(loaded.status == RegionStatus::Refused && !loaded.data, "future LMR2 blocks legacy fallback");
    bool refused = false;
    try { writeRegionFiles(path, key, current, codec); }
    catch (std::exception const& error) { refused = saveFailure(error) == RegionSaveResult::Permanent; }
    check(refused && read(path) == future && read(old) == oldBytes, "save never overwrites refused files or deletes their legacy data");
    write(path, "JUNK");
    loaded = readRegionFiles(path, key, codec);
    check(loaded.damaged && loaded.data && loaded.needsSave && loaded.data->colors[0] == legacy.colors[0], "damaged LMR2 falls back to LMR1");
    writeRegionFiles(path, key, *loaded.data, codec);
    check(readRegionFiles(path, key, codec).status == RegionStatus::Loaded && !std::filesystem::exists(old), "damaged LMR2 is replaced after migration");
    // A listing candidate is only a key: always reload both files at conversion time.
    write(old, encodeRegion(legacy));
    current.oldFile.reset(); current.colors[0] = packColor(100, 110, 120);
    write(path, encodeRegion2(current, key, codec));
    auto idle = readRegionFiles(path, key, codec);
    check(idleRegionEligible(false, idle.needsSave, idle.status) && !idleRegionEligible(true, true, RegionStatus::Loaded)
              && !idleRegionEligible(false, true, RegionStatus::Refused), "idle conversion skips residents and refused files");
    writeRegionFiles(path, key, *idle.data, codec);
    check(!std::filesystem::exists(old) && readRegionFiles(path, key, codec).data->colors[0] == current.colors[0],
          "idle conversion sees an LMR2 created after listing and merges without losing its colors");
    write(old, encodeRegion(legacy));
    auto pending = readRegionFiles(path, key, codec);
    auto beforeFailure = read(path);
    std::filesystem::permissions(path, std::filesystem::perms::owner_read, std::filesystem::perm_options::replace);
    bool replaceFailed = false;
    try { writeRegionFiles(path, key, *pending.data, codec); }
    catch (std::exception const& error) { replaceFailed = saveFailure(error) == RegionSaveResult::Transient; }
    std::filesystem::permissions(path, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
    check(replaceFailed && read(path) == beforeFailure && std::filesystem::exists(old),
          "failed atomic replacement preserves both formats");
    // A failed atomic write leaves the old map and legacy file available.
    auto blocked = folder / "blocked.lm2";
    std::filesystem::create_directory(blocked);
    write(legacyRegionPath(blocked), encodeRegion(legacy));
    bool failed = false;
    try { writeRegionFiles(blocked, key, current, codec); } catch (...) { failed = true; }
    check(failed && std::filesystem::exists(legacyRegionPath(blocked)), "failed write leaves legacy file untouched");
    auto invalidPath = folder / "invalid-legacy" / regionFileName(key);
    auto invalidOld = legacyRegionPath(invalidPath);
    auto invalidBytes = std::string("LMR1invalid");
    write(invalidPath, encodeRegion2(current, key, codec));
    write(invalidOld, invalidBytes);
    auto invalid = readRegionFiles(invalidPath, key, codec);
    check(invalid.status == RegionStatus::Loaded && !invalid.damaged && !invalid.needsSave
              && invalid.legacyStatus == RegionStatus::Damaged && invalid.data->colors == current.colors
              && invalid.data->oldFile == current.oldFile,
          "damaged legacy file does not mark a valid LMR2 damaged or schedule its replacement");
    writeRegionFiles(invalidPath, key, *invalid.data, codec);
    check(read(invalidOld) == invalidBytes, "unabsorbed damaged legacy data is kept unchanged after a save");
    write(invalidPath, "JUNK");
    invalid = readRegionFiles(invalidPath, key, codec);
    check(invalid.status == RegionStatus::Damaged && invalid.damaged && !invalid.data
              && invalid.legacyStatus == RegionStatus::Damaged && !invalid.needsSave,
          "damage in both formats is reported separately without scheduling an empty replacement");
    auto missingPath = folder / "legacy-only" / regionFileName(key);
    write(legacyRegionPath(missingPath), invalidBytes);
    invalid = readRegionFiles(missingPath, key, codec);
    check(invalid.status == RegionStatus::Missing && !invalid.damaged && !invalid.data
              && invalid.legacyStatus == RegionStatus::Damaged && !invalid.needsSave,
          "damaged legacy-only data stays distinct from an absent LMR2");
    auto unreadablePath = folder / "unreadable-legacy" / regionFileName(key);
    write(unreadablePath, encodeRegion2(current, key, codec));
    std::filesystem::create_directory(legacyRegionPath(unreadablePath));
    invalid = readRegionFiles(unreadablePath, key, codec);
    check(invalid.status == RegionStatus::Loaded && !invalid.damaged && !invalid.needsSave
              && invalid.legacyStatus == RegionStatus::Refused,
          "unreadable legacy path does not invalidate a readable LMR2");
    auto capacityPath = folder / "section-capacity" / regionFileName(key);
    RegionData capacity;
    capacity.kept.assign(61, RegionSection{"TEST", 0, 2, 1, regionCrc("x"), "x"});
    auto capacityBytes = encodeRegion2(capacity, key, codec);
    check(get(capacityBytes, 16, 2) == 64 && decodeRegion2(capacityBytes, key, codec).data->kept.size() == 61,
          "a valid 64-section container preserves all 61 unknown kept sections");
    write(capacityPath, capacityBytes);
    auto capacityOld = legacyRegionPath(capacityPath);
    auto capacityLegacy = encodeRegion(legacy);
    write(capacityOld, capacityLegacy);
    auto capacityLoad = readRegionFiles(capacityPath, key, codec);
    check(capacityLoad.needsSave && capacityLoad.data->kept.size() == 61 && capacityLoad.data->oldFile,
          "legacy merge retains kept data even when OLDF would require a 65th section");
    std::string capacityError;
    auto capacityResult = RegionSaveResult::Saved;
    try { writeRegionFiles(capacityPath, key, *capacityLoad.data, codec); }
    catch (std::exception const& error) { capacityError = error.what(); capacityResult = saveFailure(error); }
    check(capacityError.find("kept map sections") != std::string::npos && capacityError.find("64-section limit") != std::string::npos
              && read(capacityPath) == capacityBytes && read(capacityOld) == capacityLegacy
              && capacityResult == RegionSaveResult::Permanent,
          "section overflow explains preservation failure and leaves both files untouched");
    RegionSaveState state;
    state.changed(0);
    check(!state.due(9) && state.due(10) && state.begin() && !state.begin(), "save waits ten seconds and has one snapshot in flight");
    auto serial = state.serial;
    state.changed(11); state.complete(serial, RegionSaveResult::Saved, 12);
    check(state.dirty && !state.inFlight && !state.due(21.99) && state.due(22),
          "changes during a successful write stay dirty and wait a fresh save delay");
    state.begin(); serial = state.serial; state.complete(serial, RegionSaveResult::Transient, 12);
    check(state.dirty && !state.due(21.99) && state.due(22), "failed save retries after delay, never every frame");
    state.begin(); state.complete(state.serial, RegionSaveResult::Saved, 23);
    check(!state.dirty, "successful unchanged snapshot clears the dirty mark");
    RegionSaveState batching;
    batching.changed(0); batching.begin(); serial = batching.serial;
    batching.changed(1); batching.complete(serial, RegionSaveResult::Saved, 10);
    check(!batching.due(10) && batching.due(20), "successful in-flight changes do not cause immediate re-saves");
    for (int second = 11; second < 20; ++second) {
        batching.changed(second);
        check(!batching.due(second), "continuous edits share the next batch instead of writing every second");
    }
    check(batching.due(20), "continuous edits do not postpone the next batch indefinitely");
    batching.begin(); serial = batching.serial;
    batching.changed(20.5); batching.complete(serial, RegionSaveResult::Saved, 21);
    check(!batching.due(30.99) && batching.due(31), "each newer in-flight batch starts its own save delay");
    check(batching.begin(), "world exit can force pending changes to save before their delay");
    batching.complete(batching.serial, RegionSaveResult::Saved, 22);
    check(!batching.dirty, "forced final save clears only the snapshot it persisted");
    check(regionOfFileName("r.-2.3.lm2") == key && regionOfFileName("r.-2.3.lmr") == key
              && layerOfFolder("nether/y16") == MapLayer{1, 1, true}, "listing includes both formats and distinct Nether layers");
    RegionSaveState permanent;
    permanent.changed(0); permanent.begin();
    permanent.changed(1);
    permanent.complete(permanent.serial - 1, RegionSaveResult::Permanent, 2);
    permanent.changed(30);
    check(permanent.evictable() && !permanent.dirty && !permanent.due(1000) && !permanent.begin(),
          "a permanent failure discards pending save attempts and allows eviction even after more scans");
    RegionSaveState retrying;
    retrying.changed(0);
    double time = 10;
    for (double delay : {10., 20., 40., 80., 160., 300., 300.}) {
        check(retrying.begin(), "transient retry has a snapshot");
        retrying.complete(retrying.serial, RegionSaveResult::Transient, time);
        retrying.changed(time + 1);
        check(!retrying.evictable() && !retrying.due(time + delay - .01) && retrying.due(time + delay),
              "I/O retries double from ten seconds to five minutes and scans do not reset the delay");
        time += delay;
    }
    retrying.begin(); retrying.complete(retrying.serial, RegionSaveResult::Saved, time);
    retrying.changed(time + 1); retrying.begin();
    retrying.complete(retrying.serial, RegionSaveResult::Transient, time + 2);
    check(retrying.due(time + 12), "a successful save resets the I/O backoff");
    check(saveFailure(std::runtime_error("I/O")) == RegionSaveResult::Transient
              && saveFailure(RegionSaveError("invalid data")) == RegionSaveResult::Permanent,
          "save failure kind is explicit rather than inferred from a message");
    RegionData invalidData;
    invalidData.colors[0] = 1;
    auto invalidResult = RegionSaveResult::Saved;
    try { writeRegionFiles(path, key, invalidData, codec); }
    catch (std::exception const& error) { invalidResult = saveFailure(error); }
    check(invalidResult == RegionSaveResult::Permanent, "invalid region data is a permanent save failure");
    SessionMapFiles sessions;
    auto data = folder / "mod";
    write(data / "maps/.session/old/overworld/r.0.0.lm2", "leftover");
    write(data / "maps/saved/keep", "persistent");
    sessions.start(data);
    check(!std::filesystem::exists(data / "maps/.session") && read(data / "maps/saved/keep") == "persistent",
          "startup removes leftover session maps and preserves permanent maps");
    auto created = sessions.create();
    check(created.has_value(), "a session map folder is created");
    auto session = created.value_or(folder);
    auto region = session / "overworld" / regionFileName(key);
    writeRegionFiles(region, key, current, codec);
    check(readRegionFiles(region, key, codec).data->colors == current.colors,
          "session maps use ordinary region files so evicted residents can reload");
    check(!sessions.discard() && !std::filesystem::exists(session), "world exit deletes the session map");
    auto next = sessions.create();
    check(next && *next != session && std::filesystem::is_directory(*next), "each world gets a separate temporary map folder");
    SessionMapFiles unstarted;
    check(!unstarted.create(), "without a base folder the world stays in memory");
    sessions.discard();
}
}
void mapRegionFormatTests() {
    RegionCodec codec;
    roundTrips(codec);
    malformed(codec);
    revisions();
    migration(codec);
}
