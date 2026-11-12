#pragma once
// Discord data types (HomeNX H1, C-NET-2, C-UI-12).
// New OmniLaunch implementation written from scratch; upstream/homenx @ 0448cd4
// (projects/menu/src/discord/types.h) referenced for protocol behavior only.

#include <cstdint>
#include <string>
#include <vector>

namespace discord {

using Snowflake = std::uint64_t;

inline std::string snowflakeToString(Snowflake id) {
    return std::to_string(id);
}

inline Snowflake stringToSnowflake(const std::string& str) {
    if (str.empty()) return 0;
    char* end = nullptr;
    return std::strtoull(str.c_str(), &end, 10);
}

struct User {
    Snowflake id = 0;
    std::string username;
    std::string globalName;
    std::string discriminator = "0";
    std::string avatar;
    bool bot = false;

    std::string displayName() const {
        return !globalName.empty() ? globalName : username;
    }

    std::string avatarUrl(int size = 128) const {
        if (!avatar.empty()) {
            return "https://cdn.discordapp.com/avatars/" + snowflakeToString(id) + "/" + avatar +
                   ".png?size=" + std::to_string(size);
        }
        int index = 0;
        if (discriminator != "0" && !discriminator.empty()) {
            index = std::atoi(discriminator.c_str()) % 5;
        } else {
            index = static_cast<int>((id >> 22) % 6);
        }
        return "https://cdn.discordapp.com/embed/avatars/" + std::to_string(index) + ".png";
    }
};

struct Account {
    std::string id;
    std::string username;
    std::string globalName;
    std::string avatar;
    std::string token;

    std::string name() const {
        return !globalName.empty() ? globalName : username;
    }

    std::string avatarUrl(int size = 128) const {
        if (!avatar.empty() && !id.empty()) {
            return "https://cdn.discordapp.com/avatars/" + id + "/" + avatar +
                   ".png?size=" + std::to_string(size);
        }
        return "https://cdn.discordapp.com/embed/avatars/0.png";
    }
};

enum class ChannelType : int {
    GuildText = 0,
    DM = 1,
    GuildVoice = 2,
    GroupDM = 3,
    GuildCategory = 4,
    GuildAnnouncement = 5,
};

struct Channel {
    Snowflake id = 0;
    ChannelType type = ChannelType::GuildText;
    Snowflake guildId = 0;
    std::string name;
    std::string topic;
    Snowflake parentId = 0; // category id
    int position = 0;
    Snowflake lastMessageId = 0;
    std::vector<Snowflake> recipientIds;
    std::string avatarUrl;
    std::string status = "offline";
    int unreadCount = 0;
    int mentions = 0;
};

struct Guild {
    Snowflake id = 0;
    std::string name;
    std::string icon;
    std::string description;
    std::vector<Channel> channels;
    int unreadCount = 0;
    int mentions = 0;

    std::string iconUrl(int size = 128) const {
        if (!icon.empty()) {
            return "https://cdn.discordapp.com/icons/" + snowflakeToString(id) + "/" + icon +
                   ".png?size=" + std::to_string(size);
        }
        return {};
    }

    std::string initials() const {
        std::string res;
        bool word = false;
        for (char c : name) {
            if (c == ' ' || c == '-' || c == '_') {
                word = false;
            } else if (!word) {
                res += c;
                word = true;
                if (res.size() >= 3) break;
            }
        }
        return res.empty() && !name.empty() ? name.substr(0, 1) : res;
    }
};

struct MessageImage {
    std::string url;
    int width = 0;
    int height = 0;
};

struct MessageEmbed {
    std::string title;
    std::string description;
    std::string footer;
    std::uint32_t color = 0;
};

struct Message {
    Snowflake id = 0;
    Snowflake channelId = 0;
    User author;
    std::string content;
    std::string timestamp;
    std::vector<std::string> attachmentUrls;
    std::vector<MessageImage> images;
    std::vector<MessageEmbed> embeds;
    bool mentionsMe = false;
    bool mine = false;
};

struct Alert {
    Snowflake channelId = 0;
    Snowflake guildId = 0;
    std::string title;
    std::string text;
    std::string avatarUrl;
};

} // namespace discord
