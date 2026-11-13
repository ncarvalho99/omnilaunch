#include "home/AppFetch.hpp"

#include "core/DebugLog.hpp"
#include "net/NetService.hpp"
#include <nxui/core/ThreadPool.hpp>
#include <switchu/sd_commit.hpp>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace home {

std::string installedBrowserPath() {
    std::error_code ec;
    for (const char* candidate : {kBrowserNro, kLegacyBrowserNro})
        if (std::filesystem::is_regular_file(candidate, ec) && std::filesystem::file_size(candidate, ec) > 0)
            return candidate;
    return {};
}

bool AppFetch::start(net::NetService& net, nxui::ThreadPool& pool, const std::string& url,
                     const std::string& dest, const std::string& name) {
    if (running() || !pool.acceptingTasks())
        return false;
    if (m_future.valid()) {
        m_future.wait();
        try { m_future.get(); } catch (...) {}
    }
    auto shared = std::make_shared<Shared>();
    shared->state.store(Running);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(dest).parent_path(), ec);
    net::NetService* service = &net;
    try {
        m_future = pool.submit([shared, service, url, dest]() {
          try {
            const std::string part = dest + ".part";
            net::Download dl;
            dl.url = url;
            dl.path = part;
            dl.timeoutMs = 180000;
            dl.cancel = &shared->stop;
            dl.progress = [shared](std::uint64_t done, std::uint64_t total) {
                shared->done.store(done);
                if (total)
                    shared->total.store(total);
            };
            const auto res = service->download(dl);
            bool ok = res.ok();
            if (ok) {
                shared->installing.store(true);
                // Only a real .nro replaces the installed file.
                char magic[4] = {};
                if (std::FILE* f = std::fopen(part.c_str(), "rb")) {
                    ok = std::fseek(f, 0x10, SEEK_SET) == 0 && std::fread(magic, 1, 4, f) == 4 &&
                         std::memcmp(magic, "NRO0", 4) == 0;
                    std::fclose(f);
                } else {
                    ok = false;
                }
            }
            const bool stopped = shared->stop.load();
            if (ok && stopped)
                ok = false; // cancelled, power action or frontend leaving: write nothing more
            if (ok) {
                // Keep any file already there until the new one is in place.
                const std::string old = dest + ".old";
                std::error_code ec;
                const bool hadOld = std::filesystem::exists(dest, ec);
                std::remove(old.c_str());
                if (hadOld && std::rename(dest.c_str(), old.c_str()) != 0) {
                    ok = false;
                } else {
                    ok = std::rename(part.c_str(), dest.c_str()) == 0;
                    if (ok)
                        std::remove(old.c_str());
                    else if (hadOld)
                        std::rename(old.c_str(), dest.c_str());
                }
            }
            if (!ok)
                std::remove(part.c_str());
            switchu::commitSdCard("app fetch");
            DebugLog::log("[fetch] %s status=%ld ok=%d stopped=%d %s", dest.c_str(), res.status, ok ? 1 : 0,
                          stopped ? 1 : 0, res.error.c_str());
            shared->state.store(ok ? Done : (stopped ? Cancelled : Failed));
          } catch (...) {
            shared->state.store(Failed); // never leave the dialog waiting on Running
          }
        });
    } catch (...) {
        return false;
    }
    m_shared = shared;
    m_dest = dest;
    m_name = name;
    return true;
}

void AppFetch::cancel() {
    if (m_shared)
        m_shared->stop.store(true);
}

void AppFetch::stop() {
    cancel();
    if (m_future.valid()) {
        m_future.wait();
        try { m_future.get(); } catch (...) {}
    }
}

AppFetch::State AppFetch::poll() {
    if (!m_shared)
        return Idle;
    const int state = m_shared->state.load();
    if (state == Running)
        return Running;
    if (m_future.valid()) {
        try { m_future.get(); } catch (...) {}
    }
    m_shared.reset();
    return static_cast<State>(state);
}

float AppFetch::progress() const {
    if (!m_shared || m_shared->installing.load())
        return -1.f;
    const std::uint64_t total = m_shared->total.load();
    if (total == 0)
        return -1.f;
    const float p = static_cast<float>(m_shared->done.load()) / static_cast<float>(total);
    return p < 0.f ? 0.f : (p > 1.f ? 1.f : p);
}

} // namespace home
