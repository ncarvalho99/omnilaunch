#include "nextendo/NextendoService.hpp"
#include "nextendo/NextendoCatalog.hpp"

#include "core/DebugLog.hpp"
#include "home/HomeCore.hpp"
#include "net/NetService.hpp"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <thread>

namespace nextendo {

NextendoService::NextendoService() = default;

NextendoService::~NextendoService() = default;

void NextendoService::update(net::NetService& net, bool netUp) {
    if (!netUp) return;
    const auto now = std::chrono::steady_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    if (m_lastFetchTick == 0 || (nowMs - m_lastFetchTick >= 60000)) {
        refresh(net);
    }
}

void NextendoService::refresh(net::NetService& net) {
    if (m_fetching.exchange(true)) return;
    const auto now = std::chrono::steady_clock::now();
    m_lastFetchTick = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::thread([this, &net]() {
        fetchCounts(net);
        m_fetching.store(false);
    }).detach();
}

void NextendoService::fetchCounts(net::NetService& net) {
    std::map<std::uint64_t, int> counts;
    int total = 0;
    bool success = false;

    auto tryUrl = [&](const char* url) -> bool {
        net::Request req;
        req.url = url;
        req.timeoutMs = 12000;
        req.maxBytes = 1u << 20;
        const net::Response res = net.request(req);
        if (res.ok() && parseOnlineCounts(res.body, counts, total)) {
            return true;
        }
        return false;
    };

    if (tryUrl(kNextendoUrlPrimary) || tryUrl(kNextendoUrlFallback)) {
        success = true;
    }

    // Check specific game stats endpoints
    for (const auto& ep : kSpecificEndpoints) {
        net::Request req;
        req.url = std::string("https://nextendo.network") + ep.path;
        req.timeoutMs = 8000;
        req.maxBytes = 64u << 10;
        const net::Response res = net.request(req);
        if (res.ok()) {
            const auto j = nlohmann::json::parse(res.body, nullptr, false);
            if (!j.is_discarded() && j.is_object()) {
                if (ep.needsOnline && !j.value("online", false)) continue;
                if (j.contains("connected") && j["connected"].is_number_integer()) {
                    const int c = j.value("connected", 0);
                    if (c >= 0) {
                        counts[ep.titleId] = c;
                        success = true;
                    }
                }
            }
        }
    }

    if (success) {
        total = 0;
        for (const auto& kv : counts) total += kv.second;
        std::lock_guard<std::mutex> lk(m_mu);
        m_counts = std::move(counts);
        m_totalOnline = total;
        m_hasData = true;
        DebugLog::log("[nextendo] fetched counts total=%d games=%zu", total, m_counts.size());
    } else {
        DebugLog::log("[nextendo] failed to fetch counts from nextendo.network");
    }
}

int NextendoService::totalOnline() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_totalOnline;
}

int NextendoService::countForTitle(std::uint64_t titleId) const {
    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_counts.find(titleId);
    return it != m_counts.end() ? it->second : 0;
}

bool NextendoService::hasData() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_hasData;
}

int NextendoService::openNextendoApp(home::HomeCore& core, std::string& outMsg) {
    std::string candidatePaths[] = {
        core.config().nextendoPath,
        kNextendoPreludeNro,
        kNextendoNroDefault,
        "sdmc:/switch/Prelude/prelude.nro",
    };

    std::string existingPath;
    std::error_code ec;
    for (const auto& p : candidatePaths) {
        if (!p.empty() && std::filesystem::exists(p, ec)) {
            existingPath = p;
            break;
        }
    }

    if (!existingPath.empty()) {
        DebugLog::log("[nextendo] launching %s", existingPath.c_str());
        if (core.launcher().launchNro(existingPath, 0, {}, true)) {
            return 0;
        }
        outMsg = "nextendo_download_failed";
        return -1;
    }

    // App not found on SD card: offer / start download
    if (m_downloading.load()) {
        outMsg = "nextendo_getting";
        return 1;
    }

    if (!core.net().online()) {
        outMsg = "nextendo_offline";
        return -1;
    }

    m_downloading.store(true);
    outMsg = "nextendo_getting";

    std::thread([this, &core]() {
        net::Download dl;
        dl.url = kNextendoDownloadUrl;
        dl.path = kNextendoNroDefault;
        DebugLog::log("[nextendo] downloading Prelude from %s to %s", dl.url.c_str(), dl.path.c_str());
        const net::Response res = core.net().download(dl);
        m_downloading.store(false);
        if (res.ok()) {
            DebugLog::log("[nextendo] Prelude download complete: %s", dl.path.c_str());
        } else {
            DebugLog::log("[nextendo] Prelude download failed: %s (status %ld)", res.error.c_str(), res.status);
        }
    }).detach();

    return 1;
}

} // namespace nextendo
