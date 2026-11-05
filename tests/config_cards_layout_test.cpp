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

    std::system("rm -rf 'sdmc:'");
    std::printf("PASS: config_cards_layout_test passed all assertions!\n");
    return 0;
}
