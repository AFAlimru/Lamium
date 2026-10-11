#include "app/Versions.h"
#include "app/Runtime.h"

#include "ll/api/Versions.h"
#include <Windows.h>
#include <cstddef>
#include <mutex>
#include <set>
#include <vector>
#pragma comment(lib, "version.lib")

#ifndef LAMIUM_VERSION
#define LAMIUM_VERSION "dev"
#endif

namespace lamium {
std::string lamiumVersion() { return LAMIUM_VERSION; }
std::string runningGameVersion() {
    try { return ll::getGameVersion().to_string(); } catch (...) { return "?"; }
}
std::string runningLoaderVersion() {
    try { return ll::getLoaderVersion().to_string(); } catch (...) { return "?"; }
}
std::string runningVersionLine() {
    return versionLine(LAMIUM_VERSION, runningGameVersion(), runningLoaderVersion());
}
namespace {
// The game executable the version-sensitive paths were checked on. Change it
// only at step 5 of the playbook in docs/GAME-UPDATES.md, after every gated
// capability was checked on the new game; never as part of an SDK bump.
// New-ReleaseArchive.ps1 refuses a release whose SDK targets another game.
constexpr unsigned verifiedGame[4] = {1, 26, 51, 1};
bool readVerified() {
#ifdef LAMIUM_UNVERIFIED_GAME
    return false;
#else
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length == 32768) return false;
    DWORD bytes = GetFileVersionInfoSizeW(path, nullptr);
    if (!bytes || bytes > 1024 * 1024) return false;
    std::vector<std::byte> data(bytes);
    if (!GetFileVersionInfoW(path, 0, bytes, data.data())) return false;
    VS_FIXEDFILEINFO* info = nullptr;
    UINT size = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &size)
        || size < sizeof(*info) || info->dwSignature != 0xfeef04bd) return false;
    return info->dwFileVersionMS == ((verifiedGame[0] << 16) | verifiedGame[1])
        && info->dwFileVersionLS == ((verifiedGame[2] << 16) | verifiedGame[3]);
#endif
}
}
bool verifiedGameExecutable() {
    static bool const verified = readVerified();
    return verified;
}
paused::Level pausedLevel(std::string_view feature) {
    return verifiedGameExecutable() ? paused::Level::No : paused::onUnverified(feature);
}
bool versionSensitiveAllowed(std::string_view capability) {
    if (verifiedGameExecutable()) return true;
    static std::mutex mutex;
    static std::set<std::string, std::less<>> logged;
    std::lock_guard lock(mutex);
    if (logged.find(capability) == logged.end()) {
        logged.emplace(capability);
        try {
#ifdef LAMIUM_UNVERIFIED_GAME
            constexpr char const* why = "forced by the unverified_game build option";
#else
            constexpr char const* why = "the game executable is not the verified version";
#endif
            Runtime::instance().self().getLogger().warn("{} stays vanilla: {} (version {})", capability, why, runningGameVersion());
        } catch (...) {}
    }
    return false;
}
}
