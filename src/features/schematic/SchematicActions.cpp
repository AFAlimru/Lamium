#include "features/schematic/SchematicActions.h"
#include "features/camera/CameraSessions.h"
#include "features/schematic/GhostRenderer.h"
#include "features/schematic/MoveDirection.h"
#include "features/schematic/SchematicSession.h"
#include "features/schematic/Selection.h"
#include "input/Actions.h"
#include "ui/Localization.h"
#include "ui/Toast.h"
#include "ui/Widgets.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/input/MouseInputEvent.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/deps/input/MouseAction.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/screen/MinecraftUIRenderContext.h"
#include "mc/world/phys/HitResult.h"
#include <atomic>
#include <cmath>
#include <limits>

namespace lamium::schematic::actions {
namespace {
using input::Action;
std::optional<Point> feet(LocalPlayer& player) {
    auto p = player.getFeetPos();
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) return std::nullopt;
    return Point{static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)), static_cast<int>(std::floor(p.z))};
}
// The drawn view's axes: a detached Freelook or FreeCamera view owns the
// crosshair, so a step follows it rather than the body (L-230).
Axes viewAxes(IClientInstance& client) {
    if (auto ray = CameraSessions::instance().detachedViewRay(client)) {
        double length = std::sqrt(ray->dx * ray->dx + ray->dy * ray->dy + ray->dz * ray->dz);
        if (length > 1e-9) return moveAxes(ray->dx / length, ray->dy / length, ray->dz / length);
    }
    auto* player = client.getLocalPlayer();
    if (!player) return moveAxes(0, 0, 1);
    auto v = player->getViewVector(1.f);
    double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (!(length > 1e-9)) return moveAxes(0, 0, 1);
    return moveAxes(v.x / length, v.y / length, v.z / length);
}
Point stepFor(IClientInstance& client, Action action) {
    auto axes = viewAxes(client);
    switch (action) {
    case Action::MovePlacementForward: return axes.forward;
    case Action::MovePlacementBack: return {-axes.forward.x, -axes.forward.y, -axes.forward.z};
    // Left and right stay horizontal even when forward points up or down.
    case Action::MovePlacementLeft: return {-axes.right.x, -axes.right.y, -axes.right.z};
    case Action::MovePlacementRight: return axes.right;
    case Action::MovePlacementUp: return {0, 1, 0};
    default: return {0, -1, 0};
    }
}
// Applies `apply` to the selected placement and reports `message` built from it.
template <class Apply, class Message>
void changeSelected(Apply&& apply, Message&& message) {
    std::string text;
    bool found = false;
    bool saved = session::change([&](PlacementSet& set) {
        if (set.selected < 0 || set.selected >= static_cast<int>(set.placements.size())) return false;
        found = true;
        auto& p = set.placements[static_cast<size_t>(set.selected)];
        apply(p);
        text = message(p);
        return true;
    });
    if (!found) { ui::showMessageToast(ui::translated("schematic.toast.noPlacement")); return; }
    ui::showMessageToast(saved ? text : ui::translated("schematic.saveError"));
}
// The selected placement's structure, looked up before session::change: the
// session's lock is held while a change runs, so nothing inside may call back.
std::shared_ptr<Structure const> selectedStructure() {
    auto set = session::current();
    if (set.selected < 0 || set.selected >= static_cast<int>(set.placements.size())) return nullptr;
    return session::structure(set.placements[static_cast<size_t>(set.selected)].file);
}
int layers(Structure const* structure, SavedPlacement const& p) {
    return structure ? std::max(1, layerCount(placedSize(structure->size, p.placement.rotation), p.layers.axis)) : 1;
}
// Where the view ray enters a box, or nullopt when it misses within `reach`.
std::optional<double> enter(Vec3 const& eye, Vec3 const& dir, Point low, Size size, double reach) {
    double near = 0, far = reach;
    double o[3]{eye.x, eye.y, eye.z}, d[3]{dir.x, dir.y, dir.z};
    double lo[3]{static_cast<double>(low.x), static_cast<double>(low.y), static_cast<double>(low.z)};
    double hi[3]{lo[0] + size.x, lo[1] + size.y, lo[2] + size.z};
    for (int i = 0; i < 3; ++i) {
        if (std::abs(d[i]) < 1e-9) {
            if (o[i] < lo[i] || o[i] > hi[i]) return std::nullopt;
            continue;
        }
        double t1 = (lo[i] - o[i]) / d[i], t2 = (hi[i] - o[i]) / d[i];
        if (t1 > t2) std::swap(t1, t2);
        near = std::max(near, t1);
        far = std::min(far, t2);
        if (near > far) return std::nullopt;
    }
    return near;
}
// The placement the view ray reaches first. Ghosts are not blocks, so the
// crosshair hit cannot find them.
void selectLooked(LocalPlayer& player) {
    int dimension = static_cast<int>(player.getDimensionId());
    auto eye = player.getEyePos();
    auto dir = player.getViewVector(1.f);
    auto set = session::current();
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (int i = 0; i < static_cast<int>(set.placements.size()); ++i) {
        auto const& p = set.placements[static_cast<size_t>(i)];
        auto structure = p.dimension == dimension && p.visible ? session::structure(p.file) : nullptr;
        if (!structure) continue;
        auto distance = enter(eye, dir, p.placement.origin, placedSize(structure->size, p.placement.rotation), 256);
        if (distance && *distance < bestDistance) { best = i; bestDistance = *distance; }
    }
    if (best < 0) { ui::showMessageToast(ui::translated("schematic.toast.notLooking")); return; }
    session::change([&](PlacementSet& s) { s.selected = best; return true; });
    ui::showMessageToast(ui::translated("schematic.toast.selected", set.placements[static_cast<size_t>(best)].name));
}
// Always the mistake nearest to where the player is now.
void nearestMistake(LocalPlayer& player) {
    auto set = session::current();
    auto result = ghosts::verification();
    if (set.selected < 0) { ui::showMessageToast(ui::translated("schematic.toast.noPlacement")); return; }
    if (result->placement != set.selected || !result->complete || result->stale) { ui::showMessageToast(ui::translated("schematic.toast.counting")); return; }
    auto p = player.getFeetPos();
    Mismatch const* nearest = nullptr;
    double best = std::numeric_limits<double>::max();
    for (auto const& m : result->mismatches) {
        if (m.state == CellState::Missing) continue;
        double dx = m.position.x + .5 - p.x, dy = m.position.y + .5 - p.y, dz = m.position.z + .5 - p.z;
        double d = dx * dx + dy * dy + dz * dz;
        if (d < best) { best = d; nearest = &m; }
    }
    if (!nearest) { ui::showMessageToast(ui::translated("schematic.toast.noMistakes")); return; }
    ghosts::point(nearest->position);
    char const* kind = nearest->state == CellState::Wrong ? "schematic.kind.wrong"
        : nearest->state == CellState::Extra ? "schematic.kind.extra" : "schematic.kind.state";
    ui::showMessageToast(ui::translated("schematic.toast.nearest", ui::translated(kind), static_cast<int>(std::lround(std::sqrt(best)))));
}
// A corner of the area to save: the block in the crosshair.
void setCorner(IClientInstance& client, LocalPlayer& player, int which) {
    auto const& hit = client.getLatestHitResult();
    if (hit.mType != HitResultType::Tile) { ui::showMessageToast(ui::translated("schematic.toast.lookAtBlock")); return; }
    Point at{hit.mBlock.x, hit.mBlock.y, hit.mBlock.z};
    selection::setCorner(which, at, static_cast<int>(player.getDimensionId()));
    if (auto area = selection::current().area()) {
        auto size = area->size();
        ui::showMessageToast(ui::translated("schematic.toast.cornerArea", which + 1, at.x, at.y, at.z, size.x, size.y, size.z));
    } else ui::showMessageToast(ui::translated("schematic.toast.corner", which + 1, at.x, at.y, at.z));
}
menu::Target moveTarget = menu::Target::Placement;
std::optional<menu::Stepper> remembered;
std::atomic<bool> adjustHeld{false};
std::atomic<int> pendingWheel{0};
std::atomic<IClientInstance*> adjustClient{nullptr};
ll::event::ListenerPtr wheelListener;

