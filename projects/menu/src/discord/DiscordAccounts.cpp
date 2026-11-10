#include "discord/DiscordAccounts.hpp"

#include "core/DebugLog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace discord {

namespace {

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeAtomic(const std::string& path, const std::string& text) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out << text;
        out.flush();
        if (!out.good()) {
            out.close();
            std::remove(tmp.c_str());
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec) {
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

} // namespace

bool parseAccountsJson(const std::string& json, std::vector<Account>& outAccounts, std::string& outCurrentId) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded()) return false;

    outAccounts.clear();
    outCurrentId.clear();

    auto parseAccountObj = [](const nlohmann::json& obj, Account& acc) -> bool {
        if (!obj.is_object()) return false;
        acc.id = obj.value("id", "");
        acc.username = obj.value("username", "");
        acc.globalName = obj.value("globalName", obj.value("global_name", ""));
        acc.avatar = obj.value("avatar", "");
        acc.token = obj.value("token", "");
        return !acc.id.empty() && !acc.token.empty();
    };

    if (j.is_object()) {
        outCurrentId = j.value("current", "");
        auto it = j.find("accounts");
        if (it != j.end() && it->is_array()) {
            for (const auto& a : *it) {
                Account acc;
                if (parseAccountObj(a, acc)) {
                    outAccounts.push_back(std::move(acc));
                }
            }
        }
    } else if (j.is_array()) {
        // SwitchCord legacy flat array of accounts
        for (const auto& a : j) {
            Account acc;
            if (parseAccountObj(a, acc)) {
                outAccounts.push_back(std::move(acc));
            }
        }
    }

    if (outCurrentId.empty() && !outAccounts.empty()) {
        outCurrentId = outAccounts.front().id;
    }
    return !outAccounts.empty();
}

std::string dumpAccountsJson(const std::vector<Account>& accounts, const std::string& currentId) {
    nlohmann::json j;
    j["current"] = currentId;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& acc : accounts) {
        nlohmann::json a;
        a["id"] = acc.id;
        a["username"] = acc.username;
        a["globalName"] = acc.globalName;
        a["avatar"] = acc.avatar;
        a["token"] = acc.token;
        arr.push_back(std::move(a));
    }
    j["accounts"] = std::move(arr);
    return j.dump(2);
}

DiscordAccounts::DiscordAccounts() = default;

DiscordAccounts::~DiscordAccounts() = default;

bool DiscordAccounts::load(const std::string& path) {
    std::string text = readFile(path);
    if (text.empty() && path == kAccountsPath) {
        text = readFile(kLegacySwitchCordPath);
        if (!text.empty()) {
            DebugLog::log("[discord] migrating accounts from %s", kLegacySwitchCordPath);
        }
    }
    if (text.empty()) return false;

    std::vector<Account> accs;
    std::string cur;
    if (parseAccountsJson(text, accs, cur)) {
        std::lock_guard<std::mutex> lk(m_mu);
        m_accounts = std::move(accs);
        m_currentId = std::move(cur);
        DebugLog::log("[discord] loaded %zu accounts, current=%s", m_accounts.size(), m_currentId.c_str());
        return true;
    }
    return false;
}

bool DiscordAccounts::save(const std::string& path) const {
    std::string json;
    {
        std::lock_guard<std::mutex> lk(m_mu);
        json = dumpAccountsJson(m_accounts, m_currentId);
    }
    const bool ok = writeAtomic(path, json);
    if (ok) {
        DebugLog::log("[discord] accounts saved to %s", path.c_str());
    } else {
        DebugLog::log("[discord] failed to save accounts to %s", path.c_str());
    }
    return ok;
}

std::vector<Account> DiscordAccounts::accounts() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_accounts;
}

Account DiscordAccounts::current() const {
    std::lock_guard<std::mutex> lk(m_mu);
    for (const auto& acc : m_accounts) {
        if (acc.id == m_currentId) return acc;
    }
    return m_accounts.empty() ? Account{} : m_accounts.front();
}

std::string DiscordAccounts::currentId() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_currentId;
}

bool DiscordAccounts::hasAccount() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return !m_accounts.empty();
}

void DiscordAccounts::setCurrentId(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_currentId = id;
}

void DiscordAccounts::addOrUpdate(const Account& acc) {
    std::lock_guard<std::mutex> lk(m_mu);
    for (auto& existing : m_accounts) {
        if (existing.id == acc.id) {
            existing = acc;
            m_currentId = acc.id;
            return;
        }
    }
    m_accounts.push_back(acc);
    m_currentId = acc.id;
}

bool DiscordAccounts::remove(const std::string& id) {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = std::remove_if(m_accounts.begin(), m_accounts.end(),
                             [&id](const Account& a) { return a.id == id; });
    if (it != m_accounts.end()) {
        m_accounts.erase(it, m_accounts.end());
        if (m_currentId == id) {
            m_currentId = m_accounts.empty() ? "" : m_accounts.front().id;
        }
        return true;
    }
    return false;
}

void DiscordAccounts::clear() {
    std::lock_guard<std::mutex> lk(m_mu);
    m_accounts.clear();
    m_currentId.clear();
}

} // namespace discord
