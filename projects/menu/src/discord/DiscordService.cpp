#include "discord/DiscordService.hpp"

#include "core/DebugLog.hpp"
#include "discord/DiscordApi.hpp"
#include "home/HomeCore.hpp"
#include "net/NetService.hpp"

#include <nlohmann/json.hpp>

namespace discord {

DiscordService::DiscordService() = default;

DiscordService::~DiscordService() {
    shutdown();
}

void DiscordService::activate(home::HomeCore& core) {
    if (m_activated) return;
    m_activated = true;

    m_accounts.load();
    if (m_accounts.hasAccount()) {
        connectAccount(core, m_accounts.current());
    } else {
        m_state = State::SignedOut;
    }
}

void DiscordService::connectAccount(home::HomeCore& core, const Account& acc) {
    if (acc.token.empty()) {
        m_state = State::SignedOut;
        return;
    }

    m_state = State::Connecting;
    m_error.clear();
    DebugLog::log("[discord] connecting account %s...", acc.username.c_str());

    m_gateway.start(core.net(), acc.token);

    // Initial background REST load
    if (core.threadPool().acceptingTasks()) {
        core.threadPool().submit([this, &core, token = acc.token]() {
            User u;
            std::string err;
            if (api::fetchMe(core.net(), token, u, err)) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_currentUser = u;
            }
            std::vector<Channel> dms;
            if (api::fetchDms(core.net(), token, dms, err)) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_dms = std::move(dms);
            }
            std::vector<Guild> guilds;
            if (api::fetchGuilds(core.net(), token, guilds, err)) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_guilds = std::move(guilds);
            }
        });
    }
}

void DiscordService::update(home::HomeCore& core) {
    // 1. Check QR auth state if active
    if (m_state == State::QrLogin) {
        const auto snap = m_remoteAuth.snapshot();
        if (snap.phase == DiscordRemoteAuth::Phase::Done && !snap.token.empty()) {
            DebugLog::log("[discord] QR auth finished, token obtained");
            m_remoteAuth.stop();
            signInWithToken(core, snap.token);
            return;
        } else if (snap.phase == DiscordRemoteAuth::Phase::Cancelled) {
            DebugLog::log("[discord] QR auth cancelled");
            m_remoteAuth.stop();
            m_state = m_accounts.hasAccount() ? State::Ready : State::SignedOut;
        } else if (snap.phase == DiscordRemoteAuth::Phase::Failed) {
            DebugLog::log("[discord] QR auth failed: %s", snap.error.c_str());
            m_error = snap.error;
            m_remoteAuth.stop();
            m_state = State::Failed;
        }
    }

    // 2. Poll Gateway state
    if (m_state == State::Connecting && m_gateway.isConnected()) {
        m_state = State::Ready;
    } else if (m_state == State::Ready && m_gateway.state() == DiscordGateway::State::Failed) {
        m_state = State::Failed;
        m_error = m_gateway.lastError();
    }

    // 3. Process Gateway events
    const auto events = m_gateway.drainEvents();
    for (const auto& ev : events) {
        processEvent(ev);
    }
}

