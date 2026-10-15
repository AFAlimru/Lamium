#pragma once
#include "app/Capabilities.h"
#include <string>
#include <string_view>
namespace lamium::ui {
bool capabilitiesPaused();
std::string capabilityDetails(std::string_view feature, bool includeWaiting = true);
std::string capabilitySummary();
}
