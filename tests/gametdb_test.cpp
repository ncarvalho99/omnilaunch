#include "cards/GameTdbIndex.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

#define TEST_CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

int main() {
    using cards::GameTdbIndex;

    // 1. Name key normalization
    TEST_CHECK(GameTdbIndex::nameKey("Mario Kart 8 Deluxe") == "mario kart 8 deluxe");
    TEST_CHECK(GameTdbIndex::nameKey("Pokémon: Let's Go, Pikachu!") == "pokemon let s go pikachu");
    TEST_CHECK(GameTdbIndex::nameKey("Super Mario 3D World + Bowser's Fury") == "super mario 3d world bowser s fury");
    TEST_CHECK(GameTdbIndex::nameKey("   THE   LEGEND   OF   ZELDA   ") == "the legend of zelda");
    TEST_CHECK(GameTdbIndex::nameKey("HÉLLÖ WÔRLD 123") == "hello world 123");

    // 2. Lookup against romfs/data/gametdb.tsv
    const std::string tsv = "romfs/data/gametdb.tsv";
    auto mk8 = GameTdbIndex::lookup("Mario Kart 8 Deluxe", tsv);
    TEST_CHECK(mk8.has_value());
    TEST_CHECK(mk8->id == "AABPA");
    TEST_CHECK(mk8->region == 'A');

    auto cel = GameTdbIndex::lookup("Celeste", tsv);
    TEST_CHECK(cel.has_value());
    TEST_CHECK(cel->id == "ACF3A");

    auto fake = GameTdbIndex::lookup("Nonexistent Game 99999", tsv);
    TEST_CHECK(!fake.has_value());

    // 3. Cover URL regional attempts
    TEST_CHECK(GameTdbIndex::coverUrl("AABPA", 'A', 0) == "https://art.gametdb.com/switch/coverM/US/AABPA.jpg");
    TEST_CHECK(GameTdbIndex::coverUrl("AABPA", 'A', 1) == "https://art.gametdb.com/switch/coverM/EN/AABPA.jpg");
    TEST_CHECK(GameTdbIndex::coverUrl("AABPA", 'A', 2) == "https://art.gametdb.com/switch/coverM/JA/AABPA.jpg");
    TEST_CHECK(GameTdbIndex::coverUrl("AABPA", 'A', 3).empty());

    // 4. Memory unload
    TEST_CHECK(GameTdbIndex::isLoaded());
    GameTdbIndex::unload();
    TEST_CHECK(!GameTdbIndex::isLoaded());

    std::printf("PASS: gametdb_test passed all assertions!\n");
    return 0;
}
