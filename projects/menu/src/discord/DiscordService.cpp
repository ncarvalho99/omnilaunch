#include "discord/DiscordService.hpp"

#include "core/DebugLog.hpp"
#include "discord/DiscordApi.hpp"
#include "home/HomeCore.hpp"
#include "net/NetService.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

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
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_error.clear();
    }
    DebugLog::log("[discord] connecting account %s...", acc.username.c_str());

    m_gateway.start(core.net(), acc.token);

    // Initial background REST load. Each result is published only while the
    // session that asked for it is still the current one.
    if (core.threadPool().acceptingTasks()) {
        const std::uint64_t session = m_session.load();
        core.threadPool().submit([this, &core, token = acc.token, session]() {
            User u;
            std::string err;
            if (current(session) && api::fetchMe(core.net(), token, u, err)) {
                std::lock_guard<std::mutex> lk(m_mu);
                if (current(session)) {
                    m_currentUser = u;
                    bump();
                }
            }
            std::vector<Channel> dms;
            if (current(session) && api::fetchDms(core.net(), token, dms, err)) {
                const std::size_t n = dms.size();
                std::lock_guard<std::mutex> lk(m_mu);
                if (current(session)) {
                    m_dms = std::make_shared<const std::vector<Channel>>(std::move(dms));
                    applyReadStatesLocked();
                    bump();
                    DebugLog::log("[discord] REST fetched %zu DMs", n);
                }
            }
            std::vector<Guild> guilds;
            if (current(session) && api::fetchGuilds(core.net(), token, guilds, err)) {
                const std::size_t n = guilds.size();
                std::lock_guard<std::mutex> lk(m_mu);
                if (current(session)) {
                    // The REST list has no channels; keep the ones READY gave.
                    for (auto& g : guilds)
                        for (const auto& old : *m_guilds)
                            if (old.id == g.id && g.channels.empty()) g.channels = old.channels;
                    m_guilds = std::make_shared<const std::vector<Guild>>(std::move(guilds));
                    applyReadStatesLocked();
                    bump();
                    DebugLog::log("[discord] REST fetched %zu guilds", n);
                }
            }
        });
    }
}

// Ends the account session: the gateway is stopped (its queued events are
// dropped), every job in flight is invalidated, and everything that belonged
// to the previous account is cleared. Main thread only.
void DiscordService::endSession() {
    m_asleep = false;
    m_resumeAfterSleep = false;
    m_gateway.stop();
    m_remoteAuth.stop();
    ++m_session;
    std::lock_guard<std::mutex> lk(m_mu);
    m_pendingSignIn = PendingSignIn{};
    m_currentUser = User{};
    m_dms = std::make_shared<const std::vector<Channel>>();
    m_guilds = std::make_shared<const std::vector<Guild>>();
    m_messages.clear();
    m_presences.clear();
    m_readStates.clear();
    m_mutedGuilds.clear();
    m_mutedChannels.clear();
    m_hasAlert = false;
    m_pendingAlert = Alert{};
}

void DiscordService::sleep() {
    if (m_asleep) return;
    m_asleep = true;
    m_gateway.pause();
    DebugLog::log("[discord] gateway paused; account and messages retained");
}

void DiscordService::wake() {
    if (!m_asleep) return;
    m_asleep = false;
    m_resumeAfterSleep = true;
}

