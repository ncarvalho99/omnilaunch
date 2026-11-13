#pragma once
// AppFetch: downloads one homebrew .nro (BrowseNX, TikTok) for either HOME
// frontend, with progress and cancellation. The worker owns only a shared
// state block and value copies, so a frontend that leaves (after stop())
// leaves nothing dangling. One fetch at a time per instance.

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <string>

namespace net { class NetService; }
namespace nxui { class ThreadPool; }

namespace home {

/// BrowseNX (ad3333m): not shipped in the package, fetched on first use.
inline constexpr const char* kBrowserNro = "sdmc:/switch/BrowseNX/BrowseNX.nro";
/// A copy left by an older package is still opened.
inline constexpr const char* kLegacyBrowserNro = "sdmc:/switch/OmniLaunch/apps/BrowseNX.nro";
inline constexpr const char* kBrowserUrl =
    "https://github.com/ad3333m/browsenx/releases/download/latest/BrowseNX.nro";

/// Installed BrowseNX path, or empty when it still has to be downloaded.
std::string installedBrowserPath();

class AppFetch {
public:
    enum State : int { Idle = 0, Running = 1, Done = 2, Failed = 3, Cancelled = 4 };

    AppFetch() = default;
    ~AppFetch() { stop(); }
    AppFetch(const AppFetch&) = delete;
    AppFetch& operator=(const AppFetch&) = delete;

    /// Starts the download of `url` to `dest` (through `dest`.part, checked to
    /// be an NRO before it replaces anything). False when one is running or
    /// the pool refuses work.
    bool start(net::NetService& net, nxui::ThreadPool& pool, const std::string& url,
               const std::string& dest, const std::string& name);
    /// Asks the worker to stop; poll() reports Cancelled.
    void cancel();
    /// Cancels and joins (frontend leaving, power action).
    void stop();
    /// Joins a finished worker and returns its outcome once (Done, Failed or
    /// Cancelled); Running while it works, Idle otherwise.
    State poll();

    bool running() const { return m_shared && m_shared->state.load() == Running; }
    /// 0..1, or -1 while the size is unknown or the file is being installed.
    float progress() const;
    bool installing() const { return m_shared && m_shared->installing.load(); }
    std::uint64_t received() const { return m_shared ? m_shared->done.load() : 0; }
    std::uint64_t total() const { return m_shared ? m_shared->total.load() : 0; }
    const std::string& dest() const { return m_dest; }
    const std::string& name() const { return m_name; }

private:
    struct Shared {
        std::atomic<int> state{Idle};
        std::atomic<bool> stop{false};
        std::atomic<bool> installing{false};
        std::atomic<std::uint64_t> done{0};
        std::atomic<std::uint64_t> total{0};
    };
    std::shared_ptr<Shared> m_shared;
    std::future<void> m_future;
    std::string m_dest;
    std::string m_name;
};

} // namespace home
