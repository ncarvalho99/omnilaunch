#pragma once
// Play-time cache refresh (C5a). Process-lifetime: a batch started by one
// frontend can finish while another is shown. The worker only queries pdm;
// results are applied to AppConfig on the main thread in poll().

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
    /// the list is empty. The queries run on the pool.
    void start(nxui::ThreadPool& pool, std::vector<std::uint64_t> titleIds);

    /// Main thread. When the running batch has finished, writes its figures
    /// into `config` and returns how many titles changed; otherwise -1.
    int poll(AppConfig& config);

    bool running() const { return m_future.valid(); }

    /// Asks a running batch to stop where it is (launch handoff, exit).
    void cancel();

    /// Shutdown: cancel, then wait for the worker (before the pool joins).
    void shutdown();

private:
    struct State {
        std::vector<std::pair<std::uint64_t, std::uint64_t>> playtime;
        std::atomic<bool> cancelled{false};
    };
    std::shared_ptr<State> m_state;
    std::future<void> m_future;
};

} // namespace home
