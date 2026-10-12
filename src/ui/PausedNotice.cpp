#include "ui/PausedNotice.h"
#include "app/Versions.h"
#include "features/map/WaypointSession.h"
#include "ui/Localization.h"
#include "ui/SettingsRows.h"
#include "ui/Toast.h"
#include <string>

namespace lamium::ui::paused_notice {
namespace {
unsigned noticedWorld = 0;
// Long enough to read a few names after the world appears.
constexpr double seconds = 8;
}
void frame(Settings const& settings) {
    if (verifiedGameExecutable()) return;
    auto place = map::waypoints::place();
    if (!place.active || place.world == noticedWorld) return;
    noticedWorld = place.world;
    std::string names;
    for (auto feature : paused::inUse(settings)) {
        auto name = translated(featureNameKey(feature));
        if (paused::onUnverified(feature) == paused::Level::Partly) name = translated("paused.part", name);
        names += (names.empty() ? "" : ", ") + name;
    }
    if (!names.empty()) showMessageToast(translated("paused.notice", runningGameVersion(), names), seconds);
}
}
