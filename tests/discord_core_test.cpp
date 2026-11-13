#include <map>
#include "discord/DiscordAccounts.hpp"
#include "discord/DiscordApi.hpp"
#include "discord/DiscordPresentation.hpp"
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

    // 6. Explicit nulls as the gateway READY payload sends them must not throw
    // (json::value() raised type_error 302 and aborted the menu on hardware).
    const std::string nullChannelsJson = R"([
        {"id": "4001", "type": 0, "name": "general", "topic": null,
         "parent_id": null, "last_message_id": null, "position": 2},
        {"id": "4002", "type": 1, "name": null, "last_message_id": null,
         "recipients": [{"id": "5001", "username": "pal", "global_name": null, "avatar": null}]}
    ])";
    std::vector<Channel> nullChannels;
    assert(api::parseChannelsJson(nullChannelsJson, nullChannels));
    assert(nullChannels.size() == 2);
    assert(nullChannels[0].topic.empty() && nullChannels[0].parentId == 0);
    assert(nullChannels[0].position == 2);
    assert(nullChannels[1].name == "pal");

    const std::string nullGuildsJson = R"([
        {"id": "6001", "name": "G", "icon": null, "description": null,
         "channels": [{"id": "6002", "name": "c", "type": 0, "topic": null, "parent_id": null}]}
    ])";
    std::vector<Guild> nullGuilds;
    assert(api::parseGuildsJson(nullGuildsJson, nullGuilds));
    assert(nullGuilds.size() == 1 && nullGuilds[0].icon.empty());
    assert(nullGuilds[0].channels.size() == 1);

    User nullUser;
    assert(api::parseUserJson(R"({"id":"7001","username":"u","global_name":null,"avatar":null,"bot":null})", nullUser));
    assert(nullUser.globalName.empty() && nullUser.avatar.empty() && !nullUser.bot);

    std::vector<Message> nullMsgs;
    assert(api::parseMessagesJson(R"([{"id":"8001","channel_id":"4001","content":"hi","timestamp":null,
        "author":{"id":"5001","username":"pal","avatar":null},"attachments":[{"url":null}]}])", nullMsgs));
    assert(nullMsgs.size() == 1 && nullMsgs[0].author.username == "pal");

    // 7. Presentation utilities
    assert(isDiscordImageUrl("https://cdn.discordapp.com/attachments/1/2/pic.png"));
    assert(!isDiscordImageUrl("https://example.com/malicious.png"));
    assert(messageText("Read [rules](https://discord.com/rules) and **enjoy**") == "Read rules and enjoy");

    std::vector<Channel> rawChannels;
    rawChannels.push_back({10, ChannelType::GuildText, 1, "chat", "", 100, 2});
    rawChannels.push_back({100, ChannelType::GuildCategory, 1, "GENERAL", "", 0, 1});
    rawChannels.push_back({20, ChannelType::GuildText, 1, "announcements", "", 0, 0});
    const auto sortedChannels = orderedTextChannels(rawChannels);
    assert(sortedChannels.size() == 3);
    assert(sortedChannels[0].id == 20); // Uncategorised text first
    assert(sortedChannels[1].id == 100); // Category header
    assert(sortedChannels[2].id == 10);  // Text inside category

    std::vector<Account> nullAccs;
    std::string nullCur;
    assert(parseAccountsJson(R"({"current":null,"accounts":[{"id":"9001","token":"t","username":"n","avatar":null,"globalName":null}]})",
                             nullAccs, nullCur));
    assert(nullAccs.size() == 1 && nullCur == "9001"); // null current falls back to the first account
    // 8. Call records, link previews, files, mentions and read states.
    std::vector<Message> rich;
    assert(api::parseMessagesJson(R"([
        {"id":"9001","channel_id":"1","type":3,"content":"","timestamp":"2019-09-20T05:31:00+00:00",
         "author":{"id":"5001","username":"pal"},
         "call":{"participants":["5001"],"ended_timestamp":"2019-09-20T05:41:00+00:00"}},
        {"id":"9002","channel_id":"1","type":0,"content":"https://e.com/x","timestamp":null,
         "author":{"id":"5001","username":"pal"},"mention_everyone":false,
         "mentions":[{"id":"7001","username":"me"}],
         "embeds":[{"type":"link","provider":{"name":"E-RECOVERY"},"title":"HD em modo RAW",
                    "thumbnail":{"url":"https://example.com/t.png","proxy_url":"https://images-ext-1.discordapp.net/external/x/t.png","width":10,"height":10}}],
         "attachments":[{"url":"https://cdn.discordapp.com/attachments/1/2/a.zip","filename":"a.zip","content_type":"application/zip"}]}
    ])", rich));
    assert(rich.size() == 2);
    assert(rich[0].type == 3 && rich[0].hasCall && rich[0].callParticipants.size() == 1);
    assert(rich[0].callEndedTimestamp == "2019-09-20T05:41:00+00:00");
    assert(rich[1].embeds.size() == 1 && rich[1].embeds[0].provider == "E-RECOVERY");
    assert(rich[1].images.size() == 1); // the link thumbnail through Discord's proxy
    assert(rich[1].attachmentUrls.size() == 1 && rich[1].attachmentUrls[0] == "a.zip");
    assert(rich[1].mentionIds.size() == 1 && rich[1].mentionIds[0] == 7001ULL);

    std::map<Snowflake, ReadState> rs;
    assert(api::parseReadStatesJson(R"({"version":1,"partial":false,"entries":[
        {"id":"1","last_message_id":"9001","mention_count":2},
        {"id":"2","last_message_id":null,"mention_count":null},
        {"id":"3","read_state_type":1,"last_message_id":"5"}]})", rs));
    assert(rs.size() == 2 && rs[1].lastReadId == 9001ULL && rs[1].mentions == 2 && rs[2].lastReadId == 0);
    std::map<Snowflake, ReadState> rsArray;
    assert(api::parseReadStatesJson(R"([{"id":"4","last_message_id":"8"}])", rsArray) && rsArray[4].lastReadId == 8);

    std::cout << "discord_core_test: all passed" << std::endl;
    return 0;
}
