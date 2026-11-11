#pragma once
// Discord service in HomeCore (HomeNX H1, C-NET-2).
// Owns accounts, remote-auth QR login, WebSocket gateway and REST API integration.
// New OmniLaunch implementation written from scratch.

#include "discord/DiscordAccounts.hpp"
#include "discord/DiscordGateway.hpp"
#include "discord/DiscordRemoteAuth.hpp"
#include "discord/DiscordTypes.hpp"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace net { class NetService; }
namespace home { class HomeCore; }

namespace discord {

class DiscordService {
public:
    enum class State {
        SignedOut,
        QrLogin,
        Connecting,
        Ready,
        Failed,
    };

    DiscordService();
    ~DiscordService();

    DiscordService(const DiscordService&) = delete;
    DiscordService& operator=(const DiscordService&) = delete;

    /// First call loads saved accounts and connects if one exists.
    void activate(home::HomeCore& core);

    /// Main-thread update per frame: polls gateway events, handles QR auth transitions.
    void update(home::HomeCore& core);

    /// Begins graceful background teardown when launching a game.
    void beginShutdown();

    /// Full synchronous shutdown before process exit.
    void shutdown();

    State state() const { return m_state; }
    std::string error() const { return m_error; }
    std::uint64_t version() const { return m_version; }
    bool started() const { return m_state != State::SignedOut; }
    int unreadDms() const { return 0; }
    int friendsOnline() const { return 0; }
    std::string myName() const {
        return m_currentUser.username.empty() ? m_currentUser.globalName : m_currentUser.username;
    }

    DiscordAccounts& accounts() { return m_accounts; }
    const DiscordAccounts& accounts() const { return m_accounts; }

    DiscordRemoteAuth& remoteAuth() { return m_remoteAuth; }
    const DiscordRemoteAuth& remoteAuth() const { return m_remoteAuth; }

    DiscordGateway& gateway() { return m_gateway; }
    const DiscordGateway& gateway() const { return m_gateway; }

    const User& currentUser() const { return m_currentUser; }
    const std::vector<Channel>& dms() const { return m_dms; }
    const std::vector<Guild>& guilds() const { return m_guilds; }

    std::vector<Message> messages(Snowflake channelId) const;
    void sendMessage(home::HomeCore& core, Snowflake channelId, const std::string& text);
    void fetchGuildChannelsAsync(home::HomeCore& core, Snowflake guildId);
    void fetchMessagesAsync(home::HomeCore& core, Snowflake channelId);

    void startQrLogin(home::HomeCore& core);
    void cancelQrLogin();
    void signInWithToken(home::HomeCore& core, const std::string& token);
    void switchAccount(home::HomeCore& core, const std::string& id);
    void signOut();

    bool takeAlert(Alert& out);

private:
    void connectAccount(home::HomeCore& core, const Account& acc);
    void processEvent(const DiscordGateway::Event& ev);

    DiscordAccounts m_accounts;
    DiscordRemoteAuth m_remoteAuth;
    DiscordGateway m_gateway;

    State m_state = State::SignedOut;
    std::string m_error;
    bool m_activated = false;
    std::uint64_t m_version = 0;

    mutable std::mutex m_mu;
    User m_currentUser;
    std::vector<Channel> m_dms;
    std::vector<Guild> m_guilds;
    std::map<Snowflake, std::vector<Message>> m_messages;

    Alert m_pendingAlert;
    bool m_hasAlert = false;
};

} // namespace discord
