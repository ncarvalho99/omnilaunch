#include "core/PlayTime.hpp"
#include "core/Config.hpp"
#include "core/DebugLog.hpp"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <map>
#include <switch.h>

namespace switchu::menu::playtime {
namespace {
constexpr std::uint64_t kNanosecondsPerMinute = 60000000000ULL;
std::mutex g_pdmMutex;
#ifdef SWITCHU_MENU
struct EventCache {
    Profile user{};
    s32 first = 0, last = -1, count = 0;
    bool attempted = false, valid = false;
    std::map<std::uint64_t, PlayTotal> totals;
};
EventCache g_events; // accessed only under queryMutex

AccountUid uidOf(Profile profile) { return AccountUid{{profile[0], profile[1]}}; }
bool valid(Profile profile) { return profile[0] != 0 || profile[1] != 0; }

// Reuse only while the raw log's range is unchanged. A completed game changes
// the range even inside the same minute; a stale scan cannot confirm its total.
bool readEvents(Profile user, const std::atomic<bool>* cancelled) {
    s32 countAvailable = 0, first = 0, last = -1;
    if (R_FAILED(pdmqryGetAvailablePlayEventRange(&countAvailable, &first, &last)) ||
        first < 0 || countAvailable <= 0 || last < first) {
        g_events.valid = false;
        return false;
    }
    if (g_events.attempted && g_events.user == user && g_events.first == first &&
        g_events.last == last && g_events.count == countAvailable)
        return g_events.valid;
    const auto now = armGetSystemTick();
    g_events = {};
    g_events.user = user;
    g_events.attempted = true;
    g_events.first = first;
    g_events.last = last;
    g_events.count = countAvailable;
    constexpr s32 kChunk = 256;
    constexpr std::uint64_t kMaxEvents = 1'000'000, kMaxNs = 2'000'000'000ULL;
    std::vector<PdmPlayEvent> events(kChunk);
    EventTotals totals(user);
    std::uint64_t offset = static_cast<std::uint64_t>(first);
    std::uint64_t scanned = 0;
    bool ok = false;
    for (;;) {
        if (scanned >= kMaxEvents || armTicksToNs(armGetSystemTick() - now) > kMaxNs ||
            (cancelled && cancelled->load())) break;
        s32 count = 0;
        const s32 requested = static_cast<s32>(std::min<std::uint64_t>(
            kChunk, static_cast<std::uint64_t>(last) - offset + 1));
        const Result rc = pdmqryQueryPlayEvent(static_cast<s32>(offset), events.data(), requested, &count);
        if (R_FAILED(rc) || count <= 0 || count > requested) break;
        offset += static_cast<std::uint64_t>(count);
        scanned += static_cast<std::uint64_t>(count);
        for (s32 i = 0; i < count; ++i) {
            const auto& event = events[static_cast<std::size_t>(i)];
            if (event.play_event_type == PdmPlayEventType_Initialize) {
                totals.push({PlayEvent::Kind::Reset});
            } else if (event.play_event_type == PdmPlayEventType_Account) {
                const auto& a = event.event_data.account;
                if (a.type <= 1)
                    totals.push({PlayEvent::Kind::Account, 0,
                        {(std::uint64_t{a.uid[0]} << 32) | a.uid[1],
                         (std::uint64_t{a.uid[2]} << 32) | a.uid[3]}});
            } else if (event.play_event_type == PdmPlayEventType_Applet) {
                const auto& a = event.event_data.applet;
                // System/library applets must not terminate an application's session.
                if (a.applet_id != AppletId_application || a.log_policy != PdmPlayLogPolicy_All) continue;
                PlayEvent::Kind kind;
                switch (a.event_type) {
                case PdmAppletEventType_Launch: kind = PlayEvent::Kind::Launch; break;
                case PdmAppletEventType_InFocus: kind = PlayEvent::Kind::InFocus; break;
                case PdmAppletEventType_OutOfFocus:
                case PdmAppletEventType_OutOfFocus4: kind = PlayEvent::Kind::OutOfFocus; break;
                case PdmAppletEventType_Exit:
                case PdmAppletEventType_Exit5:
                case PdmAppletEventType_Exit6: kind = PlayEvent::Kind::Exit; break;
                default: continue;
                }
                totals.push({kind, (std::uint64_t{a.program_id[0]} << 32) | a.program_id[1],
                             {}, event.timestamp_steady});
            }
        }
        if (offset > static_cast<std::uint64_t>(last)) { ok = true; break; }
    }
    if (ok) {
        s32 latestCount = 0, latestFirst = 0, latestLast = -1;
        ok = R_SUCCEEDED(pdmqryGetAvailablePlayEventRange(&latestCount, &latestFirst, &latestLast)) &&
             latestCount == countAvailable && latestFirst == first && latestLast == last;
    }
    // Never publish a truncated/cancelled/changing scan. PDM statistics remain the fallback.
    if (ok) g_events.totals = totals.values();
    g_events.valid = ok;
    if (cancelled && cancelled->load()) g_events.attempted = false;
    DebugLog::log("[playtime] events=%llu ms=%llu complete=%d",
        static_cast<unsigned long long>(scanned),
        static_cast<unsigned long long>(armTicksToNs(armGetSystemTick() - now) / 1000000), ok);
    return ok;
}

Result readStatistics(std::uint64_t id, Profile user, PdmPlayStatistics& stats,
                      const std::atomic<bool>* cancelled, bool useEvents) {
    const bool perUser = valid(user);
    Result rc = perUser
        ? pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(id, uidOf(user), true, &stats)
        : pdmqryQueryPlayStatisticsByApplicationId(id, true, &stats);
    // Never label another profile's totals as this user's when its query fails.
    if (R_FAILED(rc) || !perUser) return rc;
    if (useEvents) {
        if (!readEvents(user, cancelled)) return rc;
    } else if (!g_events.valid || g_events.user != user) {
        return rc;
    }
    const auto it = g_events.totals.find(id);
    // The raw log can roll over. Even a successful scan is not lifetime
    // coverage unless its completed launch count agrees with the statistics.
    if (it != g_events.totals.end() && it->second.complete &&
        stats.first_entry_index >= static_cast<std::uint32_t>(g_events.first) &&
        stats.last_entry_index <= static_cast<std::uint32_t>(g_events.last) &&
        it->second.launches == stats.total_launches &&
        it->second.seconds <= std::numeric_limits<std::uint64_t>::max() / 1'000'000'000ULL) {
        stats.playtime = it->second.seconds * 1'000'000'000ULL;
        stats.total_launches = it->second.launches;
    }
    return rc;
}

std::optional<PdmPlayStatistics> one(std::uint64_t id, Profile user, bool useEvents) {
    if (!id) return std::nullopt;
    std::lock_guard<std::mutex> guard(g_pdmMutex);
    if (R_FAILED(pdmqryInitialize())) return std::nullopt;
    PdmPlayStatistics stats{};
    const Result rc = readStatistics(id, user, stats, nullptr, useEvents);
    pdmqryExit();
    if (R_FAILED(rc)) return std::nullopt;
    return stats;
}
#endif
} // namespace

std::mutex& queryMutex() { return g_pdmMutex; }

Profile currentProfile(const AppConfig& config) {
#ifdef SWITCHU_MENU
    AccountUid uid{};
    if (config.defaultProfileEnabled && config.defaultProfileUid.size() == 32 &&
        std::all_of(config.defaultProfileUid.begin(), config.defaultProfileUid.end(), [](char ch) {
            return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        })) {
        unsigned long long a = 0, b = 0;
        if (std::sscanf(config.defaultProfileUid.c_str(), "%16llx%16llx", &a, &b) == 2) {
            uid.uid[0] = a; uid.uid[1] = b;
            AccountProfile profile{};
            if (accountUidIsValid(&uid) && R_SUCCEEDED(accountGetProfile(&profile, uid))) {
                accountProfileClose(&profile);
                return {uid.uid[0], uid.uid[1]};
            }
        }
    }
    if (R_SUCCEEDED(accountGetLastOpenedUser(&uid)) && accountUidIsValid(&uid))
        return {uid.uid[0], uid.uid[1]};
    AccountUid users[8]{};
    s32 count = 0;
    if (R_SUCCEEDED(accountListAllUsers(users, 8, &count)) && count > 0 && accountUidIsValid(&users[0]))
        return {users[0].uid[0], users[0].uid[1]};
#else
    (void)config;
#endif
    return {};
}

std::string profileKey(Profile user) {
    char key[64];
    std::snprintf(key, sizeof(key), "events-v1:%016llX%016llX",
        static_cast<unsigned long long>(user[0]), static_cast<unsigned long long>(user[1]));
    return key;
}

std::optional<std::uint64_t> query(std::uint64_t id, Profile user) {
#ifdef SWITCHU_MENU
    // One-off UI queries use statistics, never start a full log scan.
    if (auto s = one(id, user, false)) return s->playtime;
#else
    (void)id; (void)user;
#endif
    return std::nullopt;
}
std::optional<std::uint64_t> queryLastPlayed(std::uint64_t id, Profile user) {
#ifdef SWITCHU_MENU
    if (auto s = one(id, user, false)) return s->last_timestamp_user;
#else
    (void)id; (void)user;
#endif
    return std::nullopt;
}
std::optional<std::uint32_t> queryLaunches(std::uint64_t id, Profile user) {
#ifdef SWITCHU_MENU
    if (auto s = one(id, user, true)) return s->total_launches;
#else
    (void)id; (void)user;
#endif
    return std::nullopt;
}
std::vector<std::pair<std::uint64_t, std::uint64_t>> queryAll(
    const std::vector<std::uint64_t>& ids, const std::atomic<bool>* cancelled, Profile user) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> result;
#ifdef SWITCHU_MENU
    if (ids.empty()) return result;
    std::unique_lock<std::mutex> guard(g_pdmMutex, std::defer_lock);
    while (!guard.try_lock()) {
        if (cancelled && cancelled->load()) return result;
        svcSleepThread(5'000'000);
    }
    if (cancelled && cancelled->load()) return result;
    if (R_FAILED(pdmqryInitialize())) return result;
    if (valid(user)) readEvents(user, cancelled);
    for (auto id : ids) {
        if (cancelled && cancelled->load()) break;
        if (!id) continue;
        PdmPlayStatistics stats{};
        if (R_SUCCEEDED(readStatistics(id, user, stats, cancelled, false)))
            result.emplace_back(id, stats.playtime);
    }
    pdmqryExit();
    DebugLog::log("[playtime] batch asked=%zu answered=%zu", ids.size(), result.size());
#else
    (void)ids; (void)cancelled; (void)user;
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