// Moves the move target by `d`: the selected placement, a corner or both.
void moveBy(LocalPlayer& player, Point d) {
    auto shifted = [&](Point p) { return Point{p.x + d.x, p.y + d.y, p.z + d.z}; };
    if (moveTarget == menu::Target::Placement) {
        changeSelected([&](SavedPlacement& p) { p.placement.origin = shifted(p.placement.origin); }, [](SavedPlacement const& p) {
            return ui::translated("schematic.toast.moved", p.name, p.placement.origin.x, p.placement.origin.y, p.placement.origin.z);
        });
        return;
    }
    auto state = selection::current();
    int dimension = static_cast<int>(player.getDimensionId());
    // An area in another dimension is not moved from here.
    if (state.dimension != dimension) { ui::showMessageToast(ui::translated("schematic.toast.noArea")); return; }
    auto corner = [&](std::optional<Point> const& c, int which) {
        if (!c) return false;
        selection::setCorner(which, shifted(*c), state.dimension);
        return true;
    };
    bool moved = false;
    if (moveTarget == menu::Target::Corner1 || moveTarget == menu::Target::Area) moved = corner(state.first, 0) || moved;
    if (moveTarget == menu::Target::Corner2 || moveTarget == menu::Target::Area) moved = corner(state.second, 1) || moved;
    if (!moved) { ui::showMessageToast(ui::translated("schematic.toast.noArea")); return; }
    auto now = selection::current();
    auto const& shown = moveTarget == menu::Target::Corner2 ? now.second : now.first;
    if (shown) ui::showMessageToast(ui::translated("schematic.toast.corner", moveTarget == menu::Target::Corner2 ? 2 : 1, shown->x, shown->y, shown->z));
}
std::string mirrorName(Mirror mirror) {
    return ui::translated(mirror == Mirror::X ? "schematic.mirror.x" : mirror == Mirror::Z ? "schematic.mirror.z" : "schematic.mirror.none");
}
std::string layerText(Structure const* structure, SavedPlacement const& p) {
    return ui::translated("schematic.toast.layer", p.name, p.layers.index + 1, layers(structure, p));
}
}

