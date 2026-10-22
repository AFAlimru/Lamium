#pragma once
#include "nlohmann/json.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>

namespace lamium {
template <class T>
T settingsValue(nlohmann::ordered_json const& data, std::string_view key, T fallback) {
    auto at = data.find(key);
    if (at == data.end()) return fallback;
    if constexpr (std::is_same_v<T, bool>) {
        if (!at->is_boolean()) return fallback;
    } else if constexpr (std::is_integral_v<T>) {
        // A whole float (3.0) is the number older files or hand edits wrote.
        if (at->is_number_float()) {
            auto number = at->get<double>();
            if (!std::isfinite(number) || number != std::trunc(number) || number < static_cast<double>(std::numeric_limits<T>::lowest())
                || number > static_cast<double>(std::numeric_limits<T>::max())) return fallback;
            return static_cast<T>(number);
        }
        if (!at->is_number_integer()) return fallback;
        if (at->is_number_unsigned()) {
            if (at->get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) return fallback;
        } else {
            auto number = at->get<std::int64_t>();
            if (number < std::numeric_limits<T>::lowest() || number > std::numeric_limits<T>::max()) return fallback;
        }
    } else if constexpr (std::is_floating_point_v<T>) {
        if (!at->is_number()) return fallback;
        auto number = at->get<double>();
        if (!std::isfinite(number) || number < std::numeric_limits<T>::lowest()
            || number > std::numeric_limits<T>::max()) return fallback;
    } else if constexpr (std::is_same_v<T, std::string>) {
        if (!at->is_string()) return fallback;
    }
    return at->get<T>();
}
}
