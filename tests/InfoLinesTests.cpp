#include "features/information/InfoLines.h"
#include "settings/SettingsStore.h"
#include <limits>
void check(bool, char const*);
void infoLinesTests() {
    using namespace lamium;
    using namespace lamium::information;
    check(mergeLineOrder({}) == defaultLineOrder(), "empty order falls back to every line");
    check(mergeLineOrder({"ping", "coordinates"}).front() == "ping"
          && mergeLineOrder({"ping", "coordinates"})[1] == "realTime"
          && mergeLineOrder({"ping", "coordinates"}).size() == infoLineIds.size(),
          "stored order wins and missing lines fill in");
    {
        std::vector<std::string> old{"moon", "block", "coordinates"};
        auto merged = mergeLineOrder(old);
        auto at = [&](std::string_view id) { return std::find(merged.begin(), merged.end(), id) - merged.begin(); };
        check(merged[0] == "moon" && at("targetBlock") == at("block") + 1 && at("scaledCoordinates") == at("targetBlock") + 1
              && merged.size() == infoLineIds.size(),
              "a line added since the order was saved goes after its default neighbor, not to the end");
    }
    auto cleaned = mergeLineOrder({"nope", "fps", "fps"});
    check(std::count(cleaned.begin(), cleaned.end(), "fps") == 1 && cleaned.size() == infoLineIds.size()
          && std::find(cleaned.begin(), cleaned.end(), "nope") == cleaned.end(),
          "unknown ids drop out and duplicates collapse");
    check(decodeSettings("{}").information.lineOrder == defaultLineOrder(), "fresh settings list every line");
    auto moved = moveLineOrder({"a", "b", "c"}, "b", 1);
    check((moved == std::vector<std::string>{"a", "c", "b"}), "lines move down");
    moved = moveLineOrder(moved, "b", -1);
    check((moved == std::vector<std::string>{"a", "b", "c"}), "lines move back up");
    check(moveLineOrder({"a", "b"}, "a", -1) == std::vector<std::string>{"a", "b"}
          && moveLineOrder({"a", "b"}, "b", 1) == std::vector<std::string>{"a", "b"}
          && moveLineOrder({"a", "b"}, "nope", 1) == std::vector<std::string>{"a", "b"},
          "moves clamp at the ends and ignore unknown ids");
    auto stored = decodeSettings(R"({"information":{"lineOrder":["ping","nope"]}})");
    check(stored.information.lineOrder.front() == "ping" && stored.information.lineOrder.size() == infoLineIds.size(),
          "stored line order loads and merges");
    check(formatRotation(178.44f, -12.34f) == "178.4 / -12.3", "rotation keeps one decimal");
    check(formatAngle(-12.34f) == "-12.3", "separate yaw and pitch keep one decimal");
    check(formatRealTime(7, 5) == "07:05" && formatRealTime(24, 0).empty(),
          "real time uses a bounded 24-hour clock");
    check(formatRealDateTime(2026, 9, 28, 7, 5) == "2026-09-28 07:05"
          && formatRealDateTime(2026, 13, 28, 7, 5).empty(),
          "real date and time use an unambiguous ISO-style date");
    auto nether = scaledPosition(80, 64, -40, 0);
    auto overworld = scaledPosition(10, 64, -5, 1);
    check(nether && nether->destination == ScaledDimension::Nether && nether->x == 10 && nether->z == -5
          && overworld && overworld->destination == ScaledDimension::Overworld
          && overworld->x == 80 && overworld->z == -40 && !scaledPosition(1, 2, 3, 2),
          "scaled coordinates convert only between Overworld and Nether");
    check(biomeTranslationKey("minecraft:plains") == "biome.plains.name"
          && biomeTranslationKey("custom:blue_forest") == "biome.blue_forest.name"
          && biomeTranslationKey("").empty(), "biome identifiers map to the game's translation keys");
    check(formatBiomeValue("Plains", "minecraft:plains", BiomeDisplay::NameAndId)
              == "Plains (minecraft:plains)"
          && formatBiomeValue("Plains", "minecraft:plains", BiomeDisplay::Name) == "Plains"
          && formatBiomeValue("Plains", "minecraft:plains", BiomeDisplay::Id) == "minecraft:plains"
          && formatBiomeValue("minecraft:plains", "minecraft:plains", BiomeDisplay::NameAndId)
              == "minecraft:plains",
          "biome display selects name, name with id, or id without duplicating fallback text");
    auto chunk = chunkPosition(101.3, -32.7);
    check(chunk.chunkX == 6 && chunk.chunkZ == -3 && chunk.inX == 5 && chunk.inZ == 15
          && formatChunk(chunk) == "6, -3 (5, 15)", "chunk and in-chunk position handle negatives");
    check(dayCount(0) == 0 && dayCount(24000) == 1 && dayTicks(24000 + 6000) == 6000
          && formatClock(0) == "06:00" && formatClock(6000) == "12:00" && formatClock(12000) == "18:00"
          && formatClock(18000) == "00:00" && formatClock(24000) == "06:00",
          "day count and clock follow total world ticks");
    check(moonPhase(0) == 0 && moonPhase(7 * 24000) == 7 && moonPhase(8 * 24000) == 0
          && moonPhaseKey(0) == "moon.full" && moonPhaseKey(7) == "moon.waxingGibbous",
          "moon phase cycles eight days from the full moon");
    check(formatSpeed(4.26) == "4.3", "speed keeps one decimal");
    SpeedSampler speed;
    speed.sample(0, 64, 0, 10.0);
    speed.sample(1, 64, 0, 10.2);
    check(!speed.read(), "speed needs half a second of movement");
    speed.sample(4.3, 64, 0, 11.0);
    auto blocksPerSecond = speed.read();
    check(blocksPerSecond && blocksPerSecond->total > 4.2 && blocksPerSecond->total < 4.4
          && blocksPerSecond->horizontal == blocksPerSecond->total && blocksPerSecond->vertical == 0,
          "speed splits match the old total while walking horizontally");
    speed.reset();
    speed.sample(0, 64, 0, 20);
    speed.sample(3, 68, 0, 21);
    auto split = speed.read();
    check(split && split->total == 5 && split->horizontal == 3 && split->vertical == 4,
          "speed reports horizontal magnitude and signed vertical motion");
    speed.sample(500, 64, 0, 21.5);
    check(!speed.read(), "teleports restart the window");
    speed.sample(500, 64, 0, 21.6);
    speed.sample(501, 64, 0, 24.0);
    check(!speed.read(), "stalls restart the window");
    speed.sample(0, 64, 0, std::numeric_limits<double>::quiet_NaN());
    check(!speed.read(), "invalid input restarts the window");
}