void DiscordService::update(home::HomeCore& core) {
    if (m_asleep) return;
    if (m_resumeAfterSleep && m_gateway.done()) {
        m_resumeAfterSleep = false;
        if (m_gateway.resume(core.net())) {
            if (m_state != State::QrLogin) m_state = State::Connecting;
            bump();
        }
    }

    // 0. A token validated on a worker is adopted here, on the main thread,
    //    and only for the session that asked for it.
    PendingSignIn signIn;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        if (m_pendingSignIn.done) {
            signIn = std::move(m_pendingSignIn);
            m_pendingSignIn = PendingSignIn{};
        }
    }
    if (signIn.done && current(signIn.session)) {
        if (signIn.ok) {
            m_accounts.addOrUpdate(signIn.account);
            m_accounts.save();
            bump();
            connectAccount(core, signIn.account);
        } else {
            m_state = State::Failed;
            {
                std::lock_guard<std::mutex> lk(m_mu);
                m_error = "Invalid Discord token";
            }
            bump();
        }
    }

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
            {
                std::lock_guard<std::mutex> lk(m_mu);
                m_error = snap.error;
            }
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
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_error = m_gateway.lastError();
        }
        m_version++;
    }

    // 3. Process Gateway events
    const auto events = m_gateway.drainEvents();
    for (const auto& ev : events) {
        // A malformed or unexpected payload must cost one event, never the
        // HOME Menu: an escaped exception here terminates the process.
        try {
            processEvent(ev);
        } catch (const std::exception& e) {
            DebugLog::log("[discord] dropped %s event: %s", ev.name.c_str(), e.what());
        }
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
                std::vector<Channel> dms;
                if (api::parseChannelsJson(d["private_channels"].dump(), dms))
                    m_dms = std::make_shared<const std::vector<Channel>>(std::move(dms));
            }
            if (d.contains("guilds") && d["guilds"].is_array()) {
                std::vector<Guild> guilds;
                if (api::parseGuildsJson(d["guilds"].dump(), guilds))
                    m_guilds = std::make_shared<const std::vector<Guild>>(std::move(guilds));
            }
            if (d.contains("presences") && d["presences"].is_array()) {
                for (const auto& p : d["presences"]) {
                    if (p.is_object() && p.contains("user") && p["user"].is_object() && p.contains("status")) {
                        const auto& uObj = p["user"];
                        Snowflake uId = (uObj.contains("id") && uObj["id"].is_string())
                            ? stringToSnowflake(uObj["id"].get<std::string>()) : 0;
                        std::string st = (p.contains("status") && p["status"].is_string())
                            ? p["status"].get<std::string>() : "";
                        if (uId != 0 && !st.empty()) m_presences[uId] = st;
                    }
                }
            }
            if (d.contains("read_state")) {
                m_readStates.clear();
                api::parseReadStatesJson(d["read_state"].dump(), m_readStates);
            }
            if (d.contains("user_guild_settings")) {
                // Muted servers and channels show no unread mark (as on the phone).
                const auto& ugs = d["user_guild_settings"];
                const nlohmann::json* entries = ugs.is_array() ? &ugs
                    : (ugs.is_object() && ugs.contains("entries") && ugs["entries"].is_array()) ? &ugs["entries"] : nullptr;
                m_mutedGuilds.clear();
                m_mutedChannels.clear();
                if (entries) {
                    for (const auto& s : *entries) {
                        if (!s.is_object()) continue;
                        const auto gid = s.find("guild_id");
                        const auto muted = s.find("muted");
                        if (gid != s.end() && gid->is_string() && muted != s.end() && muted->is_boolean() &&
                            muted->get<bool>())
                            m_mutedGuilds.push_back(stringToSnowflake(gid->get<std::string>()));
                        const auto ov = s.find("channel_overrides");
                        if (ov == s.end() || !ov->is_array()) continue;
                        for (const auto& o : *ov) {
                            if (!o.is_object()) continue;
                            const auto cid = o.find("channel_id");
                            const auto cm = o.find("muted");
                            if (cid != o.end() && cid->is_string() && cm != o.end() && cm->is_boolean() &&
                                cm->get<bool>())
                                m_mutedChannels.push_back(stringToSnowflake(cid->get<std::string>()));
                        }
                    }
                }
            }
            if (d.contains("merged_presences") && d["merged_presences"].is_object()) {
                const auto& mp = d["merged_presences"];
                if (mp.contains("friends") && mp["friends"].is_array()) {
                    for (const auto& p : mp["friends"]) {
                        Snowflake uId = (p.is_object() && p.contains("user_id") && p["user_id"].is_string())
                            ? stringToSnowflake(p["user_id"].get<std::string>()) : 0;
                        std::string st = (p.is_object() && p.contains("status") && p["status"].is_string())
                            ? p["status"].get<std::string>() : "";
                        if (uId != 0 && !st.empty()) m_presences[uId] = st;
                    }
                }
            }
            applyReadStatesLocked();
            m_version++;
            DebugLog::log("[discord] Gateway READY: user=%s dms=%zu guilds=%zu presences=%zu read_states=%zu",
                          m_currentUser.username.c_str(), m_dms->size(), m_guilds->size(), m_presences.size(),
                          m_readStates.size());
        }
    } else if (ev.name == "PRESENCE_UPDATE") {
        const auto j = nlohmann::json::parse(ev.rawJson, nullptr, false);
        if (j.is_object() && j.contains("d") && j["d"].is_object()) {
            const auto& d = j["d"];
            if (d.contains("user") && d["user"].is_object() && d.contains("status")) {
                const auto& uObj = d["user"];
                Snowflake uId = (uObj.contains("id") && uObj["id"].is_string())
                    ? stringToSnowflake(uObj["id"].get<std::string>()) : 0;
                std::string st = (d.contains("status") && d["status"].is_string())
                    ? d["status"].get<std::string>() : "";
                if (uId != 0 && !st.empty()) {
                    std::lock_guard<std::mutex> lk(m_mu);
                    m_presences[uId] = st;
                    m_version++;
                }
            }
        }
    } else if (ev.name == "GUILD_CREATE") {
        const auto j = nlohmann::json::parse(ev.rawJson, nullptr, false);
        if (j.is_object() && j.contains("d") && j["d"].is_object()) {
            // Same null-safe parser as the REST and READY paths: Discord sends
            // explicit nulls (icon, description) that json::value() rejects.
            std::vector<Guild> parsed;
            api::parseGuildsJson("[" + j["d"].dump() + "]", parsed);
            if (!parsed.empty()) {
                Guild g = std::move(parsed.front());
                std::lock_guard<std::mutex> lk(m_mu);
                auto next = std::make_shared<std::vector<Guild>>(*m_guilds);
                bool found = false;
                for (auto& existing : *next) {
                    if (existing.id == g.id) {
                        existing = std::move(g);
                        found = true;
                        break;
                    }
                }
                if (!found) next->push_back(std::move(g));
                m_guilds = std::move(next);
                applyReadStatesLocked();
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
                const Snowflake guildId = j["d"].contains("guild_id") && j["d"]["guild_id"].is_string()
                    ? stringToSnowflake(j["d"]["guild_id"].get<std::string>()) : 0;
                {
                    std::lock_guard<std::mutex> lk(m_mu);
                    const bool fromOther = (m.author.id != 0 && m.author.id != m_currentUser.id);
                    const bool mentionsMe = m.mentionEveryone ||
                        std::find(m.mentionIds.begin(), m.mentionIds.end(), m_currentUser.id) != m.mentionIds.end();
                    const Snowflake msgId = m.id;
                    const std::string author = m.author.displayName(), text = m.content;
                    const std::string avatar = m.author.avatarUrl(64);
                    // Only a chat that was opened keeps its messages here; the
                    // others are fetched when opened.
                    if (m_messages.count(chanId))
                        addMessageLocked(std::move(m));

                    // Unread markers: the newest message of the channel moves,
                    // a message of mine counts as read.
                    auto& rs = m_readStates[chanId];
                    if (!fromOther) {
                        rs.lastReadId = std::max(rs.lastReadId, msgId);
                        rs.mentions = 0;
                    } else if (guildId == 0 || mentionsMe) {
                        ++rs.mentions; // every DM counts, as on Discord
                    }
                    bool knownDm = false;
                    {
                        auto next = std::make_shared<std::vector<Channel>>(*m_dms);
                        for (auto& dm : *next)
                            if (dm.id == chanId) { dm.lastMessageId = std::max(dm.lastMessageId, msgId); knownDm = true; }
                        m_dms = std::move(next);
                    }
                    if (guildId != 0) {
                        auto next = std::make_shared<std::vector<Guild>>(*m_guilds);
                        for (auto& g : *next)
                            if (g.id == guildId)
                                for (auto& c : g.channels)
                                    if (c.id == chanId) c.lastMessageId = std::max(c.lastMessageId, msgId);
                        m_guilds = std::move(next);
                    }
                    applyReadStatesLocked();

                    if (fromOther && (guildId == 0 || mentionsMe)) {
                        m_pendingAlert.channelId = chanId;
                        m_pendingAlert.guildId = guildId;
                        m_pendingAlert.title = author;
                        m_pendingAlert.text = text;
                        m_pendingAlert.avatarUrl = avatar;
                        m_hasAlert = true;
                    }
                    (void)knownDm;
                    m_version++;
                }
            }
        }
    }
}

