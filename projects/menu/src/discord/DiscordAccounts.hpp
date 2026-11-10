#pragma once
// Discord accounts persistence on SD (HomeNX H1, C-NET-2).
// Stores saved accounts at sdmc:/config/OmniLaunch/discord_accounts.json with
// migration support from legacy sdmc:/switch/switchcord/accounts.json.
// New OmniLaunch implementation written from scratch.

#include "discord/DiscordTypes.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace discord {

constexpr const char* kAccountsPath = "sdmc:/config/OmniLaunch/discord_accounts.json";
constexpr const char* kLegacySwitchCordPath = "sdmc:/switch/switchcord/accounts.json";

class DiscordAccounts {
public:
    DiscordAccounts();
    ~DiscordAccounts();

    bool load(const std::string& path = kAccountsPath);
    bool save(const std::string& path = kAccountsPath) const;

    std::vector<Account> accounts() const;
    Account current() const;
    std::string currentId() const;
    bool hasAccount() const;

    void setCurrentId(const std::string& id);
    void addOrUpdate(const Account& acc);
    bool remove(const std::string& id);
    void clear();

private:
    mutable std::mutex m_mu;
    std::vector<Account> m_accounts;
    std::string m_currentId;
};

/// Pure helper for parsing accounts JSON, used in host tests.
bool parseAccountsJson(const std::string& json, std::vector<Account>& outAccounts, std::string& outCurrentId);

/// Pure helper for dumping accounts to JSON string.
std::string dumpAccountsJson(const std::vector<Account>& accounts, const std::string& currentId);

} // namespace discord
