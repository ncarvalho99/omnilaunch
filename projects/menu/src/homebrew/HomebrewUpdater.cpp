// Homebrew updates, network and SD side (HomeNX G5). Written from scratch on
// HomeNX's behaviour; see HomebrewUpdater.hpp.
#include "homebrew/HomebrewUpdater.hpp"

#include "core/DebugLog.hpp"
#include "homebrew/HomebrewStore.hpp"
#include "net/NetService.hpp"
#include "themeshop/ZipReader.hpp"

#ifdef SWITCHU_MENU
#include <switchu/sd_commit.hpp>
#endif

#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace hbupdate {

namespace {

constexpr const char* kSelfNro = "sdmc:/switch/OmniLaunch/OmniLaunch.nro";
constexpr const char* kWorkDir = "sdmc:/config/OmniLaunch/tmp/hbupdate";
constexpr const char* kStoreUrl = "https://switch.cdn.fortheusers.org/repo.json";

bool stopped(const std::atomic<bool>& stop) {
    return stop.load(std::memory_order_acquire);
}

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

std::int64_t fileSize(const std::string& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(path, ec);
    return ec ? -1 : static_cast<std::int64_t>(n);
}

// An NRO: "NRO0" at 0x10 and a header size that fits the file.
bool looksLikeNro(const std::string& path) {
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char head[0x20] = {};
    const bool read = std::fread(head, 1, sizeof(head), f) == sizeof(head);
    std::fclose(f);
    if (!read || std::memcmp(head + 0x10, "NRO0", 4) != 0) return false;
    const std::uint32_t size = static_cast<std::uint32_t>(head[0x18]) | static_cast<std::uint32_t>(head[0x19]) << 8 |
                               static_cast<std::uint32_t>(head[0x1A]) << 16 |
                               static_cast<std::uint32_t>(head[0x1B]) << 24;
    const std::int64_t have = fileSize(path);
    return size >= 0x80 && have >= static_cast<std::int64_t>(size);
}

void commit(const char* why) {
#ifdef SWITCHU_MENU
    switchu::commitSdCard(why);
#else
    (void)why;
#endif
}

} // namespace

void recoverInterrupted(const std::string& path) {
    const std::string old = path + ".old", fresh = path + ".new";
    if (fileSize(path) > 0) {
        // The swap finished (or never started): stale helpers only.
        if (fileSize(old) >= 0 && looksLikeNro(path))
            std::remove(old.c_str());
        return;
    }
    // The target is gone: the old copy goes back, else a complete new one.
    if (looksLikeNro(old) && std::rename(old.c_str(), path.c_str()) == 0) {
        DebugLog::log("[hbupdate] %s restored from interrupted update", path.c_str());
        commit("homebrew update recovery");
    } else if (looksLikeNro(fresh) && std::rename(fresh.c_str(), path.c_str()) == 0) {
        DebugLog::log("[hbupdate] %s completed from interrupted update", path.c_str());
        commit("homebrew update recovery");
    }
}

void recoverInterruptedAll() {
    std::error_code ec;
    for (const char* root : {"sdmc:/switch"}) {
        std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            if (it.depth() > 1) { it.disable_recursion_pending(); continue; }
            const std::string p = it->path().string();
            if (p.size() > 8 && p.compare(p.size() - 8, 8, ".nro.old") == 0) {
                const std::string target = p.substr(0, p.size() - 4);
                if (!managedPath(target)) recoverInterrupted(target);
            }
        }
    }
}

State loadState(const std::string& path) {
    return parseState(readFile(path));
}

bool saveState(const State& state, const std::string& path) {
    const bool ok = writeAtomic(path, dumpState(state));
    commit("homebrew updates state");
    return ok;
}

std::vector<Installed> installedApps() {
    std::vector<Installed> out;
    for (HomebrewApp& h : scanHomebrew(kSelfNro, false)) {
        if (managedPath(h.path)) continue;
        out.push_back(Installed{h.path, h.name, h.version});
    }
    return out;
}

