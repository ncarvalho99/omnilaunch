#pragma once
// Nextendo catalog and online counts parser (HomeNX H3, C-NET-3).
// Pure functions with host test coverage.

#include <cstdint>
#include <map>
#include <string>

namespace nextendo {

constexpr const char* kNextendoUrlPrimary = "https://nextendo.network/api/online-counts";
constexpr const char* kNextendoUrlFallback = "https://nextendo.network/service-catalog/v1/online-counts";
constexpr const char* kNextendoNroDefault = "sdmc:/switch/nextendo.nro";
constexpr const char* kNextendoPreludeNro = "sdmc:/switch/prelude.nro";
constexpr const char* kNextendoDownloadUrl =
    "https://git.nextendo.network/NextendoNetwork/Prelude-Nextendo/releases/download/v5.0.0/nextendo.nro";

struct SpecificEndpoint {
    const char* path;
    std::uint64_t titleId;
    bool needsOnline;
};

const SpecificEndpoint kSpecificEndpoints[] = {
    {"/game-stats/sm3dw", 0x010028600EBDA000ULL, false},
    {"/game-stats/jamboree", 0x0100965017338000ULL, true},
    {"/game-stats/stardew", 0x0100E65002BB8000ULL, true},
    {"/game-stats/kirby", 0x0100A8E016236000ULL, true},
    {"/game-stats/justdance22", 0x0100EA6014BB8000ULL, true},
    {"/game-stats/uno-count", 0x0100B6E012EBE000ULL, false},
};

/// Parse Nextendo online-counts JSON response into titleId->count map and total.
bool parseOnlineCounts(const std::string& json, std::map<std::uint64_t, int>& outCounts, int& outTotal);

} // namespace nextendo
