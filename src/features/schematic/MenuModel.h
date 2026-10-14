#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <variant>

// The schematic menu (BACKLOG L-93, docs/demos/schematic-controls.html): eight
// categories, each a ring of items. A stepper changes a value with the wheel
// (a click steps once); a command runs on a click. What they do is glue.
namespace lamium::schematic::menu {
enum class Stepper : std::uint8_t { ForwardBack, LeftRight, UpDown, Rotate, Mirror, LayerAxis, LayerMode, Layer, Placement, Target };
enum class Command : std::uint8_t {
    ToFeet, ResetTurn, LayerHere, ShowAll, ToggleShown, ToggleExtras, ToggleEntities, ToggleHud, ToggleFeature,
    Corner1Here, Corner2Here, MoveCorner1, MoveCorner2, MoveArea, MovePlacement, SaveArea, ClearArea,
    SelectLooked, PlaceFile, DeletePlacement, NearestMistake, CheckTab, MaterialsTab, FilesTab, PlacedTab, KeySettings,
};
// What the Move category moves.
enum class Target : std::uint8_t { Placement, Corner1, Corner2, Area };

struct Item {
    std::string_view label; // translation key
    std::variant<Stepper, Command> what;
    bool stepper() const { return std::holds_alternative<Stepper>(what); }
};
struct Category {
    std::string_view label;
    std::span<Item const> items;
};

namespace detail {
using S = Stepper;
using C = Command;
inline constexpr std::array<Item, 5> move{{{"schematic.menu.forwardBack", S::ForwardBack}, {"schematic.menu.leftRight", S::LeftRight},
    {"schematic.menu.upDown", S::UpDown}, {"schematic.menu.toFeet", C::ToFeet}, {"schematic.menu.moveTarget", S::Target}}};
inline constexpr std::array<Item, 3> turn{{{"schematic.menu.rotate", S::Rotate}, {"schematic.menu.mirror", S::Mirror},
    {"schematic.menu.resetTurn", C::ResetTurn}}};
inline constexpr std::array<Item, 5> layers{{{"schematic.menu.layerAxis", S::LayerAxis}, {"schematic.menu.layerMode", S::LayerMode},
    {"schematic.menu.layer", S::Layer}, {"schematic.menu.layerHere", C::LayerHere}, {"schematic.menu.showAll", C::ShowAll}}};
inline constexpr std::array<Item, 5> show{{{"schematic.menu.toggleShown", C::ToggleShown}, {"schematic.menu.toggleExtras", C::ToggleExtras},
    {"schematic.menu.toggleEntities", C::ToggleEntities}, {"schematic.menu.toggleHud", C::ToggleHud},
    {"schematic.menu.toggleFeature", C::ToggleFeature}}};
inline constexpr std::array<Item, 7> area{{{"schematic.menu.corner1Here", C::Corner1Here}, {"schematic.menu.corner2Here", C::Corner2Here},
    {"schematic.menu.moveCorner1", C::MoveCorner1}, {"schematic.menu.moveCorner2", C::MoveCorner2}, {"schematic.menu.moveArea", C::MoveArea},
    {"schematic.menu.saveArea", C::SaveArea}, {"schematic.menu.clearArea", C::ClearArea}}};
inline constexpr std::array<Item, 5> placement{{{"schematic.menu.placement", S::Placement}, {"schematic.menu.movePlacement", C::MovePlacement},
    {"schematic.menu.selectLooked", C::SelectLooked}, {"schematic.menu.placeFile", C::PlaceFile},
    {"schematic.menu.deletePlacement", C::DeletePlacement}}};
inline constexpr std::array<Item, 3> check{{{"schematic.menu.nearestMistake", C::NearestMistake}, {"schematic.menu.checkTab", C::CheckTab},
    {"schematic.menu.materialsTab", C::MaterialsTab}}};
inline constexpr std::array<Item, 5> screen{{{"schematic.menu.filesTab", C::FilesTab}, {"schematic.menu.placedTab", C::PlacedTab},
    {"schematic.menu.checkTab", C::CheckTab}, {"schematic.menu.materialsTab", C::MaterialsTab}, {"schematic.menu.keySettings", C::KeySettings}}};
}
inline constexpr int moveCategory = 0;
inline constexpr int showCategory = 3; // Holds the feature switch.
// With Schematics off, only what turns it on or opens a screen runs; the rest
// would act on placements and an area nothing draws.
inline constexpr bool worksWhileOff(Item const& item) {
    if (item.stepper()) return false;
    switch (std::get<Command>(item.what)) {
    case Command::ToggleFeature: case Command::ToggleHud: case Command::PlaceFile: case Command::DeletePlacement:
    case Command::FilesTab: case Command::PlacedTab: case Command::CheckTab: case Command::MaterialsTab: case Command::KeySettings:
        return true;
    default: return false;
    }
}
inline constexpr std::array<Category, 8> categories{{
    {"schematic.menu.cat.move", detail::move}, {"schematic.menu.cat.turn", detail::turn}, {"schematic.menu.cat.layers", detail::layers},
    {"schematic.menu.cat.show", detail::show}, {"schematic.menu.cat.area", detail::area}, {"schematic.menu.cat.placement", detail::placement},
    {"schematic.menu.cat.check", detail::check}, {"schematic.menu.cat.screen", detail::screen},
}};
// The "move ... ->" commands: which target they choose before opening Move.
inline constexpr bool choosesTarget(Command command, Target& target) {
    switch (command) {
    case Command::MovePlacement: target = Target::Placement; return true;
    case Command::MoveCorner1: target = Target::Corner1; return true;
    case Command::MoveCorner2: target = Target::Corner2; return true;
    case Command::MoveArea: target = Target::Area; return true;
    default: return false;
    }
}
// Steppers the adjust key may repeat: not the one that only picks the target.
inline constexpr bool repeatable(Stepper stepper) { return stepper != Stepper::Target; }
// Where the menu opens: the category list, or (an option) the level shown
// when it was last closed.
inline constexpr int openAt(bool whereClosed, int closedAt) {
    return whereClosed && closedAt >= 0 && closedAt < static_cast<int>(categories.size()) ? closedAt : -1;
}
}
