#pragma once
#include "app/Capabilities.h"
#include <initializer_list>

namespace lamium::capabilities {
template<class Remove, class Failed>
void removeHook(bool& installed, Remove remove, Failed failed) noexcept {
    if (!installed) return;
    try {
        if (remove()) { installed = false; return; }
    } catch (...) {}
    failed();
}
template<class Attach, class Read>
bool attachHook(bool& installed, std::initializer_list<Id> ids, Attach attach, Read read) {
    bool usable = false;
    for (auto id : ids)
        if (read(id).runtime.reason != Reason::RemovalFailed) usable = true;
    if (!usable) return false;
    if (!installed) installed = attach() == 0;
    return installed;
}
template<class Hook>
bool attachHook(bool& installed, std::initializer_list<Id> ids) {
    return attachHook(installed,ids,[] { return Hook::hook(true); },[] (Id id) { return registry().status(id); });
}
template<class Hook>
void detachHook(bool& installed, std::initializer_list<Id> ids) noexcept {
    removeHook(installed, [] { return Hook::unhook(true); }, [ids] {
        for (auto id : ids) fail(id,Reason::RemovalFailed);
    });
}
}
