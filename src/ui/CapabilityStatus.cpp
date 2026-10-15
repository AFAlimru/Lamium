#include "ui/CapabilityStatus.h"
#include "ui/Localization.h"
#include "ui/SettingsRows.h"
#include "app/Versions.h"

namespace lamium::ui {
bool capabilitiesPaused() {
    for (auto const& d : capabilities::definitions)
        if (!d.feature.empty() && capabilities::registry().status(d.id).paused()) return true;
    return false;
}
std::string capabilityDetails(std::string_view feature, bool includeWaiting) {
    std::string out;
    for (auto const& d : capabilities::definitions) {
        if (d.feature != feature || feature.empty()) continue;
        auto state = capabilities::registry().status(d.id);
        if (!state.paused() && !(includeWaiting && state.waiting())) continue;
        if (!out.empty()) out += '\n';
        out += translated(d.nameKey) + ": " + translated(state.waiting() ? "capability.waiting" : capabilities::reasonKey(state.reason()));
    }
    return out;
}
std::string capabilitySummary() {
    std::string out;
    for (auto feature : paused::gatedFeatures) {
        auto detail = capabilityDetails(feature,false);
        if (detail.empty()) continue;
        if (!out.empty()) out += '\n';
        out += translated(featureNameKey(feature)) + ": " + detail;
    }
    return out;
}
}
