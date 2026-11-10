#pragma once
// Nextendo service (HomeNX H3, C-NET-3): player counts from nextendo.network,
// status pill representation, and Prelude/Nextendo app launching via NRO host.
// New OmniLaunch implementation written from scratch; upstream/homenx @ 0448cd4
// (projects/cards/src/Library.cpp nextendoOnline, CardsActivity.cpp openNextendo)
// referenced for protocol behavior only.

#include "nextendo/NextendoCatalog.hpp"
#include <atomic>
#include <cstdint>
#include <future>
#include <map>
#include <mutex>
#include <string>

namespace net { class NetService; }
namespace home { class HomeCore; }

namespace nextendo {

class NextendoService {
public:
    NextendoService();
    ~NextendoService();

    NextendoService(const NextendoService&) = delete;
    NextendoService& operator=(const NextendoService&) = delete;

    /// Update loop called on main thread. Periodically refreshes counts (every 60s)
    /// when network is up.
    void update(home::HomeCore& core, bool netUp);

    /// Force immediate background refresh of online counts.
    void refresh(home::HomeCore& core);

    /// Total players online across all reported titles (-1 = counting/no data).
    int totalOnline() const;

    /// Players online for a specific title (0 if title not reported or unknown).
    int countForTitle(std::uint64_t titleId) const;

    /// True if counts have been fetched at least once.
    bool hasData() const;

    /// Launch Nextendo application (Prelude) or trigger download if missing.
    /// Returns: 0 = launched, 1 = started download, -1 = error (missing game / offline).
    int openNextendoApp(home::HomeCore& core, std::string& outMsg);

    bool isDownloading() const { return m_downloading.load(); }

private:
    void fetchCounts(net::NetService& net);

    mutable std::mutex m_mu;
    std::map<std::uint64_t, int> m_counts;
    int m_totalOnline = -1;
    bool m_hasData = false;
    std::uint64_t m_lastFetchTick = 0;
    std::atomic<bool> m_fetching{false};
    std::atomic<bool> m_downloading{false};
};

} // namespace nextendo
