// Host test for HomeNX batch 1 pure logic: page order/visibility (E1),
// hand order (E2), Media assignment, the "cards" config object round trip and
// legacy-config defaults, and achievement milestones (E11).
//
// docker run --rm --network none -v "${PWD}:/src:ro" -v switchu-xmake:/root/.xmake:ro -w /src
//   --entrypoint sh switchu-build:latest -c 'g++ -std=c++20 -Wall -Wextra -Iprojects/menu/src
//   -Iprojects/common/include -I<xmake nlohmann_json include> -idirafter lib/nxui/include
//   tests/cards_pages_test.cpp projects/menu/src/cards/CardsPages.cpp
//   projects/menu/src/cards/CardsAchievements.cpp projects/menu/src/core/Config.cpp -o /tmp/t && cd /tmp && ./t'
// (nlohmann_json comes from the switchu-xmake volume: /root/.xmake/packages/n/nlohmann_json/<ver>/<hash>/include)

#include "cards/CardsAchievements.hpp"
#include "cards/CardsPages.hpp"
#include "core/Config.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <sstream>
#include <streambuf>
#include <nlohmann/json.hpp>

#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

using Prefs = AppConfig::CardsPrefs;
namespace pg = cards::pages;

static void pagesTest() {
    Prefs p;
    p.normalize();
    CHECK((p.pageOrder == std::vector<int>{Prefs::kGames, Prefs::kNintendo, Prefs::kMedia}));
    CHECK(pg::first(p) == Prefs::kGames);
    CHECK(pg::step(p, Prefs::kGames, 1) == Prefs::kNintendo);
    CHECK(pg::step(p, Prefs::kGames, -1) == Prefs::kMedia); // wraps
    CHECK(pg::step(p, Prefs::kMedia, 1) == Prefs::kGames);

    // Hidden pages are skipped; the last shown page cannot be hidden.
    CHECK(pg::togglePage(p, Prefs::kNintendo));
    CHECK(pg::step(p, Prefs::kGames, 1) == Prefs::kMedia);
    CHECK(pg::togglePage(p, Prefs::kMedia));
    CHECK(!pg::togglePage(p, Prefs::kGames));
    CHECK(pg::shown(p).size() == 1);
    CHECK(pg::step(p, Prefs::kGames, 1) == Prefs::kGames);
    CHECK(pg::togglePage(p, Prefs::kMedia)); // shows again

    // Order: the first shown page is where Cards opens.
    CHECK(pg::movePage(p, Prefs::kMedia, -1));
    CHECK(pg::movePage(p, Prefs::kMedia, -1));
    CHECK(!pg::movePage(p, Prefs::kMedia, -1));
    CHECK(pg::first(p) == Prefs::kMedia);

    // normalize: junk, duplicates, Homebrew (not offered yet), all hidden.
    Prefs q;
    q.pageOrder = {3, 3, 9, 1, 0};
    q.hiddenPages = {0, 2, 3, 3, 7};
    q.normalize();
    CHECK((q.pageOrder == std::vector<int>{Prefs::kMedia, Prefs::kGames, Prefs::kNintendo}));
    CHECK(pg::shown(q).size() == 1 && pg::shown(q).front() == Prefs::kMedia);
}

static void orderTest() {
    const std::vector<std::uint64_t> ids = {10, 20, 30, 40};
    CHECK((pg::ordered(ids, {30, 10}) == std::vector<std::uint64_t>{30, 10, 20, 40}));
    CHECK((pg::ordered(ids, {}) == ids));
    CHECK(pg::rank({5, 6}, 6) == 1 && pg::rank({5, 6}, 7) == 2);

    Prefs p;
    CHECK(pg::setMedia(p, 0x0100AAAA00000000ULL, true));
    CHECK(!pg::setMedia(p, 0x0100AAAA00000000ULL, true));
    CHECK(p.isMedia(0x0100AAAA00000000ULL));
    p.mediaOrder = {0x0100AAAA00000000ULL};
    CHECK(pg::setMedia(p, 0x0100AAAA00000000ULL, false));
    CHECK(p.mediaOrder.empty() && !p.isMedia(0x0100AAAA00000000ULL));

    CHECK(pg::isSystemId(pg::systemId(pg::SystemTile::Album)));
    CHECK(!pg::isSystemId(0x0100ABCD12340000ULL));
    CHECK(pg::systemTileOf(pg::systemId(pg::SystemTile::Internet)) == pg::SystemTile::Internet);
}

