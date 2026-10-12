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
#include "mc/world/level/BlockSource.h"
#include "mc/world/phys/AABB.h"
#include <cmath>
#ifdef LAMIUM_EDGE_TRACE
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/network/LoopbackPacketSender.h"
#include "mc/network/MinecraftPacketIds.h"
#include "mc/network/packet/CorrectPlayerMovePredictionPacket.h"
#include "mc/network/packet/PlayerAuthInputPacket.h"
#include <format>
#include <string>
#endif
#include <atomic>
#include <stdexcept>
#include <thread>
#include <vector>

namespace lamium::interaction::edgeGuard {
namespace {
// Drops up to this height (stairs, slabs, a carpet) stay walkable, as with
// vanilla sneaking.
constexpr float stepHeight = 0.6f;
// Ground friction of ordinary blocks (block 0.6 times 0.91): the velocity kept
// per tick. Slipperier blocks slide further than the input guard expects.
constexpr double groundFriction = 0.546;
bool installed = false;
bool reportedServerCopy = false;
bool reportedInput = false;
// A local world's server computes move vectors on its own thread; only the
// client's are guarded there, and client state is read only on its thread.
std::atomic<std::thread::id> clientThread;
#ifdef LAMIUM_EDGE_TRACE
// L-160 trace: bounded lines while the guard is acting (and shortly after).
int traceLines = 0, traceQuiet = 1000;
void trace(std::string const& text) {
    if (traceLines >= 600) return;
    ++traceLines;
    Runtime::instance().self().getLogger().info("L-160 {}", text);
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
                              {F::UpRight, "UR"}, {F::JumpDown, "J"}, {F::SprintDown, "S"}})
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
bool walking(LocalPlayer& player) {
    return player.isOnGround() && !player.isSneaking() && !player.isFlying() && !player.isGliding()
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
    // Only walking on the ground; vanilla sneaking already guards edges.
    if (!walking(*player)) return;
    Vec3& speed = request.mSpeed;
    if (speed.y > 0) return; // A jump may leave the edge on purpose.
    AABB const& box = shape.mAABB;
    std::vector<AABB> shapes;
    auto supported = [&](double dx, double dz) {
        AABB probe{Vec3{box.min.x + static_cast<float>(dx), box.min.y - stepHeight, box.min.z + static_cast<float>(dz)},
                   Vec3{box.max.x + static_cast<float>(dx), box.min.y, box.max.z + static_cast<float>(dz)}};
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
    if (std::this_thread::get_id() != clientThread.load() || (move.x == 0 && move.z == 0)) return;
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
                if (traceQuiet > 40) return;
                auto const& pos = player->getPosition();
                auto const& delta = player->getPosDelta();
                trace(std::format("calc {} flags [{}] analog {:.2f},{:.2f} move {:.2f},{:.2f} -> {:.2f},{:.2f} pos {:.3f},{:.3f} v {:.3f},{:.3f} ground {}",
                    stateName(state, *player), flagText(state), state.mAnalogMoveVector->x, state.mAnalogMoveVector->z,
                    before.x, before.z, move.x, move.z, pos.x, pos.z, delta.x, delta.z, player->isOnGround()));
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
    if (!own || !walking(*player)) return;
    if (state.mFlagValues->test(static_cast<size_t>(MoveInputState::Flag::JumpDown))) return; // May leave on purpose.
    auto const& box = player->getAABB();
    auto const& region = player->getDimensionBlockSourceConst();
    std::vector<AABB> shapes;
    auto supported = [&](double dx, double dz) {
        AABB probe{Vec3{box.min.x + static_cast<float>(dx), box.min.y - stepHeight, box.min.z + static_cast<float>(dz)},
                   Vec3{box.max.x + static_cast<float>(dx), box.min.y, box.max.z + static_cast<float>(dz)}};
        shapes.clear();
        region.fetchCollisionShapes(shapes, probe, false, nullptr, nullptr);
        return !shapes.empty();
    };
    if (!supported(0, 0)) return;
    double yaw = player->getRotation().y;
    auto [x, z] = worldMove(move.x, move.z, yaw);
    auto const& velocity = player->getPosDelta();
    double accel = std::max(0.1, static_cast<double>(player->getSpeed()));
    auto [gx, gz] = guardInput(x, z, velocity.x, velocity.z, accel, groundFriction, supported);
    if (gx == x && gz == z) return;
    auto [strafe, forward] = inputMove(gx, gz, yaw);
    move.x = static_cast<float>(strafe);
    move.z = static_cast<float>(forward);
    if (!reportedInput) {
        reportedInput = true;
        runtime.self().getLogger().info("Edge Guard: guarding the move input");
    }
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
        if (packet.getId() == MinecraftPacketIds::PlayerAuthInputPacket && traceQuiet <= 40) {
            ++traceQuiet;
            auto& auth = static_cast<PlayerAuthInputPacket&>(packet);
            auto& data = *auth.mInputData;
            std::string flags;
            for (auto [flag, name] : {std::pair{AuthInput::Up, "U"}, {AuthInput::Down, "D"}, {AuthInput::Left, "L"},
                                      {AuthInput::Right, "R"}, {AuthInput::UpLeft, "UL"}, {AuthInput::UpRight, "UR"},
                                      {AuthInput::Sprinting, "S"}, {AuthInput::Sneaking, "Sn"}, {AuthInput::HorizontalCollision, "HC"}})
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
        if (traceQuiet <= 80)
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
    installed = true;
}
void stop() {
    if (!installed) return;
    bool input = EdgeGuardInputHook::unhook(true), movement = EdgeGuardHook::unhook(true);
    if (input && movement) installed = false;
}
}
