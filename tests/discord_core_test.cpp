#include "discord/DiscordAccounts.hpp"
#include "discord/DiscordApi.hpp"
#include "discord/DiscordTypes.hpp"
#include "net/NetService.hpp"

#include <cassert>
#include <iostream>

namespace net {
Response NetService::request(const Request&) { return {}; }
}

using namespace discord;

int main() {
    // 1. Snowflake conversions
    const Snowflake id = 123456789012345678ULL;
    assert(snowflakeToString(id) == "123456789012345678");
    assert(stringToSnowflake("123456789012345678") == id);
    assert(stringToSnowflake("") == 0);

    // 2. User & Avatar URLs
    User u;
    u.id = 12345;
    u.username = "tester";
    u.discriminator = "1234";
    assert(u.displayName() == "tester");
    assert(u.avatarUrl().find("embed/avatars/") != std::string::npos);
    u.globalName = "Cool Tester";
    assert(u.displayName() == "Cool Tester");
    u.avatar = "abcdef123456";
    assert(u.avatarUrl().find("avatars/12345/abcdef123456.png") != std::string::npos);

    // 3. Guild Initials
    Guild g;
    g.name = "Nintendo Switch Homebrew";
    assert(g.initials() == "NSH");
    g.name = "SingleWord";
    assert(g.initials() == "S");

    // 4. Accounts serialization & round trip
    std::vector<Account> accs = {
        {"1001", "alice", "Alice W", "avatar1", "token123"},
        {"1002", "bob", "Bob B", "", "token456"}
    };
    std::string dumped = dumpAccountsJson(accs, "1002");
    std::vector<Account> parsedAccs;
    std::string currentId;
    assert(parseAccountsJson(dumped, parsedAccs, currentId));
    assert(parsedAccs.size() == 2);
    assert(currentId == "1002");
    assert(parsedAccs[0].username == "alice" && parsedAccs[0].token == "token123");
    assert(parsedAccs[1].username == "bob" && parsedAccs[1].token == "token456");

    // Legacy switchcord format (flat array)
    const std::string legacyJson = R"([
        {"id": "2001", "username": "legacy_user", "global_name": "Legacy", "token": "leg_token"}
    ])";
    parsedAccs.clear();
    currentId.clear();
    assert(parseAccountsJson(legacyJson, parsedAccs, currentId));
    assert(parsedAccs.size() == 1);
    assert(parsedAccs[0].id == "2001" && parsedAccs[0].globalName == "Legacy");
    assert(currentId == "2001");

    // 5. REST API JSON Parsers
    // User
    const std::string userJson = R"({
        "id": "1122334455",
        "username": "discord_fan",
        "global_name": "Fan",
        "avatar": "hash99"
    })";
    User parsedUser;
    assert(api::parseUserJson(userJson, parsedUser));
    assert(parsedUser.id == 1122334455ULL);
    assert(parsedUser.username == "discord_fan");
    assert(parsedUser.globalName == "Fan");

    // Channels / DMs
    const std::string channelsJson = R"([
        {
            "id": "99887766",
            "type": 1,
            "recipients": [
                {"id": "112233", "username": "friend1", "global_name": "Best Friend"}
            ]
        }
    ])";
    std::vector<Channel> channels;
    assert(api::parseChannelsJson(channelsJson, channels));
    assert(channels.size() == 1);
    assert(channels[0].id == 99887766ULL);
    assert(channels[0].name == "Best Friend");
    assert(channels[0].type == ChannelType::DM);

    // Guilds with channels
    const std::string guildsJson = R"([
        {
            "id": "55667788",
            "name": "OmniServer",
            "icon": "icon_hash",
            "channels": [
                {"id": "111222", "name": "general", "type": 0}
            ]
        }
    ])";
    std::vector<Guild> guilds;
    assert(api::parseGuildsJson(guildsJson, guilds));
    assert(guilds.size() == 1);
    assert(guilds[0].id == 55667788ULL);
    assert(guilds[0].name == "OmniServer");
    assert(guilds[0].channels.size() == 1);
    assert(guilds[0].channels[0].name == "general");

    // Messages
    const std::string messagesJson = R"([
        {
            "id": "77889900",
            "channel_id": "99887766",
            "content": "Hello from Switch!",
            "timestamp": "2026-11-10T12:00:00Z",
            "author": {
                "id": "112233",
                "username": "friend1"
            }
        }
    ])";
    std::vector<Message> msgs;
    assert(api::parseMessagesJson(messagesJson, msgs));
    assert(msgs.size() == 1);
    assert(msgs[0].id == 77889900ULL);
    assert(msgs[0].content == "Hello from Switch!");
    assert(msgs[0].author.username == "friend1");

    std::cout << "discord_core_test: all passed" << std::endl;
    return 0;
}
