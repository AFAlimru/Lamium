#include "features/interaction/EdgeGuard.h"
#include "features/interaction/EdgeGuardPlan.h"
#include "app/Runtime.h"
#include "app/Versions.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/thread/ClientThreadExecutor.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/deps/ecs/gamerefs_entity/EntityContext.h"
#include "mc/deps/ecs/gamerefs_entity/EntityRegistry.h"
#include "mc/deps/ecs/strict/StrictEntityContext.h"
#include "mc/deps/vanilla_components/AABBShapeComponent.h"
#include "mc/deps/vanilla_components/IConstBlockSource.h"
#include "mc/deps/vanilla_components/MoveRequestComponent.h"
#include "mc/entity/components/MoveInputComponent.h"
#include "mc/entity/components/RawMoveInputComponent.h"
#include "mc/entity/systems/move_collision_system/MoveCollisionSystem.h"
#include "mc/input/MoveInputState.h"
#include "mc/world/actor/provider/PlayerMovement.h"
#include "mc/world/gamemode/GameMode.h"
#include "mc/world/gamemode/InteractionResult.h"
#include "mc/world/gamemode/SurvivalMode.h"
#include "mc/world/item/ItemStack.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/block/BlockType.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"
#include <cmath>
#ifdef LAMIUM_EDGE_TRACE
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/packet/CorrectPlayerMovePredictionPacket.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include <deque>
#include <format>
#include <string>
#endif
#include <atomic>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <vector>

