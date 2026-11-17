#include "core/Config.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

static nlohmann::json readCfg(const char* path) {
    std::ifstream f(path);
    nlohmann::json j;
    f >> j;
    return j;
}

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "Assertion failed: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

int main() {
    std::system("mkdir -p 'sdmc:/config/OmniLaunch'");
    const char* kPath = AppConfig::kConfigPath;

    // 1. Verify migration of legacy Cards layout mode to Grid (HomeNX I3 / D2)
    {
        nlohmann::json j;
        j["appLayoutMode"] = "cards";
        std::ofstream f(kPath);
        f << j.dump(2);
        f.close();

        AppConfig loaded;
        TEST_ASSERT(loaded.load());
        TEST_ASSERT(loaded.appLayoutMode == AppLayoutMode::Grid);

        // Next save writes "grid", completing migration on disk
        TEST_ASSERT(loaded.save());
        auto migrated = readCfg(kPath);
        TEST_ASSERT(migrated["appLayoutMode"] == "grid");
    }

    // 2. Verify all valid layout modes round-trip cleanly without regression
    struct TestCase {
        AppLayoutMode mode;
        const char* expectedStr;
    };

    const TestCase cases[] = {
        {AppLayoutMode::Grid, "grid"},
        {AppLayoutMode::DynamicLine, "dynamic_line"},
        {AppLayoutMode::Flow, "flow"},
        {AppLayoutMode::Shelf, "shelf"},
        {AppLayoutMode::Deck, "deck"},
        {AppLayoutMode::Cover, "cover"},
        {AppLayoutMode::Xmb, "xmb"},
        {AppLayoutMode::List, "list"},
        {AppLayoutMode::Metro, "metro"},
    };

    for (const auto& tc : cases) {
        AppConfig cfg;
        cfg.appLayoutMode = tc.mode;
        TEST_ASSERT(cfg.save());

        auto j = readCfg(kPath);
        TEST_ASSERT(j["appLayoutMode"] == tc.expectedStr);

        AppConfig roundtrip;
        TEST_ASSERT(roundtrip.load());
        TEST_ASSERT(roundtrip.appLayoutMode == tc.mode);
    }

    // 4. Verify unknown layout strings fall back to Grid safely
    {
        nlohmann::json j = readCfg(kPath);
        j["appLayoutMode"] = "unknown_future_layout";
        std::ofstream f(kPath);
        f << j.dump(2);
        f.close();

        AppConfig fallback;
        TEST_ASSERT(fallback.load());
        TEST_ASSERT(fallback.appLayoutMode == AppLayoutMode::Grid);
    }

    // 5. Verify cards.panelButtonEnabled and panelTitleId serialization
    {
        AppConfig cfg;
        TEST_ASSERT(cfg.cards.panelButtonEnabled == true);
        TEST_ASSERT(cfg.cards.panelTitleId == 0);

        cfg.cards.panelTitleId = 0x0100EA80032EA000ULL;
        cfg.cards.panelButtonEnabled = false;
        TEST_ASSERT(cfg.save());

        auto j = readCfg(kPath);
        TEST_ASSERT(j["cards"]["panelButtonEnabled"] == false);
        TEST_ASSERT(!j["cards"]["panelTitle"].empty());

        AppConfig loaded;
        TEST_ASSERT(loaded.load());
        TEST_ASSERT(loaded.cards.panelTitleId == 0x0100EA80032EA000ULL);
        TEST_ASSERT(loaded.cards.panelButtonEnabled == false);
    }

    // 6. Sidebar order must be a permutation of 0..9; a swap round-trips, a
    //    duplicate (one button twice, another hidden) falls back to default.
    {
        AppConfig cfg;
        cfg.sidebarOrder = {0, 2, 1, 3, 4, 5, 6, 7, 8, 9};
        TEST_ASSERT(cfg.save());
        AppConfig loaded;
        TEST_ASSERT(loaded.load());
        TEST_ASSERT((loaded.sidebarOrder == std::vector<int>{0, 2, 1, 3, 4, 5, 6, 7, 8, 9}));

        nlohmann::json j = readCfg(kPath);
        j["sidebarOrder"] = {0, 2, 2, 3, 4, 5, 6, 7, 8, 9};
        std::ofstream(kPath) << j.dump(2);
        AppConfig dup;
        TEST_ASSERT(dup.load());
        TEST_ASSERT((dup.sidebarOrder == std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    }

    // 7. SteamGridDB per-view flags default on and round-trip.
    {
        AppConfig cfg;
        TEST_ASSERT(cfg.steamGridDbShowInGrid && cfg.steamGridDbShowInDynamicLine && cfg.steamGridDbShowInFolders);
        cfg.steamGridDbShowInDynamicLine = false;
        cfg.steamGridDbShowInFolders = false;
        TEST_ASSERT(cfg.save());
        AppConfig loaded;
        TEST_ASSERT(loaded.load());
        TEST_ASSERT(loaded.steamGridDbShowInGrid);
        TEST_ASSERT(!loaded.steamGridDbShowInDynamicLine);
        TEST_ASSERT(!loaded.steamGridDbShowInFolders);
    }

    // 8. Safe mode: snapshotForSave preserves touched status across copies and saves.
    {
        AppConfig cfg;
        cfg.musicEnabled = true;
        TEST_ASSERT(cfg.save());

        AppConfig safeCfg;
        TEST_ASSERT(safeCfg.load());
        safeCfg.enterSafeMode();
        TEST_ASSERT(safeCfg.safeMode() == true);
        TEST_ASSERT(safeCfg.musicEnabled == false); // overridden in safe mode

        // Player toggles music on
        safeCfg.musicEnabled = true;
        AppConfig snap = safeCfg.snapshotForSave();
        TEST_ASSERT(snap.save());

        // Player now toggles music back off
        safeCfg.musicEnabled = false;
        AppConfig snap2 = safeCfg.snapshotForSave();
        TEST_ASSERT(snap2.save());

        AppConfig reloaded;
        TEST_ASSERT(reloaded.load());
        // Since the player explicitly touched and chose off, it must remain off on disk
        TEST_ASSERT(reloaded.musicEnabled == false);
    }

    std::system("rm -rf 'sdmc:'");
    std::printf("PASS: config_cards_layout_test passed all assertions!\n");
    return 0;
}
