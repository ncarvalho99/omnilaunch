#pragma once
// NetService (HomeNX G1, requirement C-NET-1): the network layer for HomeCore
// features that talk to third-party services (art sources, news, homebrew
// updates, Discord). New OmniLaunch code; HomeNX net/ and util/ @ 0448cd4 were
// read for protocol behaviour only (audit D7).
//
// - Network state from nifm, cached for a second; a request can wait for the
//   network to come up (DNS failure at boot is normal).
// - Blocking HTTP on the caller's thread (run it on a pool worker), with
//   retries and back-off, a response size cap, and cancellation through the
//   same frontend/terminal scope as the Theme Shop client.
// - TLS: the console's store first; a host it refuses is retried once with the
//   bundled roots in switch/OmniLaunch/certs/ (still verified), and the root
//   that worked is remembered for that host.
// - Never attaches the official client key. Requests to the project hosts go
//   through themeshop::http, which is the only code allowed to send it.

#include "themeshop/ThemeHttpScope.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace net {

enum class NetState { Unknown, Offline, Connecting, Online };
const char* netStateName(NetState state);

struct Request {
    std::string method = "GET";
    std::string url;
    std::vector<std::string> headers; ///< "Name: value"
    std::string body;
    std::size_t maxBytes = 4u << 20;  ///< responses larger than this fail
    long timeoutMs = 20000;
    int attempts = 0;                 ///< 0: 3 for GET/HEAD, 1 otherwise
    int waitOnlineMs = 0;             ///< wait this long for the network first
    /// Off by default. When on, only HTTPS redirects to the same host are
    /// followed, so headers and body never reach another origin in clear or
    /// at all.
    bool followSameOriginRedirects = false;
    const std::atomic<bool>* cancel = nullptr; ///< extra per-request cancel
};

/// A GET whose body goes to a file (HomeNX G5 homebrew updates).
struct Download {
    std::string url;
    std::string path;                    ///< written in place; removed on failure
    std::uint64_t maxBytes = 64u << 20;  ///< larger bodies fail
    long timeoutMs = 300000;
    /// Release downloads redirect to a CDN on another host. Followed only for
    /// HTTPS -> HTTPS, and a Download carries no headers or body to leak.
    int maxRedirects = 5;
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(std::uint64_t done, std::uint64_t total)> progress; ///< worker thread
};

struct Response {
    long status = 0;          ///< HTTP status, 0 when no response arrived
    std::string body;
    std::string error;        ///< empty on transport success
    bool cancelled = false;
    int attempts = 0;
    std::uint32_t elapsedMs = 0;
    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

class NetService {
public:
    NetService() = default;
    ~NetService();
    NetService(const NetService&) = delete;
    NetService& operator=(const NetService&) = delete;

    /// Cached for one second; brings the network runtime up on first use.
    NetState state();
    bool online() { return state() == NetState::Online; }

    /// Blocking; never call it on the render thread.
    Response request(const Request& req);
    Response get(const std::string& url, std::vector<std::string> headers = {});
    /// Blocking. `Response::body` stays empty; status/error/cancelled as above.
    Response download(const Download& dl);

    /// Directory of the bundled root certificates (with trailing slash).
    static const char* certDir();
    /// Root file remembered as working for the host of `url`; empty for the
    /// console store. Used by WebSocket too.
    std::string rootFor(const std::string& url);
    /// Bundled root files present on the card, in trial order. Each request
    /// or connection walks this list itself (no shared cursor).
    static std::vector<std::string> bundledRoots();
    /// Records the root that verified `url` (empty: back to the console store).
    void rememberRoot(const std::string& url, const std::string& root);

    /// Services phase: refuses new requests and releases the runtime.
    void shutdown();

private:
    bool ensureRuntime();

    std::mutex m_mutex;
    bool m_runtime = false;
    bool m_closed = false;
    NetState m_state = NetState::Unknown;
    std::uint64_t m_stateTick = 0;

    std::mutex m_rootsMutex;
    std::map<std::string, std::string> m_roots; ///< host -> working root file
};

/// True when `scope` was cancelled for `token` or `extra` is set.
inline bool cancelledFor(const themeshop::http::CancelScope& scope,
                         const themeshop::http::CancelScope::Token& token,
                         const std::atomic<bool>* extra) {
    return scope.cancelled(token) || (extra && extra->load(std::memory_order_acquire));
}

} // namespace net
