#include "features/camera/FreeCameraCulling.h"
#include "features/camera/CameraSessions.h"
#include "app/Runtime.h"
#include "app/Versions.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/client/renderer/game/LevelRenderer.h"
#include "mc/client/renderer/game/LevelRendererPlayer.h"
#include "mc/legacy/ActorRuntimeID.h"
#include "mc/world/level/BlockPos.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/block/Block.h"
#include "mc/world/level/dimension/Dimension.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <vector>
#pragma comment(lib, "version.lib")

namespace lamium::camera {
namespace {
constexpr auto capability = capabilities::Id::FreeCameraTerrain;
bool enabled() { return capabilities::registry().status(capability).canProbe(); }
std::atomic<DWORD> rendererThread{0};
std::uintptr_t cullerTarget = 0;
bool renderInstalled = false, cullerInstalled = false;
std::atomic<bool> appliedLogged{false};
CullerCheck cullerCheck; // Renderer thread only.
SpectatorHold spectatorHold; // Renderer thread only, the primary camera's requests.
double seconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

void unavailable(char const* reason, capabilities::Reason kind = capabilities::Reason::NativeContract) noexcept {
    capabilities::fail(capability,kind,reason);
}

bool gameCode(void* address) {
    MEMORY_BASIC_INFORMATION page{};
    if (!VirtualQuery(address, &page, sizeof(page)) || page.State != MEM_COMMIT
        || page.AllocationBase != GetModuleHandleW(nullptr)
        || (page.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    auto protection = page.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ
        || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

LL_TYPE_INSTANCE_HOOK(TerrainCullerRequest, ll::memory::HookPriority::Normal, LevelRendererCameraListeners,
    cullerTarget, void, LevelCullerType requested) {
    bool own = false, free = false, inside = false;
    try {
        if (enabled() && GetCurrentThreadId() == rendererThread.load()) {
            auto& renderer = static_cast<LevelRenderer&>(mLevelRenderer);
            std::shared_ptr<LevelRendererPlayer> const& primary = renderer.mLevelRendererPlayer;
            own = primary && static_cast<LevelRendererCamera*>(primary.get()) == static_cast<LevelRendererCamera*>(this);
            auto& client = static_cast<IClientInstance&>(renderer.mClientInstance);
            auto* player = own ? client.getLocalPlayer() : nullptr;
            free = player && player->hasRuntimeID()
                && CameraSessions::instance().freeCameraFor(client, player->getRuntimeID().rawID);
            // The spectator culler draws terrain while the camera is inside
            // it, where the normal culler clips at chunk borders (L-37). In
            // open space — above ground, in caves, under the Nether ceiling —
            // the normal culler renders the view normally, so the extra
            // frame cost is not paid (L-207).
            if (free) {
                if (auto ray = CameraSessions::instance().detachedViewRay(client)) {
                    BlockPos at{static_cast<int>(std::floor(ray->x)), static_cast<int>(std::floor(ray->y)),
                                static_cast<int>(std::floor(ray->z))};
                    auto const& range = player->getDimension().mHeightRange;
                    auto& region = player->getDimensionBlockSource();
                    // An unloaded column or a height outside the dimension
                    // reads as no terrain, never as a surface to swap for.
                    if (at.y >= range->mMin && at.y < range->mMax && region.getChunkAt(at))
                        inside = !region.getBlock(at).isAir();
                }
            }
        }
    } catch (...) { unavailable("native request ownership could not be verified"); }
    auto native = static_cast<std::uint8_t>(requested);
    // Other render stages' requests never move the primary camera's hold.
    int choice = undergroundHoldDefault;
    try { if (own) choice = Runtime::instance().snapshot()->camera.freeCameraUndergroundHold; } catch (...) {}
    bool wanted = own && spectatorHold.wanted(free, inside, seconds(), choice);
    auto selected = terrainCuller(native, wanted && versionSensitiveAllowed(capability));
    origin(static_cast<LevelCullerType>(selected));
    if (!own || !enabled() || native != 3) return;
    // Observe the unmodified call first; never guess an ABI from the slot alone.
    bool retained = static_cast<std::uint8_t>(static_cast<LevelCullerType const&>(mLastCullerType)) == selected;
    if (cullerCheck.failed(retained)) {
        unavailable("native culler update did not retain its requested type");
        return;
    }
    if (!retained) return;
    capabilities::ready(capability);
    if (selected != native && !appliedLogged.exchange(true)) {
        try { Runtime::instance().self().getLogger().info("FreeCamera terrain: native request 3 -> 5 retained"); }
        catch (...) {}
    }
}

bool bindCuller(LevelRendererPlayer& camera) {
    static auto const index = ll::memory::getVtableIndex(&LevelRendererCamera::updateLevelCullerType);
    if (!index || *index > 64) { unavailable("unsupported virtual-call thunk"); return false; }
    auto* base = static_cast<LevelRendererCamera*>(&camera);
    auto** table = *reinterpret_cast<void***>(base);
    void* target = table[*index];
    if (!gameCode(target)) { unavailable("culler entry is outside game executable code"); return false; }
    auto address = reinterpret_cast<std::uintptr_t>(target);
    if (cullerInstalled) {
        if (address != cullerTarget) { unavailable("renderer culler implementation changed",capabilities::Reason::TargetChanged); return false; }
        return true;
    }
    // Hook the implementation obtained from the SDK virtual method, not a
    // missing exported thunk or a hard-coded slot/address.
    if (cullerTarget && cullerTarget != address) { unavailable("culler entry changed after restart",capabilities::Reason::TargetChanged); return false; }
    cullerTarget = address;
    if (TerrainCullerRequest::hook(true) != 0) { unavailable("culler hook unavailable",capabilities::Reason::HookUnavailable); return false; }
    cullerInstalled = true;
    Runtime::instance().self().getLogger().info("FreeCamera terrain: culler hook bound at virtual slot {}", *index);
    return true;
}

LL_TYPE_INSTANCE_HOOK(TerrainPreRender, ll::memory::HookPriority::Normal, LevelRenderer,
    &LevelRenderer::preRenderUpdate, void,
    ScreenContext& context, LevelRenderPreRenderUpdateParameters& parameters) {
    // Bind from the real primary renderer. Later native requests may come
    // from another render stage; only this render thread can override them.
    try {
        std::shared_ptr<LevelRendererPlayer> const& camera = mLevelRendererPlayer;
        auto& client = static_cast<IClientInstance&>(mClientInstance);
        auto* player = client.getLocalPlayer();
        if (enabled() && camera && player && player->hasRuntimeID()
            && CameraSessions::instance().freeCameraFor(client, player->getRuntimeID().rawID)) {
            rendererThread = GetCurrentThreadId();
            bindCuller(*camera);
        }
    } catch (...) { unavailable("renderer binding failed"); }
    origin(context, parameters);
}
}

void startTerrainCulling() noexcept {
    if (renderInstalled || cullerInstalled) { unavailable("previous terrain hooks could not be removed"); return; }
    capabilities::begin(capability);
    appliedLogged = false;
    cullerCheck = {};
    spectatorHold = {};
    try {
        if (capabilities::registry().status(capability).compatibility != capabilities::Reason::None) {
            versionSensitiveAllowed(capability); return;
        }
        if (TerrainPreRender::hook(true) != 0) { unavailable("pre-render hook unavailable",capabilities::Reason::HookUnavailable); return; }
        renderInstalled = true;
        Runtime::instance().self().getLogger().info("FreeCamera terrain: adapter armed; awaiting FreeCamera renderer");
    } catch (...) { unavailable("initialization failed"); }
}

void stopTerrainCulling() noexcept {
    capabilities::stop(capability);
    rendererThread = 0;
    try {
        if (renderInstalled) {
            if (TerrainPreRender::unhook(true)) renderInstalled = false;
            else unavailable("pre-render hook removal failed",capabilities::Reason::RemovalFailed);
        }
    } catch (...) { unavailable("pre-render hook removal failed",capabilities::Reason::RemovalFailed); }
    try {
        if (cullerInstalled) {
            if (TerrainCullerRequest::unhook(true)) cullerInstalled = false;
            else unavailable("culler hook removal failed",capabilities::Reason::RemovalFailed);
        }
    } catch (...) { unavailable("culler hook removal failed",capabilities::Reason::RemovalFailed); }
}
}