void DiscordService::addMessageLocked(Message m) {
    auto& list = m_messages[m.channelId];
    for (auto& existing : list) {
        if (existing.id == m.id) {
            existing = std::move(m); // same message from the other path
            return;
        }
    }
    // Keep chronological order (a send reply can arrive after the echo of a
    // later message).
    auto pos = std::upper_bound(list.begin(), list.end(), m.id,
                                [](Snowflake id, const Message& x) { return id < x.id; });
    list.insert(pos, std::move(m));
    if (list.size() > 100)
        list.erase(list.begin());
}

void DiscordService::applyReadStatesLocked() {
    auto muted = [](const std::vector<Snowflake>& v, Snowflake id) {
        return std::find(v.begin(), v.end(), id) != v.end();
    };
    auto dms = std::make_shared<std::vector<Channel>>(*m_dms);
    for (auto& dm : *dms) {
        const auto it = m_readStates.find(dm.id);
        // No marker: Discord never saw it opened; treat old DMs as read.
        const bool unread = it != m_readStates.end() && dm.lastMessageId > it->second.lastReadId;
        dm.mentions = it != m_readStates.end() ? it->second.mentions : 0;
        dm.muted = muted(m_mutedChannels, dm.id);
        dm.unreadCount = unread && !dm.muted ? 1 : 0;
        if (dm.muted) dm.mentions = 0;
    }
    // Newest conversation first, like the phone.
    std::stable_sort(dms->begin(), dms->end(),
                     [](const Channel& a, const Channel& b) { return a.lastMessageId > b.lastMessageId; });
    m_dms = std::move(dms);

    auto guilds = std::make_shared<std::vector<Guild>>(*m_guilds);
    for (auto& g : *guilds) {
        g.muted = muted(m_mutedGuilds, g.id);
        g.unreadCount = 0;
        g.mentions = 0;
        for (auto& c : g.channels) {
            if (c.type != ChannelType::GuildText && c.type != ChannelType::GuildAnnouncement) continue;
            const auto it = m_readStates.find(c.id);
            c.muted = muted(m_mutedChannels, c.id) ||
                      (c.parentId != 0 && muted(m_mutedChannels, c.parentId));
            c.mentions = it != m_readStates.end() ? it->second.mentions : 0;
            const bool unread = it != m_readStates.end() && c.lastMessageId > it->second.lastReadId;
            c.unreadCount = unread && !c.muted ? 1 : 0;
            if (!g.muted && c.unreadCount > 0) ++g.unreadCount;
            g.mentions += c.mentions; // mentions show even in a muted server
        }
    }
    m_guilds = std::move(guilds);
}