bool handles(Action action) {
    switch (action) {
    case Action::NearestMistake: case Action::SelectLookedPlacement: case Action::NextPlacement:
    case Action::MovePlacementForward: case Action::MovePlacementBack: case Action::MovePlacementHere:
    case Action::MovePlacementLeft: case Action::MovePlacementRight: case Action::MovePlacementUp: case Action::MovePlacementDown:
    case Action::RotatePlacement: case Action::MirrorPlacement: case Action::LayerUp: case Action::LayerDown: case Action::LayerHere:
    case Action::SchematicCorner1: case Action::SchematicCorner2:
        return true;
    default: return false;
    }
}

void press(IClientInstance& client, Action action) {
    auto* player = client.getLocalPlayer();
    if (!player) return;
    auto moved = [](SavedPlacement const& p) {
        return ui::translated("schematic.toast.moved", p.name, p.placement.origin.x, p.placement.origin.y, p.placement.origin.z);
    };
    switch (action) {
    case Action::NearestMistake: nearestMistake(*player); return;
    case Action::SelectLookedPlacement: selectLooked(*player); return;
    case Action::SchematicCorner1: setCorner(client, *player, 0); return;
    case Action::SchematicCorner2: setCorner(client, *player, 1); return;
    case Action::NextPlacement: {
        std::string name;
        bool any = session::change([&](PlacementSet& set) {
            if (set.placements.empty()) return false;
            set.selected = (set.selected + 1) % static_cast<int>(set.placements.size());
            name = set.placements[static_cast<size_t>(set.selected)].name;
            return true;
        });
        ui::showMessageToast(any ? ui::translated("schematic.toast.selected", name) : ui::translated("schematic.toast.noPlacement"));
        return;
    }
    case Action::MovePlacementForward: case Action::MovePlacementBack: case Action::MovePlacementLeft:
    case Action::MovePlacementRight: case Action::MovePlacementUp: case Action::MovePlacementDown: {
        auto d = stepFor(client, action);
        changeSelected([&](SavedPlacement& p) {
            p.placement.origin.x += d.x; p.placement.origin.y += d.y; p.placement.origin.z += d.z;
        }, moved);
        return;
    }
    case Action::MovePlacementHere: {
        auto at = feet(*player);
        if (!at) return;
        int dimension = static_cast<int>(player->getDimensionId());
        changeSelected([&](SavedPlacement& p) { p.placement.origin = *at; p.dimension = dimension; }, moved);
        return;
    }
    case Action::RotatePlacement:
        changeSelected([](SavedPlacement& p) { p.placement.rotation = quarterTurns(p.placement.rotation + 1); },
            [](SavedPlacement const& p) { return ui::translated("schematic.toast.rotated", p.name, p.placement.rotation * 90); });
        return;
    case Action::MirrorPlacement:
        changeSelected([](SavedPlacement& p) { p.placement.mirror = static_cast<Mirror>((static_cast<int>(p.placement.mirror) + 1) % 3); },
            [](SavedPlacement const& p) { return ui::translated("schematic.toast.mirror", p.name, mirrorName(p.placement.mirror)); });
        return;
    case Action::LayerUp: case Action::LayerDown: {
        int direction = action == Action::LayerUp ? 1 : -1;
        auto structure = selectedStructure();
        changeSelected([&](SavedPlacement& p) {
            if (p.layers.mode == LayerMode::All) p.layers.mode = LayerMode::Only;
            else p.layers.index = std::clamp(p.layers.index + direction, 0, layers(structure.get(), p) - 1);
        }, [&](SavedPlacement const& p) { return layerText(structure.get(), p); });
        return;
    }
    case Action::LayerHere: {
        auto at = feet(*player);
        auto structure = selectedStructure();
        if (!at || !structure) return;
        changeSelected([&](SavedPlacement& p) {
            auto placed = placedSize(structure->size, p.placement.rotation);
            Point offset{at->x - p.placement.origin.x, at->y - p.placement.origin.y, at->z - p.placement.origin.z};
            p.layers.index = std::clamp(layerOf(placed, p.layers.axis, offset), 0, layerCount(placed, p.layers.axis) - 1);
            if (p.layers.mode == LayerMode::All) p.layers.mode = LayerMode::Only;
        }, [&](SavedPlacement const& p) { return layerText(structure.get(), p); });
        return;
    }
    default: return;
    }
}

