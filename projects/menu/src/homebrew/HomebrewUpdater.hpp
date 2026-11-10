#pragma once
// Homebrew updates (HomeNX G5, C-NET-6, C-UI-13): the network and SD side.
// Runs on a HomeCore pool worker, one job at a time (the caller owns the
// future), so the saved state is never written by two jobs at once.
// Network through NetService (never the client key; these are third-party
// hosts). Written from scratch on HomeNX's behaviour (audit D7):
// upstream/homenx @ 0448cd4 projects/updater/src/Catalog.cpp and
// projects/cards/src/CardsActivity.cpp checkHomebrewUpdates/updateNro.

#include "homebrew/HomebrewUpdateCatalog.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace net { class NetService; }

namespace hbupdate {

constexpr const char* kStatePath = "sdmc:/config/OmniLaunch/homebrew_updates.json";
constexpr std::int64_t kFreshSeconds = 6 * 3600;

State loadState(const std::string& path = kStatePath);
bool saveState(const State& state, const std::string& path = kStatePath);

struct CheckProgress {
    std::atomic<int> done{0};
    std::atomic<int> total{0};
};

/// Finishes a swap a crash interrupted: when `path` is missing, its `.old`
/// copy (else a complete `.new`) is put back. Stale helpers are removed.
void recoverInterrupted(const std::string& path);
/// The same for every `*.nro.old` under sdmc:/switch (one folder deep).
void recoverInterruptedAll();

/// The installed homebrew that can be updated here (OmniLaunch's own files
/// and hbmenu left out). Reads .nro headers from the SD.
std::vector<Installed> installedApps();

/// Loads the saved state; when it is older than kFreshSeconds (or `force`)
/// and the console is online, asks the sources again and saves. Returns the
/// state to show. `stop` aborts between and inside requests. With `mayFetch`
/// false only the saved state is read (back from a game: no network work).
State check(net::NetService& net, bool force, CheckProgress& progress, const std::atomic<bool>& stop,
            bool mayFetch = true);

struct InstallProgress {
    std::atomic<int> index{0};       ///< app being installed (0-based)
    std::atomic<int> count{0};
    std::atomic<float> fraction{0.f};
};
struct InstallResult {
    std::vector<std::string> updated; ///< paths now on the new version
    std::vector<std::string> failed;  ///< "name: reason"
    bool cancelled = false;
};

/// Installs the offers for `paths` in order, one at a time. The old .nro is
/// kept aside until the new one is in place, and only a file that reads as an
/// NRO replaces it. `skipPath` (a homebrew open right now) is refused.
/// Records each success in `state` and saves it.
InstallResult install(net::NetService& net, State& state, const std::vector<std::string>& paths,
                      const std::string& skipPath, InstallProgress& progress, const std::atomic<bool>& stop);

} // namespace hbupdate
