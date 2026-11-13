#pragma once
// Starts and stops the OmniLaunch Notifier companion (homenx_todo J1).
//
// The notifier is a separate system module (program 0x0100000000001E00) that
// shows Discord direct messages as Ultrahand toasts while a game runs. It only
// exists while: a game owns the foreground, the player turned the option on
// (flag file), Ultrahand's notifications folder exists and the notifier is
// installed. Everything here is best effort: a failure is logged and the game,
// the menu and power handling carry on.
//
// The daemon never links any network code for this; it only asks pm to start
// or end the other process. Timing is wall-clock: the main loop ticks every
// 10 ms only when it has work, otherwise once a second.

#include <switch.h>
#include <switchu/file_log.hpp>

#include <filesystem>
#include <system_error>

namespace switchu::daemon::notifier {

inline constexpr u64 kProgramId = 0x0100000000001E00ULL;
inline constexpr const char* kEnabledFlag = "sdmc:/config/OmniLaunch/ingame_discord";
inline constexpr const char* kInstalled = "sdmc:/atmosphere/contents/0100000000001E00/exefs.nsp";
inline constexpr const char* kToastDir = "sdmc:/config/ultrahand/notifications";

namespace detail {
inline bool g_pmUp = false;
inline bool g_running = false;
// Set once per game when the notifier was started or refused: if it then
// leaves on its own (option off, no account, token refused), it is not started
// again until the next game. Cleared when no game is in front.
inline bool g_triedThisGame = false;
inline int g_launchFailures = 0;
inline u64 g_nextCheckTick = 0;

inline u64 secondsFromNow(u64 s) {
    return armGetSystemTick() + armNsToTicks(s * 1'000'000'000ULL);
}

inline bool ensurePm() {
    if (g_pmUp) return true;
    g_pmUp = R_SUCCEEDED(pmshellInitialize());
    return g_pmUp;
}

inline bool processAlive() {
    u64 pid = 0;
    return ensurePm() && R_SUCCEEDED(pmshellGetProcessId(&pid, kProgramId)) && pid != 0;
}
} // namespace detail

/// Ends the notifier if it runs and waits (up to 200 ms) until pm no longer
/// lists it, so its file handles are closed before the caller commits the
/// card. Safe to call at any time, any number of times.
inline void stop(const char* why) {
    using namespace detail;
    if (!g_running && !g_pmUp) return;
    if (processAlive()) {
        const Result rc = pmshellTerminateProgram(kProgramId);
        int waitedMs = 0;
        while (waitedMs < 200 && processAlive()) {
            svcSleepThread(5'000'000ULL);
            waitedMs += 5;
        }
        switchu::FileLog::log("[notifier] stop (%s) rc=0x%X waited=%dms", why, rc, waitedMs);
    }
    g_running = false;
}

/// Called from the main loop. `gameInFront` is true while an application holds
/// the foreground; anything else (HOME, an applet, sleep, power) means the
/// notifier must not run.
inline void reconcile(bool gameInFront) {
    using namespace detail;
    if (!gameInFront) {
        if (g_running) stop("no game in front");
        g_triedThisGame = false;
        g_nextCheckTick = 0;
        return;
    }
    const u64 now = armGetSystemTick();
    if (now < g_nextCheckTick) return;
    g_nextCheckTick = secondsFromNow(5);

    if (g_running) {
        if (!processAlive()) {
            g_running = false; // left on its own: not again in this game
            switchu::FileLog::log("[notifier] exited by itself");
        }
        return;
    }
    if (g_triedThisGame || g_launchFailures >= 3) return;

    std::error_code ec;
    if (!std::filesystem::exists(kEnabledFlag, ec) || !std::filesystem::exists(kInstalled, ec) ||
        !std::filesystem::is_directory(kToastDir, ec)) {
        g_nextCheckTick = secondsFromNow(30); // the card is not read every 5 s
        return;
    }
    if (!ensurePm()) return;
    g_triedThisGame = true;
    if (processAlive()) {
        g_running = true;
        return;
    }
    const NcmProgramLocation loc{kProgramId, NcmStorageId_None, {}};
    u64 pid = 0;
    const Result rc = pmshellLaunchProgram(0, &loc, &pid);
    switchu::FileLog::log("[notifier] start rc=0x%X pid=%llu", rc, static_cast<unsigned long long>(pid));
    if (R_SUCCEEDED(rc)) {
        g_running = true;
        g_launchFailures = 0;
    } else {
        ++g_launchFailures; // after 3, not until the daemon restarts
    }
}

} // namespace switchu::daemon::notifier