State check(net::NetService& net, bool force, CheckProgress& progress, const std::atomic<bool>& stop,
            bool mayFetch) {
    State state = loadState();
    const std::int64_t now = static_cast<std::int64_t>(std::time(nullptr));
    recoverInterruptedAll();
    const bool fresh = state.checkedAt > 0 && now >= state.checkedAt && now - state.checkedAt < kFreshSeconds;
    if (!mayFetch || (!force && fresh) || !net.online() || stopped(stop))
        return state;

    const std::vector<Installed> apps = installedApps();
    progress.total = static_cast<int>(apps.size());
    progress.done = 0;

    std::vector<StorePackage> store;
    bool storeOk = false;
    {
        net::Request req;
        req.url = kStoreUrl;
        req.maxBytes = 8u << 20;
        req.timeoutMs = 30000;
        req.cancel = &stop;
        const net::Response res = net.request(req);
        if (res.cancelled) return state;
        if (res.ok()) {
            store = parseStore(res.body);
            storeOk = !store.empty();
        }
    }
    DebugLog::log("[hbupdate] store packages=%d apps=%d", static_cast<int>(store.size()), static_cast<int>(apps.size()));

    State next;
    next.installed = state.installed;
    int githubCalls = 0;
    bool incomplete = !storeOk;
    for (const Installed& app : apps) {
        if (stopped(stop)) return state; // keep the previous state; nothing saved
        Offer offer;
        bool found = false;
        bool answered = false; // some source gave a real answer for this app
        const std::string repo = knownRepo(app.name);
        // GitHub allows 60 unauthenticated API calls an hour per address.
        if (!repo.empty() && githubCalls < 30) {
            ++githubCalls;
            net::Request req;
            req.url = "https://api.github.com/repos/" + repo + "/releases/latest";
            req.headers = {"Accept: application/vnd.github+json"};
            req.attempts = 1;
            req.cancel = &stop;
            const net::Response res = net.request(req);
            if (res.cancelled) return state;
            if (res.ok()) {
                answered = true;
                found = githubOffer(res.body, repo, app, fileSize(app.path), offer);
            }
        }
        if (!found && storeOk) {
            answered = true;
            if (const StorePackage* p = matchStore(store, app)) {
                offer = storeOffer(*p);
                found = true;
            }
        }
        if (found) {
            next.offers[app.path] = offer;
            if (const Offer* o = pendingOffer(next, app))
                DebugLog::log("[hbupdate] %s %s -> %s (%s)", app.name.c_str(), app.version.c_str(),
                              o->latest.c_str(), o->source.c_str());
        } else if (!answered) {
            // No source answered (outage, rate limit): keep what was known.
            incomplete = true;
            if (auto old = state.offers.find(app.path); old != state.offers.end())
                next.offers[app.path] = old->second;
        }
        ++progress.done;
    }
    // A check that could not reach its sources is not "fresh": the next visit
    // tries again instead of waiting six hours.
    next.checkedAt = incomplete ? state.checkedAt : now;
    saveState(next);
    DebugLog::log("[hbupdate] check done offers=%d%s", static_cast<int>(next.offers.size()),
                  incomplete ? " (incomplete)" : "");
    return next;
}

