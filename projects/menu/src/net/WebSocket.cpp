#include "net/WebSocket.hpp"

#include "net/NetService.hpp"
#include "net/NetUtil.hpp"
#include "core/DebugLog.hpp"
#include "services/NetRuntime.hpp"
#include "themeshop/ThemeHttp.hpp"

#include <curl/curl.h>
#include <switch.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <poll.h>

namespace net {

namespace {

using Clock = std::chrono::steady_clock;

bool certRefused(CURLcode rc) {
    return rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CONNECT_ERROR ||
           rc == CURLE_SSL_ISSUER_ERROR || rc == CURLE_SSL_CACERT_BADFILE;
}

bool flagSet(const std::atomic<bool>* flag) {
    return flag && flag->load(std::memory_order_acquire);
}

std::string lowered(std::string s) {
    for (char& ch : s)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return s;
}

// Value of header `name` (lower case, with colon) in an HTTP response head
// that ends with "\r\n".
std::string headerValue(const std::string& head, const char* name) {
    const std::string lowerHead = lowered(head);
    const std::size_t at = lowerHead.find(std::string("\r\n") + name);
    if (at == std::string::npos)
        return {};
    std::size_t v = at + 2 + std::char_traits<char>::length(name);
    while (v < head.size() && (head[v] == ' ' || head[v] == '\t')) ++v;
    const std::size_t end = head.find("\r\n", v);
    std::string value = head.substr(v, end == std::string::npos ? std::string::npos : end - v);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) value.pop_back();
    return value;
}

// True when the comma-separated header value contains `token` (lower case).
bool hasToken(const std::string& value, const char* token) {
    const std::string v = lowered(value);
    std::size_t start = 0;
    while (start <= v.size()) {
        std::size_t end = v.find(',', start);
        if (end == std::string::npos) end = v.size();
        std::size_t a = start, b = end;
        while (a < b && v[a] == ' ') ++a;
        while (b > a && v[b - 1] == ' ') --b;
        if (v.compare(a, b - a, token) == 0)
            return true;
        start = end + 1;
    }
    return false;
}

} // namespace

WebSocket::~WebSocket() {
    close();
}

void WebSocket::release() {
    if (m_curl) {
        curl_easy_cleanup(static_cast<CURL*>(m_curl));
        m_curl = nullptr;
    }
    if (m_lease) {
        switchu::services::net::releaseHttp();
        m_lease = false;
    }
    m_open = false;
    m_outbox.clear();
}

void WebSocket::close(int code) {
    if (m_curl && m_open && !m_closeSent) {
        m_closeSent = true;
        std::string p;
        p.push_back(static_cast<char>((code >> 8) & 0xFF));
        p.push_back(static_cast<char>(code & 0xFF));
        queueFrame(ws::Close, p);
        // Best effort, never blocking: the close frame is a courtesy.
        flush(false);
    }
    release();
}

