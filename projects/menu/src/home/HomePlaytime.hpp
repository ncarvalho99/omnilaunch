#pragma once
// Play-time cache refresh (C5a). Process-lifetime: a batch started by one
// frontend can finish while another is shown. The worker only queries pdm;
// results are applied to AppConfig on the main thread in poll().

#include <array>
#include <chrono>
#include <string>
#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <utility>
#include <vector>

struct AppConfig;
namespace nxui { class ThreadPool; }

namespace home {

class HomePlaytime {
public:
    /// Queues a batch for these titles. Ignored while one is running or when
    /// the list is empty. The queries run on the pool. `checkedSequence` is
    /// the AppConfig::lastOpenedSequence value the batch covers; it is stored
    /// as AppConfig::playtimeCheckedSequence only if every title answered.
    /// Returns whether a batch was started.
    bool start(nxui::ThreadPool& pool, std::vector<std::uint64_t> titleIds,
               std::uint64_t checkedSequence);

    /// Main thread. When the running batch has finished, writes its figures
    /// into `config` and returns how many titles changed; otherwise -1.
    int poll(AppConfig& config);

    /// Resolve profile on the UI thread (at most every 3 s unless preference
    /// changes), invalidate the single persisted cache, cancel stale work.
    bool syncProfile(AppConfig& config);
    std::array<std::uint64_t, 2> profile() const { return m_profile; }

    bool running() const { return m_future.valid(); }

    /// Asks a running batch to stop where it is (launch handoff, exit).
    void cancel();

    /// Shutdown: cancel, then wait for the worker (before the pool joins).
    void shutdown();

private:
    struct State {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> playtime;
        std::array<std::uint64_t, 2> profile{};
        std::size_t requested = 0;
        std::uint64_t checkedSequence = 0;
        std::atomic<bool> cancelled{false};
    };
    std::shared_ptr<State> m_state;
    std::future<void> m_future;
    std::array<std::uint64_t, 2> m_profile{};
    std::string m_preference;
    bool m_profileReady = false;
    std::chrono::steady_clock::time_point m_profileChecked{};
};

} // namespace home
