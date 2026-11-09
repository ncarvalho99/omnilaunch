#pragma once
// Third-party art sources for OmniLaunch (HomeNX G2, C-NET-4, C-UI-4):
// - GameTDB Switch box covers (via romfs/data/gametdb.tsv + art.gametdb.com)
// - nlib.cc official eShop banners (640x360 for cards, 1280x720 for backgrounds)
// - SteamGridDB covers, heroes and logos (via existing SteamGridDbManager)
//
// All network requests use NetService (bundled certs, retry, back-off, cancel).
// Saved images land in sdmc:/config/OmniLaunch/covers/ and steamgriddb/<id>/.

#include "net/NetService.hpp"
#include "steamgriddb/SteamGridDbManager.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

struct AppEntry;

namespace cards {

class CardsArtSources {
public:
    using ProgressCallback = std::function<void(const std::string&, float)>;

    /// Hex string representation of a Switch titleId (16 uppercase hex chars).
    static std::string hexId(std::uint64_t titleId);

    /// Formats GameTDB cover URL for title, if indexed. Attempt: 0 (US/EN), 1, 2.
    static std::string gametdbCoverUrl(std::string_view title, int attempt = 0);

    /// Formats nlib.cc banner URL for a Switch title ID.
    static std::string nlibBannerUrl(std::uint64_t titleId, int width = 1280, int height = 720);

    /// Destination path for box covers on SD card.
    static std::string coverDestination(std::uint64_t titleId);

    /// Destination path for background hero images on SD card.
    static std::string heroDestination(std::uint64_t titleId);

    /// Destination path for title logo images on SD card.
    static std::string logoDestination(std::uint64_t titleId);

    /// Checks if a valid cover/hero/logo file exists on disk.
    static bool hasCover(std::uint64_t titleId);
    static bool hasHero(std::uint64_t titleId);
    static bool hasLogo(std::uint64_t titleId);

    /// Atomically writes bytes to disk.
    static bool saveCover(std::uint64_t titleId, const std::vector<std::uint8_t>& bytes);
    static bool saveHero(std::uint64_t titleId, const std::vector<std::uint8_t>& bytes);
    static bool saveLogo(std::uint64_t titleId, const std::vector<std::uint8_t>& bytes);

    /// Deletes local cover, hero, logo and associated decode caches for title.
    static void forgetArt(std::uint64_t titleId);

    /// Fetches a verified image from GameTDB or nlib.cc using NetService.
    static std::vector<std::uint8_t> fetchGametdbCover(net::NetService& net,
                                                       std::string_view title,
                                                       const std::atomic<bool>* cancel = nullptr);

    static std::vector<std::uint8_t> fetchNlibBanner(net::NetService& net,
                                                     std::uint64_t titleId,
                                                     int width = 1280, int height = 720,
                                                     const std::atomic<bool>* cancel = nullptr);

    /// Candidate search for the interactive picker: SteamGridDB + GameTDB + nlib.
    static SteamGridDbManager::BrowseResult browseAll(
        net::NetService& net,
        const std::string& apiKey,
        std::uint64_t titleId,
        const std::string& title,
        const std::string& query,
        SteamGridDbManager::ArtworkKind kind);

    /// Applies a candidate selected from the picker (downloads & prepares cache).
    static bool applyCandidate(
        net::NetService& net,
        const SteamGridDbManager::BrowseResult& browse,
        const SteamGridDbManager::Candidate& candidate,
        const std::atomic<bool>* cancel = nullptr,
        const ProgressCallback& onProgress = {});

    /// Downloads missing pictures for a single title in the background.
    struct SingleTitleResult {
        bool cover = false;
        bool hero = false;
        bool logo = false;
    };
    static SingleTitleResult downloadTitleArt(
        net::NetService& net,
        const std::string& apiKey,
        std::uint64_t titleId,
        const std::string& title,
        bool boxArt = true,
        bool titlePics = true,
        bool force = false,
        const std::atomic<bool>* cancel = nullptr);

    /// Batch operation for Settings > Pictures > Save every picture now / Download all again.
    static void runSaveAll(
        net::NetService& net,
        const std::string& apiKey,
        const std::vector<AppEntry>& apps,
        bool boxArt,
        bool titlePics,
        bool forceAll,
        std::atomic<int>& done,
        std::atomic<int>& total,
        std::atomic<int>& saved,
        std::atomic<bool>& stop);
};

} // namespace cards