bool WebSocket::connect(const std::string& url, const std::vector<std::string>& headers,
                        std::string& error, const std::atomic<bool>* cancel) {
    close();
    m_reader = ws::Reader();
    m_closeSent = false;
    m_closeCode = 0;
    m_closeReason.clear();
    auto& scope = themeshop::http::cancelScope();
    m_token = scope.begin();

    UrlParts parts;
    if (!parseUrl(url, parts) || (parts.scheme != "wss" && parts.scheme != "ws")) {
        error = "bad websocket url";
        return false;
    }
    if (!m_service.online()) {
        error = "offline";
        return false;
    }
    m_lease = switchu::services::net::acquireHttp();
    if (!m_lease) {
        error = "network runtime unavailable";
        return false;
    }
    CURL* c = curl_easy_init();
    if (!c) {
        error = "curl init failed";
        release();
        return false;
    }
    m_curl = c;
    const bool tls = parts.scheme == "wss";
    const std::string target = std::string(tls ? "https://" : "http://") + parts.host + ":" +
                               std::to_string(parts.port) + "/";
    curl_easy_setopt(c, CURLOPT_URL, target.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    struct ConnectCancel { const std::atomic<bool>* flag; themeshop::http::CancelScope::Token token; };
    ConnectCancel progress{cancel, m_token};
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &progress);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION,
        +[](void* ptr, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            const auto& state = *static_cast<ConnectCancel*>(ptr);
            return flagSet(state.flag) || themeshop::http::cancelScope().cancelled(state.token);
        });
    // WebSocket needs HTTP/1.1; never let ALPN negotiate HTTP/2.
    curl_easy_setopt(c, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    curl_easy_setopt(c, CURLOPT_SSL_ENABLE_ALPN, 0L);
    const std::string root = tls ? m_service.rootFor(url) : std::string();
    if (!root.empty())
        curl_easy_setopt(c, CURLOPT_CAINFO, (std::string(NetService::certDir()) + root).c_str());
    CURLcode rc = curl_easy_perform(c);
    if (rc != CURLE_OK && tls && certRefused(rc)) {
        for (const std::string& next : NetService::bundledRoots()) {
            if (next == root)
                continue;
            if (flagSet(cancel) || scope.cancelled(m_token))
                break;
            curl_easy_setopt(c, CURLOPT_CAINFO, (std::string(NetService::certDir()) + next).c_str());
            rc = curl_easy_perform(c);
            if (rc == CURLE_OK) {
                m_service.rememberRoot(url, next);
                break;
            }
            if (!certRefused(rc))
                break;
        }
        if (rc != CURLE_OK && !root.empty() && certRefused(rc))
            m_service.rememberRoot(url, {});
    }
    if (rc != CURLE_OK) {
        // Which stage failed (HomeNX 3dc5d93): ip=none means before any address
        // was found (name lookup); errno is the socket's.
        const char* ip = nullptr;
        long osErr = 0;
        curl_easy_getinfo(c, CURLINFO_PRIMARY_IP, &ip);
        curl_easy_getinfo(c, CURLINFO_OS_ERRNO, &osErr);
        error = std::string("connect: ") + curl_easy_strerror(rc) +
                " (rc=" + std::to_string(static_cast<int>(rc)) +
                " ip=" + (ip && *ip ? ip : "none") +
                " errno=" + std::to_string(osErr) + ")";
        release();
        return false;
    }
    // The progress pointer is stack-owned and only used by perform().
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, nullptr);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, nullptr);
    m_open = true;

    std::uint8_t keyBytes[16];
    randomGet(keyBytes, sizeof(keyBytes));
    const std::string key = base64(keyBytes, sizeof(keyBytes));
    const bool defaultPort = parts.port == (tls ? 443 : 80);
    std::string req = "GET " + parts.target + " HTTP/1.1\r\n";
    req += "Host: " + parts.host + (defaultPort ? std::string() : ":" + std::to_string(parts.port)) + "\r\n";
    req += "Upgrade: websocket\r\nConnection: Upgrade\r\n";
    req += "Sec-WebSocket-Key: " + key + "\r\n";
    req += "Sec-WebSocket-Version: 13\r\n";
    req += std::string("User-Agent: OmniLaunch/") + SWITCHU_VERSION + "\r\n";
    for (const auto& h : headers)
        req += h + "\r\n";
    req += "\r\n";
    m_outbox = req;
    if (!flush(true, cancel)) {
        error = "handshake send failed";
        release();
        return false;
    }

    const auto deadline = Clock::now() + std::chrono::seconds(12);
    std::string head;
    char buf[4096];
    for (;;) {
        if (flagSet(cancel) || scope.cancelled(m_token)) {
            error = "cancelled";
            release();
            return false;
        }
        size_t n = 0;
        rc = curl_easy_recv(c, buf, sizeof(buf), &n);
        if (rc == CURLE_OK && n > 0) {
            head.append(buf, n);
            const std::size_t end = head.find("\r\n\r\n");
            if (end != std::string::npos) {
                // Bytes after the head are already WebSocket frames.
                m_reader.feed(head.data() + end + 4, head.size() - end - 4);
                head.resize(end + 2);
                const std::size_t sp = head.find(' ');
                const int status = sp == std::string::npos ? 0 : std::atoi(head.c_str() + sp + 1);
                if (status != 101) {
                    error = "handshake rejected (HTTP " + std::to_string(status) + ")";
                    release();
                    return false;
                }
                if (lowered(headerValue(head, "upgrade:")) != "websocket" ||
                    !hasToken(headerValue(head, "connection:"), "upgrade")) {
                    error = "handshake missing upgrade headers";
                    release();
                    return false;
                }
                if (headerValue(head, "sec-websocket-accept:") != wsAcceptKey(key)) {
                    error = "handshake accept key mismatch";
                    release();
                    return false;
                }
                DebugLog::log("[ws] connected %s", parts.host.c_str());
                return true;
            }
            if (head.size() > 65536)
                break;
            continue;
        }
        if (rc == CURLE_AGAIN && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        break;
    }
    error = "handshake failed";
    release();
    return false;
}

bool WebSocket::queueFrame(ws::Opcode op, const std::string& payload) {
    if (!m_curl || !m_open)
        return false;
    std::uint8_t mask[4];
    randomGet(mask, sizeof(mask));
    const std::string frame = ws::encode(op, payload, mask);
    // A peer that pings but never reads cannot grow this without bound.
    if (m_outbox.size() + frame.size() > kMaxOutbox && op != ws::Close)
        return false;
    m_outbox += frame;
    return true;
}

