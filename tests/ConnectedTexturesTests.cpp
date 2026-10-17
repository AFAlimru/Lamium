#include "features/visuals/ConnectedTextures.h"
#include <cstdint>
#include <vector>
void check(bool, char const*);
void connectedTexturesTests() {
    using namespace lamium::visuals::connected;
    check(connects("minecraft:glass") && connects("minecraft:red_stained_glass") && connects("minecraft:tinted_glass"),
          "glass blocks connect");
    check(!connects("minecraft:glass_pane") && !connects("minecraft:red_stained_glass_pane") && !connects("minecraft:stone"),
          "panes and other blocks do not");
    for (auto face : {Face::Down, Face::Up, Face::North, Face::South, Face::West, Face::East}) {
        auto s = sides(face);
        bool opposite = s.left.x == -s.right.x && s.left.y == -s.right.y && s.left.z == -s.right.z
            && s.top.x == -s.bottom.x && s.top.y == -s.bottom.y && s.top.z == -s.bottom.z;
        check(opposite, "left/right and top/bottom are opposite neighbors");
    }
    check(sides(Face::North).top.y == 1 && sides(Face::East).top.y == 1 && sides(Face::Up).top.y == 0,
          "side faces have the block above at their top; horizontal faces stay in their plane");
    check(ruleFor("minecraft:glass") && !ruleFor("minecraft:glass")->sidesOnly && ruleFor("minecraft:bookshelf")
              && ruleFor("minecraft:bookshelf")->sidesOnly && ruleFor("minecraft:bookshelf")->top == 0
              && ruleFor("minecraft:sandstone")->top == 4 && ruleFor("minecraft:sandstone")->left == 0
              && !ruleFor("minecraft:stone") && !ruleFor("minecraft:chiseled_sandstone"),
          "rules per block");
    // Textures that get sprites, by file name.
    check(ruleForTexture("textures/blocks/glass") && ruleForTexture("textures/blocks/glass_light_blue")
              && ruleForTexture("textures/blocks/tinted_glass") && ruleForTexture("textures/blocks/bookshelf")
              && ruleForTexture("textures/blocks/sandstone_normal") && ruleForTexture("textures/blocks/red_sandstone_normal"),
          "the rule blocks' textures get sprites");
    check(!ruleForTexture("textures/blocks/glass_pane_top") && !ruleForTexture("textures/blocks/glass_pane_top_red")
              && !ruleForTexture("textures/blocks/hard_glass") && !ruleForTexture("textures/blocks/sandstone_top")
              && !ruleForTexture("textures/blocks/chiseled_bookshelf_empty") && !ruleForTexture("textures/blocks/stone"),
          "pane edges, tops and other textures do not");
    check(ruleForTexture("textures/blocks/bookshelf")->left == ruleFor("minecraft:bookshelf")->left
              && ruleForTexture("textures/blocks/sandstone_normal")->top == ruleFor("minecraft:sandstone")->top,
          "a texture is baked with its block's rule");
    check(variantMasks(glassRule()).size() == 19 && variantMasks(bookshelfRule()).size() == 3
              && variantMasks(sandstoneRule()).size() == 1 && variantMasks(sandstoneRule())[0] == Top,
          "one sprite per joined combination a rule can show, and a two-side corner pose twice");
    check(spriteName("textures/blocks/glass_red", Left | Top) == "lamium_ct_glass_red_5"
              && spritePath("textures/blocks/glass", 15) == "textures/blocks/lamium_ct_glass_15"
              && spriteName("textures/blocks/bookshelf", 0) == "lamium_ct_bookshelf_0",
          "sprite names carry the file and the joined sides");
    check(bakeable(16, 16) && bakeable(64, 64) && !bakeable(128, 128) && !bakeable(16, 32) && !bakeable(8, 8),
          "only square textures up to 64 px get sprites");
    // Baking: a 16x16 image whose texel is its own column/row number.
    std::vector<std::uint32_t> image(16 * 16);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) image[y * 16 + x] = static_cast<std::uint32_t>(y * 100 + x);
    auto at = [](std::vector<std::uint32_t> const& pixels, int x, int y) { return pixels[y * 16 + x]; };
    auto glass = bake(image.data(), 16, 16, glassRule(), Left | Right | Top | Bottom);
    check(at(glass, 0, 5) == 507 && at(glass, 1, 5) == 508 && at(glass, 14, 5) == 507 && at(glass, 15, 5) == 508
              && at(glass, 5, 5) == 505,
          "joined glass refills two texels on each side from the middle (7-8)");
    check(at(glass, 0, 0) == 707 && at(glass, 15, 15) == 808, "corners take the middle on both axes");
    auto leftOnly = bake(image.data(), 16, 16, glassRule(), Left);
    check(at(leftOnly, 0, 0) == 7 && at(leftOnly, 15, 0) == 15 && at(leftOnly, 15, 15) == 1515,
          "only the joined side changes");
    auto shelf = bake(image.data(), 16, 16, bookshelfRule(), Left | Right);
    check(at(shelf, 0, 3) == 301 && at(shelf, 15, 3) == 314 && at(shelf, 1, 3) == 301 && at(shelf, 14, 3) == 314,
          "bookshelves copy the column next to the dropped one");
    auto sandstone = bake(image.data(), 16, 16, sandstoneRule(), Top);
    check(at(sandstone, 4, 0) == 804 && at(sandstone, 4, 3) == 1104 && at(sandstone, 4, 4) == 404,
          "sandstone under another shows rock rows 8-11 for its band");
    check(cornerPose(Left | Bottom) && cornerPose(Right | Top) && !cornerPose(Left | Right) && !cornerPose(Top)
              && !cornerPose(Left | Right | Top | Bottom),
          "only two joined sides on different axes take a corner variant");
    auto turn = bake(image.data(), 16, 16, glassRule(), Left | Bottom | Corner);
    check(at(turn, 0, 15) == 1500 && at(turn, 1, 14) == 1401 && at(turn, 0, 0) == 7,
          "a corner variant keeps its own texels at the joined corner");
    check(at(turn, 0, 5) == 507 && at(turn, 5, 15) == 805,
          "the joined strips still refill away from the corner");
    check(sourceTexel(1, 32, true, 2, 7, false, 0, -1) == 15 && sourceTexel(4, 32, true, 2, 7, false, 0, -1) == 4,
          "larger textures scale the strips");
    // Orientation.
    check(imageMask(true, false, true, false, false, false) == (Left | Top)
              && imageMask(true, false, true, false, true, true) == (Right | Bottom),
          "a face whose texture runs backwards joins the image's other side");
    check(edgeSide(0.25f, 0.25f, 0.5f, 0.25f / 16, Left, Right) == Left && edgeSide(0.5f, 0.25f, 0.5f, 0.25f / 16, Left, Right) == Right
              && edgeSide(0.375f, 0.25f, 0.5f, 0.25f / 16, Left, Right) == 0,
          "a pane vertex on a joined edge names the tile edge it shows");
    // Panes (geometry dumped in game 2026-10-11).
    check(connectsPane("minecraft:glass_pane") && connectsPane("minecraft:lime_stained_glass_pane") && !connectsPane("minecraft:glass"),
          "panes take the pane path");
    check(partOf(0.437f, 0.563f, 0.437f, 0.563f) == Part::Center && partOf(0.563f, 1.f, 0.437f, 0.563f) == Part::East
              && partOf(0.f, 0.437f, 0.437f, 0.563f) == Part::West && partOf(0.437f, 0.563f, 0.563f, 1.f) == Part::South
              && partOf(0.563f, 0.563f, 0.f, 0.437f) == Part::North && partOf(0.437f, 0.563f, 0.563f, 0.563f) == Part::Center,
          "faces belong to the center post or the arm they reach into");
    auto corner = partsOf(std::vector<Box>{{0.5f, 1.f, 0.438f, 0.562f}, {0.438f, 0.562f, 0.f, 0.5f}});
    check(corner.has(Part::Center) && corner.has(Part::East) && corner.has(Part::North) && !corner.has(Part::West)
              && !corner.has(Part::South),
          "an L corner has its two arms and the center its boxes leave out");
    auto post = partsOf(std::vector<Box>{{0.438f, 0.562f, 0.438f, 0.562f}});
    check(post.has(Part::Center) && !post.has(Part::East) && !post.has(Part::North), "a lone pane is only its post");
    auto straight = partsOf(std::vector<Box>{{0.f, 1.f, 0.438f, 0.562f}});
    check(straight.has(Part::East) && straight.has(Part::West), "a straight pane has both arms");
    check(!Parts{}.has(Part::Center), "no pane, no parts");
}
