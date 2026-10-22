#pragma once
#include "settings/Settings.h"
#include "ll/api/mod/NativeMod.h"
#include <atomic>
#include <memory>
#include <mutex>

namespace lamium {
class Runtime {
    ll::mod::NativeMod& mod = *ll::mod::NativeMod::current();
    Settings settings;
    // Readers take the published copy without the lock; hooks read it per
    // entity and per frame.
    std::atomic<std::shared_ptr<Settings const>> published{std::make_shared<Settings const>()};
    std::atomic<bool> running{false};
    bool writable = true; // false when settings.json could not be read: never overwrite it
    mutable std::mutex settingsMutex;
public:
    static Runtime& instance();
    bool load();
    bool enable();
    bool disable();
    bool enabled() const { return running.load(); }
    ll::mod::NativeMod& self() { return mod; }
    Settings preferences() const { return *published.load(); }
    // Hot paths: keep the pointer while reading, not a reference into it.
    std::shared_ptr<Settings const> snapshot() const { return published.load(); }
    bool save(Settings value);
};
}
