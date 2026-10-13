#include "features/research/LanguageTrace.h"
#ifdef LAMIUM_LANGUAGE_TRACE
#include "app/Runtime.h"
#include "app/Versions.h"
#include "ll/api/memory/Hook.h"
#include "ll/api/service/TargetedBedrock.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/game/IClientInstance.h"
#include "mc/client/game/MinecraftGame.h"
#include "mc/client/gui/FontRepository.h"
#include "mc/client/gui/screens/controllers/GeneralSettingsScreenController.h"
#include "mc/client/network/ClientNetworkHandler.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/locale/I18n.h"
#include "mc/locale/I18nImpl.h"
#include "mc/locale/Localization.h"
#include "mc/network/packet/PackInfoData.h"
#include "mc/network/packet/ResourcePacksInfoPacket.h"
#include "mc/resources/PackInstance.h"
#include "mc/resources/ResourcePackManager.h"
#include "mc/resources/ResourcePackStack.h"
#include <algorithm>
#include <atomic>
#include <format>
#include <mutex>
#include <stdexcept>
#include <string>

namespace lamium::researchTrace::language {
namespace {
std::atomic<unsigned> lines{0};
std::mutex snapshotMutex;
std::string lastSettings;
template<class... Args>
void log(std::format_string<Args...> text, Args&&... args) noexcept {
    try {
        if (lines.fetch_add(1) >= 300) return;
        Runtime::instance().self().getLogger().info("L-167 {}", std::format(text, std::forward<Args>(args)...));
    } catch (...) {}
}
std::string state() {
    auto client = ll::service::getClientInstance();
    return std::format("current={} saved={} screen={} inWorld={} languages={}",
        getI18n().getCurrentLanguage()->getFullLanguageCode(),
        client ? client->getOptions().getLanguage() : std::string{},
        client ? client->getTopScreenName() : std::string{},
        client && client->getLocalPlayer(), getI18n().getSupportedLanguageCodes().size());
}
}
LL_TYPE_INSTANCE_HOOK(LanguageSettingsTick, ll::memory::HookPriority::Normal, GeneralSettingsScreenController,
    &GeneralSettingsScreenController::$tick, ::ui::DirtyFlag) {
    auto result = origin();
    try {
        auto value = std::format("settings global={} resourceTab={} entries={} selected={} {}",
            bool(mIsGlobalSettings), bool(mGlobalResourcePacksVisible), mLanguages->size(),
            uint64_t(mCurrentlySelectedIndex), state());
        std::lock_guard lock{snapshotMutex};
        if (value != lastSettings) {
            lastSettings = value;
            log("{}", value);
            // Enumerate only when the owned snapshot changes, never every tick.
            std::string list;
            auto const& available = mLanguages.get();
            for (size_t i = 0; i < std::min<size_t>(available.size(), 96); ++i)
                list += std::format(" [{}: {} / {}]", i, available[i].first, available[i].second);
            log("settings languages{}", list);
        }
    } catch (...) {}
    return result;
}
LL_TYPE_INSTANCE_HOOK(LanguageSelected, ll::memory::HookPriority::Normal, GeneralSettingsScreenController,
    &GeneralSettingsScreenController::_chooseLanguage, void, std::string const& code, int index) {
    try { log("select begin code={} index={} {}", code, index, state()); } catch (...) {}
    origin(code, index);
    try { log("select end {}", state()); } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(LanguageChosen, ll::memory::HookPriority::Normal, I18nImpl,
    &I18nImpl::$chooseLanguage, void, std::string const& code) {
    try { log("i18n begin code={} {}", code, state()); } catch (...) {}
    origin(code);
    try { log("i18n end {}", state()); } catch (...) {}
}
LL_TYPE_INSTANCE_HOOK(LanguageGameChanged, ll::memory::HookPriority::Normal, MinecraftGame,
    &MinecraftGame::$onLanguageChanged, void, std::string const& code, bool initializing) {
    log("game observer begin code={} initializing={}", code, initializing);
    origin(code, initializing);
    log("game observer end code={}", code);
}
LL_TYPE_INSTANCE_HOOK(LanguageFontChanged, ll::memory::HookPriority::Normal, FontRepository,
    &FontRepository::onLanguageChanged, void, std::string const& code) {
    log("font begin code={}", code);
    origin(code);
    log("font end code={}", code);
}
LL_TYPE_INSTANCE_HOOK(LanguagePacksChanged, ll::memory::HookPriority::Normal, ResourcePackManager,
    &ResourcePackManager::onLanguageChanged, void) {
    log("pack language begin");
    origin();
    log("pack language end");
}
LL_TYPE_INSTANCE_HOOK(LanguagePackStack, ll::memory::HookPriority::Normal, ResourcePackManager,
    &ResourcePackManager::setStack, bool, std::unique_ptr<ResourcePackStack> stack, ResourcePackStackType type) {
    try { log("stack begin type={} entries={} {}", int(type), stack ? stack->mStack->size() : 0, state()); } catch (...) {}
    auto result = origin(std::move(stack), type);
    log("stack end type={} result={}", int(type), result);
    return result;
}
LL_TYPE_INSTANCE_HOOK(LanguageServerPacks, ll::memory::HookPriority::Normal, ClientNetworkHandler,
    &ClientNetworkHandler::$handle, void, NetworkIdentifier const& source, ResourcePacksInfoPacket const& packet) {
    try {
        auto const& p = static_cast<ResourcePacksInfoPacketPayload const&>(packet);
        log("server pack info required={} entries={} addons={} scripts={}", bool(p.mResourcePackRequired),
            p.mResourcePacks->size(), bool(p.mHasAddonPacks), bool(p.mHasScripts));
    } catch (...) {}
    origin(source, packet);
}
namespace {
struct Hook { int (*install)(bool); bool (*remove)(bool); bool installed = false; };
Hook hooks[] = {
    {LanguageSettingsTick::hook, LanguageSettingsTick::unhook}, {LanguageSelected::hook, LanguageSelected::unhook},
    {LanguageChosen::hook, LanguageChosen::unhook}, {LanguageGameChanged::hook, LanguageGameChanged::unhook},
    {LanguageFontChanged::hook, LanguageFontChanged::unhook}, {LanguagePacksChanged::hook, LanguagePacksChanged::unhook},
    {LanguagePackStack::hook, LanguagePackStack::unhook}, {LanguageServerPacks::hook, LanguageServerPacks::unhook}
};
}
void start() {
    if (!versionSensitiveAllowed("Language diagnostics")) return;
    try {
        for (auto& hook : hooks) if (!hook.installed) {
            if (hook.install(true) != 0) throw std::runtime_error("Could not install language diagnostics");
            hook.installed = true;
        }
        log("read-only diagnostics installed (300 lines maximum); {}", runningVersionLine());
    } catch (...) { stop(); throw; }
}
void stop() {
    for (auto& hook : hooks) if (hook.installed && hook.remove(true)) hook.installed = false;
    std::lock_guard lock{snapshotMutex};
    lastSettings.clear();
    lines.store(0);
}
}
#else
namespace lamium::researchTrace::language { void start() {} void stop() {} }
#endif
