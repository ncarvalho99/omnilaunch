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
                m_version++;
            }
            std::vector<Channel> dms;
            if (api::fetchDms(core.net(), token, dms, err)) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_dms = std::move(dms);
                m_version++;
                DebugLog::log("[discord] REST fetched %zu DMs", m_dms.size());
            }
            std::vector<Guild> guilds;
            if (api::fetchGuilds(core.net(), token, guilds, err)) {
                std::lock_guard<std::mutex> lk(m_mu);
                m_guilds = std::move(guilds);
                m_version++;
                DebugLog::log("[discord] REST fetched %zu guilds", m_guilds.size());
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
            m_version++;
        } else if (snap.phase == DiscordRemoteAuth::Phase::Failed) {
            DebugLog::log("[discord] QR auth failed: %s", snap.error.c_str());
            m_error = snap.error;
            m_remoteAuth.stop();
            m_state = State::Failed;
            m_version++;
        }
    }

    // 2. Poll Gateway state
    if (m_state == State::Connecting && m_gateway.isConnected()) {
        m_state = State::Ready;
        m_version++;
    } else if (m_state == State::Ready && m_gateway.state() == DiscordGateway::State::Failed) {
        m_state = State::Failed;
        m_error = m_gateway.lastError();
        m_version++;
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
            std::lock_guard<std::mutex> lk(m_mu);
            if (d.contains("user") && d["user"].is_object()) {
                api::parseUserJson(d["user"].dump(), m_currentUser);
            }
            if (d.contains("private_channels") && d["private_channels"].is_array()) {
                api::parseChannelsJson(d["private_channels"].dump(), m_dms);
            }
            if (d.contains("guilds") && d["guilds"].is_array()) {
                api::parseGuildsJson(d["guilds"].dump(), m_guilds);
            }
            m_version++;
            DebugLog::log("[discord] Gateway READY: user=%s dms=%zu guilds=%zu",
                          m_currentUser.username.c_str(), m_dms.size(), m_guilds.size());
        }
    } else if (ev.name == "GUILD_CREATE") {
        const auto j = nlohmann::json::parse(ev.rawJson, nullptr, false);
        if (j.is_object() && j.contains("d") && j["d"].is_object()) {
            const auto& d = j["d"];
            Guild g;
            g.id = stringToSnowflake(d.value("id", "0"));
            g.name = d.value("name", "");
            g.icon = d.value("icon", "");
            g.description = d.value("description", "");
            if (d.contains("channels") && d["channels"].is_array()) {
                api::parseChannelsJson(d["channels"].dump(), g.channels);
            }
            if (g.id != 0) {
                std::lock_guard<std::mutex> lk(m_mu);
                bool found = false;
                for (auto& existing : m_guilds) {
                    if (existing.id == g.id) {
                        existing = std::move(g);
                        found = true;
                        break;
                    }
                }
                if (!found) m_guilds.push_back(std::move(g));
                m_version++;
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
                    m_version++;
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
            m_version++;

            connectAccount(core, acc);
        } else {
            DebugLog::log("[discord] token validation failed: %s", err.c_str());
            m_state = State::Failed;
            m_error = "Invalid Discord token";
            m_version++;
        }
    });
}

void DiscordService::switchAccount(home::HomeCore& core, const std::string& id) {
    m_accounts.setCurrentId(id);
    m_accounts.save();
    shutdown();
    m_version++;
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
    m_version++;
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
            m_version++;
        } else {
            DebugLog::log("[discord] sendMessage error: %s", err.c_str());
        }
    });
}

void DiscordService::fetchGuildChannelsAsync(home::HomeCore& core, Snowflake guildId) {
    const std::string token = m_accounts.current().token;
    if (token.empty() || guildId == 0) return;
    if (!core.threadPool().acceptingTasks()) return;

    core.threadPool().submit([this, &core, token, guildId]() {
        std::vector<Channel> channels;
        std::string err;
        if (api::fetchGuildChannels(core.net(), token, guildId, channels, err)) {
            std::lock_guard<std::mutex> lk(m_mu);
            for (auto& g : m_guilds) {
                if (g.id == guildId) {
                    g.channels = std::move(channels);
                    break;
                }
            }
            m_version++;
            DebugLog::log("[discord] fetched %zu channels for guild %llu",
                          channels.size(), (unsigned long long)guildId);
        } else {
            DebugLog::log("[discord] fetchGuildChannels error: %s", err.c_str());
        }
    });
}

void DiscordService::fetchMessagesAsync(home::HomeCore& core, Snowflake channelId) {
    const std::string token = m_accounts.current().token;
    if (token.empty() || channelId == 0) return;
    if (!core.threadPool().acceptingTasks()) return;

    core.threadPool().submit([this, &core, token, channelId]() {
        std::vector<Message> msgs;
        std::string err;
        if (api::fetchMessages(core.net(), token, channelId, msgs)) {
            std::lock_guard<std::mutex> lk(m_mu);
            m_messages[channelId] = std::move(msgs);
            m_version++;
            DebugLog::log("[discord] fetched %zu messages for channel %llu",
                          msgs.size(), (unsigned long long)channelId);
        } else {
            DebugLog::log("[discord] fetchMessages error for channel %llu: %s",
                          (unsigned long long)channelId, err.c_str());
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
