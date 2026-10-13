#include "ui/GameLanguage.h"
#include "ui/SettingsScreen.h"
#include "app/Runtime.h"
#include "app/Versions.h"
#include "features/inspection/EnglishSearch.h"
#include "features/schematic/GhostRenderer.h"
#include "features/schematic/SchematicItems.h"
#include "ll/api/service/TargetedBedrock.h"
#include "app/ClientTasks.h"
#include "mc/client/game/ClientInstance.h"
#include "mc/client/options/IOptionRegistry.h"
#include "mc/client/player/LocalPlayer.h"
#include "mc/locale/I18n.h"
#include "mc/locale/Localization.h"
#include "mc/resources/ResourcePackManager.h"

namespace lamium::ui::game_language {
std::string currentCode() { return getI18n().getCurrentLanguage()->getFullLanguageCode(); }
std::string currentName() {
    auto code = currentCode();
    auto name = getI18n().getLanguageName(code);
    return name.empty() ? code : name;
}
bool inWorld() {
    auto client = ll::service::getClientInstance();
    return client && client->getLocalPlayer();
}
std::string savedCode() {
    auto client = ll::service::getClientInstance();
    return client ? client->getOptions().getLanguage() : std::string{};
}
namespace {
// Every language the game lists, as its own language screen does; whether
// it applied is read back from I18n afterwards.
bool supported(std::string const& code) {
    auto const& codes = getI18n().getSupportedLanguageCodes();
    return std::find(codes.begin(), codes.end(), code) != codes.end();
}
LanguageContext context() {
    auto client = ll::service::getClientInstance();
    auto player = client ? client->getLocalPlayer() : nullptr;
    // Outside a world (L-175) there is no dimension; -1 tells a later join apart.
    return {currentCode(), player ? int(player->getDimensionId()) : -1, Runtime::instance().enabled() && ownsInput()};
}
void run(Request& request) noexcept {
    try {
        if (!request.expected.permits(context(), request.cancelled.load())) {
            request.result.store(Result::Cancelled); return;
        }
        if (!versionSensitiveAllowed("Game language (native apply)") || !supported(request.code)) {
            request.result.store(Result::Unavailable); return;
        }
        // Resource changes invalidate the save's live block/actor readers.
        if (schematic::ghosts::saveStatus()) { request.result.store(Result::Saving); return; }
        auto client = ll::service::getClientInstance();
        if (!client) { request.result.store(Result::Cancelled); return; }
        auto& options = client->getOptions();
        Runtime::instance().self().getLogger().info("Game language: applying {} -> {}", request.expected.code, request.code);
        if (currentCode() != request.code) {
            inspection::englishSearch::forget();
            schematic::items::forget();
            schematic::ghosts::invalidateResources();
            // Choosing notifies game, font and pack observers itself (traced
            // in-world); the saved preference follows, as in OreUI.
            getI18n().chooseLanguage(request.code);
            if (currentCode() != request.code) { request.result.store(Result::Failed); return; }
        }
        options.setLanguage(request.code);
        options.saveIfNeeded();
        bool applied = currentCode() == request.code && options.getLanguage() == request.code;
        Runtime::instance().self().getLogger().info("Game language: current={} preference={} applied={}",
            currentCode(), options.getLanguage(), applied);
        request.result.store(applied ? Result::Applied : Result::Failed);
    } catch (...) {
        request.result.store(Result::Failed);
        try { Runtime::instance().self().getLogger().warn("Game language: native apply could not complete"); } catch (...) {}
    }
}
}
LanguageSelection choices() {
    LanguageSelection out;
    if (!versionSensitiveAllowed("Game language (native apply)")) return out;
    auto const& codes = getI18n().getSupportedLanguageCodes();
    for (size_t i = 0; i < std::min(codes.size(), LanguageSelection::limit); ++i)
        out.add(codes[i], getI18n().getLanguageName(codes[i]));
    auto current = currentCode();
    for (size_t i = 0; i < out.choices.size(); ++i)
        if (out.choices[i].code == current) out.selected = static_cast<int>(i);
    if (out.selected < 0 && !out.choices.empty()) out.selected = 0;
    return out;
}
std::shared_ptr<Request> apply(std::string code) {
    auto request = std::make_shared<Request>();
    request->code = std::move(code);
    request->expected = context();
    clientTasks::post([request] { run(*request); });
    return request;
}
}
