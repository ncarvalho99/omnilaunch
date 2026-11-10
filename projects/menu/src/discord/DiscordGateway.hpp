#pragma once
// Discord Gateway v10 WebSocket client (HomeNX H1, C-NET-2).
// Runs on a background thread; handles heartbeats, identify, resume and
// queues dispatch events for the main thread.
// New OmniLaunch implementation written from scratch.

#include "discord/DiscordTypes.hpp"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace net { class NetService; }

namespace discord {

class DiscordGateway {
public:
    enum class State {
        Disconnected,
        Connecting,
        Handshaking,
        Connected,
        Reconnecting,
        Failed,
    };

    struct Event {
        std::string name; // "READY", "MESSAGE_CREATE", etc.
        std::string rawJson;
    };

    DiscordGateway();
    ~DiscordGateway();

    void start(net::NetService& net, const std::string& token);
    void stop();

    State state() const;
    bool isConnected() const;

    /// Sends raw JSON text over the gateway.
    bool send(const std::string& text);

    /// Drain incoming dispatch events received since last call.
    std::vector<Event> drainEvents();

    std::string lastError() const;

private:
    void run(net::NetService& net);
    void setState(State s, const std::string& err = "");

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<State> m_state{State::Disconnected};
    std::string m_token;

    mutable std::mutex m_mu;
    std::deque<Event> m_inEvents;
    std::string m_lastError;
    std::string m_sessionId;
    std::string m_resumeGatewayUrl;
    std::int64_t m_lastSeq = -1;
};

} // namespace discord
