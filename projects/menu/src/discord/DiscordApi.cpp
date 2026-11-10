#include "discord/DiscordApi.hpp"

#include "core/DebugLog.hpp"
#include "net/NetService.hpp"

#include <nlohmann/json.hpp>

namespace discord {
namespace api {

namespace {

std::vector<std::string> authHeaders(const std::string& token) {
    std::vector<std::string> h;
    if (!token.empty()) {
        h.push_back("Authorization: " + token);
    }
    h.push_back("Content-Type: application/json");
    h.push_back("User-Agent: Mozilla/5.0 (Nintendo Switch; Share/1.0.0) OmniLaunch/1.2");
    return h;
}

std::string formatError(const net::Response& res) {
    if (!res.error.empty()) return res.error;
    if (!res.body.empty()) {
        const auto j = nlohmann::json::parse(res.body, nullptr, false);
        if (!j.is_discarded() && j.is_object() && j.contains("message") && j["message"].is_string()) {
            return j["message"].get<std::string>();
        }
    }
    return "HTTP " + std::to_string(res.status);
}

// Discord sends explicit nulls for absent optional fields (topic, parent_id,
// last_message_id, icon, avatar, global_name, ...). nlohmann's value() only
// falls back for a missing key and throws type_error 302 for a null or
// mistyped one, so every field read goes through these instead.
std::string str(const nlohmann::json& o, const char* key, const char* def = "") {
    const auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string(def);
}

template <typename T>
T num(const nlohmann::json& o, const char* key, T def) {
    const auto it = o.find(key);
    return (it != o.end() && it->is_number()) ? it->get<T>() : def;
}

bool flag(const nlohmann::json& o, const char* key, bool def) {
    const auto it = o.find(key);
    return (it != o.end() && it->is_boolean()) ? it->get<bool>() : def;
}

Snowflake snowflake(const nlohmann::json& o, const char* key) {
    const auto it = o.find(key);
    if (it == o.end()) return 0;
    if (it->is_string()) return stringToSnowflake(it->get<std::string>());
    if (it->is_number_unsigned()) return it->get<Snowflake>();
    return 0;
}

} // namespace

bool parseUserJson(const std::string& json, User& outUser) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    outUser.id = snowflake(j, "id");
    outUser.username = str(j, "username");
    outUser.globalName = str(j, "global_name");
    outUser.discriminator = str(j, "discriminator", "0");
    outUser.avatar = str(j, "avatar");
    outUser.bot = flag(j, "bot", false);
    return outUser.id != 0;
}

bool parseChannelsJson(const std::string& json, std::vector<Channel>& outChannels) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return false;
    outChannels.clear();
    for (const auto& item : j) {
        if (!item.is_object()) continue;
        Channel c;
        c.id = snowflake(item, "id");
        c.type = static_cast<ChannelType>(num<int>(item, "type", 0));
        c.guildId = snowflake(item, "guild_id");
        c.name = str(item, "name");
        c.topic = str(item, "topic");
        c.parentId = snowflake(item, "parent_id");
        c.position = num<int>(item, "position", 0);
        c.lastMessageId = snowflake(item, "last_message_id");
        if (item.contains("recipients") && item["recipients"].is_array()) {
            for (const auto& r : item["recipients"]) {
                if (r.is_object()) {
                    c.recipientIds.push_back(snowflake(r, "id"));
                    if (c.name.empty()) {
                        std::string gName;
                        if (r.contains("global_name") && r["global_name"].is_string()) {
                            gName = r["global_name"].get<std::string>();
                        }
                        std::string uName;
                        if (r.contains("username") && r["username"].is_string()) {
                            uName = r["username"].get<std::string>();
                        }
                        c.name = !gName.empty() ? gName : uName;
                    }
                }
            }
        }
        if (c.id != 0) outChannels.push_back(std::move(c));
    }
    return true;
}

bool parseGuildsJson(const std::string& json, std::vector<Guild>& outGuilds) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return false;
    outGuilds.clear();
    for (const auto& item : j) {
        if (!item.is_object()) continue;
        Guild g;
        g.id = snowflake(item, "id");
        g.name = str(item, "name");
        g.icon = str(item, "icon");
        g.description = str(item, "description");
        if (item.contains("channels") && item["channels"].is_array()) {
            parseChannelsJson(item["channels"].dump(), g.channels);
        }
        if (g.id != 0) outGuilds.push_back(std::move(g));
    }
    return true;
}

bool parseMessagesJson(const std::string& json, std::vector<Message>& outMessages) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_array()) return false;
    outMessages.clear();
    for (const auto& item : j) {
        if (!item.is_object()) continue;
        Message m;
        m.id = snowflake(item, "id");
        m.channelId = snowflake(item, "channel_id");
        m.content = str(item, "content");
        m.timestamp = str(item, "timestamp");
        if (item.contains("author") && item["author"].is_object()) {
            parseUserJson(item["author"].dump(), m.author);
        }
        if (item.contains("attachments") && item["attachments"].is_array()) {
            for (const auto& a : item["attachments"]) {
                if (a.is_object() && a.contains("url")) {
                    m.attachmentUrls.push_back(str(a, "url"));
                }
            }
        }
        if (m.id != 0) outMessages.push_back(std::move(m));
    }
    return true;
}

