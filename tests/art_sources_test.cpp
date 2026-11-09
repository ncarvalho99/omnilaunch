#define STB_IMAGE_IMPLEMENTATION
#include <nxui/third_party/stb/stb_image.h>

#include "cards/CardsArtSources.hpp"
#include "cards/GameTdbIndex.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

// Stubs for offline host test
namespace net {
NetService::~NetService() = default;
Response NetService::request(const Request&) {
    return Response{};
}
}

SteamGridDbManager::BrowseResult SteamGridDbManager::browse(
    const std::string&, std::uint64_t titleId, const std::string& title,
    const std::string& query, ArtworkKind kind) {
    BrowseResult r;
    r.titleId = titleId;
    r.title = title;
    r.query = query;
    r.kind = kind;
    r.success = false;
    return r;
}

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

int main() {
    using cards::CardsArtSources;
    using cards::GameTdbIndex;

    const std::uint64_t kTestId = 0x0100152000022000ULL; // Mario Kart 8 Deluxe

    // 1. Hex ID formatting
    TEST_CHECK(CardsArtSources::hexId(kTestId) == "0100152000022000");
    TEST_CHECK(CardsArtSources::hexId(0x0100000000010000ULL) == "0100000000010000");

    // 2. URL builders
    const std::string tsv = "romfs/data/gametdb.tsv";
    auto entry = GameTdbIndex::lookup("Mario Kart 8 Deluxe", tsv);
    TEST_CHECK(entry.has_value());
    TEST_CHECK(entry->id == "AABPA");

    const std::string gUrl = CardsArtSources::gametdbCoverUrl("Mario Kart 8 Deluxe", 0);
    TEST_CHECK(gUrl == "https://art.gametdb.com/switch/coverM/US/AABPA.jpg");

    const std::string nlibWide = CardsArtSources::nlibBannerUrl(kTestId, 1280, 720);
    TEST_CHECK(nlibWide == "https://api.nlib.cc/nx/0100152000022000/banner/1280/720");

    const std::string nlibCard = CardsArtSources::nlibBannerUrl(kTestId, 640, 360);
    TEST_CHECK(nlibCard == "https://api.nlib.cc/nx/0100152000022000/banner/640/360");

    // 3. Destinations
    TEST_CHECK(CardsArtSources::coverDestination(kTestId) == "sdmc:/config/OmniLaunch/covers/0100152000022000.jpg");
    TEST_CHECK(CardsArtSources::heroDestination(kTestId) == "sdmc:/config/OmniLaunch/steamgriddb/0100152000022000/hero.img");
    TEST_CHECK(CardsArtSources::logoDestination(kTestId) == "sdmc:/config/OmniLaunch/steamgriddb/0100152000022000/logo.img");

    // 4. File I/O & picture validation (saving fake data vs valid picture data)
    std::system("mkdir -p 'sdmc:/config/OmniLaunch/covers'");
    std::system("mkdir -p 'sdmc:/config/OmniLaunch/steamgriddb/0100152000022000'");

    std::vector<std::uint8_t> notAPic = {0x00, 0x01, 0x02};
    TEST_CHECK(!CardsArtSources::saveCover(kTestId, notAPic));

    // Valid minimal JPEG header
    std::vector<std::uint8_t> fakeJpeg(128, 0x00);
    fakeJpeg[0] = 0xFF;
    fakeJpeg[1] = 0xD8;
    fakeJpeg[2] = 0xFF;
    fakeJpeg[3] = 0xE0;
    TEST_CHECK(CardsArtSources::saveCover(kTestId, fakeJpeg));
    TEST_CHECK(CardsArtSources::hasCover(kTestId));

    CardsArtSources::forgetArt(kTestId);
    TEST_CHECK(!CardsArtSources::hasCover(kTestId));

    // 5. Candidate injection into browseAll
    net::NetService net;
    auto browse = CardsArtSources::browseAll(net, "", kTestId, "Mario Kart 8 Deluxe", "",
                                            SteamGridDbManager::ArtworkKind::Cover);
    TEST_CHECK(browse.success);
    TEST_CHECK(!browse.candidates.empty());
    // Candidate 1 must be GameTDB
    TEST_CHECK(browse.candidates[0].url.find("art.gametdb.com") != std::string::npos);
    // Candidate 2 must be nlib.cc
    TEST_CHECK(browse.candidates.size() >= 2);
    TEST_CHECK(browse.candidates[1].url.find("api.nlib.cc") != std::string::npos);

    std::system("rm -rf 'sdmc:'");
    std::printf("PASS: art_sources_test passed all assertions!\n");
    return 0;
}
