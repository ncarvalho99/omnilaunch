#include "net/NetService.hpp"

#include "net/NetBackoff.hpp"
#include "net/NetUtil.hpp"
#include "core/DebugLog.hpp"
#include "services/NetRuntime.hpp"
#include "themeshop/ThemeHttp.hpp"

#include <curl/curl.h>
#include <switch.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <thread>

namespace net {

namespace {

// Bundled roots (Mozilla list, copied from HomeNX romfs/certs @ 0448cd4):
// Let's Encrypt and Google Trust Services, which the console's own store does
// not always accept. Order follows HomeNX: the most common chains first.
constexpr const char* kCertDir = "sdmc:/switch/OmniLaunch/certs/";
constexpr const char* kRootFiles[] = {"gts-root-r4.pem",  "gts-root-r1.pem", "isrg-root-x1.pem",
                                      "isrg-root-x2.pem", "gts-root-r3.pem", "globalsign-root-r3.pem"};
constexpr std::size_t kRootCount = sizeof(kRootFiles) / sizeof(kRootFiles[0]);

bool certRefused(CURLcode rc) {
    return rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CONNECT_ERROR ||
           rc == CURLE_SSL_ISSUER_ERROR || rc == CURLE_SSL_CACERT_BADFILE;
}

bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

std::uint64_t nowMs() {
    return static_cast<std::uint64_t>(armTicksToNs(armGetSystemTick()) / 1000000ULL);
}

struct Sink {
    std::string* body;
    std::size_t max;
    bool overflow = false;
};

size_t onBody(char* ptr, size_t size, size_t count, void* user) {
    auto* s = static_cast<Sink*>(user);
    const size_t n = size * count;
    if (s->body->size() + n > s->max) {
        s->overflow = true;
        return 0;
    }
    try { // an exception must never unwind through curl
        s->body->append(ptr, n);
    } catch (...) {
        s->overflow = true;
        return 0;
    }
    return n;
}

struct HeaderSink {
    double retryAfter = 0.0;
};

size_t onHeader(char* buf, size_t size, size_t count, void* user) {
    const size_t n = size * count;
    static const char kRetry[] = "retry-after:";
    if (n > sizeof(kRetry) - 1 && strncasecmp(buf, kRetry, sizeof(kRetry) - 1) == 0)
        static_cast<HeaderSink*>(user)->retryAfter = std::atof(buf + sizeof(kRetry) - 1);
    return n;
}

struct CancelCtx {
    const themeshop::http::CancelScope* scope;
    themeshop::http::CancelScope::Token token;
    const std::atomic<bool>* extra;
    bool hit = false;
};

int onProgress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* c = static_cast<CancelCtx*>(user);
    if (cancelledFor(*c->scope, c->token, c->extra)) {
        c->hit = true;
        return 1;
    }
    return 0;
}

// Sleeps up to `ms`, waking early on cancel. True when cancelled.
bool sleepCancellable(std::uint32_t ms, const CancelCtx& c) {
    const std::uint64_t end = nowMs() + ms;
    while (nowMs() < end) {
        if (cancelledFor(*c.scope, c.token, c.extra))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return cancelledFor(*c.scope, c.token, c.extra);
}

} // namespace

const char* netStateName(NetState state) {
    switch (state) {
    case NetState::Offline: return "offline";
    case NetState::Connecting: return "connecting";
    case NetState::Online: return "online";
    default: return "unknown";
    }
}

NetService::~NetService() {
    shutdown();
}

const char* NetService::certDir() {
    return kCertDir;
}

bool NetService::ensureRuntime() {
    // Caller holds m_mutex.
    if (m_closed)
        return false;
    if (!m_runtime) {
        m_runtime = switchu::services::net::acquireHttp();
        if (!m_runtime)
            DebugLog::log("[net] runtime unavailable");
    }
    return m_runtime;
}

NetState NetService::state() {
    std::lock_guard<std::mutex> lk(m_mutex);
    const std::uint64_t now = nowMs();
    if (m_stateTick != 0 && now - m_stateTick < 1000)
        return m_state;
    m_stateTick = now;
    NetState next = NetState::Unknown;
    if (ensureRuntime()) {
        NifmInternetConnectionStatus status = NifmInternetConnectionStatus_ConnectingUnknown1;
        u32 strength = 0;
        NifmInternetConnectionType type{};
        if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)))
            next = NetState::Offline;
        else if (status == NifmInternetConnectionStatus_Connected)
            next = NetState::Online;
        else
            next = NetState::Connecting;
    }
    if (next != m_state)
        DebugLog::log("[net] state %s -> %s", netStateName(m_state), netStateName(next));
    m_state = next;
    return m_state;
}