bool fetchMe(net::NetService& net, const std::string& token, User& outUser, std::string& outError) {
    net::Request req;
    req.url = std::string(kBaseUrl) + "/users/@me";
    req.headers = authHeaders(token);
    req.timeoutMs = 12000;
    const net::Response res = net.request(req);
    if (!res.ok()) {
        outError = formatError(res);
        return false;
    }
    if (!parseUserJson(res.body, outUser)) {
        outError = "Failed to parse user profile";
        return false;
    }
    return true;
}

bool fetchDms(net::NetService& net, const std::string& token, std::vector<Channel>& outChannels, std::string& outError) {
    net::Request req;
    req.url = std::string(kBaseUrl) + "/users/@me/channels";
    req.headers = authHeaders(token);
    req.timeoutMs = 15000;
    const net::Response res = net.request(req);
    if (!res.ok()) {
        outError = formatError(res);
        return false;
    }
    if (!parseChannelsJson(res.body, outChannels)) {
        outError = "Failed to parse channels";
        return false;
    }
    return true;
}

bool fetchGuilds(net::NetService& net, const std::string& token, std::vector<Guild>& outGuilds, std::string& outError) {
    net::Request req;
    req.url = std::string(kBaseUrl) + "/users/@me/guilds";
    req.headers = authHeaders(token);
    req.timeoutMs = 15000;
    const net::Response res = net.request(req);
    if (!res.ok()) {
        outError = formatError(res);
        return false;
    }
    if (!parseGuildsJson(res.body, outGuilds)) {
        outError = "Failed to parse guilds";
        return false;
    }
    return true;
}

bool fetchGuildChannels(net::NetService& net, const std::string& token, Snowflake guildId,
                        std::vector<Channel>& outChannels, std::string& outError) {
    net::Request req;
    req.url = std::string(kBaseUrl) + "/guilds/" + snowflakeToString(guildId) + "/channels";
    req.headers = authHeaders(token);
    req.timeoutMs = 15000;
    const net::Response res = net.request(req);
    if (!res.ok()) {
        outError = formatError(res);
        return false;
    }
    if (!parseChannelsJson(res.body, outChannels)) {
        outError = "Failed to parse guild channels";
        return false;
    }
    return true;
}

bool fetchMessages(net::NetService& net, const std::string& token, Snowflake channelId,
                   std::vector<Message>& outMessages, Snowflake before, int limit,
                   std::string& outError) {
    net::Request req;
    req.url = std::string(kBaseUrl) + "/channels/" + snowflakeToString(channelId) +
              "/messages?limit=" + std::to_string(limit);
    if (before != 0) {
        req.url += "&before=" + snowflakeToString(before);
    }
    req.headers = authHeaders(token);
    req.timeoutMs = 15000;
    const net::Response res = net.request(req);
    if (!res.ok()) {
        outError = formatError(res);
        return false;
    }
    if (!parseMessagesJson(res.body, outMessages)) {
        outError = "Failed to parse messages";
        return false;
    }
    return true;
}

bool sendMessage(net::NetService& net, const std::string& token, Snowflake channelId,
                 const std::string& content, Message& outMsg, std::string& outError) {
    net::Request req;
    req.method = "POST";
    req.url = std::string(kBaseUrl) + "/channels/" + snowflakeToString(channelId) + "/messages";
    req.headers = authHeaders(token);
    nlohmann::json body;
    body["content"] = content;
    req.body = body.dump();
    req.timeoutMs = 15000;
    const net::Response res = net.request(req);
    if (!res.ok()) {
        outError = formatError(res);
        return false;
    }
    std::vector<Message> tmp;
    const std::string wrapped = "[" + res.body + "]";
    if (parseMessagesJson(wrapped, tmp) && !tmp.empty()) {
        outMsg = std::move(tmp.front());
        return true;
    }
    outError = "Failed to parse sent message response";
    return false;
}

bool exchangeRemoteAuthTicket(net::NetService& net, const std::string& ticket,
                             std::string& outEncryptedToken, std::string& outError) {
    net::Request req;
    req.method = "POST";
    req.url = std::string(kBaseUrl) + "/users/@me/remote-auth/login";
    req.headers = authHeaders("");
    nlohmann::json body;
    body["ticket"] = ticket;
    req.body = body.dump();
    req.timeoutMs = 15000;
    const net::Response res = net.request(req);
    if (!res.ok()) {
        outError = formatError(res);
        return false;
    }
    const auto j = nlohmann::json::parse(res.body, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("encrypted_token") ||
        !j["encrypted_token"].is_string()) {
        outError = "Invalid ticket exchange response";
        return false;
    }
    outEncryptedToken = j["encrypted_token"].get<std::string>();
    return !outEncryptedToken.empty();
}

} // namespace api
} // namespace discord