static void configTest() {
    std::filesystem::create_directories("sdmc:/config/OmniLaunch");
    const char* path = AppConfig::kConfigPath;
    std::filesystem::remove(AppConfig::kBackupPath);

    // A config from before batch 1 (no "cards", cardsSort 2) keeps its sort
    // and gets default pages.
    {
        nlohmann::json j;
        j["cardsSort"] = 2;
        j["homeStyle"] = "cards";
        std::ofstream(path) << j.dump(2);
        AppConfig c;
        CHECK(c.load());
        CHECK(c.cardsSort == 2);
        CHECK(pg::first(c.cards) == Prefs::kGames);
        CHECK(!c.cards.tutorialDone);
    }
    // Round trip of every field, and "my order" (3) is now valid.
    {
        AppConfig c;
        c.cardsSort = 3;
        c.cards.pageOrder = {Prefs::kNintendo, Prefs::kGames, Prefs::kMedia};
        c.cards.hiddenPages = {Prefs::kMedia};
        c.cards.gamesOrder = {0x0100000000010000ULL, 0x01007EF00011E000ULL};
        c.cards.mediaOrder = {0x01003A400C3DA000ULL};
        c.cards.mediaTitles = {0x01003A400C3DA000ULL};
        c.cards.settingsOrder = {"about", "home"};
        c.cards.tutorialDone = true;
        c.cards.panelTitleId = 0x0100000000010000ULL;
        c.cards.iconTitles = c.cards.gamesOrder;
        c.cards.coverBackgroundTitles = c.cards.gamesOrder;
        c.cards.plainBackgroundTitles = c.cards.mediaTitles;
        c.cards.logoTitles = c.cards.mediaTitles;
        CHECK(c.save());
        AppConfig d;
        CHECK(d.load());
        CHECK(d.cardsSort == 3);
        CHECK(d.cards.pageOrder == c.cards.pageOrder);
        CHECK(d.cards.hiddenPages == c.cards.hiddenPages);
        CHECK(d.cards.gamesOrder == c.cards.gamesOrder);
        CHECK(d.cards.mediaOrder == c.cards.mediaOrder);
        CHECK(d.cards.mediaTitles == c.cards.mediaTitles);
        CHECK(d.cards.settingsOrder == c.cards.settingsOrder);
        CHECK(d.cards.tutorialDone);
        CHECK(d.cards.panelTitleId == c.cards.panelTitleId);
        CHECK(d.cards.iconTitles == c.cards.iconTitles);
        CHECK(d.cards.coverBackgroundTitles == c.cards.coverBackgroundTitles);
        CHECK(d.cards.plainBackgroundTitles == c.cards.plainBackgroundTitles);
        CHECK(d.cards.logoTitles == c.cards.logoTitles);
        std::ifstream saved(path);
        nlohmann::json j; saved >> j;
        CHECK(j["cards"]["sort"] == 3);
        CHECK(!j.contains("cardsSort")); // migrated, stored once
        CHECK(pg::first(d.cards) == Prefs::kNintendo);
    }
    // Out-of-range sort and junk ids fall back safely.
    {
        nlohmann::json j;
        j["cardsSort"] = 9;
        j["cards"] = {{"gamesOrder", {"zz", "0", 5, "0100000000010000"}}, {"hiddenPages", "bad"}};
        std::ofstream(path) << j.dump(2);
        AppConfig c;
        CHECK(c.load());
        CHECK(c.cardsSort == 0);
        CHECK((c.cards.gamesOrder == std::vector<std::uint64_t>{0x0100000000010000ULL}));
        CHECK(c.cards.hiddenPages.empty());
    }
}