menu::Target target() { return moveTarget; }
void setTarget(menu::Target value) { moveTarget = value; }

void step(IClientInstance& client, menu::Stepper stepper, int amount) {
    using S = menu::Stepper;
    auto* player = client.getLocalPlayer();
    if (!player || !amount) return;
    if (menu::repeatable(stepper)) remembered = stepper;
    switch (stepper) {
    case S::Target: {
        moveTarget = static_cast<menu::Target>(((static_cast<int>(moveTarget) + amount) % 4 + 4) % 4);
        ui::showMessageToast(ui::translated("schematic.menu.moveTarget") + ": " + value(S::Target));
        return;
    }
    case S::ForwardBack: case S::LeftRight: {
        auto axes = viewAxes(client);
        moveBy(*player, scaled(stepper == S::ForwardBack ? axes.forward : axes.right, amount));
        return;
    }
    case S::UpDown: moveBy(*player, {0, amount, 0}); return;
    case S::Rotate:
        changeSelected([&](SavedPlacement& p) { p.placement.rotation = quarterTurns(p.placement.rotation + amount); },
            [](SavedPlacement const& p) { return ui::translated("schematic.toast.rotated", p.name, p.placement.rotation * 90); });
        return;
    case S::Mirror:
        changeSelected([&](SavedPlacement& p) { p.placement.mirror = static_cast<Mirror>(((static_cast<int>(p.placement.mirror) + amount) % 3 + 3) % 3); },
            [](SavedPlacement const& p) { return ui::translated("schematic.toast.mirror", p.name, mirrorName(p.placement.mirror)); });
        return;
    case S::LayerAxis: case S::LayerMode: case S::Layer: {
        auto structure = selectedStructure();
        changeSelected([&](SavedPlacement& p) {
            if (stepper == S::LayerAxis) {
                Size placed = structure ? placedSize(structure->size, p.placement.rotation) : Size{1, 1, 1};
                p.layers = withAxis(p.layers, placed, static_cast<LayerAxis>(((static_cast<int>(p.layers.axis) + amount) % 6 + 6) % 6));
            }
            else if (stepper == S::LayerMode) p.layers.mode = static_cast<LayerMode>(((static_cast<int>(p.layers.mode) + amount) % 3 + 3) % 3);
            else if (p.layers.mode == LayerMode::All) p.layers.mode = LayerMode::Only;
            else p.layers.index += amount;
            p.layers.index = std::clamp(p.layers.index, 0, layers(structure.get(), p) - 1);
        }, [&](SavedPlacement const& p) { return layerText(structure.get(), p); });
        return;
    }
    case S::Placement: {
        std::string name;
        bool any = session::change([&](PlacementSet& set) {
            if (set.placements.empty()) return false;
            int count = static_cast<int>(set.placements.size());
            set.selected = ((set.selected + amount) % count + count) % count;
            name = set.placements[static_cast<size_t>(set.selected)].name;
            return true;
        });
        ui::showMessageToast(any ? ui::translated("schematic.toast.selected", name) : ui::translated("schematic.toast.noPlacement"));
        return;
    }
    }
}