std::string NetService::rootFor(const std::string& url) {
    std::lock_guard<std::mutex> lk(m_rootsMutex);
    auto it = m_roots.find(hostOf(url));
    return it == m_roots.end() ? std::string() : it->second;
}

std::vector<std::string> NetService::bundledRoots() {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < kRootCount; ++i)
        if (fileExists(std::string(kCertDir) + kRootFiles[i]))
            out.emplace_back(kRootFiles[i]);
    return out;
}

void NetService::rememberRoot(const std::string& url, const std::string& root) {
    std::lock_guard<std::mutex> lk(m_rootsMutex);
    std::string& h = m_roots[hostOf(url)];
    if (h != root && !root.empty())
        DebugLog::log("[net] %s verified with bundled root %s", hostOf(url).c_str(), root.c_str());
    h = root;
}

Response NetService::get(const std::string& url, std::vector<std::string> headers) {
    Request req;
    req.url = url;
    req.headers = std::move(headers);
    return request(req);
}

Response NetService::request(const Request& req) {
    Response res;
    const std::uint64_t start = nowMs();
    auto& scope = themeshop::http::cancelScope();
    CancelCtx cancel{&scope, scope.begin(), req.cancel};
    auto finish = [&](Response& r) -> Response& {
        r.elapsedMs = static_cast<std::uint32_t>(nowMs() - start);
        return r;
    };

    UrlParts parts;
    if (!parseUrl(req.url, parts) || (parts.scheme != "https" && parts.scheme != "http")) {
        res.error = "bad url";
        return finish(res);
    }
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!ensureRuntime()) {
            res.error = m_closed ? "network service stopped" : "network runtime unavailable";
            return finish(res);
        }
    }
    // Own reference for the whole request, so shutdown() releasing the
    // service's reference never tears curl down under a transfer.
    switchu::services::net::HttpLease lease;
    if (!lease) {
        res.error = "network runtime unavailable";
        return finish(res);
    }

    // DNS and DHCP are not up for a while after boot or wake (C-NET-1).
    if (req.waitOnlineMs > 0) {
        const std::uint64_t until = nowMs() + static_cast<std::uint64_t>(req.waitOnlineMs);
        while (state() != NetState::Online && nowMs() < until)
            if (sleepCancellable(250, cancel))
                break;
    }
    if (cancelledFor(scope, cancel.token, req.cancel)) {
        res.cancelled = true;
        res.error = "cancelled";
        return finish(res);
    }
    if (state() != NetState::Online) {
        res.error = "offline";
        return finish(res);
    }

    const bool idempotent = req.method == "GET" || req.method == "HEAD";
    const int attempts = req.attempts > 0 ? req.attempts : (idempotent ? 3 : 1);
    CURL* c = curl_easy_init();
    if (!c) {
        res.error = "curl init failed";
        return finish(res);
    }
    std::string url = req.url;
    UrlParts current = parts;
    std::string root = rootFor(url);
    int redirects = 0;
    for (int attempt = 1; attempt <= attempts; ++attempt) {
        res.attempts = attempt;
        res.status = 0;
        res.body.clear();
        res.error.clear();
        curl_easy_reset(c);
        Sink sink{&res.body, req.maxBytes};
        HeaderSink headers;
        struct curl_slist* list = nullptr;
        for (const auto& h : req.headers)
            list = curl_slist_append(list, h.c_str());
        const std::string agent = std::string("OmniLaunch/") + SWITCHU_VERSION;
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
        // Redirects are handled below, never by curl: a Location to another
        // origin or to plain HTTP must not receive the headers or body.
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
        curl_easy_setopt(c, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, req.timeoutMs);
        curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
        curl_easy_setopt(c, CURLOPT_USERAGENT, agent.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
        curl_easy_setopt(c, CURLOPT_HEADERFUNCTION, onHeader);
        curl_easy_setopt(c, CURLOPT_HEADERDATA, &headers);
        curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
        curl_easy_setopt(c, CURLOPT_XFERINFODATA, &cancel);
        if (list)
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, list);
        if (req.method == "HEAD") {
            curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
        } else if (req.method != "GET") {
            curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, req.method.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, req.body.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(req.body.size()));
        }
        if (!root.empty())
            curl_easy_setopt(c, CURLOPT_CAINFO, (std::string(kCertDir) + root).c_str());

        CURLcode rc = curl_easy_perform(c);
        if (rc != CURLE_OK && certRefused(rc) && current.scheme == "https") {
            // The console store (or the remembered root) refused this host:
            // try each bundled root once for this request, still verifying.
            const std::string refused = root;
            for (const std::string& next : bundledRoots()) {
                if (next == refused)
                    continue;
                if (cancelledFor(scope, cancel.token, req.cancel))
                    break;
                res.body.clear();
                sink.overflow = false;
                curl_easy_setopt(c, CURLOPT_CAINFO, (std::string(kCertDir) + next).c_str());
                rc = curl_easy_perform(c);
                if (rc == CURLE_OK) {
                    rememberRoot(url, next);
                    root = next;
                    break;
                }
                if (!certRefused(rc))
                    break; // the network, not the certificate
            }
            if (rc != CURLE_OK && !refused.empty() && certRefused(rc))
                rememberRoot(url, {}); // the remembered root stopped working
        }
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &res.status);
        std::string location;
        if (rc == CURLE_OK && res.status >= 300 && res.status < 400) {
            char* redirect = nullptr;
            if (curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &redirect) == CURLE_OK && redirect)
                location = redirect;
        }
        if (list)
            curl_slist_free_all(list);

        // Same-origin HTTPS redirects only, and only for GET/HEAD; anything
        // else is returned to the caller as the 3xx it is.
        if (!location.empty() && req.followSameOriginRedirects && idempotent && redirects < 5 &&
            !cancelledFor(scope, cancel.token, req.cancel)) {
            UrlParts next;
            if (parseUrl(location, next) && next.scheme == "https" && current.scheme == "https" &&
                next.host == current.host && next.port == current.port) {
                ++redirects;
                url = location;
                current = next;
                --attempt; // a redirect is not a failed attempt
                continue;
            }
            DebugLog::log("[net] %s: redirect to another origin not followed", current.host.c_str());
        }

        if (cancel.hit || cancelledFor(scope, cancel.token, req.cancel)) {
            res.cancelled = true;
            res.error = "cancelled";
            break;
        }
        if (sink.overflow) {
            res.error = "response too large";
            break; // the same answer next time
        }
        bool retry = false;
        if (rc != CURLE_OK) {
            res.error = curl_easy_strerror(rc);
            retry = !certRefused(rc) && rc != CURLE_URL_MALFORMAT && rc != CURLE_UNSUPPORTED_PROTOCOL;
        } else if (retryableStatus(res.status)) {
            retry = true;
        }
        if (!retry || !idempotent || attempt == attempts)
            break;
        const std::uint32_t wait = backoffMs(attempt, headers.retryAfter);
        DebugLog::log("[net] %s %s: %s (status %ld), retry %d in %u ms", req.method.c_str(),
                      parts.host.c_str(), res.error.empty() ? "http" : res.error.c_str(), res.status,
                      attempt, wait);
        if (sleepCancellable(wait, cancel)) {
            res.cancelled = true;
            res.error = "cancelled";
            break;
        }
    }
    curl_easy_cleanup(c);
    if (!res.ok() && !res.cancelled)
        DebugLog::log("[net] %s %s failed: %s (status %ld, %d attempt(s))", req.method.c_str(),
                      parts.host.c_str(), res.error.empty() ? "http" : res.error.c_str(), res.status,
                      res.attempts);
    return finish(res);
}

void NetService::shutdown() {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_closed = true;
    if (m_runtime) {
        switchu::services::net::releaseHttp();
        m_runtime = false;
    }
}

} // namespace net
