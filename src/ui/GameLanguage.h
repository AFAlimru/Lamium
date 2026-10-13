#pragma once
#include "ui/LanguageSelection.h"
#include <atomic>
#include <memory>
#include <string>

namespace lamium::ui::game_language {
enum class Result { Pending, Applied, Cancelled, Unavailable, Failed, Saving };
struct Request {
    std::string code;
    LanguageContext expected;
    std::atomic<bool> cancelled{false};
    std::atomic<Result> result{Result::Pending};
};
// Owned language values only, from languages the game has already loaded.
LanguageSelection choices();
std::string currentCode();
std::string currentName();
std::string savedCode();
// The interval against in-world freezes applies only there (L-175).
bool inWorld();
// Queued to the client executor; no game pointer crosses frames.
std::shared_ptr<Request> apply(std::string code);
}