void run(IClientInstance& client, menu::Command command) {
    using C = menu::Command;
    auto* player = client.getLocalPlayer();
    if (!player) return;
    switch (command) {
    case C::ToFeet: {
        auto at = feet(*player);
        if (!at) return;
        if (moveTarget == menu::Target::Placement) { press(client, Action::MovePlacementHere); return; }
        auto state = selection::current();
        // A corner goes to the feet; the whole area moves with corner 1 there.
        Point from = moveTarget == menu::Target::Corner2 ? state.second.value_or(*at) : state.first.value_or(*at);
        moveBy(*player, {at->x - from.x, at->y - from.y, at->z - from.z});
        return;
    }
    case C::ResetTurn:
        changeSelected([](SavedPlacement& p) { p.placement.rotation = 0; p.placement.mirror = Mirror::None; },
            [](SavedPlacement const& p) { return ui::translated("schematic.toast.rotated", p.name, 0); });
        return;
    case C::LayerHere: press(client, Action::LayerHere); return;
    case C::ShowAll:
        changeSelected([](SavedPlacement& p) { p.layers.mode = LayerMode::All; },
            [](SavedPlacement const& p) { return p.name + ": " + ui::translated("schematic.mode.all"); });
        return;
    case C::ToggleShown:
        changeSelected([](SavedPlacement& p) { p.visible = !p.visible; },
            [](SavedPlacement const& p) { return p.name + ": " + ui::translated(p.visible ? "on" : "off"); });
        return;
    case C::ToggleExtras:
        changeSelected([](SavedPlacement& p) { p.countExtras = !p.countExtras; },
            [](SavedPlacement const& p) { return p.name + ": " + ui::translated(p.countExtras ? "schematic.extras.show" : "schematic.extras.ignore"); });
        return;
    case C::ToggleEntities:
        changeSelected([](SavedPlacement& p) { p.entities = !p.entities; },
            [](SavedPlacement const& p) { return p.name + ": " + ui::translated(p.entities ? "on" : "off"); });
        return;
    case C::Corner1Here: press(client, Action::SchematicCorner1); return;
    case C::Corner2Here: press(client, Action::SchematicCorner2); return;
    case C::ClearArea:
        selection::clear();
        ui::showMessageToast(ui::translated("schematic.toast.areaCleared"));
        return;
    case C::SelectLooked: press(client, Action::SelectLookedPlacement); return;
    case C::NearestMistake: press(client, Action::NearestMistake); return;
    default: return; // screen commands
    }
}

