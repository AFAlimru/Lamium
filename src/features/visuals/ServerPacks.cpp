#include "features/visuals/ServerPacks.h"
#include "app/Runtime.h"
#include "app/Versions.h"
#include "ll/api/memory/Hook.h"
#include "mc/client/events/PlayerJoinWorldContext.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/network/packet/ResourcePackStackPacket.h"
#include <atomic>

namespace lamium::visuals::serverPacks {
namespace {
// Vanilla says at the start of each join whether it is a local world; a join
// it did not announce keeps its packs.
enum class Join { Unknown, Local, Remote };
std::atomic<Join> join{Join::Unknown};
std::atomic<int> skipped{0};
bool hooked = false;
}
LL_TYPE_INSTANCE_HOOK(ServerPacksJoin, ll::memory::HookPriority::Normal, ClientInstance, &ClientInstance::$onStartJoinGame,
    void, bool isLocalServer, std::string const& correlation, std::string const& serverName, std::string const& worldName,
    ::NetworkType networkType, ::Social::MultiplayerServiceIdentifier service, ::PlayerJoinWorldContext context) {
    join.store(isLocalServer ? Join::Local : Join::Remote);
    skipped.store(0);
    origin(isLocalServer, correlation, serverName, worldName, networkType, service, context);
}
// The stack to apply arrives after the download handshake has finished, so
// the server already has the client's acceptance; the packet is consumed by
// this call only.
LL_TYPE_INSTANCE_HOOK(ServerPacksStack, ll::memory::HookPriority::Normal, ClientNetworkHandler,
    &ClientNetworkHandler::$handle, void, NetworkIdentifier const& source, ResourcePackStackPacket const& packet) {
    try {
        auto& payload = const_cast<ResourcePackStackPacketPayload&>(static_cast<ResourcePackStackPacketPayload const&>(packet));
        auto& ids = payload.mTexturePackIdsAndVersions.get();
        if (join.load() == Join::Remote && !ids.empty() && Runtime::instance().snapshot()->visuals.skipServerPacks
            && versionSensitiveAllowed("Skip server resource packs (pack stack packet)")) {
            Runtime::instance().self().getLogger().info("Server resource packs: not applying {}", ids.size());
            skipped.store(static_cast<int>(ids.size()));
            ids.clear();
        }
    } catch (...) {}
    origin(source, packet);
}
bool start() {
    if (hooked) return true;
    if (ServerPacksJoin::hook(true) != 0) return false;
    if (ServerPacksStack::hook(true) != 0) { ServerPacksJoin::unhook(true); return false; }
    hooked = true;
    return true;
}
void stop() {
    if (!hooked) return;
    ServerPacksStack::unhook(true);
    ServerPacksJoin::unhook(true);
    hooked = false;
    join.store(Join::Unknown);
    skipped.store(0);
}
int takeSkipped() { return skipped.exchange(0); }
}