InstallResult install(net::NetService& net, State& state, const std::vector<std::string>& paths,
                      const std::string& skipPath, InstallProgress& progress, const std::atomic<bool>& stop) {
    InstallResult result;
    progress.count = static_cast<int>(paths.size());
    std::error_code ec;
    std::filesystem::create_directories(kWorkDir, ec);
    for (std::size_t i = 0; i < paths.size(); ++i) {
        progress.index = static_cast<int>(i);
        progress.fraction = 0.f;
        if (stopped(stop)) {
            result.cancelled = true;
            break;
        }
        const std::string& path = paths[i];
        auto it = state.offers.find(path);
        HomebrewApp info;
        readHomebrew(path, info, false);
        const std::string name = info.name.empty() ? path : info.name;
        auto fail = [&](const std::string& why) {
            result.failed.push_back(name + ": " + why);
            DebugLog::log("[hbupdate] %s failed: %s", path.c_str(), why.c_str());
        };
        if (it == state.offers.end()) { fail("no update"); continue; }
        if (managedPath(path) || (!skipPath.empty() && path == skipPath)) { fail("in use"); continue; }
        const Offer offer = it->second;
        const std::string fresh = path + ".new", old = path + ".old";
        std::remove(fresh.c_str());

        net::Download dl;
        dl.cancel = &stop;
        dl.progress = [&progress](std::uint64_t done, std::uint64_t total) {
            if (total > 0) progress.fraction = static_cast<float>(done) / static_cast<float>(total);
        };
        const bool zip = !offer.zipEntry.empty();
        const std::string zipPath = std::string(kWorkDir) + "/download.zip";
        const std::string unpackDir = std::string(kWorkDir) + "/unpack";
        dl.url = offer.url;
        dl.path = zip ? zipPath : fresh;
        const net::Response res = net.download(dl);
        if (res.cancelled) {
            result.cancelled = true;
            break;
        }
        if (!res.ok()) {
            fail(res.error.empty() ? "HTTP " + std::to_string(res.status) : res.error);
            continue;
        }
        if (zip) {
            std::filesystem::remove_all(unpackDir, ec);
            themeshop::ZipExtractPolicy policy;
            policy.allowExecutablePayload = true;
            policy.onlyRoots = {offer.zipEntry};
            const auto x = themeshop::extractZipFile(zipPath, unpackDir, {}, policy);
            std::remove(zipPath.c_str());
            const std::string unpacked = unpackDir + "/" + offer.zipEntry;
            if (!x.success || x.filesWritten != 1 || fileSize(unpacked) <= 0) {
                std::filesystem::remove_all(unpackDir, ec);
                fail(x.error.empty() ? "no .nro in the download" : x.error);
                continue;
            }
            std::error_code moveEc, cleanEc;
            std::filesystem::rename(unpacked, fresh, moveEc);
            std::filesystem::remove_all(unpackDir, cleanEc); // leftovers only; not a failure
            if (moveEc) { std::remove(fresh.c_str()); fail("could not unpack"); continue; }
        }
        if (!looksLikeNro(fresh)) {
            std::remove(fresh.c_str());
            fail("the download is not an .nro");
            continue;
        }
        // An earlier interrupted swap may have left the only copy in `.old`.
        recoverInterrupted(path);
        if (fileSize(path) <= 0) {
            std::remove(fresh.c_str());
            fail("the installed file is missing");
            continue;
        }
        // The old one aside, the new one in, then the old one gone. The new
        // file is committed before the old one is deleted, so a crash at any
        // point leaves one valid .nro (recovered on the next scan/install).
        std::remove(old.c_str());
        commit("homebrew update stage");
        if (std::rename(path.c_str(), old.c_str()) != 0) {
            std::remove(fresh.c_str());
            fail("could not move the old file");
            continue;
        }
        if (std::rename(fresh.c_str(), path.c_str()) != 0) {
            const bool restored = std::rename(old.c_str(), path.c_str()) == 0;
            std::remove(fresh.c_str());
            commit("homebrew update rollback");
            // When the restore fails `.old` is kept; recoverInterrupted() puts it back later.
            fail(restored ? "could not put the new file in place" : "could not put the new file in place (old copy kept)");
            continue;
        }
        commit("homebrew update swap");
        std::remove(old.c_str());
        state.installed[path] = offer.latest;
        state.offers.erase(path);
        saveState(state); // also commits the card
        result.updated.push_back(path);
        progress.fraction = 1.f;
        DebugLog::log("[hbupdate] %s updated to %s from %s", path.c_str(), offer.latest.c_str(), offer.source.c_str());
    }
    std::filesystem::remove_all(kWorkDir, ec);
    return result;
}

} // namespace hbupdate
