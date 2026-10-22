#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace lamium::map {
enum class RegionSaveResult { Saved, Permanent, Transient };
class RegionSaveError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
inline RegionSaveResult saveFailure(std::exception const& error) {
    return dynamic_cast<RegionSaveError const*>(&error) ? RegionSaveResult::Permanent : RegionSaveResult::Transient;
}
struct RegionRetry {
    double delay = 10;
    double failed() { return std::exchange(delay, std::min(delay * 2, 300.)); }
};
// A snapshot in flight does not own changes made after it was queued.
struct RegionSaveState {
    bool dirty = false, inFlight = false, permanent = false;
    std::uint64_t serial = 0;
    double dirtySince = 0, retryAt = 0;
    RegionRetry retry;
    void changed(double now) {
        if (permanent) return;
        if (!dirty) dirtySince = now;
        dirty = true;
        ++serial;
    }
    bool due(double now) const { return dirty && !inFlight && !permanent && now - dirtySince >= 10 && now >= retryAt; }
    bool begin() { if (!dirty || inFlight || permanent) return false; inFlight = true; return true; }
    void refuse() { permanent = true; dirty = false; inFlight = false; }
    bool evictable() const { return !dirty && !inFlight; }
    void complete(std::uint64_t saved, RegionSaveResult result, double now) {
        inFlight = false;
        if (result == RegionSaveResult::Permanent) { refuse(); return; }
        if (result == RegionSaveResult::Saved) {
            if (saved == serial) dirty = false;
            retry = {};
            retryAt = 0;
        } else retryAt = now + retry.failed();
        dirtySince = now;
    }
};
}
