#pragma once

#include "discord/DiscordTypes.hpp"
#include <algorithm>
#include <cctype>

namespace discord {

// Media requests are unauthenticated and confined to Discord's image hosts.
inline bool isDiscordImageUrl(const std::string& url) {
    for (const char* origin : {"https://cdn.discordapp.com/", "https://media.discordapp.net/",
                               "https://images-ext-1.discordapp.net/", "https://images-ext-2.discordapp.net/"}) {
        if (url.rfind(origin, 0) == 0) return true;
    }
    return false;
}

inline std::vector<Channel> orderedTextChannels(const std::vector<Channel>& channels) {
    std::vector<Channel> sorted = channels, out;
    std::stable_sort(sorted.begin(), sorted.end(), [](const Channel& a, const Channel& b) {
        return a.position != b.position ? a.position < b.position : a.id < b.id;
    });
    auto text = [](const Channel& c) {
        return c.type == ChannelType::GuildText || c.type == ChannelType::GuildAnnouncement;
    };
    // Uncategorised channels come first; category positions and child positions
    // are separate Discord orderings, not one global numeric sequence.
    for (const auto& c : sorted) {
        const bool parentExists = std::any_of(sorted.begin(), sorted.end(), [&](const Channel& p) {
            return p.type == ChannelType::GuildCategory && p.id == c.parentId;
        });
        if (text(c) && !parentExists) out.push_back(c);
    }
    for (const auto& category : sorted) {
        if (category.type != ChannelType::GuildCategory) continue;
        const bool hasText = std::any_of(sorted.begin(), sorted.end(), [&](const Channel& c) {
            return text(c) && c.parentId == category.id;
        });
        if (!hasText) continue;
        out.push_back(category);
        for (const auto& c : sorted)
            if (text(c) && c.parentId == category.id) out.push_back(c);
    }
    return out;
}

// Readable plain-text rendering for the common Discord markdown forms. Links
// remain text only; neither display nor selection opens arbitrary URLs.
inline std::string messageText(std::string text) {
    for (std::size_t start = 0; (start = text.find('[', start)) != std::string::npos;) {
        const auto mid = text.find("](", start + 1);
        if (mid == std::string::npos) break;
        const auto end = text.find(')', mid + 2);
        if (end == std::string::npos) break;
        const auto label = text.substr(start + 1, mid - start - 1);
        const auto url = text.substr(mid + 2, end - mid - 2);
        if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) { start = end + 1; continue; }
        text.replace(start, end - start + 1, label);
        start += label.size();
    }
    for (const char* mark : {"**", "__", "```", "`"}) {
        const std::string token = mark;
        for (auto pos = text.find(token); pos != std::string::npos; pos = text.find(token))
            text.erase(pos, token.size());
    }
    return text;
}

} // namespace discord
