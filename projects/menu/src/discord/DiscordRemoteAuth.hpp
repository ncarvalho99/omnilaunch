#pragma once
// Discord Remote Auth v2 QR login (HomeNX H1, C-NET-2).
// Allows logging in by scanning a QR code with the Discord mobile app.
// New OmniLaunch implementation written from scratch.

#include "discord/DiscordTypes.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace net { class NetService; }

namespace discord {

class DiscordRemoteAuth {
public:
    enum class Phase {
        Idle,
        Preparing,
        ShowQr,
        Confirm,
        LoggingIn,
        Done,
        Cancelled,
        Failed,
    };

    struct Snapshot {
        Phase phase = Phase::Idle;
        std::string qrUrl;
        int qrSize = 0;
        std::vector<std::uint8_t> qrModules; // size * size, 1 = dark, 0 = light
        std::string userId;
        std::string username;
        std::string avatar;
        std::string token;
        std::string error;
        std::int64_t expiresMs = 0;
    };

    DiscordRemoteAuth();
    ~DiscordRemoteAuth();

    void start(net::NetService& net);
    void stop();

    Snapshot snapshot() const;
    Phase phase() const;
    bool isDone() const;

private:
    struct RsaKey;

    void run(net::NetService& net);
    bool runSession(net::NetService& net, RsaKey& key);
    void setPhase(Phase p, const std::string& err = "");

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_done{true};
    mutable std::mutex m_mu;
    Snapshot m_snap;
    bool m_hashedProof = true;
};

} // namespace discord
