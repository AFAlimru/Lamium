#include "app/ClientTasks.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "ll/api/thread/ClientThreadExecutor.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/MinecraftGame.h"
#include <mutex>
#include <vector>

namespace lamium::clientTasks {
namespace {
std::mutex lock;
std::vector<std::function<void()>> queued;
bool scheduled = false, hooked = false;
void drain() {
    std::vector<std::function<void()>> work;
    {
        std::scoped_lock guard(lock);
        work.swap(queued);
        scheduled = false;
    }
    for (auto& task : work) try { task(); } catch (...) {}
}
}
LL_TYPE_INSTANCE_HOOK(ClientTasksMenuUpdate, ll::memory::HookPriority::Normal, MinecraftGame, &MinecraftGame::$update, void) {
    origin();
    try {
        {
            std::scoped_lock guard(lock);
            if (queued.empty()) return;
        }
        auto client = ll::service::getClientInstance();
        if (client && !client->getLocalPlayer()) drain();
    } catch (...) {}
}
void post(std::function<void()> task) {
    std::scoped_lock guard(lock);
    queued.push_back(std::move(task));
    if (scheduled) return;
    scheduled = true;
    ll::thread::ClientThreadExecutor::getDefault().execute(drain);
}
bool start() {
    if (!hooked) hooked = ClientTasksMenuUpdate::hook(true) == 0;
    return hooked;
}
void stop() {
    if (hooked && ClientTasksMenuUpdate::unhook(true)) hooked = false;
    std::scoped_lock guard(lock);
    queued.clear();
    scheduled = false;
}
}
