#pragma once
// Discord v10 REST API client (HomeNX H1, C-NET-2).
// Built on net::NetService. Blocking calls designed for worker threads.
// New OmniLaunch implementation written from scratch.

#include "discord/DiscordTypes.hpp"

#include <string>
#include <vector>

namespace net { class NetService; }

namespace discord {
namespace api {

constexpr const char* kBaseUrl = "https://discord.com/api/v10";

/// Validate a token and fetch account details (GET /users/@me).
bool fetchMe(net::NetService& net, const std::string& token, User& outUser, std::string& outError);

/// Fetch current user's DM and group DM channels (GET /users/@me/channels).
bool fetchDms(net::NetService& net, const std::string& token, std::vector<Channel>& outChannels, std::string& outError);

/// Fetch current user's guilds/servers (GET /users/@me/guilds).
bool fetchGuilds(net::NetService& net, const std::string& token, std::vector<Guild>& outGuilds, std::string& outError);

/// Fetch recent messages from a channel (GET /channels/{id}/messages).
bool fetchMessages(net::NetService& net, const std::string& token, Snowflake channelId,
                   std::vector<Message>& outMessages, Snowflake before = 0, int limit = 30,
                   std::string& outError = *(new std::string()));

/// Send a message to a channel (POST /channels/{id}/messages).
bool sendMessage(net::NetService& net, const std::string& token, Snowflake channelId,
                 const std::string& content, Message& outMsg, std::string& outError);

/// Exchange a remote auth ticket for an encrypted token (POST /users/@me/remote-auth/login).
bool exchangeRemoteAuthTicket(net::NetService& net, const std::string& ticket,
                             std::string& outEncryptedToken, std::string& outError);

// ── Pure JSON parser helpers for host unit testing ──────────────────────────
bool parseUserJson(const std::string& json, User& outUser);
bool parseChannelsJson(const std::string& json, std::vector<Channel>& outChannels);
bool parseGuildsJson(const std::string& json, std::vector<Guild>& outGuilds);
bool parseMessagesJson(const std::string& json, std::vector<Message>& outMessages);

} // namespace api
} // namespace discord