std::string value(menu::Stepper stepper) {
    using S = menu::Stepper;
    auto set = session::current();
    SavedPlacement const* p = set.selected >= 0 && set.selected < static_cast<int>(set.placements.size())
        ? &set.placements[static_cast<size_t>(set.selected)] : nullptr;
    switch (stepper) {
    case S::Target: {
        static constexpr std::array<char const*, 4> targets{"schematic.target.placement", "schematic.target.corner1",
            "schematic.target.corner2", "schematic.target.area"};
        return ui::translated(targets[static_cast<size_t>(moveTarget)]);
    }
    case S::Placement: return p ? p->name : std::string{};
    case S::Rotate: return p ? std::format("{}°", p->placement.rotation * 90) : std::string{};
    case S::Mirror: return p ? mirrorName(p->placement.mirror) : std::string{};
    case S::LayerAxis: {
        static constexpr std::array<char const*, 6> axes{"schematic.axis.up", "schematic.axis.down", "schematic.axis.east",
            "schematic.axis.west", "schematic.axis.south", "schematic.axis.north"};
        return p ? ui::translated(axes[static_cast<size_t>(p->layers.axis)]) : std::string{};
    }
    case S::LayerMode:
        return p ? ui::translated(p->layers.mode == LayerMode::Only ? "schematic.mode.only"
            : p->layers.mode == LayerMode::UpTo ? "schematic.mode.upTo" : "schematic.mode.all") : std::string{};
    case S::Layer: {
        if (!p) return {};
        auto structure = session::structure(p->file);
        return ui::translated("schematic.layerValue", p->layers.index + 1, layers(structure.get(), *p));
    }
    default: return {};
    }
}

// What the adjust key repeats, for its toast: the item's name, and the
// move target or the current value.
std::string adjustText() {
    if (!remembered) return ui::translated("schematic.adjust.none");
    std::string name;
    for (auto const& category : menu::categories)
        for (auto const& item : category.items)
            if (item.stepper() && std::get<menu::Stepper>(item.what) == *remembered && name.empty()) name = ui::translated(item.label);
    bool moving = *remembered == menu::Stepper::ForwardBack || *remembered == menu::Stepper::LeftRight || *remembered == menu::Stepper::UpDown;
    auto shown = moving ? value(menu::Stepper::Target) : value(*remembered);
    return ui::translated("schematic.adjust.hint", shown.empty() ? name : ui::translated("schematic.withDetail", name, shown));
}
void setAdjustHeld(bool held) {
    // Pressing it says what the wheel will repeat, like other toggles' toasts.
    if (held && !adjustHeld.exchange(true)) ui::showMessageToast(adjustText());
    adjustHeld = held;
    if (!held) pendingWheel = 0;
}
std::optional<menu::Stepper> lastStepper() { return remembered; }
void startAdjust() {
    if (wheelListener) return;
    // The wheel arrives on the window's input thread: only count it here;
    // adjustFrame applies it on the client thread.
    wheelListener = ll::event::EventBus::getInstance().emplaceListener<ll::event::input::MouseInputEvent>([](auto& event) {
        if (!adjustHeld.load() || event.actionButtonId() != MouseAction::ActionWheel || event.buttonData() == 0) return;
        auto* current = adjustClient.load();
        if (!current || !lamium::gameplayScreen(current->getScreenName())) return;
        pendingWheel += event.buttonData() > 0 ? 1 : -1;
        event.cancel();
    });
}
void stopAdjust() {
    if (wheelListener) {
        ll::event::EventBus::getInstance().removeListener(wheelListener);
        wheelListener.reset();
    }
    adjustHeld = false;
    pendingWheel = 0;
}
void adjustFrame(MinecraftUIRenderContext& context, float, float) {
    IClientInstance& client = context.mClient;
    adjustClient = &client;
    if (!adjustHeld.load()) return;
    // One toast: what the wheel repeats, then what the step reported, so the
    // step's own toast does not replace the first at once.
    if (int turns = pendingWheel.exchange(0); turns && remembered) {
        step(client, *remembered, turns);
        auto shown = ui::currentToggleToast(ui::toastNow());
        auto prefix = adjustText();
        ui::showMessageToast(shown && shown->text != prefix ? prefix + "\n" + shown->text : prefix);
    }
}
}
