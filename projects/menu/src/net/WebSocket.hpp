#pragma once
// Minimal RFC 6455 client for NetService (HomeNX G1, C-NET-1). The console's
// libcurl (7.69) has no WebSocket support, so curl opens the TCP/TLS stream
// (CONNECT_ONLY) and this class does the upgrade and framing over
// curl_easy_send/recv. New OmniLaunch code; HomeNX net/websocket @ 0448cd4 was
// read for protocol behaviour only. One owner thread; not thread-safe.

#include "net/WsFrame.hpp"
#include "themeshop/ThemeHttpScope.hpp"

#include <atomic>
#include <string>
#include <vector>

namespace net {

class NetService;

class WebSocket {
public:
    explicit WebSocket(NetService& service) : m_service(service) {}
    ~WebSocket();
    WebSocket(const WebSocket&) = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    /// wss:// or ws:// URL; extra headers are "Name: value". Blocking (up to
    /// about 12 s); `cancel` aborts the wait.
    bool connect(const std::string& url, const std::vector<std::string>& headers, std::string& error,
                 const std::atomic<bool>* cancel = nullptr);
    bool sendText(const std::string& text);
    /// Non-blocking: appends complete messages, answers pings. False once the
    /// connection is closed or the frontend/terminal network scope ended.
    bool poll(std::vector<ws::Message>& out);
    /// Polls until at least one message arrives, the connection ends or
    /// `timeoutMs` passes.
    bool wait(std::vector<ws::Message>& out, int timeoutMs, const std::atomic<bool>* cancel = nullptr);
    void close(int code = 1000);

    bool isOpen() const { return m_curl && m_open; }
    int closeCode() const { return m_closeCode; }
    const std::string& closeReason() const { return m_closeReason; }

private:
    /// Queues a frame. Control replies (pong, close echo) stay queued and are
    /// flushed without blocking by poll(); sendText() flushes until done or
    /// cancelled.
    bool queueFrame(ws::Opcode op, const std::string& payload);
    /// Sends queued bytes. `blocking`: until empty, a 10 s deadline or a
    /// cancel; otherwise only what the socket takes now.
    bool flush(bool blocking, const std::atomic<bool>* cancel = nullptr);
    void queueCloseEcho();
    void release();

    static constexpr std::size_t kMaxOutbox = 1u << 20;
    /// Bytes read per poll(); the rest waits for the next call.
    static constexpr std::size_t kPollReadBudget = 256u << 10;

    NetService& m_service;
    void* m_curl = nullptr;
    bool m_lease = false;
    bool m_open = false;
    bool m_closeSent = false;
    int m_closeCode = 0;
    std::string m_closeReason;
    ws::Reader m_reader;
    std::string m_outbox;
    themeshop::http::CancelScope::Token m_token{};
};

} // namespace net