void DiscordService::processEvent(const DiscordGateway::Event& ev) {
    if (ev.name == "READY") {
        m_state = State::Ready;
        const auto j = nlohmann::json::parse(ev.rawJson, nullptr, false);
        if (j.is_object() && j.contains("d") && j["d"].is_object()) {
            const auto& d = j["d"];
            if (d.contains("user") && d["user"].is_object()) {
                std::lock_guard<std::mutex> lk(m_mu);
                api::parseUserJson(d["user"].dump(), m_currentUser);
            }
        }
    } else if (ev.name == "MESSAGE_CREATE") {
        const auto j = nlohmann::json::parse(ev.rawJson, nullptr, false);
        if (j.is_object() && j.contains("d") && j["d"].is_object()) {
            std::vector<Message> tmp;
            const std::string wrapped = "[" + j["d"].dump() + "]";
            if (api::parseMessagesJson(wrapped, tmp) && !tmp.empty()) {
                Message m = std::move(tmp.front());
                const Snowflake chanId = m.channelId;
                const bool fromOther = (m.author.id != 0 && m.author.id != m_currentUser.id);

                {
                    std::lock_guard<std::mutex> lk(m_mu);
                    m_messages[chanId].push_back(m);
                    if (m_messages[chanId].size() > 50) {
                        m_messages[chanId].erase(m_messages[chanId].begin());
                    }

                    if (fromOther) {
                        m_pendingAlert.channelId = chanId;
                        m_pendingAlert.title = m.author.displayName();
                        m_pendingAlert.text = m.content;
                        m_pendingAlert.avatarUrl = m.author.avatarUrl(64);
                        m_hasAlert = true;
                    }
                }
            }
        }
    }
}

void DiscordService::startQrLogin(home::HomeCore& core) {
    m_state = State::QrLogin;
    m_error.clear();
    m_remoteAuth.start(core.net());
}

void DiscordService::cancelQrLogin() {
    m_remoteAuth.stop();
    m_state = m_accounts.hasAccount() ? State::Ready : State::SignedOut;
}

void DiscordService::signInWithToken(home::HomeCore& core, const std::string& token) {
    m_state = State::Connecting;
    m_error.clear();

    if (!core.threadPool().acceptingTasks()) {
        m_state = State::Failed;
        m_error = "Background tasks unavailable";
        return;
    }

    core.threadPool().submit([this, &core, token]() {
        User u;
        std::string err;
        if (api::fetchMe(core.net(), token, u, err)) {
            Account acc;
            acc.id = snowflakeToString(u.id);
            acc.username = u.username;
            acc.globalName = u.globalName;
            acc.avatar = u.avatar;
            acc.token = token;

            m_accounts.addOrUpdate(acc);
            m_accounts.save();

            connectAccount(core, acc);
        } else {
            DebugLog::log("[discord] token validation failed: %s", err.c_str());
            m_state = State::Failed;
            m_error = "Invalid Discord token";
        }
    });
}

void DiscordService::switchAccount(home::HomeCore& core, const std::string& id) {
    m_accounts.setCurrentId(id);
    m_accounts.save();
    shutdown();
    connectAccount(core, m_accounts.current());
}

void DiscordService::signOut() {
    const std::string cur = m_accounts.currentId();
    if (!cur.empty()) {
        m_accounts.remove(cur);
        m_accounts.save();
    }
    shutdown();
    m_state = State::SignedOut;
    m_currentUser = User{};
    m_dms.clear();
    m_guilds.clear();
    m_messages.clear();
}

std::vector<Message> DiscordService::messages(Snowflake channelId) const {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_messages.find(channelId);
    return it != m_messages.end() ? it->second : std::vector<Message>{};
}

void DiscordService::sendMessage(home::HomeCore& core, Snowflake channelId, const std::string& text) {
    const std::string token = m_accounts.current().token;
    if (token.empty() || text.empty()) return;

    if (!core.threadPool().acceptingTasks()) return;

    core.threadPool().submit([this, &core, token, channelId, text]() {
        Message outMsg;
        std::string err;
        if (api::sendMessage(core.net(), token, channelId, text, outMsg, err)) {
            std::lock_guard<std::mutex> lk(m_mu);
            m_messages[channelId].push_back(outMsg);
        } else {
            DebugLog::log("[discord] sendMessage error: %s", err.c_str());
        }
    });
}

bool DiscordService::takeAlert(Alert& out) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (!m_hasAlert) return false;
    out = m_pendingAlert;
    m_hasAlert = false;
    return true;
}

void DiscordService::beginShutdown() {
    m_gateway.stop();
    m_remoteAuth.stop();
}

void DiscordService::shutdown() {
    m_gateway.stop();
    m_remoteAuth.stop();
}

} // namespace discord
