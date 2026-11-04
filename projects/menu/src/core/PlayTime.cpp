#include "core/PlayTime.hpp"
#include "DebugLog.hpp"
#include <mutex>

#include <switch.h>

namespace switchu::menu::playtime {

namespace {

constexpr std::uint64_t kNanosecondsPerMinute = 60000000000ULL;
#ifdef SWITCHU_MENU
// pdm:qry has a single global session in libnx. Serialize queries so the
// details panel's one-off lookup cannot race an ongoing batch.
std::mutex g_pdmMutex;
#endif

} // namespace

std::optional<std::uint64_t> query(std::uint64_t titleId) {
#ifdef SWITCHU_MENU
    if (titleId == 0)
        return std::nullopt;
    std::lock_guard<std::mutex> lock(g_pdmMutex);
    const Result initRc = pdmqryInitialize();
    if (R_FAILED(initRc)) {
        DebugLog::log("[playtime] pdmqry init failed title=%016llX rc=0x%X",
                      static_cast<unsigned long long>(titleId), initRc);
        return std::nullopt;
    }
    PdmPlayStatistics stats{};
    const Result queryRc = pdmqryQueryPlayStatisticsByApplicationId(titleId, true, &stats);
    pdmqryExit();
    if (R_FAILED(queryRc)) {
        DebugLog::log("[playtime] query failed title=%016llX rc=0x%X",
                      static_cast<unsigned long long>(titleId), queryRc);
        return std::nullopt;
    }
    return stats.playtime;
#else
    (void)titleId;
    return std::nullopt;
#endif
}

std::optional<std::uint64_t> queryLastPlayed(std::uint64_t titleId) {
#ifdef SWITCHU_MENU
    if (titleId == 0)
        return std::nullopt;
    if (R_FAILED(pdmqryInitialize()))
        return std::nullopt;
    PdmPlayStatistics stats{};
    const Result rc = pdmqryQueryPlayStatisticsByApplicationId(titleId, true, &stats);
    pdmqryExit();
    if (R_FAILED(rc))
        return std::nullopt;
    if (stats.last_timestamp_user == 0)
        return std::uint64_t{0};
    // libnx converts V1 to PdmPlayStatistics, so this field is always POSIX
    // seconds (docs: "in PosixTime").
    return stats.last_timestamp_user;
#else
    (void)titleId;
    return std::nullopt;
#endif
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> queryAll(
    const std::vector<std::uint64_t>& titleIds,
    const std::atomic<bool>* cancelled) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> result;
#ifdef SWITCHU_MENU
    if (titleIds.empty())
        return result;
    result.reserve(titleIds.size());
    int failed = 0;
    const std::uint64_t startTick = armGetSystemTick();
    bool stopped = false;
    std::unique_lock<std::mutex> lock(g_pdmMutex);
    const Result initRc = pdmqryInitialize();
    if (R_FAILED(initRc)) {
        DebugLog::log("[playtime] pdmqry init failed for batch of %zu rc=0x%X",
                      titleIds.size(), initRc);
        return result;
    }
    for (const std::uint64_t titleId : titleIds) {
        if (cancelled && cancelled->load()) {
            stopped = true;
            break;
        }
        if (titleId == 0)
            continue;
        PdmPlayStatistics stats{};
        const Result rc = pdmqryQueryPlayStatisticsByApplicationId(titleId, true, &stats);
        if (R_FAILED(rc)) {
            ++failed;
            DebugLog::log("[playtime] query failed title=%016llX rc=0x%X",
                          static_cast<unsigned long long>(titleId), rc);
            continue;
        }
        result.emplace_back(titleId, stats.playtime);
    }
    pdmqryExit();
    const std::uint64_t elapsedMs =
        armTicksToNs(armGetSystemTick() - startTick) / 1000000ULL;
    DebugLog::log("[playtime] batch queried=%zu answered=%zu failed=%d stopped=%d in %llums",
                  titleIds.size(), result.size(), failed, stopped ? 1 : 0,
                  static_cast<unsigned long long>(elapsedMs));
#else
    (void)titleIds;
#endif
    return result;
}

std::string format(std::uint64_t nanoseconds) {
    const std::uint64_t minutes = nanoseconds / kNanosecondsPerMinute;
    if (nanoseconds == 0)
        return {};
    if (minutes >= 60)
        return std::to_string(minutes / 60) + " h " + std::to_string(minutes % 60) + " min";
    return std::to_string(minutes) + " min";
}

std::string formatCompact(std::uint64_t nanoseconds) {
    const std::uint64_t minutes = nanoseconds / kNanosecondsPerMinute;
    if (nanoseconds == 0)
        return {};
    if (minutes >= 60)
        return std::to_string(minutes / 60) + " h";
    return std::to_string(minutes) + " min";
}

} // namespace switchu::menu::playtime