void DiscordService::markRead(Snowflake channelId) {
    std::lock_guard<std::mutex> lk(m_mu);
    Snowflake last = 0;
    for (const auto& dm : *m_dms)
        if (dm.id == channelId) last = dm.lastMessageId;
    for (const auto& g : *m_guilds)
        for (const auto& c : g.channels)
            if (c.id == channelId) last = c.lastMessageId;
    const auto it = m_messages.find(channelId);
    if (it != m_messages.end() && !it->second.empty())
        last = std::max(last, it->second.back().id);
    auto& rs = m_readStates[channelId];
    if (rs.lastReadId >= last && rs.mentions == 0)
        return; // nothing changed: no version bump (the panel calls this often)
    rs.lastReadId = std::max(rs.lastReadId, last);
    rs.mentions = 0;
    applyReadStatesLocked();
    m_version++;
}

std::string DiscordService::presenceStatus(Snowflake userId) const {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_presences.find(userId);
    return (it != m_presences.end() && !it->second.empty()) ? it->second : "offline";
}

void DiscordService::startQrLogin(home::HomeCore& core) {
    m_state = State::QrLogin;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_error.clear();
    }
    m_remoteAuth.start(core.net());
}

void DiscordService::cancelQrLogin() {
    m_remoteAuth.stop();
    m_state = m_accounts.hasAccount() ? State::Ready : State::SignedOut;
    m_version++;
}

