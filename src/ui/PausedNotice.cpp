#include "ui/PausedNotice.h"
#include "app/Versions.h"
#include "app/CapabilityNotice.h"
#include "features/map/WaypointSession.h"
#include "ui/Localization.h"
#include "ui/SettingsRows.h"
#include "ui/Toast.h"
#include <string>

namespace lamium::ui::paused_notice {
namespace {
capabilities::Notice notice;
// Long enough to read a few names after the world appears.
constexpr double seconds = 8;
}
void frame(Settings const& settings) {
    auto place = map::waypoints::place();
    if (!notice.poll(place.active ? place.world : 0,capabilities::registry().revision())) return;
    std::array<capabilities::Status,capabilities::count> states;
    for (auto const& d : capabilities::definitions) states[static_cast<size_t>(d.id)] = capabilities::registry().status(d.id);
    auto read = [&](capabilities::Id id) { return states[static_cast<size_t>(id)]; };
    std::string names, identity, firstReason;
    auto affected = paused::affected(settings,read);
    size_t shown = 0;
    for (auto feature : affected) {
        auto name = translated(featureNameKey(feature));
        if (capabilities::featureImpact(feature,read) == capabilities::Impact::Partly) name = translated("paused.part", name);
        if (shown++ < 3) names += (names.empty() ? "" : ", ") + name;
        for (auto const& d : capabilities::definitions)
            if (d.feature == feature && paused::requested(d.id,settings)) {
                auto state = read(d.id);
                if (!state.paused()) continue;
                identity += std::string(d.logName) + ": " + std::string(capabilities::reasonName(state.reason())) + "\n";
                if (firstReason.empty()) firstReason = translated(capabilities::reasonKey(state.reason()));
            }
    }
    if (!notice.changed(identity)) return;
    if (affected.size() > 3) names += ", " + translated("capability.more",affected.size()-3);
    // One paragraph uses the existing toast's four-line wrapping budget;
    // full per-capability reasons remain reachable in settings help.
    showMessageToast(translated("capability.notice",firstReason + ": " + names)
        + " " + translated("capability.details"), seconds);
}
}
