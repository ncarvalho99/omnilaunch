#include "widgets/LockPassword.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#ifdef SWITCHU_MENU
#include "core/DebugLog.hpp"
#include "widgets/LockScreen.hpp"
#include <switch.h>
#include <switchu/sd_commit.hpp>
#endif

namespace lockpw {

namespace {
bool readStored(const std::string& path, std::size_t& n, std::string& h) {
    std::ifstream in(path);
    if (!in)
        return false;
    in >> n >> h;
    return h.size() == 64 && n >= kMin && n <= kMax;
}
bool validPresses(const std::string& presses) {
    if (presses.size() < kMin || presses.size() > kMax)
        return false;
    for (char c : presses)
        if (c != 'U' && c != 'D' && c != 'L' && c != 'R')
            return false;
    return true;
}
} // namespace

std::string digest(const HashFn& hash, const std::string& presses) {
    return hash("omnilaunch-lock:" + presses);
}

std::size_t storedLength(const std::string& path) {
    std::size_t n = 0;
    std::string h;
    return readStored(path, n, h) ? n : 0;
}

bool check(const HashFn& hash, const std::string& presses, const std::string& path) {
    std::size_t n = 0;
    std::string h;
    return readStored(path, n, h) && n == presses.size() && digest(hash, presses) == h;
}

bool save(const HashFn& hash, const std::string& presses, const std::string& path) {
    if (presses.empty())
        return std::remove(path.c_str()) == 0 || storedLength(path) == 0;
    if (!validPresses(presses))
        return false;
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
            return false;
        out << presses.size() << ' ' << digest(hash, presses) << '\n';
        if (!out.flush())
            return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
    }
    if (ec)
        return false;
    return check(hash, presses, path);
}

#ifdef SWITCHU_MENU
std::string sha256Hex(const std::string& data) {
    unsigned char d[32];
    sha256CalculateHash(d, data.data(), data.size());
    char hex[65] = {};
    for (int i = 0; i < 32; ++i)
        std::snprintf(hex + i * 2, 3, "%02x", d[i]);
    return std::string(hex, 64);
}

void attach(LockScreen& lock) {
    const std::size_t n = storedLength();
    lock.setPassword(
        n, [](const std::string& presses) { return check(sha256Hex, presses); },
        []() {
            save(sha256Hex, "");
            switchu::commitSdCard("lock password cleared");
        });
    DebugLog::log("[lock] password %s", n ? "on" : "off");
}
#else
std::string sha256Hex(const std::string&) { return {}; }
void attach(LockScreen&) {}
#endif

} // namespace lockpw
