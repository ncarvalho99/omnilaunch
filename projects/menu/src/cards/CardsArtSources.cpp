#include "cards/CardsArtSources.hpp"

#include "cards/GameTdbIndex.hpp"
#include "core/DebugLog.hpp"
#include "core/GridModel.hpp"
#include "steamgriddb/ArtworkCache.hpp"

#include <nlohmann/json.hpp>
#include <sys/stat.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace cards {

namespace {

constexpr const char* kCoversDir = "sdmc:/config/OmniLaunch/covers";
constexpr const char* kSgdbDir = "sdmc:/config/OmniLaunch/steamgriddb";
std::atomic<std::uint64_t> s_tempCounter{0};

bool isRegularFile(const std::string& path) {
    struct stat st{};
    return !path.empty() && stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool isPictureData(std::string_view data) {
    if (data.size() < 64)
        return false;
    const auto b0 = static_cast<unsigned char>(data[0]);
    const auto b1 = static_cast<unsigned char>(data[1]);
    const bool isJpeg = (b0 == 0xFF && b1 == 0xD8);
    const bool isPng = (data.size() > 8 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G');
    return isJpeg || isPng;
}

bool isPictureData(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 64)
        return false;
    return isPictureData(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

bool atomicWrite(const std::string& destPath, const std::vector<std::uint8_t>& bytes) {
    if (destPath.empty() || bytes.empty())
        return false;
    std::error_code ec;
    const std::filesystem::path p(destPath);
    std::filesystem::create_directories(p.parent_path(), ec);
    const std::string tmpPath = destPath + ".tmp." + std::to_string(++s_tempCounter);
    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }
    std::filesystem::rename(tmpPath, destPath, ec);
    if (ec) {
        std::error_code ec2;
        std::filesystem::remove(destPath, ec2);
        std::filesystem::rename(tmpPath, destPath, ec);
    }
    if (ec) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    return true;
}

std::string titleDir(std::uint64_t titleId) {
    return std::string(kSgdbDir) + "/" + CardsArtSources::hexId(titleId);
}

} // namespace

std::string CardsArtSources::hexId(std::uint64_t titleId) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llX", static_cast<unsigned long long>(titleId));
    return buf;
}

std::string CardsArtSources::gametdbCoverUrl(std::string_view title, int attempt) {
    auto entry = GameTdbIndex::lookup(title);
    if (!entry.has_value())
        return {};
    return GameTdbIndex::coverUrl(entry->id, entry->region, attempt);
}

std::string CardsArtSources::nlibBannerUrl(std::uint64_t titleId, int width, int height) {
    if (titleId == 0)
        return {};
    return "https://api.nlib.cc/nx/" + hexId(titleId) + "/banner/" +
           std::to_string(width) + "/" + std::to_string(height);
}

std::string CardsArtSources::coverDestination(std::uint64_t titleId) {
    if (titleId == 0)
        return {};
    return std::string(kCoversDir) + "/" + hexId(titleId) + ".jpg";
}

std::string CardsArtSources::heroDestination(std::uint64_t titleId) {
    if (titleId == 0)
        return {};
    return titleDir(titleId) + "/hero.img";
}

std::string CardsArtSources::logoDestination(std::uint64_t titleId) {
    if (titleId == 0)
        return {};
    return titleDir(titleId) + "/logo.img";
}

bool CardsArtSources::hasCover(std::uint64_t titleId) {
    return isRegularFile(coverDestination(titleId));
}

bool CardsArtSources::hasHero(std::uint64_t titleId) {
    return isRegularFile(heroDestination(titleId));
}

bool CardsArtSources::hasLogo(std::uint64_t titleId) {
    return isRegularFile(logoDestination(titleId));
}

bool CardsArtSources::saveCover(std::uint64_t titleId, const std::vector<std::uint8_t>& bytes) {
    if (!isPictureData(bytes))
        return false;
    return atomicWrite(coverDestination(titleId), bytes);
}

bool CardsArtSources::saveHero(std::uint64_t titleId, const std::vector<std::uint8_t>& bytes) {
    if (!isPictureData(bytes))
        return false;
    const std::string path = heroDestination(titleId);
    if (!atomicWrite(path, bytes))
        return false;
    steamgriddb::artwork::remove(path, 1280, 720);
    steamgriddb::artwork::prepare(path, 1280, 720, true);
    try {
        const std::string metaPath = titleDir(titleId) + "/metadata.json";
        nlohmann::json meta;
        if (std::filesystem::exists(metaPath)) {
            std::ifstream in(metaPath);
            in >> meta;
        }
        meta["titleId"] = titleId;
        meta["hero"] = true;
        meta["matcherVersion"] = 2; // kMatcherVersion in SteamGridDbManager
        std::ofstream out(metaPath, std::ios::trunc);
        if (out.is_open())
            out << meta.dump(2);
    } catch (...) {
    }
    return true;
}

bool CardsArtSources::saveLogo(std::uint64_t titleId, const std::vector<std::uint8_t>& bytes) {
    if (!isPictureData(bytes))
        return false;
    const std::string path = logoDestination(titleId);
    if (!atomicWrite(path, bytes))
        return false;
    steamgriddb::artwork::remove(path, 640, 180);
    steamgriddb::artwork::prepare(path, 640, 180, false);
    try {
        const std::string metaPath = titleDir(titleId) + "/metadata.json";
        nlohmann::json meta;
        if (std::filesystem::exists(metaPath)) {
            std::ifstream in(metaPath);
            in >> meta;
        }
        meta["titleId"] = titleId;
        meta["logo"] = true;
        meta["matcherVersion"] = 2;
        std::ofstream out(metaPath, std::ios::trunc);
        if (out.is_open())
            out << meta.dump(2);
    } catch (...) {
    }
    return true;
}

void CardsArtSources::forgetArt(std::uint64_t titleId) {
    if (titleId == 0)
        return;
    std::error_code ec;
    std::filesystem::remove(coverDestination(titleId), ec);
    ec.clear();
    const std::string hero = heroDestination(titleId);
    std::filesystem::remove(hero, ec);
    ec.clear();
    std::filesystem::remove(hero + ".1280x720.rgba-cache", ec);
    ec.clear();
    const std::string logo = logoDestination(titleId);
    std::filesystem::remove(logo, ec);
    ec.clear();
    std::filesystem::remove(logo + ".640x180.rgba-cache", ec);
    ec.clear();
    std::filesystem::remove(titleDir(titleId) + "/metadata.json", ec);
}

std::vector<std::uint8_t> CardsArtSources::fetchGametdbCover(net::NetService& net,
                                                             std::string_view title,
                                                             const std::atomic<bool>* cancel) {
    auto entry = GameTdbIndex::lookup(title);
    if (!entry.has_value())
        return {};
    for (int attempt = 0; attempt < 3; ++attempt) {
        if (cancel && cancel->load(std::memory_order_acquire))
            break;
        const std::string url = GameTdbIndex::coverUrl(entry->id, entry->region, attempt);
        if (url.empty())
            break;
        net::Request req;
        req.url = url;
        req.timeoutMs = 12000;
        req.attempts = 1;
        req.cancel = cancel;
        auto res = net.request(req);
        if (res.ok() && isPictureData(res.body)) {
            return std::vector<std::uint8_t>(res.body.begin(), res.body.end());
        }
    }
    return {};
}

std::vector<std::uint8_t> CardsArtSources::fetchNlibBanner(net::NetService& net,
                                                           std::uint64_t titleId,
                                                           int width, int height,
                                                           const std::atomic<bool>* cancel) {
    const std::string url = nlibBannerUrl(titleId, width, height);
    if (url.empty())
        return {};
    net::Request req;
    req.url = url;
    req.timeoutMs = 12000;
    req.attempts = 1;
    req.cancel = cancel;
    auto res = net.request(req);
    if (res.ok() && isPictureData(res.body)) {
        return std::vector<std::uint8_t>(res.body.begin(), res.body.end());
    }
    return {};
}

SteamGridDbManager::BrowseResult CardsArtSources::browseAll(
    net::NetService& net,
    const std::string& apiKey,
    std::uint64_t titleId,
    const std::string& title,
    const std::string& query,
    SteamGridDbManager::ArtworkKind kind) {
    (void)net; // future net use for direct custom scraping if required
    auto res = SteamGridDbManager::browse(apiKey, titleId, title, query, kind);

    // Prepend GameTDB and nlib candidates for Covers and Heroes
    if (kind == SteamGridDbManager::ArtworkKind::Cover) {
        const std::string gametdbUrl = gametdbCoverUrl(title, 0);
        if (!gametdbUrl.empty()) {
            SteamGridDbManager::Candidate c;
            c.id = 9000001;
            c.url = gametdbUrl;
            c.thumbnailUrl = gametdbUrl;
            c.width = 600;
            c.height = 900;
            res.candidates.insert(res.candidates.begin(), c);
        }
        const std::string nlibUrl = nlibBannerUrl(titleId, 640, 360);
        if (!nlibUrl.empty()) {
            SteamGridDbManager::Candidate c;
            c.id = 9000002;
            c.url = nlibUrl;
            c.thumbnailUrl = nlibUrl;
            c.width = 640;
            c.height = 360;
            res.candidates.insert(res.candidates.begin() + (res.candidates.empty() ? 0 : 1), c);
        }
    } else if (kind == SteamGridDbManager::ArtworkKind::Hero) {
        // Console resolution (1280x720) 16:9 banner and official screenshots from nlib
        const std::string nlibUrl = nlibBannerUrl(titleId, 1280, 720);
        if (!nlibUrl.empty()) {
            SteamGridDbManager::Candidate c;
            c.id = 9000003;
            c.url = nlibUrl;
            c.thumbnailUrl = nlibUrl;
            c.width = 1280;
            c.height = 720;
            res.candidates.insert(res.candidates.begin(), c);
        }
        if (titleId != 0) {
            const std::string hid = hexId(titleId);
            for (int s = 1; s <= 4; ++s) {
                SteamGridDbManager::Candidate sc;
                sc.id = 9000100 + s;
                sc.url = "https://api.nlib.cc/nx/" + hid + "/screen/" + std::to_string(s);
                sc.thumbnailUrl = sc.url;
                sc.width = 1280;
                sc.height = 720;
                res.candidates.push_back(sc);
            }
        }
    }

    if (!res.candidates.empty()) {
        res.success = true;
        res.error.clear();
        if (res.gameName.empty())
            res.gameName = title;
    }
    return res;
}

bool CardsArtSources::applyCandidate(
    net::NetService& net,
    const SteamGridDbManager::BrowseResult& browse,
    const SteamGridDbManager::Candidate& candidate,
    const std::atomic<bool>* cancel,
    const ProgressCallback& onProgress) {
    if (candidate.url.empty())
        return false;
    if (cancel && cancel->load(std::memory_order_acquire))
        return false;
    if (onProgress)
        onProgress("Downloading candidate artwork...", 0.05f);

    net::Request req;
    req.url = candidate.url;
    req.timeoutMs = 15000;
    req.attempts = 2;
    req.cancel = cancel;
    auto res = net.request(req);
    if (!res.ok() || res.body.empty() || (cancel && cancel->load(std::memory_order_acquire)))
        return false;

    if (!isPictureData(res.body))
        return false;
    const std::vector<std::uint8_t> bytes(res.body.begin(), res.body.end());

    if (onProgress)
        onProgress("Saving artwork...", 0.70f);

    bool saved = false;
    if (browse.kind == SteamGridDbManager::ArtworkKind::Cover) {
        saved = saveCover(browse.titleId, bytes);
    } else if (browse.kind == SteamGridDbManager::ArtworkKind::Hero) {
        saved = saveHero(browse.titleId, bytes);
    } else if (browse.kind == SteamGridDbManager::ArtworkKind::Logo) {
        saved = saveLogo(browse.titleId, bytes);
    }

    if (saved) {
        try {
            std::filesystem::path p(titleDir(browse.titleId) + "/metadata.json");
            nlohmann::json meta;
            if (std::filesystem::exists(p)) {
                std::ifstream in(p);
                in >> meta;
            }
            meta["titleId"] = browse.titleId;
            meta["title"] = browse.title;
            const char* idKey = browse.kind == SteamGridDbManager::ArtworkKind::Hero ? "selectedHeroId"
                              : browse.kind == SteamGridDbManager::ArtworkKind::Logo ? "selectedLogoId"
                              : "selectedCoverId";
            meta[idKey] = candidate.id;
            meta["matcherVersion"] = 2;
            std::ofstream out(p, std::ios::trunc);
            if (out.is_open())
                out << meta.dump(2);
        } catch (...) {
        }
    }

    if (onProgress)
        onProgress("Artwork applied", 1.0f);
    return saved;
}

CardsArtSources::SingleTitleResult CardsArtSources::downloadTitleArt(
    net::NetService& net,
    const std::string& apiKey,
    std::uint64_t titleId,
    const std::string& title,
    bool boxArt,
    bool titlePics,
    bool force,
    const std::atomic<bool>* cancel) {
    SingleTitleResult result;
    if (titleId == 0)
        return result;
    if (cancel && cancel->load(std::memory_order_acquire))
        return result;

    // 1. Cover
    if (force || !hasCover(titleId)) {
        std::vector<std::uint8_t> bytes;
        if (boxArt) {
            bytes = fetchGametdbCover(net, title, cancel);
        }
        if (bytes.empty()) {
            bytes = fetchNlibBanner(net, titleId, 640, 360, cancel);
        }
        if (!bytes.empty()) {
            result.cover = saveCover(titleId, bytes);
        }
    } else {
        result.cover = true;
    }

    // 2. Hero Background (prefer 16:9 widescreen 1280x720 / 1920x1080)
    if (force || !hasHero(titleId)) {
        std::vector<std::uint8_t> bytes = fetchNlibBanner(net, titleId, 1280, 720, cancel);
        if (bytes.empty() && titleId != 0) {
            const std::string hid = hexId(titleId);
            for (int s = 1; s <= 4; ++s) {
                if (cancel && cancel->load(std::memory_order_acquire))
                    break;
                net::Request req;
                req.url = "https://api.nlib.cc/nx/" + hid + "/screen/" + std::to_string(s);
                req.timeoutMs = 12000;
                req.attempts = 1;
                req.cancel = cancel;
                auto res = net.request(req);
                if (res.ok() && isPictureData(res.body)) {
                    bytes = std::vector<std::uint8_t>(res.body.begin(), res.body.end());
                    break;
                }
            }
        }
        if (!bytes.empty()) {
            result.hero = saveHero(titleId, bytes);
        }
    } else {
        result.hero = true;
    }

    // 3. Logo (if titlePics enabled and API key is present)
    if (titlePics && !apiKey.empty() && (force || !hasLogo(titleId))) {
        if (!cancel || !cancel->load(std::memory_order_acquire)) {
            try {
                auto browse = SteamGridDbManager::browse(apiKey, titleId, title, title,
                                                         SteamGridDbManager::ArtworkKind::Logo);
                if (!browse.candidates.empty()) {
                    result.logo = applyCandidate(net, browse, browse.candidates.front(), cancel);
                }
            } catch (...) {
            }
        }
    }

    return result;
}

void CardsArtSources::runSaveAll(
    net::NetService& net,
    const std::string& apiKey,
    const std::vector<AppEntry>& apps,
    bool boxArt,
    bool titlePics,
    bool forceAll,
    std::atomic<int>& done,
    std::atomic<int>& total,
    std::atomic<int>& saved,
    std::atomic<bool>& stop) {
    std::vector<AppEntry> eligible;
    for (const auto& a : apps) {
        if (a.titleId != 0 && a.isApplication())
            eligible.push_back(a);
    }
    total.store(static_cast<int>(eligible.size()), std::memory_order_release);
    done.store(0, std::memory_order_release);
    saved.store(0, std::memory_order_release);

    for (const auto& app : eligible) {
        if (stop.load(std::memory_order_acquire))
            break;
        auto r = downloadTitleArt(net, apiKey, app.titleId, app.steamGridDbTitle(),
                                  boxArt, titlePics, forceAll, &stop);
        if (r.cover || r.hero || r.logo)
            saved.fetch_add(1, std::memory_order_relaxed);
        done.fetch_add(1, std::memory_order_release);
    }
}

} // namespace cards