void DiscordService::signInWithToken(home::HomeCore& core, const std::string& token) {
    // A sign-in starts a new session: whatever the previous one still had in
    // flight is ignored from here on.
    endSession();
    m_state = State::Connecting;
    m_version++;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_error.clear();
    }

    if (!core.threadPool().acceptingTasks()) {
        m_state = State::Failed;
        std::lock_guard<std::mutex> lk(m_mu);
        m_error = "Background tasks unavailable";
        return;
    }

    const std::uint64_t session = m_session.load();
    // The worker only validates; update() saves and connects on the main
    // thread, and only if the player did not sign out or switch meanwhile.
    core.threadPool().submit([this, &core, token, session]() {
        User u;
        std::string err;
        const bool ok = api::fetchMe(core.net(), token, u, err);
        if (!ok)
            DebugLog::log("[discord] token validation failed: %s", err.c_str());
        std::lock_guard<std::mutex> lk(m_mu);
        if (!current(session))
            return;
        m_pendingSignIn.session = session;
        m_pendingSignIn.done = true;
        m_pendingSignIn.ok = ok;
        if (ok) {
            Account acc;
            acc.id = snowflakeToString(u.id);
            acc.username = u.username;
            acc.globalName = u.globalName;
            acc.avatar = u.avatar;
            acc.token = token;
            m_pendingSignIn.account = std::move(acc);
        }
    });
}

void DiscordService::switchAccount(home::HomeCore& core, const std::string& id) {
    m_accounts.setCurrentId(id);
    m_accounts.save();
    endSession();
    m_version++;
    connectAccount(core, m_accounts.current());
}

void DiscordService::signOut() {
    endSession();
    const std::string cur = m_accounts.currentId();
    if (!cur.empty()) {
        m_accounts.remove(cur);
        m_accounts.save();
    }
    m_state = State::SignedOut;
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

    const std::uint64_t session = m_session.load();
    core.threadPool().submit([this, &core, token, channelId, text, session]() {
        Message outMsg;
        std::string err;
        if (api::sendMessage(core.net(), token, channelId, text, outMsg, err)) {
            std::lock_guard<std::mutex> lk(m_mu);
            if (!current(session)) return;
            auto& rs = m_readStates[channelId];
            rs.lastReadId = std::max(rs.lastReadId, outMsg.id);
            addMessageLocked(std::move(outMsg));
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

    const std::uint64_t session = m_session.load();
    core.threadPool().submit([this, &core, token, guildId, session]() {
        std::vector<Channel> channels;
        std::string err;
        if (api::fetchGuildChannels(core.net(), token, guildId, channels, err)) {
            const std::size_t n = channels.size();
            std::lock_guard<std::mutex> lk(m_mu);
            if (!current(session)) return;
            auto next = std::make_shared<std::vector<Guild>>(*m_guilds);
            for (auto& g : *next) {
                if (g.id == guildId) {
                    g.channels = std::move(channels);
                    break;
                }
            }
            m_guilds = std::move(next);
            applyReadStatesLocked();
            m_version++;
            DebugLog::log("[discord] fetched %zu channels for guild %llu",
                          n, (unsigned long long)guildId);
        } else {
            DebugLog::log("[discord] fetchGuildChannels error: %s", err.c_str());
        }
    });
}

void DiscordService::fetchMessagesAsync(home::HomeCore& core, Snowflake channelId) {
    const std::string token = m_accounts.current().token;
    if (token.empty() || channelId == 0) return;
    if (!core.threadPool().acceptingTasks()) return;

    const std::uint64_t session = m_session.load();
    core.threadPool().submit([this, &core, token, channelId, session]() {
        std::vector<Message> msgs;
        std::string err;
        if (api::fetchMessages(core.net(), token, channelId, msgs)) {
            const std::size_t n = msgs.size();
            std::lock_guard<std::mutex> lk(m_mu);
            if (!current(session)) return;
            // Messages that arrived on the gateway while the request ran are
            // merged by id instead of being dropped or doubled.
            auto live = std::move(m_messages[channelId]);
            m_messages[channelId] = std::move(msgs);
            for (auto& m : live) addMessageLocked(std::move(m));
            m_version++;
            DebugLog::log("[discord] fetched %zu messages for channel %llu",
                          n, (unsigned long long)channelId);
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
    m_resumeAfterSleep = false;
    m_asleep = false;
    m_gateway.stop();
    m_remoteAuth.stop();
}

void DiscordService::shutdown() {
    m_resumeAfterSleep = false;
    m_asleep = false;
    m_gateway.stop();
    m_remoteAuth.stop();
}

} // namespace discord
