#include "nextendo/NextendoCatalog.hpp"

#include <cassert>
#include <iostream>

using namespace nextendo;

int main() {
    // 1. Valid Nextendo counts payload
    const std::string validJson = R"({
        "counts": {
            "0100152000022000": 8,
            "01006a800016e000": 15,
            "0100b04011742000": 17
        },
        "jeux": [
            {
                "nom": "MONSTER HUNTER RISE",
                "joueurs": 17,
                "titres": ["0100b04011742000"]
            },
            {
                "nom": "Super Smash Bros. Ultimate",
                "joueurs": 15,
                "titres": ["01006a800016e000"]
            },
            {
                "nom": "Mario Kart 8 Deluxe",
                "joueurs": 8,
                "titres": ["0100152000022000"]
            }
        ]
    })";

    std::map<std::uint64_t, int> counts;
    int total = 0;
    bool ok = parseOnlineCounts(validJson, counts, total);
    assert(ok);
    assert(total == 40);
    assert(counts.size() == 3);
    assert(counts[0x0100152000022000ULL] == 8);
    assert(counts[0x01006A800016E000ULL] == 15);
    assert(counts[0x0100B04011742000ULL] == 17);

    // 2. Fallback when "counts" is empty but "jeux" has titles
    const std::string jeuxOnlyJson = R"({
        "jeux": [
            {
                "nom": "Game A",
                "joueurs": 5,
                "titres": ["0100123400000000"]
            }
        ]
    })";
    counts.clear();
    total = 0;
    ok = parseOnlineCounts(jeuxOnlyJson, counts, total);
    assert(ok);
    assert(total == 5);
    assert(counts[0x0100123400000000ULL] == 5);

    // 3. Malformed JSON
    counts.clear();
    total = 0;
    assert(!parseOnlineCounts("{ broken json", counts, total));
    assert(!parseOnlineCounts("[]", counts, total));
    assert(!parseOnlineCounts("{}", counts, total));

    std::cout << "nextendo_test: all passed" << std::endl;
    return 0;
}