class BrokenRead : public std::streambuf {
    std::string prefix = "0100000000010000 first 1000\n";
public:
    BrokenRead() { char* p = prefix.data(); setg(p, p, p+prefix.size()); }
    int_type underflow() override { throw std::ios_base::failure("injected read failure"); }
};

static void achievementsTest() {
    namespace ach = cards::achievements;
    ach::Play p;
    p.seconds = 3600 * 12;
    p.launches = 12;
    ach::Earned e;
    const auto fresh = ach::record(e, 0x0100000000010000ULL, p, 1000);
    // first, h1, h10, l10 earned; h25/h50/h100/l50 not.
    CHECK(fresh.size() == 4);
    CHECK(ach::record(e, 0x0100000000010000ULL, p, 2000).empty());
    const std::string path = "achievements_test.txt";
    CHECK(ach::save(path, e));
    const auto back = ach::load(path);
    CHECK(back.when.size() == 4);
    CHECK(back.when.at(ach::key(0x0100000000010000ULL, "h10")) == 1000);
    CHECK(ach::progress(ach::kPerGame[3], p) > 0.47f && ach::progress(ach::kPerGame[3], p) < 0.49f);
    std::filesystem::remove(path);

    // Two successive transactions for two titles both survive (review finding 2).
    bool ok = false;
    CHECK(ach::recordAndSave(path, 0x0100000000010000ULL, p, 1000, ok).size() == 4 && ok);
    CHECK(ach::recordAndSave(path, 0x0100000000020000ULL, p, 2000, ok).size() == 4 && ok);
    CHECK(ach::recordAndSave(path, 0x0100000000020000ULL, p, 3000, ok).empty() && ok);
    CHECK(ach::load(path).when.size() == 8);
    // Only a backup left (crash between renames): it is recovered.
    std::filesystem::rename(path, path + ".bak");
    ach::Play more = p;
    more.seconds = 3600 * 30;
    CHECK(ach::recordAndSave(path, 0x0100000000010000ULL, more, 4000, ok).size() == 1 && ok);
    CHECK(ach::load(path).when.size() == 9);
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".bak");

    // Concurrent titles must merge under the same transaction lock.
    std::thread a([&] { bool good; ach::recordAndSave(path, 0x0100000000030000ULL, p, 5000, good); CHECK(good); });
    std::thread b([&] { bool good; ach::recordAndSave(path, 0x0100000000040000ULL, p, 6000, good); CHECK(good); });
    a.join(); b.join();
    CHECK(ach::load(path).when.size() == 8);
    // Failed staging open must keep both the old destination and its history.
    std::filesystem::create_directory(path + ".tmp");
    CHECK(ach::recordAndSave(path, 0x0100000000050000ULL, p, 7000, ok).size() == 4 && !ok);
    CHECK(ach::load(path).when.size() == 8);
    std::filesystem::remove(path + ".tmp");
    CHECK(ach::recordAndSave(path, 0x0100000000050000ULL, p, 7000, ok).size() == 4 && ok);
    CHECK(ach::load(path).when.size() == 12);
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".bak");
    // Read failure after the first valid record must not publish partial data.
    BrokenRead broken; std::istream failStream(&broken);
    ach::Earned sentinel; sentinel.when["sentinel"] = 42;
    CHECK(ach::readStream(failStream, sentinel) == ach::LoadStatus::Error);
    CHECK(sentinel.when.size() == 1 && sentinel.when.count("sentinel"));
    // Existing-but-unopenable history must not turn into a fresh successful store.
    std::filesystem::create_directory(path);
    CHECK(ach::recordAndSave(path, 0x0100000000050000ULL, p, 7000, ok).empty() && !ok);
    CHECK(std::filesystem::is_directory(path));
    std::filesystem::remove(path);
}

int main() {
    pagesTest();
    orderTest();
    configTest();
    achievementsTest();
    std::puts("cards_pages_test: PASS");
    return 0;
}