bool WebSocket::flush(bool blocking, const std::atomic<bool>* cancel) {
    CURL* c = static_cast<CURL*>(m_curl);
    if (!c)
        return false;
    auto& scope = themeshop::http::cancelScope();
    const auto deadline = Clock::now() + std::chrono::seconds(10);
    while (!m_outbox.empty()) {
        size_t sent = 0;
        const CURLcode rc = curl_easy_send(c, m_outbox.data(), m_outbox.size(), &sent);
        if (rc == CURLE_OK) {
            m_outbox.erase(0, sent);
            continue;
        }
        if (rc != CURLE_AGAIN) {
            DebugLog::log("[ws] send failed: %s", curl_easy_strerror(rc));
            m_open = false;
            return false;
        }
        if (!blocking)
            return true; // the rest goes out on a later poll()
        if (Clock::now() >= deadline || flagSet(cancel) || scope.cancelled(m_token))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

bool WebSocket::sendText(const std::string& text, const std::atomic<bool>* cancel) {
    if (themeshop::http::cancelScope().cancelled(m_token))
        return false;
    return !flagSet(cancel) && queueFrame(ws::Text, text) && flush(true, cancel);
}

void WebSocket::queueCloseEcho() {
    if (m_closeSent)
        return;
    m_closeSent = true;
    std::string p;
    if (m_reader.closeHasCode()) {
        p.push_back(static_cast<char>((m_closeCode >> 8) & 0xFF));
        p.push_back(static_cast<char>(m_closeCode & 0xFF));
    }
    queueFrame(ws::Close, p);
}

bool WebSocket::poll(std::vector<ws::Message>& out) {
    CURL* c = static_cast<CURL*>(m_curl);
    if (!c || !m_open)
        return false;
    // A frontend switch or launch hand-off ends the connection, as it ends
    // every request of that scope.
    if (themeshop::http::cancelScope().cancelled(m_token)) {
        m_closeCode = 1001;
        close(1001);
        return false;
    }
    flush(false); // earlier control replies
    char buf[16384];
    std::size_t budget = kPollReadBudget;
    bool alive = true;
    while (budget > 0 && alive && m_open) {
        size_t n = 0;
        const CURLcode rc = curl_easy_recv(c, buf, sizeof(buf), &n);
        if (rc == CURLE_OK) {
            if (n == 0) { // orderly end without a close frame
                if (!m_closeCode) m_closeCode = 1006;
                m_open = false;
                break;
            }
            budget = n >= budget ? 0 : budget - n;
            m_reader.feed(buf, n);
            alive = m_reader.parse(out); // parse per chunk: memory stays bounded
            continue;
        }
        if (rc != CURLE_AGAIN) {
            DebugLog::log("[ws] recv failed: %s", curl_easy_strerror(rc));
            if (!m_closeCode) m_closeCode = 1006;
            m_open = false;
        }
        break;
    }
    if (alive)
        alive = m_reader.parse(out);
    for (auto& pong : m_reader.pendingPongs())
        queueFrame(ws::Pong, pong);
    m_reader.pendingPongs().clear();
    if (m_reader.closeReceived()) {
        m_closeCode = m_reader.closeCode();
        m_closeReason = m_reader.closeReason();
        queueCloseEcho();
        flush(false);
        m_open = false;
    } else if (!alive) {
        DebugLog::log("[ws] protocol error: %s", m_reader.error().c_str());
        if (!m_closeCode) m_closeCode = 1002;
        close(1002);
        return false;
    } else {
        flush(false);
    }
    if (!m_open)
        release();
    return m_open;
}

bool WebSocket::wait(std::vector<ws::Message>& out, int timeoutMs, const std::atomic<bool>* cancel) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        const std::size_t before = out.size();
        if (!poll(out))
            return false;
        if (out.size() > before || Clock::now() >= deadline || flagSet(cancel))
            return true;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        const int budget = static_cast<int>(std::min<std::int64_t>(200, std::max<std::int64_t>(1, left)));
        curl_socket_t fd = CURL_SOCKET_BAD;
        if (curl_easy_getinfo(static_cast<CURL*>(m_curl), CURLINFO_ACTIVESOCKET, &fd) == CURLE_OK &&
            fd != CURL_SOCKET_BAD) {
            pollfd socket{fd, POLLIN, 0};
            const auto beforeWait = Clock::now();
            const int rc = ::poll(&socket, 1, budget);
            // A broken/always-ready socket must not turn into a tight loop.
            if (rc < 0 || (rc > 0 && Clock::now() - beforeWait < std::chrono::milliseconds(1)))
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

} // namespace net