namespace lamium::interaction::edgeGuard {
namespace {
// Drops up to this height (stairs, slabs, a carpet) stay walkable, as with
// vanilla sneaking.
constexpr float stepHeight = 0.6f;
// Ground friction of ordinary blocks (block 0.6 times 0.91): the velocity kept
// per tick. Slipperier blocks slide further than the input guard expects.
constexpr double groundFriction = 0.546;
// The move-input guard keeps the feet this far over the block, not touching
// its edge: released from sneaking right at an edge, a server drops a player
// whose box only touches the block (seen on BDS, L-160).
constexpr float overlapMargin = 0.02f;
bool installed = false;
bool reportedServerCopy = false;
bool reportedInput = false;
// A local world's server computes move vectors on its own thread; only the
// client's are guarded there, and client state is read only on its thread.
std::atomic<std::thread::id> clientThread;
#ifdef LAMIUM_EDGE_TRACE
// L-160 trace: the last lines around the guard's activity are kept in memory
// and written out only when the player falls soon after the guard acted.
int traceQuiet = 1000, traceDumps = 0, traceSends = 0;
std::deque<std::string> traceRing;
bool traceFalling = false;
float traceGroundY = 0;
void trace(std::string text) {
    traceRing.push_back(std::format("#{} {}", traceSends, std::move(text)));
    if (traceRing.size() > 200) traceRing.pop_front();
}
void traceFall(float y, bool ground) {
    if (ground) { traceGroundY = y; traceFalling = false; return; }
    if (traceFalling || y > traceGroundY - 1.2f) return;
    traceFalling = true;
    if (traceQuiet > 100 || traceDumps >= 12) return;
    ++traceDumps;
    auto& logger = Runtime::instance().self().getLogger();
    logger.info("L-160 fall {} after the guard acted {} sends ago; last {} lines:", traceDumps, traceQuiet, traceRing.size());
    for (auto const& line : traceRing) logger.info("L-160 {}", line);
    traceRing.clear();
}
char const* stateName(MoveInputState const& state, LocalPlayer& player) {
    auto& context = player.getEntityContext();
    auto raw = context.tryGetComponent<RawMoveInputComponent>();
    auto input = context.tryGetComponent<MoveInputComponent>();
    if (raw && &state == &*raw->mRawInput) return "raw";
    if (input && &state == &*input->mInputState) return "input";
    if (input && &state == &*input->mRawInputState) return "inputRaw";
    return "other";
}
std::string flagText(MoveInputState const& state) {
    using F = MoveInputState::Flag;
    std::string out;
    for (auto [flag, name] : {std::pair{F::Up, "U"}, {F::Down, "D"}, {F::Left, "L"}, {F::Right, "R"}, {F::UpLeft, "UL"},
                              {F::UpRight, "UR"}, {F::JumpDown, "J"}, {F::SprintDown, "S"}, {F::SneakDown, "Sh"}})
        if (state.mFlagValues->test(static_cast<size_t>(flag))) { out += name; out += ' '; }
    return out;
}
#endif

bool isLocal(StrictEntityContext const& entity, LocalPlayer& player) {
    auto const& own = player.getEntityContext();
    return static_cast<EntityId const&>(entity.mEntity) == own.mEntity
        && static_cast<uint const&>(entity.mRegistryId) == static_cast<uint const&>(own.mRegistry.mId);
}
// Walking on the ground, the case both guards cover.
bool walking(LocalPlayer& player, bool sneakingToo = false) {
    return player.isOnGround() && (sneakingToo || !player.isSneaking()) && !player.isFlying() && !player.isGliding()
        && !player.isSwimming() && !player.isInWater() && !player.getVehicle();
}
void guard(StrictEntityContext const& entity, AABBShapeComponent const& shape, MoveRequestComponent& request,
           IConstBlockSource const& region, GetCollisionShapeInterface const& collisionShape) {
    auto& runtime = Runtime::instance();
    if (!runtime.enabled() || !runtime.snapshot()->interaction.edgeGuard) return;
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return;
    // In a local world the integrated server moves its own copy of this
    // player and corrects the client to it, so that copy is guarded too. It
    // is the other registry's entity whose box sits where the player's does.
    if (!isLocal(entity, *player)) {
        auto const& own = player->getAABB();
        auto const& other = shape.mAABB.get();
        if (std::abs(own.min.x - other.min.x) > .3f || std::abs(own.min.y - other.min.y) > .3f
            || std::abs(own.min.z - other.min.z) > .3f || std::abs((own.max.x - own.min.x) - (other.max.x - other.min.x)) > .05f)
            return;
        if (!reportedServerCopy) {
            reportedServerCopy = true;
            runtime.self().getLogger().info("Edge Guard: also guarding the local server's copy of the player");
        }
    }
    // Walking on the ground, sneaking too: vanilla sneaking stops with the box
    // only touching an edge, which a server treats as no support (L-160).
    if (!walking(*player, true)) return;
    Vec3& speed = request.mSpeed;
    if (speed.y > 0) return; // A jump may leave the edge on purpose.
    AABB const& box = shape.mAABB;
    std::vector<AABB> shapes;
    auto supported = [&](double dx, double dz) {
        // The margin keeps the box over the block: sliding along a wall into
        // a corner left it only touching, and the client then fell (L-160).
        AABB probe{Vec3{box.min.x + overlapMargin + static_cast<float>(dx), box.min.y - stepHeight,
                        box.min.z + overlapMargin + static_cast<float>(dz)},
                   Vec3{box.max.x - overlapMargin + static_cast<float>(dx), box.min.y,
                        box.max.z - overlapMargin + static_cast<float>(dz)}};
        shapes.clear();
        region.fetchCollisionShapes(shapes, probe, false, collisionShape, nullptr);
        return !shapes.empty();
    };
    if (!supported(0, 0)) return; // Already over a drop: do not freeze in the air.
    auto [dx, dz] = guardEdge(speed.x, speed.z, supported);
    speed.x = static_cast<float>(dx);
    speed.z = static_cast<float>(dz);
}
// L-160: a server moves the player from the move vector the client sends, so
// the vector itself is guarded where vanilla computes it from the input state
// (sent and simulated alike). Only the local player's own states are touched.
void guardMoveVector(MoveInputState const& state, Vec2& move) {
    if (std::this_thread::get_id() != clientThread.load()) return;
#ifdef LAMIUM_EDGE_TRACE
    Vec2 before = move;
    struct Report {
        MoveInputState const& state; Vec2 const& before; Vec2& move;
        ~Report() {
            try {
                auto client = ll::service::getClientInstance();
                auto* player = client ? client->getLocalPlayer() : nullptr;
                if (!player) return;
                bool changed = before.x != move.x || before.z != move.z;
                if (changed) traceQuiet = 0;
                if (traceQuiet > 100) return;
                char const* name = stateName(state, *player);
                if (std::string_view{name} == "other") return; // Many calls a tick; they filled the ring.
                auto const& pos = player->getPosition();
                auto const& delta = player->getPosDelta();
                trace(std::format("calc {} flags [{}] analog {:.2f},{:.2f} move {:.2f},{:.2f} -> {:.2f},{:.2f} pos {:.3f},{:.3f} v {:.3f},{:.3f} ground {}",
                    name, flagText(state), state.mAnalogMoveVector->x, state.mAnalogMoveVector->z,
                    before.x, before.z, move.x, move.z, pos.x, pos.z, delta.x, delta.z, player->isOnGround()));
                if (std::string_view{name} != "raw") return;
                // What the support probes can find around the feet (0.5 beyond the box).
                auto const& box = player->getAABB();
                std::vector<AABB> found;
                AABB around{Vec3{box.min.x - .5f, box.min.y - stepHeight, box.min.z - .5f}, Vec3{box.max.x + .5f, box.min.y + .5f, box.max.z + .5f}};
                player->getDimensionBlockSourceConst().fetchCollisionShapes(found, around, false, nullptr, nullptr);
                std::string text = std::format("feet {:.3f} box x {:.3f}..{:.3f} z {:.3f}..{:.3f} shapes", box.min.y, box.min.x, box.max.x,
                    box.min.z, box.max.z);
                for (size_t i = 0; i < found.size() && i < 8; ++i)
                    text += std::format(" [{:.2f}..{:.2f} y{:.2f}..{:.2f} {:.2f}..{:.2f}]", found[i].min.x, found[i].max.x, found[i].min.y,
                        found[i].max.y, found[i].min.z, found[i].max.z);
                trace(text);
            } catch (...) {}
        }
    } report{state, before, move};
#endif
    auto& runtime = Runtime::instance();
    if (!runtime.enabled() || !runtime.snapshot()->interaction.edgeGuard) return;
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player) return;
    auto& context = player->getEntityContext();
    auto raw = context.tryGetComponent<RawMoveInputComponent>();
    auto input = context.tryGetComponent<MoveInputComponent>();
    bool own = (raw && &state == &*raw->mRawInput)
        || (input && (&state == &*input->mInputState || &state == &*input->mRawInputState));
    // Sneaking too: the guard then keeps the margin, so letting go of sneak
    // at an edge does not leave the player only touching it.
    if (!own || !walking(*player, true)) return;
    if (state.mFlagValues->test(static_cast<size_t>(MoveInputState::Flag::JumpDown))) return; // May leave on purpose.
    auto const& box = player->getAABB();
    auto const& region = player->getDimensionBlockSourceConst();
    std::vector<AABB> shapes;
    auto supported = [&](double dx, double dz) {
        AABB probe{Vec3{box.min.x + overlapMargin + static_cast<float>(dx), box.min.y - stepHeight,
                        box.min.z + overlapMargin + static_cast<float>(dz)},
                   Vec3{box.max.x - overlapMargin + static_cast<float>(dx), box.min.y,
                        box.max.z - overlapMargin + static_cast<float>(dz)}};
        shapes.clear();
        region.fetchCollisionShapes(shapes, probe, false, nullptr, nullptr);
        return !shapes.empty();
    };
    double yaw = player->getRotation().y;
    auto [x, z] = worldMove(move.x, move.z, yaw);
    double gx = x, gz = z;
    if (supported(0, 0)) {
        auto const& velocity = player->getPosDelta();
        double accel = std::max(0.1, static_cast<double>(player->getSpeed()));
        std::tie(gx, gz) = guardInput(x, z, velocity.x, velocity.z, accel, groundFriction, supported);
    } else if (auto back = stepBack([&](double dx, double dz) {
                   // Only where the body can go: in a corner the first
                   // supported direction was into the wall (L-160).
                   AABB body{Vec3{box.min.x + static_cast<float>(dx), box.min.y + 0.1f, box.min.z + static_cast<float>(dz)},
                             Vec3{box.max.x + static_cast<float>(dx), box.max.y, box.max.z + static_cast<float>(dz)}};
                   shapes.clear();
                   region.fetchCollisionShapes(shapes, body, false, nullptr, nullptr);
                   return shapes.empty() && supported(dx, dz);
               })) {
        std::tie(gx, gz) = *back; // Only touching an edge: back onto the block.
    } else {
        return; // Already over a drop: do not freeze in the air.
    }
    if (gx == x && gz == z) return;
    auto [strafe, forward] = inputMove(gx, gz, yaw);
    move.x = static_cast<float>(strafe);
    move.z = static_cast<float>(forward);
    // The server rebuilds the vector from the sent input state (seen on BDS:
    // the packet's move was guarded, its direction keys were not, and the
    // server's correction followed the keys). The state therefore carries the
    // guarded vector as an analog input without direction keys; it is rebuilt
    // from the devices every tick.
    using Flag = MoveInputState::Flag;
    auto& sent = const_cast<MoveInputState&>(state);
    for (auto flag : {Flag::Up, Flag::Down, Flag::Left, Flag::Right, Flag::UpLeft, Flag::UpRight, Flag::DownLeft, Flag::DownRight})
        sent.mFlagValues->set(static_cast<size_t>(flag), false);
    // The sent input is scaled for sneaking again (0.3, seen in the trace).
    float scale = player->isSneaking() ? 0.3f : 1.0f;
    Vec2 analog{move.x / scale, move.z / scale};
    *sent.mAnalogMoveVector = analog;
    if (raw && &state == &*raw->mRawInput) *raw->mRawMove = analog;
    if (!reportedInput) {
        reportedInput = true;
        runtime.self().getLogger().info("Edge Guard: guarding the move input");
    }
}
// L-161: sneaking refuses a block placed into the player's own cell; walking
// with Edge Guard does not, and a slab merged into the slab the player barely
// stands on pushes them out sideways, off the edge. Refused like sneaking,
// only then: a merge under the player's center lifts them and stays allowed.
bool refusesMerge(Player& actor, ItemStack const& item, BlockPos const& pos, uchar face) {
    auto& runtime = Runtime::instance();
    if (!runtime.enabled() || !runtime.snapshot()->interaction.edgeGuard) return false;
    auto client = ll::service::getClientInstance();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player || static_cast<Player*>(player) != &actor || !walking(*player)) return false;
    Block const* held = item.mBlock;
    if (!held || !held->getBlockType().isSlabBlock()) return false;
    auto const& box = player->getAABB();
    Box body{box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z};
    auto const& region = player->getDimensionBlockSourceConst();
    auto pushes = [&](BlockPos const& cell) {
        return region.getBlock(cell).getBlockType().isSlabBlock() && mergePushesOff(body, cell.x, cell.y, cell.z);
    };
    BlockPos next = pos;
    switch (face) {
    case 0: --next.y; break;
    case 1: ++next.y; break;
    case 2: --next.z; break;
    case 3: ++next.z; break;
    case 4: --next.x; break;
    case 5: ++next.x; break;
    default: return false;
    }
    // The clicked slab merges only through its top or bottom face.
    return ((face == 0 || face == 1) && pushes(pos)) || pushes(next);
}
bool refuses(Player& actor, ItemStack const& item, BlockPos const& pos, uchar face) {
    try {
        return versionSensitiveAllowed("Edge Guard (movement request)") && refusesMerge(actor, item, pos, face);
    } catch (...) { return false; }
}
LL_TYPE_INSTANCE_HOOK(EdgeGuardPlaceHook, ll::memory::HookPriority::Normal, GameMode,
    &GameMode::$useItemOn, InteractionResult, ItemStack& item, BlockPos const& pos, uchar face,
    Vec3 const& hit, HandSlot hand, Block const* target, bool first) {
    if (refuses(mPlayer, item, pos, face)) return InteractionResult{false, false};
    return origin(item, pos, face, hit, hand, target, first);
}
// SurvivalMode overrides useItemOn; the base hook does not run in survival.
LL_TYPE_INSTANCE_HOOK(EdgeGuardSurvivalPlaceHook, ll::memory::HookPriority::Normal, SurvivalMode,
    &SurvivalMode::$useItemOn, InteractionResult, ItemStack& item, BlockPos const& pos, uchar face,
    Vec3 const& hit, HandSlot hand, Block const* target, bool first) {
    if (refuses(mPlayer, item, pos, face)) return InteractionResult{false, false};
    return origin(item, pos, face, hit, hand, target, first);
}
LL_STATIC_HOOK(EdgeGuardInputHook, ll::memory::HookPriority::Normal, &PlayerMovement::calculateMoveVector, Vec2,
    MoveInputState const& inputState, bool playerIsFlying, ActorDataFlagComponent const& playerData, bool playerIsInWater,
    SneakingComponent const* sneakComponent) {
    auto move = origin(inputState, playerIsFlying, playerData, playerIsInWater, sneakComponent);
    try {
        if (versionSensitiveAllowed("Edge Guard (movement request)")) guardMoveVector(inputState, move);
    } catch (...) {}
    return move;
}
LL_STATIC_HOOK(EdgeGuardHook, ll::memory::HookPriority::Normal, &MoveCollisionSystem::fetchCollisionShapes, void,
    StrictEntityContext const& entity, AABBShapeComponent const& aabb, MaxAutoStepComponent const& autoStep,
    Optional<CollidableMobNearFlagComponent const> collidableMobNear, MoveRequestComponent& request,
    Optional<MinecartFlagComponent const> isMinecart,
    ViewT<StrictEntityContext, Include<CollidableMobFlagComponent>, AABBShapeComponent const> const& collidableMobs,
    ViewT<StrictEntityContext, AABBShapeComponent const, ActorDataFlagComponent const> const& stackableView,
    ViewT<StrictEntityContext, Include<FallingBlockFlagComponent>> const& fallingBlocks, IConstBlockSource const& region,
    LocalSpatialEntityFetcher& fetcher, GetCollisionShapeInterface const& collisionShape,
    std::vector<BlockSourceVisitor::CollisionShape>& tempCollisionShapes,
    std::vector<BlockSourceVisitor::CollisionShape>& scratchCollisionShapes, std::vector<AABB>& tempShapes) {
    try {
        if (versionSensitiveAllowed("Edge Guard (movement request)")) guard(entity, aabb, request, region, collisionShape);
    } catch (...) {}
    origin(entity, aabb, autoStep, collidableMobNear, request, isMinecart, collidableMobs, stackableView, fallingBlocks,
        region, fetcher, collisionShape, tempCollisionShapes, scratchCollisionShapes, tempShapes);
}
}
#ifdef LAMIUM_EDGE_TRACE
using AuthInput = PlayerAuthInputPacketPayload::InputData;
LL_TYPE_INSTANCE_HOOK(EdgeTraceSend, ll::memory::HookPriority::Normal, LoopbackPacketSender,
    &LoopbackPacketSender::$sendToServer, void, Packet& packet) {
    try {
        if (packet.getId() == MinecraftPacketIds::PlayerAuthInputPacket) {
            ++traceSends;
            ++traceQuiet;
            auto& auth = static_cast<PlayerAuthInputPacket&>(packet);
            auto client = ll::service::getClientInstance();
            auto* player = client ? client->getLocalPlayer() : nullptr;
            if (player) traceFall(auth.mPos->y, player->isOnGround());
            if (traceQuiet > 100) { origin(packet); return; }
            auto& data = *auth.mInputData;
            std::string flags;
            for (auto [flag, name] : {std::pair{AuthInput::Up, "U"}, {AuthInput::Down, "D"}, {AuthInput::Left, "L"},
                                      {AuthInput::Right, "R"}, {AuthInput::UpLeft, "UL"}, {AuthInput::UpRight, "UR"},
                                      {AuthInput::Sprinting, "S"}, {AuthInput::Sneaking, "Sn"}, {AuthInput::HorizontalCollision, "HC"},
                                      {AuthInput::VerticalCollision, "VC"}, {AuthInput::PerformItemInteraction, "Item"},
                                      {AuthInput::PerformBlockActions, "Block"}, {AuthInput::StartJumping, "Jump"},
                                      {AuthInput::HandledTeleport, "Tp"}})
                if (data.contains(flag)) { flags += name; flags += ' '; }
            trace(std::format("send move {:.2f},{:.2f} raw {:.2f},{:.2f} analog {:.2f},{:.2f} pos {:.3f},{:.3f},{:.3f} delta {:.3f},{:.3f} flags [{}]",
                auth.mMove->x, auth.mMove->z, auth.mRawMoveVector->x, auth.mRawMoveVector->z, auth.mAnalogMoveVector->x,
                auth.mAnalogMoveVector->z, auth.mPos->x, auth.mPos->y, auth.mPos->z, auth.mPosDelta->x, auth.mPosDelta->z, flags));
        }
    } catch (...) {}
    origin(packet);
}
LL_TYPE_INSTANCE_HOOK(EdgeTraceCorrection, ll::memory::HookPriority::Normal, ClientNetworkHandler,
    static_cast<void (ClientNetworkHandler::*)(NetworkIdentifier const&, CorrectPlayerMovePredictionPacket const&)>(
        &ClientNetworkHandler::$handle),
    void, NetworkIdentifier const& source, CorrectPlayerMovePredictionPacket const& packet) {
    try {
        if (traceQuiet <= 200)
            trace(std::format("correction pos {:.3f},{:.3f},{:.3f} delta {:.3f},{:.3f},{:.3f} ground {}", packet.mPos->x,
                packet.mPos->y, packet.mPos->z, packet.mPosDelta->x, packet.mPosDelta->y, packet.mPosDelta->z, static_cast<bool>(packet.mOnGround)));
    } catch (...) {}
    origin(source, packet);
}
#endif
void start() {
    if (installed) return;
#ifdef LAMIUM_EDGE_TRACE
    EdgeTraceSend::hook();
    EdgeTraceCorrection::hook();
    Runtime::instance().self().getLogger().warn("Edge Guard diagnostics enabled (L-160)");
#endif
    ll::thread::ClientThreadExecutor::getDefault().execute([] { clientThread.store(std::this_thread::get_id()); });
    if (EdgeGuardHook::hook(true) != 0) throw std::runtime_error("Could not install the edge guard hook");
    if (EdgeGuardInputHook::hook(true) != 0) {
        EdgeGuardHook::unhook(true);
        throw std::runtime_error("Could not install the edge guard input hook");
    }
    if (EdgeGuardPlaceHook::hook(true) != 0 || EdgeGuardSurvivalPlaceHook::hook(true) != 0) {
        EdgeGuardPlaceHook::unhook(true);
        EdgeGuardInputHook::unhook(true);
        EdgeGuardHook::unhook(true);
        throw std::runtime_error("Could not install the edge guard placement hooks");
    }
    installed = true;
}
void stop() {
    if (!installed) return;
    bool place = EdgeGuardPlaceHook::unhook(true), survival = EdgeGuardSurvivalPlaceHook::unhook(true);
    bool input = EdgeGuardInputHook::unhook(true), movement = EdgeGuardHook::unhook(true);
    if (place && survival && input && movement) installed = false;
}
}
