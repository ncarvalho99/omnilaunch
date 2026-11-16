#include "discord/DiscordApi.hpp"
#include "discord/DiscordPresentation.hpp"

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
        // A CAPTCHA cannot be solved here; the token sign-in still works
        // (HomeNX cc4be05).
        if (!j.is_discarded() && j.is_object() && j.contains("captcha_key"))
            return "Discord is asking for a CAPTCHA, which HOME can't solve. Use \"Sign in with User Token\" instead.";
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
        c.mentions = num<int>(item, "mention_count", 0);
        if (item.contains("recipients") && item["recipients"].is_array()) {
            for (const auto& r : item["recipients"]) {
                if (r.is_object()) {
                    Snowflake rId = snowflake(r, "id");
                    c.recipientIds.push_back(rId);
                    if (c.avatarUrl.empty()) {
                        std::string av = str(r, "avatar");
                        if (!av.empty()) {
                            c.avatarUrl = "https://cdn.discordapp.com/avatars/" + snowflakeToString(rId) + "/" + av + ".png?size=64";
                        } else {
                            c.avatarUrl = "https://cdn.discordapp.com/embed/avatars/" + std::to_string((rId >> 22) % 6) + ".png";
                        }
                    }
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
        g.mentions = num<int>(item, "mention_count", 0);
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
        m.type = num<int>(item, "type", 0);
        m.content = str(item, "content");
        m.timestamp = str(item, "timestamp");
        if (item.contains("call") && item["call"].is_object()) {
            // Voice call record: participants and the end time (null while it
            // is still ringing or running).
            const auto& call = item["call"];
            m.hasCall = true;
            m.callEndedTimestamp = str(call, "ended_timestamp");
            if (call.contains("participants") && call["participants"].is_array())
                for (const auto& p : call["participants"])
                    if (p.is_string()) m.callParticipants.push_back(stringToSnowflake(p.get<std::string>()));
        }
        m.mentionEveryone = flag(item, "mention_everyone", false);
        if (item.contains("mentions") && item["mentions"].is_array())
            for (const auto& u : item["mentions"])
                if (u.is_object()) m.mentionIds.push_back(snowflake(u, "id"));
        if (item.contains("sticker_items") && item["sticker_items"].is_array() && !item["sticker_items"].empty() &&
            item["sticker_items"][0].is_object())
            m.sticker = str(item["sticker_items"][0], "name");
        if (item.contains("author") && item["author"].is_object()) {
            parseUserJson(item["author"].dump(), m.author);
        }
        auto addImage = [&](const nlohmann::json& image) {
            if (!image.is_object() || m.images.size() >= 4) return;
            auto url = str(image, "proxy_url");
            if (!isDiscordImageUrl(url)) url = str(image, "url");
            if (!isDiscordImageUrl(url)) return;
            if (std::any_of(m.images.begin(), m.images.end(), [&](const MessageImage& i) { return i.url == url; })) return;
            m.images.push_back({url, std::clamp(num<int>(image, "width", 0), 0, 16384),
                                   std::clamp(num<int>(image, "height", 0), 0, 16384)});
        };
        if (item.contains("attachments") && item["attachments"].is_array()) {
            for (const auto& attachment : item["attachments"]) {
                if (!attachment.is_object()) continue;
                const auto url = str(attachment, "url");
                const auto mime = str(attachment, "content_type");
                const bool image = mime.rfind("image/", 0) == 0 ||
                    (mime.empty() && num<int>(attachment, "width", 0) > 0 && num<int>(attachment, "height", 0) > 0);
                if (image) {
                    addImage(attachment);
                } else if (m.attachmentUrls.size() < 10) {
                    // Files the chat cannot show: listed by name.
                    const auto name = str(attachment, "filename");
                    if (!name.empty()) m.attachmentUrls.push_back(name);
                    else if (!url.empty()) m.attachmentUrls.push_back(url.substr(url.find_last_of('/') + 1));
                }
            }
        }
        if (item.contains("embeds") && item["embeds"].is_array()) {
            for (const auto& embed : item["embeds"]) {
                if (!embed.is_object() || m.embeds.size() >= 4) continue;
                MessageEmbed e;
                e.title = str(embed, "title");
                e.description = str(embed, "description");
                e.color = num<std::uint32_t>(embed, "color", 0) & 0xffffffu;
                if (embed.contains("footer") && embed["footer"].is_object()) e.footer = str(embed["footer"], "text");
                if (embed.contains("provider") && embed["provider"].is_object()) e.provider = str(embed["provider"], "name");
                if (embed.contains("author") && embed["author"].is_object()) e.author = str(embed["author"], "name");
                if (!e.title.empty() || !e.description.empty() || !e.footer.empty() || !e.provider.empty() ||
                    !e.author.empty())
                    m.embeds.push_back(std::move(e));
                // Link previews: the large image, else the thumbnail (a video
                // embed carries its poster frame as the thumbnail).
                if (embed.contains("image") && embed["image"].is_object()) addImage(embed["image"]);
                else if (embed.contains("thumbnail") && embed["thumbnail"].is_object()) addImage(embed["thumbnail"]);
            }
        }
        if (m.id != 0) outMessages.push_back(std::move(m));
    }
    return true;
}

bool parseReadStatesJson(const std::string& json, std::map<Snowflake, ReadState>& out) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded()) return false;
    // Either a bare array or {"entries": [...], "partial": ..., "version": ...}.
    const nlohmann::json* entries = &j;
    if (j.is_object()) {
        const auto it = j.find("entries");
        if (it == j.end() || !it->is_array()) return false;
        entries = &*it;
    }
    if (!entries->is_array()) return false;
    for (const auto& e : *entries) {
        if (!e.is_object()) continue;
        // Type 0 (or absent) is a channel; other types track guild events etc.
        if (num<int>(e, "read_state_type", 0) != 0) continue;
        const Snowflake id = snowflake(e, "id");
        if (id == 0) continue;
        ReadState rs;
        rs.lastReadId = snowflake(e, "last_message_id");
        rs.mentions = std::max(0, num<int>(e, "mention_count", 0));
        out[id] = rs;
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
    // Discord returns messages newest-first; chronological order is needed for a top-to-bottom chat.
    std::reverse(outMessages.begin(), outMessages.end());
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
