#include "features/information/TargetCard.h"
#include "features/information/SchematicTarget.h"
void check(bool, char const*);
void targetCardTests() {
    using namespace lamium::information;
    {
        auto beside = targetCardLayout(2, false, {640, 240}, {180, 110}, {160, 80}, 12, 14);
        check(beside.beside && beside.width == 352 && beside.height == 124 && beside.ordinaryWidth == 180
              && beside.schematicWidth == 160 && beside.schematicX == 192 && beside.ordinaryY == 14,
              "side-by-side sections keep independent widths and reserve the ordinary heading above its content");
        auto narrow = targetCardLayout(2, false, {320, 240}, {180, 110}, {160, 80}, 12, 14);
        check(!narrow.beside && narrow.width == 180 && narrow.height == 202 && narrow.schematicY == 122
              && narrow.ordinaryY == 0, "narrow screens stack instead of squeezing ordinary information");
        auto shortScreen = targetCardLayout(2, false, {640, 120}, {180, 110}, {160, 80}, 12, 14);
        check(!shortScreen.beside && shortScreen.ordinaryY == 0,
              "a short screen does not add a column heading that would push ordinary information off screen");
        auto stacked = targetCardLayout(1, false, {640, 240}, {180, 110}, {160, 80}, 9, 14);
        check(!stacked.beside && stacked.width == 180 && stacked.height == 199 && stacked.schematicY == 119,
              "stacked display retains the upper ordinary section and lower schematic section");
        for (int mode : {1, 2}) {
            auto ghost = targetCardLayout(mode, true, {640, 240}, {}, {160, 80}, 12, 14);
            check(!ghost.beside && ghost.width == 160 && ghost.height == 80 && ghost.schematicX == 0
                  && ghost.schematicY == 0, "both schematic layouts show a nearer ghost as one standalone section");
        }
        auto none = targetCardLayout(0, false, {640, 240}, {180, 110}, {160, 80}, 12, 14);
        auto hiddenGhost = targetCardLayout(0, true, {640, 240}, {}, {160, 80}, 12, 14);
        check(none.width == 180 && none.height == 110 && none.schematicWidth == 0 && hiddenGhost.width == 0,
              "none reserves no schematic area and does not turn a ghost into ordinary block information");
    }
    TargetInfo mob{"Zombie", "minecraft:zombie"};
    mob.details.push_back({"target.armor", "2"});
    mob.details.push_back({"target.health", "16 / 20", false, .8f, DetailKind::Health, 16, 20});
    mob.details.push_back({"target.age", "target.adult", true});
    CardOptions options;
    auto rows = cardRows(mob, options);
    check(rows.size() == 1 && rows[0].label == "target.health" && rows[0].meter == Meter::Hearts,
          "health leads the card and other details wait for their switch");
    options.details = true;
    options.health = Meter::Bar;
    rows = cardRows(mob, options);
    check(rows.size() == 3 && rows[0].meter == Meter::Bar && rows[1].label == "target.armor"
          && rows[2].valueIsKey, "details follow health in their original order");

    TargetInfo crop{"Wheat", "minecraft:wheat"};
    crop.blockPosition = TargetInfo::BlockPosition{1, 2, 3};
    crop.details.push_back({"target.growth", "5 / 7", false, 5.f / 7, DetailKind::Growth});
    crop.states.push_back("custom_state: 4");
    CardOptions cropOptions;
    cropOptions.coordinates = true;
    cropOptions.growth = Meter::Number;
    rows = cardRows(crop, cropOptions);
    check(rows.size() == 2 && rows[0].meter == Meter::Number && rows[1].label == "target.position"
          && rows[1].value == "1, 2, 3", "growth then coordinates; growth can be a plain number");
    cropOptions.details = true;
    rows = cardRows(crop, cropOptions);
    check(rows.back().label == "custom_state" && !rows.back().labelIsKey && rows.back().value == "4",
          "raw states split into a label and a value");
    check(cardRows(crop, cropOptions, 1).size() == 1, "the row limit holds");

    TargetInfo unranged{"Mystery", "minecraft:mystery"};
    unranged.details.push_back({"target.growth", "9", false, std::nullopt, DetailKind::Growth});
    check(cardRows(unranged, CardOptions{}).front().meter == Meter::Number, "values without a range stay numbers");

    auto full = hearts(1.f);
    check(full[0] == Heart::Full && full[9] == Heart::Full, "full health fills every heart");
    auto some = hearts(.75f); // 15 of 20 halves
    check(some[6] == Heart::Full && some[7] == Heart::Half && some[8] == Heart::Empty, "odd halves draw a half heart");
    check(hearts(0)[0] == Heart::Empty && hearts(2.f)[9] == Heart::Full, "hearts clamp to the range");
    auto count = [](std::vector<Heart> const& icons, Heart kind) {
        return static_cast<int>(std::count(icons.begin(), icons.end(), kind));
    };
    auto zombie = healthHearts(20, 20);
    check(zombie.size() == 10 && count(zombie, Heart::Full) == 10, "20/20 is ten full hearts");
    auto hurt = healthHearts(10, 20);
    check(count(hurt, Heart::Full) == 5 && count(hurt, Heart::Empty) == 5, "10/20 is five of ten");
    auto enderman = healthHearts(20, 40);
    check(enderman.size() == 20 && count(enderman, Heart::Full) == 10 && enderman[10] == Heart::Empty,
          "20/40 fills ten of twenty slots instead of looking full");
    auto odd = healthHearts(19, 20);
    check(count(odd, Heart::Full) == 9 && odd[9] == Heart::Half, "19/20 is nine and a half hearts");
    check(healthHearts(15, 15).size() == 8 && healthHearts(15, 15)[7] == Heart::Half, "an odd maximum ends on a half slot");
    check(healthHearts(-3, 20)[0] == Heart::Empty && count(healthHearts(30, 20), Heart::Full) == 10
          && healthHearts(5, 0).empty(), "health clamps to the slots");
    check(heartLines(20) == 1 && heartLines(40) == 2 && heartLines(100) == 5 && heartLines(101) == 6,
          "ten hearts per line");
    auto meterFor = [&](int current, int maximum) {
        TargetInfo target{"Mob", "minecraft:mob"};
        target.details.push_back({"target.health", "", false, 1.f, DetailKind::Health, current, maximum});
        return cardRows(target, CardOptions{}).front().meter;
    };
    check(meterFor(100, 100) == Meter::Hearts && meterFor(600, 600) == Meter::Bar && meterFor(1, 0) == Meter::Bar,
          "up to five lines stay hearts; bosses become a bar");
    auto zombieEggs = spawnEggCandidates("minecraft:zombie");
    check(zombieEggs.size() == 1 && zombieEggs[0] == "minecraft:zombie_spawn_egg" && spawnEggCandidates("").empty(),
          "mobs use their spawn egg as the icon");
    auto villagerEggs = spawnEggCandidates("minecraft:villager_v2");
    check(villagerEggs.size() == 2 && villagerEggs[0] == "minecraft:villager_v2_spawn_egg"
          && villagerEggs[1] == "minecraft:villager_spawn_egg"
          && spawnEggCandidates("minecraft:zombie_villager_v2")[1] == "minecraft:zombie_villager_spawn_egg"
          && spawnEggCandidates("minecraft:evocation_illager")[1] == "minecraft:evoker_spawn_egg"
          && spawnEggCandidates("minecraft:vindication_illager")[1] == "minecraft:vindicator_spawn_egg",
          "the exact egg is tried before the renamed entity's egg");
    auto tridentItem = entityItemCandidates("minecraft:thrown_trident");
    check(tridentItem.size() == 2 && tridentItem[0] == "minecraft:thrown_trident"
          && tridentItem[1] == "minecraft:trident" && entityItemCandidates("minecraft:snowball").size() == 1
          && entityItemCandidates("").empty(),
          "an actor named after its throw keeps the item and the stripped id");
    check(entityItemCandidates("minecraft:ender_crystal")[1] == "minecraft:end_crystal"
          && entityItemCandidates("minecraft:eye_of_ender_signal")[1] == "minecraft:ender_eye"
          && entityItemCandidates("minecraft:xp_bottle")[1] == "minecraft:experience_bottle",
          "the bounded aliases cover Bedrock ids the item no longer shares");
    auto pick = chooseBlockIcon("minecraft:stone", 3, "textures/blocks/stone");
    check(pick.kind == IconKind::Item && pick.name == "minecraft:stone" && pick.aux == 3,
          "a block with an item keeps its pick item even when a texture exists");
    auto texture = chooseBlockIcon("", 0, "textures/blocks/portal");
    check(texture.kind == IconKind::Texture && texture.name == "textures/blocks/portal",
          "a block without an item falls back to its own texture");
    check(chooseBlockIcon("", 0, "").kind == IconKind::None, "without either source the icon stays empty");
    auto portalFrame = fileFrameUv(TargetIcon{IconKind::Texture, "textures/blocks/portal", 0, .2812f, .7812f, .2969f,
                                               .8125f},
                                   1024, 512, 16, 512);
    check(portalFrame.u0 == 0 && portalFrame.v0 == 0 && portalFrame.u1 == 1
          && portalFrame.v1 > .03f && portalFrame.v1 < .032f,
          "an atlas tile becomes the first frame of the source file");
    auto plain = fileFrameUv(TargetIcon{IconKind::Texture, "textures/blocks/stone", 0, .5f, .25f, .75f, .5f}, 16, 16,
                             16, 16);
    check(plain.u0 == .5f && plain.u1 == .75f, "a uv set that already matches its file is unchanged");
    check(morphProgress(0) == 0 && morphProgress(morphSeconds) == 1 && morphProgress(1) == 1
          && morphProgress(morphSeconds / 2) > .5f, "the card eases out and settles");
    {
        TargetInfo block{"Oak Stairs", "minecraft:oak_stairs"};
        TargetInfo::DetailRow line{"target.schematic", "Oak Stairs (Wrong state)"};
        line.kind = DetailKind::Schematic;
        line.icon = "icon";
        TargetInfo::DetailRow state{"weirdo_direction", "2 (now 1)"};
        state.kind = DetailKind::Schematic;
        state.labelIsKey = false;
        TargetInfo::Schematic schematic;
        schematic.details = {line, state};
        block.schematic = schematic;
        block.states = {"weirdo_direction: 1"};
        CardOptions plain;
        auto shown = schematicCardRows(*block.schematic, plain);
        check(shown.size() == 2 && shown[0].icon == "icon" && shown[1].label == "weirdo_direction" && !shown[1].labelIsKey,
              "the separate schematic rows show without details and keep their icons");
        check(cardRows(block, plain).empty(), "schematic rows are not part of the ordinary card");
        plain.details = true;
        auto actual = cardRows(block, plain, 1);
        check(actual.size() == 1 && actual[0].value == "1", "a schematic difference does not replace the real state");
        check(schematicCardRows(schematic, plain, 0).empty()
              && schematicCardRows(schematic, plain, 1).size() == 1, "schematic capacity is independent, including zero");
    }
    {
        // L-112: common states by name.
        auto facing = interpretBlockState("weirdo_direction", StateKind::Integer, 3, {}, "minecraft:stone_stairs");
        check(facing && facing->label == "target.facing" && facing->value == "target.dirNorth", "stairs facing is named");
        auto trapdoor = interpretBlockState("direction", StateKind::Integer, 0, {}, "minecraft:oak_trapdoor");
        auto other = interpretBlockState("direction", StateKind::Integer, 0, {}, "minecraft:bed");
        check(trapdoor && trapdoor->value == "target.dirEast" && !other, "only a trapdoor's direction is named");
        auto slab = interpretBlockState("minecraft:vertical_half", StateKind::Text, 0, "top", "minecraft:stone_slab");
        auto axis = interpretBlockState("pillar_axis", StateKind::Text, 0, "x", "minecraft:oak_log");
        auto flipped = interpretBlockState("upside_down_bit", StateKind::Integer, 1, {}, "minecraft:stone_stairs");
        check(slab && slab->value == "target.upper" && axis && axis->value == "X" && flipped && flipped->value == "target.yes",
              "slab half, axis and upside down are named");
        check(!interpretBlockState("minecraft:corner", StateKind::Text, 0, "none", "minecraft:stone_stairs"),
              "unknown states keep their raw rows");
        // L-93: schematic rows say what to do.
        auto translate = [](std::string_view key) { return "<" + std::string(key) + ">"; };
        using lamium::schematic::CellState;
        auto stairs = schematicStateRows(
            {{"weirdo_direction", "0", "3"}, {"minecraft:corner", "none", "inner_left"}}, "minecraft:stone_stairs", translate);
        check(stairs.size() == 2 && stairs[0].label == "target.facing" && stairs[0].labelIsKey
              && stairs[0].before == "<target.dirNorth>" && stairs[0].value == "<target.dirEast>" && stairs[0].tone == Tone::State,
              "a wrong state reads '<state>: now -> should be' by name");
        check(stairs[1].label == "minecraft:corner" && !stairs[1].labelIsKey && stairs[1].before == "inner_left"
              && stairs[1].value == "none",
              "an unknown state keeps its raw name and values");
        TargetInfo card{"Stairs", "minecraft:stone_stairs"};
        TargetInfo::Schematic section;
        section.details = stairs;
        card.schematic = section;
        auto drawn = schematicCardRows(*card.schematic, CardOptions{});
        check(drawn.size() == 2 && drawn[0].before == "<target.dirNorth>" && drawn[0].tone == Tone::State,
              "card rows keep the change and its color");
        lamium::schematic::Mismatch mismatch;
        mismatch.state = CellState::State;
        mismatch.expectedName = "Stairs";
        mismatch.expected = "item";
        mismatch.identifier = "minecraft:stone_stairs";
        mismatch.states = {{"weirdo_direction", "0", "3"}};
        auto compared = schematicTarget(mismatch, translate);
        check(compared && compared->name == "Stairs" && compared->item == "item"
              && compared->status == "target.schematicState" && compared->tone == Tone::State
              && compared->details[0].before == "<target.dirNorth>" && compared->details[0].value == "<target.dirEast>",
              "the schematic section carries the expected block and named state comparison");
        mismatch.state = CellState::Extra;
        compared = schematicTarget(mismatch, translate);
        check(compared && compared->name == "<schematic.air>" && compared->tone == Tone::Wrong
              && compared->details.empty() && compared->item.empty(),
              "extra blocks have air as the expected header and no stale state differences");
        mismatch.state = CellState::Wrong;
        compared = schematicTarget(mismatch, translate);
        check(compared && compared->name == "Stairs" && compared->item == "item"
              && compared->status == "target.schematicWrong" && compared->tone == Tone::Wrong,
              "a wrong block retains the expected name and full-size item icon");
        mismatch.state = CellState::Missing;
        compared = schematicTarget(mismatch, translate);
        check(compared && compared->status == "schematic.kind.missing" && compared->tone == Tone::Missing,
              "missing blocks have an explicit light-blue status");
        for (auto kind : {CellState::Correct, CellState::Unknown, CellState::Ignored}) {
            mismatch.state = kind;
            check(!schematicTarget(mismatch, translate), "non-mistakes do not add a comparison section");
        }
    }
    {
        TargetInfo expected{"Grass Block", "minecraft:grass_block"};
        expected.blockPosition = TargetInfo::BlockPosition{12, 65, 80};
        expected.icon = {IconKind::Item, "minecraft:grass_block"};
        expected.states = {"snowy: 0"};
        auto ghost = makeGhostTarget(expected);
        CardOptions all;
        all.coordinates = all.details = true;
        auto shown = schematicCardRows(*ghost.schematic, all);
        check(ghost.ghostOnly && cardRows(ghost, all).empty() && ghost.schematic->status == "schematic.kind.missing",
              "a nearer ghost is explicitly missing and has no ordinary real-block section");
        check(ghost.schematic->blockPosition && ghost.schematic->blockPosition->y == 65
              && shown.size() == 2 && shown[0].value == "12, 65, 80" && shown[1].value == "0"
              && ghost.schematic->name == "Grass Block" && ghost.schematic->icon.name == "minecraft:grass_block",
              "a ghost keeps its own coordinate, expected block, icon and states together");
        TargetInfo actual{"Grass Block", "minecraft:grass_block"};
        actual.blockPosition = TargetInfo::BlockPosition{12, 64, 80};
        auto real = cardRows(actual, all);
        check(!actual.ghostOnly && !actual.schematic && real.size() == 1 && real[0].value == "12, 64, 80",
              "looking at a real block still uses its own coordinates even when its name matches a ghost");
    }
    {
        TargetInfo actual{"Wire", "minecraft:redstone_wire"};
        actual.blockPosition = TargetInfo::BlockPosition{12, 64, 80};
        actual.details = {{"target.power", "12", false, .8f}};
        for (int i = 0; i < 12; ++i) actual.states.push_back("state" + std::to_string(i) + ": 1");
        CardOptions all;
        all.details = all.coordinates = true;
        auto before = cardRows(actual, all, 10);
        TargetInfo::Schematic section;
        section.details = schematicStateRows({{"redstone_signal", "0", "12"}},
            actual.identifier, [](std::string_view key) { return std::string(key); });
        actual.schematic = section;
        auto after = cardRows(actual, all, 10);
        bool same = before.size() == after.size();
        for (size_t i = 0; same && i < before.size(); ++i)
            same = before[i].label == after[i].label && before[i].value == after[i].value && before[i].progress == after[i].progress;
        check(same && after.size() == 10 && after[0].label == "target.position" && after[1].label == "target.power",
              "adding a schematic preserves the full ordinary row budget, coordinates and actual power");
        section.details.clear();
        section.blockPosition = TargetInfo::BlockPosition{12, 65, 80};
        section.details.push_back({"target.growth", "5 / 7", false, 5.f / 7, DetailKind::Growth});
        section.states = {"custom: 4"};
        auto ghost = schematicCardRows(section, all);
        check(ghost.size() == 3 && ghost[0].label == "target.growth" && ghost[1].value == "12, 65, 80"
              && ghost[2].value == "4", "a ghost's states and position belong to the lower section");
    }
    check(cardContentFits(0, 0, 100, 50, 0, 0, 100, 50) && cardContentFits(-10, -5, 120, 60, 0, 0, 100, 50),
          "the card content shows when the background covers its box");
    check(!cardContentFits(0, 0, 80, 50, 0, 0, 100, 50) && !cardContentFits(0, 0, 100, 30, 0, 0, 100, 50)
          && !cardContentFits(5, 0, 100, 50, 0, 0, 100, 50),
          "the card content waits while a growing background is smaller than it");
}
