#pragma once
// Discord service in HomeCore (HomeNX H1, C-NET-2).
// Owns accounts, remote-auth QR login, WebSocket gateway and REST API integration.
// New OmniLaunch implementation written from scratch.

#include "discord/DiscordAccounts.hpp"
#include "discord/DiscordGateway.hpp"
#include "discord/DiscordRemoteAuth.hpp"
#include "discord/DiscordTypes.hpp"

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace net { class NetService; }
namespace home { class HomeCore; }

namespace discord {

/// Read-only view of a list the service publishes. Workers never modify a
/// published list: they build a new one and swap it in under the lock, so a
/// reader keeps a consistent copy for as long as it holds the view.
template <class T>
struct ListView {
    std::shared_ptr<const std::vector<T>> items;
    std::size_t size() const { return items ? items->size() : 0; }
    bool empty() const { return size() == 0; }
    const T& operator[](std::size_t i) const { return (*items)[i]; }
    typename std::vector<T>::const_iterator begin() const { return items ? items->begin() : kEmpty.begin(); }
    typename std::vector<T>::const_iterator end() const { return items ? items->end() : kEmpty.end(); }
private:
    static inline const std::vector<T> kEmpty{};
};

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

    State state() const { return m_state.load(); }
    std::string error() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_error;
    }
    std::uint64_t version() const { return m_version.load(); }
    bool started() const { return m_state.load() != State::SignedOut; }
    int unreadDms() const { return 0; }
    int friendsOnline() const { return 0; }
    std::string myName() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_currentUser.username.empty() ? m_currentUser.globalName : m_currentUser.username;
    }

    DiscordAccounts& accounts() { return m_accounts; }
    const DiscordAccounts& accounts() const { return m_accounts; }

    DiscordRemoteAuth& remoteAuth() { return m_remoteAuth; }
    const DiscordRemoteAuth& remoteAuth() const { return m_remoteAuth; }

    DiscordGateway& gateway() { return m_gateway; }
    const DiscordGateway& gateway() const { return m_gateway; }

    User currentUser() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return m_currentUser;
    }
    ListView<Channel> dms() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return {m_dms};
    }
    ListView<Guild> guilds() const {
        std::lock_guard<std::mutex> lk(m_mu);
        return {m_guilds};
    }
    std::string presenceStatus(Snowflake userId) const;

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
    /// Ends the current account session: every job started before this
    /// returns finds a different generation and publishes nothing.
    void endSession();
    bool current(std::uint64_t session) const { return m_session.load() == session; }
    void bump() { ++m_version; }

    DiscordAccounts m_accounts;
    DiscordRemoteAuth m_remoteAuth;
    DiscordGateway m_gateway;

    std::atomic<State> m_state{State::SignedOut};
    bool m_activated = false;
    std::atomic<std::uint64_t> m_version{0};
    std::atomic<std::uint64_t> m_session{1};

    mutable std::mutex m_mu;
    std::string m_error;
    User m_currentUser;
    std::shared_ptr<const std::vector<Channel>> m_dms = std::make_shared<const std::vector<Channel>>();
    std::shared_ptr<const std::vector<Guild>> m_guilds = std::make_shared<const std::vector<Guild>>();
    // Token sign-in validated on a worker; adopted by update() on the main
    // thread (accounts file, gateway start) if the session is still current.
    struct PendingSignIn {
        std::uint64_t session = 0;
        bool done = false;
        bool ok = false;
        Account account;
    };
    PendingSignIn m_pendingSignIn;
    std::map<Snowflake, std::vector<Message>> m_messages;
    std::map<Snowflake, std::string> m_presences;

    Alert m_pendingAlert;
    bool m_hasAlert = false;
};

} // namespace discord
