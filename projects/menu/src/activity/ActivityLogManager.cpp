#include "ActivityLogManager.hpp"
#include "core/DebugLog.hpp"
#include "core/PlayTime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>

#ifdef __SWITCH__
#include <switch.h>
#endif
#ifdef SWITCHU_MENU
#include <switchu/control_cache.hpp>
#endif

namespace switchu::activity {

namespace {

static std::mutex s_namesMutex;
static std::unordered_map<std::uint64_t, std::string> s_resolvedNames;
static std::unordered_map<std::uint64_t, std::uint64_t> s_canonicalAliases;
static bool s_mappingsInitialized = false;

struct KnownPortDef {
    std::uint64_t altId;
    std::uint64_t canonicalId;
    const char* name;
};

static const KnownPortDef kKnownPortDefs[] = {
    {0x05BC0A1F4DA64000ULL, 0x05BC0A1F4DA84000ULL, "Grand Theft Auto - San Andreas (Android Port)"},
    {0x05BC0A1F4DA84000ULL, 0x05BC0A1F4DA84000ULL, "Grand Theft Auto - San Andreas (Android Port)"},
    {0x05BECF2629BB0000ULL, 0x05BECF2629BA0000ULL, "Super Mario 64 - Render 96 (Port)"},
    {0x05BECF2629BA0000ULL, 0x05BECF2629BA0000ULL, "Super Mario 64 - Render 96 (Port)"},
    {0x05BECF2629BD0000ULL, 0x05BECF2629BC0000ULL, "Simpsons Hit & Run (Port)"},
    {0x05BECF2629BC0000ULL, 0x05BECF2629BC0000ULL, "Simpsons Hit & Run (Port)"},
    {0x05BECF2629B8C000ULL, 0x05BECF2629B8D000ULL, "The Legend of Zelda - Ocarina of Time (Port)"},
    {0x05BECF2629B8D000ULL, 0x05BECF2629B8D000ULL, "The Legend of Zelda - Ocarina of Time (Port)"},
    {0x05BECF2629BF0000ULL, 0x05BECF2629BF2000ULL, "The Legend of Zelda - Majoras Mask (Port)"},
    {0x05BECF2629BF2000ULL, 0x05BECF2629BF2000ULL, "The Legend of Zelda - Majoras Mask (Port)"},
    {0x0539210E01A62000ULL, 0x0539210E01B62000ULL, "The Legend of Zelda - Link's Awakening DX HD"},
    {0x0539210E01B62000ULL, 0x0539210E01B62000ULL, "The Legend of Zelda - Link's Awakening DX HD"},
    {0x05BECF2629C50000ULL, 0x05BECF2629C40000ULL, "The Legend of Zelda - A Link to the Past (Port)"},
    {0x05BECF2629C40000ULL, 0x05BECF2629C40000ULL, "The Legend of Zelda - A Link to the Past (Port)"},
    {0x054195EEBB4F2000ULL, 0x054195EEBB5F2000ULL, "The Legend of Zelda - Twilight Princess (Port)"},
    {0x054195EEBB5F2000ULL, 0x054195EEBB5F2000ULL, "The Legend of Zelda - Twilight Princess (Port)"},
    {0x05454A7359780000ULL, 0x05454A7359880000ULL, "Bully - Anniversary Edition (Port)"},
    {0x05454A7359880000ULL, 0x05454A7359880000ULL, "Bully - Anniversary Edition (Port)"},
    {0x052CC68B92306000ULL, 0x052CC68B92306000ULL, "Counter Strike Source (Android Port)"},
};

void initMappings(const std::vector<std::pair<std::uint64_t, std::string>>& installed) {
    std::lock_guard<std::mutex> lock(s_namesMutex);
    for (const auto& d : kKnownPortDefs) {
        s_canonicalAliases[d.altId] = d.canonicalId;
        if (s_resolvedNames.find(d.altId) == s_resolvedNames.end()) {
            s_resolvedNames[d.altId] = d.name;
        }
        if (s_resolvedNames.find(d.canonicalId) == s_resolvedNames.end()) {
            s_resolvedNames[d.canonicalId] = d.name;
        }
    }

    // Load sdmc:/switch/DBI/dbi.titles if present
    std::ifstream dbiFile("sdmc:/switch/DBI/dbi.titles");
    if (dbiFile.is_open()) {
        std::string line;
        while (std::getline(dbiFile, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos || eq < 16) continue;
            std::string hexStr = line.substr(0, eq);
            std::string name = line.substr(eq + 1);
            while (!name.empty() && (name.back() == '\r' || name.back() == '\n' || name.back() == ' '))
                name.pop_back();
            while (!name.empty() && name.front() == ' ')
                name.erase(name.begin());
            if (name.empty()) continue;
            char* end = nullptr;
            std::uint64_t tid = std::strtoull(hexStr.c_str(), &end, 16);
            if (tid != 0 && end != hexStr.c_str()) {
                s_resolvedNames[tid] = name;
            }
        }
    }

    // Map any title with identical name to the installed title ID
    for (const auto& [instId, instName] : installed) {
        if (instId == 0 || instName.empty()) continue;
        s_resolvedNames[instId] = instName;
        s_canonicalAliases[instId] = instId;

        for (const auto& [otherId, otherName] : s_resolvedNames) {
            if (otherId != instId && otherName == instName) {
                s_canonicalAliases[otherId] = instId;
            }
        }
    }
    s_mappingsInitialized = true;
}

inline std::uint64_t toPosixTimestamp(std::uint64_t ts) {
    if (ts == 0) return 0;
    if (ts > 1000000000ULL) return ts;
    return ts * 60 + 946598400ULL;
}

void timestampToDate(std::uint64_t posixSeconds, int& outYear, int& outMonth, int& outDay) {
    if (posixSeconds == 0) {
        outYear = 2026;
        outMonth = 1;
        outDay = 1;
        return;
    }
    std::time_t raw = static_cast<std::time_t>(posixSeconds);
    std::tm t{};
#if defined(_WIN32) && !defined(__SWITCH__)
    localtime_s(&t, &raw);
#else
    localtime_r(&raw, &t);
#endif
    outYear = t.tm_year + 1900;
    outMonth = t.tm_mon + 1;
    outDay = t.tm_mday;
}

} // namespace

std::uint64_t ActivityLogManager::canonicalTitleId(std::uint64_t titleId) {
    if (titleId == 0) return 0;
    std::lock_guard<std::mutex> lock(s_namesMutex);
    auto it = s_canonicalAliases.find(titleId);
    if (it != s_canonicalAliases.end()) {
        return it->second;
    }
    for (const auto& d : kKnownPortDefs) {
        if (d.altId == titleId) return d.canonicalId;
    }
    return titleId;
}

std::string ActivityLogManager::resolveTitleName(std::uint64_t titleId) {
    if (titleId == 0) return "";
    {
        std::lock_guard<std::mutex> lock(s_namesMutex);
        auto it = s_resolvedNames.find(titleId);
        if (it != s_resolvedNames.end() && !it->second.empty()) {
            return it->second;
        }
        auto cit = s_canonicalAliases.find(titleId);
        if (cit != s_canonicalAliases.end() && cit->second != titleId) {
            auto nit = s_resolvedNames.find(cit->second);
            if (nit != s_resolvedNames.end() && !nit->second.empty()) {
                return nit->second;
            }
        }
    }

#ifdef SWITCHU_MENU
    // Check control_cache
    switchu::control_cache::Meta meta{};
    if (switchu::control_cache::readMeta(titleId, meta) && meta.name[0] != '\0') {
        std::string n = meta.name;
        std::lock_guard<std::mutex> lock(s_namesMutex);
        s_resolvedNames[titleId] = n;
        return n;
    }
    std::uint64_t canon = canonicalTitleId(titleId);
    if (canon != titleId && switchu::control_cache::readMeta(canon, meta) && meta.name[0] != '\0') {
        std::string n = meta.name;
        std::lock_guard<std::mutex> lock(s_namesMutex);
        s_resolvedNames[titleId] = n;
        s_resolvedNames[canon] = n;
        return n;
    }
#ifdef __SWITCH__
    // Query NS if available
    auto* controlData = new (std::nothrow) NsApplicationControlData();
    if (controlData) {
        size_t controlSize = 0;
        Result rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                                titleId,
                                                controlData,
                                                sizeof(NsApplicationControlData),
                                                &controlSize);
        if (R_FAILED(rc) && canon != titleId) {
            rc = nsGetApplicationControlData(NsApplicationControlSource_Storage,
                                             canon,
                                             controlData,
                                             sizeof(NsApplicationControlData),
                                             &controlSize);
        }
        if (R_SUCCEEDED(rc) && controlSize > sizeof(controlData->nacp)) {
            NacpLanguageEntry* preferred = nullptr;
            if (R_SUCCEEDED(nacpGetLanguageEntry(&controlData->nacp, &preferred)) && preferred && preferred->name[0] != '\0') {
                std::string n = preferred->name;
                switchu::control_cache::writeFromControlData(titleId, *controlData, controlSize);
                delete controlData;
                std::lock_guard<std::mutex> lock(s_namesMutex);
                s_resolvedNames[titleId] = n;
                return n;
            }
        }
        delete controlData;
    }
#endif
#endif

    return "";
}

int ActivityLogManager::daysInMonth(int year, int month) {
    if (month == 2) {
        const bool leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
        return leap ? 29 : 28;
    }
    if (month == 4 || month == 6 || month == 9 || month == 11) {
        return 30;
    }
    return 31;
}

bool ActivityLogManager::isUtilityOrLauncher(std::uint64_t titleId, const std::string& titleName) {
    switch (titleId) {
        case 0x05446530ACA7E000ULL: // sphaira
        case 0x05FBF3FAE702C000ULL: // CNX Updater
        case 0x05D45EEC8EB90000ULL: // DBI
        case 0x05421CBDD110A000ULL: // P2PNX
        case 0x05FB92703CF35000ULL: // HB App Store
        case 0x050000BADDAD0000ULL: // Tinfoil
        case 0x05B173DA652BC000ULL: // NX-Mod-Manager
        case 0x056DC0E20BB75000ULL: // NetherSX2
        case 0x053E096E5B598000ULL: // duckstation
        case 0x056FCC911FF23000ULL: // TelegramNX
        case 0x05D8698824D69000ULL: // PortNX
        case 0x057565ABF674E000ULL: // AmiiboGenerator
        case 0x010000000000100DULL: // Album / hbmenu applet
        case 0x0100000000001008ULL: // Mii Editor
        case 0x010000000000100BULL: // Controller pairing applet
        case 0x0100000000001000ULL: // qlaunch / SwitchU itself
        case 0x0100000000000025ULL: // erpt
        case 0x0100000000001007ULL: // photo viewer
        case 0x0100000000001009ULL: // user select
        case 0x010000000000100EULL: // web browser applet
            return true;
        default:
            break;
    }

    if (titleName.empty()) return false;

    std::string lower = titleName;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Explicit game ports must never be filtered out
    if (lower.find("port") != std::string::npos) {
        return false;
    }

    if (lower.find("sphaira") != std::string::npos ||
        lower.find("updater") != std::string::npos ||
        lower.find("dbi") != std::string::npos ||
        lower.find("hbmenu") != std::string::npos ||
        lower.find("homebrew menu") != std::string::npos ||
        lower.find("tinfoil") != std::string::npos ||
        lower.find("awoo") != std::string::npos ||
        lower.find("goldleaf") != std::string::npos ||
        lower.find("daybreak") != std::string::npos ||
        lower.find("edizon") != std::string::npos ||
        lower.find("breeze") != std::string::npos ||
        lower.find("jksv") != std::string::npos ||
        lower.find("p2pnx") != std::string::npos ||
        lower.find("app store") != std::string::npos ||
        lower.find("appstore") != std::string::npos ||
        lower.find("mod manager") != std::string::npos ||
        lower.find("mod-manager") != std::string::npos ||
        lower.find("amiibogenerator") != std::string::npos ||
        lower.find("activity-log") != std::string::npos ||
        lower.find("switchu") != std::string::npos)
    {
        return true;
    }

    return false;
}

void ActivityLogManager::refresh(const std::vector<std::pair<std::uint64_t, std::string>>& installedTitles) {
    m_allTimeRankings.clear();
    m_statsByTitle.clear();
    m_dailyRecords.clear();

    initMappings(installedTitles);

    std::unordered_map<std::uint64_t, std::string> titleNames;
    for (const auto& [tid, name] : installedTitles) {
        if (tid != 0) {
            titleNames[tid] = name;
        }
    }
    {
        std::lock_guard<std::mutex> lock(s_namesMutex);
        for (const auto& [tid, name] : s_resolvedNames) {
            if (titleNames.find(tid) == titleNames.end()) {
                titleNames[tid] = name;
            }
        }
    }

    queryPdmStatistics(installedTitles);
    if (m_cancel.load())
        return;
    queryPdmAppletEvents(titleNames);

    // Reconcile daily event totals against authoritative PDM statistics.
    // If unclosed sessions or sleep caused daily sums to exceed official playtime,
    // proportionally scale down daily sessions to match reality.
    std::unordered_map<std::uint64_t, std::uint64_t> totalEventPlaytimeByTitle;
    for (const auto& [dayKey, day] : m_dailyRecords) {
        for (const auto& t : day.titles) {
            totalEventPlaytimeByTitle[t.titleId] += t.playtimeSeconds;
        }
    }

    for (const auto& [tid, eventTotal] : totalEventPlaytimeByTitle) {
        auto it = m_statsByTitle.find(tid);
        if (it != m_statsByTitle.end() && it->second.totalPlaytimeSeconds > 0 && eventTotal > it->second.totalPlaytimeSeconds) {
            const double scale = static_cast<double>(it->second.totalPlaytimeSeconds) / static_cast<double>(eventTotal);
            for (auto& [dayKey, day] : m_dailyRecords) {
                for (auto& t : day.titles) {
                    if (t.titleId == tid) {
                        t.playtimeSeconds = std::max<std::uint64_t>(
                            1, static_cast<std::uint64_t>(std::round(t.playtimeSeconds * scale)));
                    }
                }
            }
        }
    }

    // Recompute totalPlaytimeSeconds for each day
    for (auto& [dayKey, day] : m_dailyRecords) {
        day.totalPlaytimeSeconds = 0;
        for (const auto& t : day.titles) {
            day.totalPlaytimeSeconds += t.playtimeSeconds;
        }
    }

    // If event logs are sparse or empty, ensure any title with non-zero playtime has at least
    // a representation on its last-played date so calendar navigation is populated.
    for (const auto& stats : m_allTimeRankings) {
        if (stats.totalPlaytimeSeconds > 0 && stats.lastPlayedTimestamp > 0) {
            auto totalIt = totalEventPlaytimeByTitle.find(stats.titleId);
            if (totalIt == totalEventPlaytimeByTitle.end() || totalIt->second == 0) {
                int y = 0, m = 0, d = 0;
                timestampToDate(stats.lastPlayedTimestamp, y, m, d);
                int dayKey = makeDayKey(y, m, d);
                auto& dayRecord = m_dailyRecords[dayKey];
                dayRecord.year = y;
                dayRecord.month = m;
                dayRecord.day = d;

                auto it = std::find_if(dayRecord.titles.begin(), dayRecord.titles.end(),
                                       [tid = stats.titleId](const DailyTitleEntry& e) {
                                           return e.titleId == tid;
                                       });
                if (it == dayRecord.titles.end()) {
                    DailyTitleEntry entry;
                    entry.titleId = stats.titleId;
                    entry.titleName = stats.titleName;
                    entry.playtimeSeconds = stats.averageSessionSeconds > 0 ? stats.averageSessionSeconds : stats.totalPlaytimeSeconds;
                    entry.playtimeSeconds = std::min(entry.playtimeSeconds, stats.totalPlaytimeSeconds);
                    entry.launches = std::max<std::uint32_t>(1, stats.totalLaunches > 0 ? 1 : 0);
                    dayRecord.titles.push_back(entry);
                    dayRecord.totalPlaytimeSeconds += entry.playtimeSeconds;
                }
            }
        }
    }

    // Sort daily titles descending by playtime
    for (auto& [_, day] : m_dailyRecords) {
        std::sort(day.titles.begin(), day.titles.end(),
                  [](const DailyTitleEntry& a, const DailyTitleEntry& b) {
                      return a.playtimeSeconds > b.playtimeSeconds;
                  });
    }

    // Ensure all played canonical titles found in event query are also in all-time rankings
    for (const auto& [dayKey, day] : m_dailyRecords) {
        for (const auto& t : day.titles) {
            auto it = m_statsByTitle.find(t.titleId);
            if (it == m_statsByTitle.end()) {
                TitlePlayStats s;
                s.titleId = t.titleId;
                s.titleName = t.titleName.empty() ? resolveTitleName(t.titleId) : t.titleName;
                s.totalPlaytimeSeconds = t.playtimeSeconds;
                s.totalLaunches = t.launches;
                m_statsByTitle[t.titleId] = s;
                m_allTimeRankings.push_back(s);
            } else {
                if (it->second.totalPlaytimeSeconds == 0 && t.playtimeSeconds > 0) {
                    it->second.totalPlaytimeSeconds = t.playtimeSeconds;
                }
                if (it->second.titleName.empty() && !t.titleName.empty()) {
                    it->second.titleName = t.titleName;
                }
            }
        }
    }

    std::sort(m_allTimeRankings.begin(), m_allTimeRankings.end(),
              [](const TitlePlayStats& a, const TitlePlayStats& b) {
                  if (a.totalPlaytimeSeconds != b.totalPlaytimeSeconds)
                      return a.totalPlaytimeSeconds > b.totalPlaytimeSeconds;
                  return a.totalLaunches > b.totalLaunches;
              });

    DebugLog::log("[activity] refresh complete: %zu ranked titles, %zu active days",
                  m_allTimeRankings.size(), m_dailyRecords.size());
    for (const auto& [dayKey, day] : m_dailyRecords) {
        DebugLog::log("[activity] dayKey=%d: %zu titles, totalPlaytime=%llus",
                      dayKey, day.titles.size(), (unsigned long long)day.totalPlaytimeSeconds);
        for (const auto& t : day.titles) {
            DebugLog::log("[activity]   -> %016llX ('%s') %llus",
                          (unsigned long long)t.titleId, t.titleName.c_str(), (unsigned long long)t.playtimeSeconds);
        }
    }
}

const TitlePlayStats* ActivityLogManager::findTitleStats(std::uint64_t titleId) const {
    auto it = m_statsByTitle.find(titleId);
    if (it != m_statsByTitle.end())
        return &it->second;
    return nullptr;
}

DailyLogDay ActivityLogManager::queryDay(int year, int month, int day) const {
    int key = makeDayKey(year, month, day);
    auto it = m_dailyRecords.find(key);
    if (it != m_dailyRecords.end())
        return it->second;

    DailyLogDay empty;
    empty.year = year;
    empty.month = month;
    empty.day = day;
    return empty;
}

MonthlyLogSummary ActivityLogManager::queryMonth(int year, int month) const {
    MonthlyLogSummary summary;
    summary.year = year;
    summary.month = month;
    summary.dailyPlaytimeSeconds.resize(31, 0);

    std::unordered_map<std::uint64_t, DailyTitleEntry> titleMap;

    for (int day = 1; day <= 31; ++day) {
        int key = makeDayKey(year, month, day);
        auto it = m_dailyRecords.find(key);
        if (it != m_dailyRecords.end()) {
            summary.dailyPlaytimeSeconds[day - 1] = it->second.totalPlaytimeSeconds;
            summary.totalPlaytimeSeconds += it->second.totalPlaytimeSeconds;
            if (it->second.totalPlaytimeSeconds > 0)
                summary.activeDaysCount++;

            for (const auto& t : it->second.titles) {
                auto& agg = titleMap[t.titleId];
                agg.titleId = t.titleId;
                agg.titleName = t.titleName;
                agg.playtimeSeconds += t.playtimeSeconds;
                agg.launches += t.launches;
            }
        }
    }

    for (auto& [_, t] : titleMap) {
        summary.titles.push_back(t);
    }
    std::sort(summary.titles.begin(), summary.titles.end(),
              [](const DailyTitleEntry& a, const DailyTitleEntry& b) {
                  return a.playtimeSeconds > b.playtimeSeconds;
              });

    DebugLog::log("[activity] queryMonth %d/%d: activeDays=%u, totalPlaytime=%llus, titles=%zu",
                  year, month, summary.activeDaysCount, (unsigned long long)summary.totalPlaytimeSeconds, summary.titles.size());
    for (const auto& t : summary.titles) {
        DebugLog::log("[activity]   monthly title -> %016llX ('%s') %llus",
                      (unsigned long long)t.titleId, t.titleName.c_str(), (unsigned long long)t.playtimeSeconds);
    }

    return summary;
}

void ActivityLogManager::queryPdmStatistics(const std::vector<std::pair<std::uint64_t, std::string>>& installedTitles) {
#ifdef __SWITCH__
    PdmPlayStatistics stats{};
    std::lock_guard<std::mutex> pdmGuard(switchu::menu::playtime::queryMutex());
    const Result initRc = pdmqryInitialize();
    if (R_FAILED(initRc)) {
        DebugLog::log("[activity] pdmqryInitialize failed rc=0x%08X", (unsigned int)initRc);
        return;
    }

    for (const auto& [titleId, name] : installedTitles) {
        if (titleId == 0) continue;
        if (isUtilityOrLauncher(titleId, name)) {
            DebugLog::log("[activity] skipping utility/motor: %016llX ('%s')",
                          (unsigned long long)titleId, name.c_str());
            continue;
        }
        if (m_cancel.load())
            break; // the frontend is leaving
        std::memset(&stats, 0, sizeof(stats));
        const Result qrc = pdmqryQueryPlayStatisticsByApplicationId(titleId, true, &stats);
        if (R_SUCCEEDED(qrc)) {
            TitlePlayStats s;
            s.titleId = titleId;
            s.titleName = name;
            s.totalPlaytimeSeconds = stats.playtime / 1000000000ULL;
            s.totalLaunches = stats.total_launches;
            s.firstPlayedTimestamp = toPosixTimestamp(stats.first_timestamp_user);
            s.lastPlayedTimestamp = toPosixTimestamp(stats.last_timestamp_user);
            s.averageSessionSeconds = s.totalLaunches > 0 ? (s.totalPlaytimeSeconds / s.totalLaunches) : 0;

            m_statsByTitle[titleId] = s;
            m_allTimeRankings.push_back(s);
            if (s.totalLaunches > 0 || s.totalPlaytimeSeconds > 0) {
                int fy = 0, fm = 0, fd = 0;
                int ly = 0, lm = 0, ld = 0;
                timestampToDate(s.firstPlayedTimestamp, fy, fm, fd);
                timestampToDate(s.lastPlayedTimestamp, ly, lm, ld);
                DebugLog::log("[activity] %016llX ('%s') launches=%u playtime=%llus first=%d-%02d-%02d last=%d-%02d-%02d",
                              (unsigned long long)titleId, name.c_str(), stats.total_launches,
                              (unsigned long long)s.totalPlaytimeSeconds,
                              fy, fm, fd, ly, lm, ld);
            }
        }
    }
    pdmqryExit();
#else
    // Development/test mock entries
    for (size_t i = 0; i < installedTitles.size(); ++i) {
        const auto& [titleId, name] = installedTitles[i];
        if (titleId == 0) continue;
        TitlePlayStats s;
        s.titleId = titleId;
        s.titleName = name;
        s.totalPlaytimeSeconds = (installedTitles.size() - i) * 3600ULL * 4 + (i * 1234ULL % 3600);
        s.totalLaunches = static_cast<std::uint32_t>((installedTitles.size() - i) * 5 + 1);
        s.firstPlayedTimestamp = 1704067200ULL + i * 86400; // ~Jan 2024
        s.lastPlayedTimestamp = 1789000000ULL - i * 43200;
        s.averageSessionSeconds = s.totalLaunches > 0 ? (s.totalPlaytimeSeconds / s.totalLaunches) : 0;

        m_statsByTitle[titleId] = s;
        m_allTimeRankings.push_back(s);
    }
#endif

    std::sort(m_allTimeRankings.begin(), m_allTimeRankings.end(),
              [](const TitlePlayStats& a, const TitlePlayStats& b) {
                  if (a.totalPlaytimeSeconds != b.totalPlaytimeSeconds)
                      return a.totalPlaytimeSeconds > b.totalPlaytimeSeconds;
                  return a.totalLaunches > b.totalLaunches;
              });
}

void ActivityLogManager::queryPdmAppletEvents(const std::unordered_map<std::uint64_t, std::string>& titleNames) {
#ifdef __SWITCH__
    std::lock_guard<std::mutex> pdmGuard(switchu::menu::playtime::queryMutex());
    const Result initRc = pdmqryInitialize();
    if (R_FAILED(initRc)) {
        DebugLog::log("[activity] pdmqryInitialize failed in applet events rc=0x%08X", (unsigned int)initRc);
        return;
    }

    s32 total_entries = 0, start_entry = 0, end_entry = 0;
    const Result rangeRc = pdmqryGetAvailablePlayEventRange(&total_entries, &start_entry, &end_entry);
    DebugLog::log("[activity] pdm range: rc=0x%08X total=%d start=%d end=%d",
                  (unsigned int)rangeRc, total_entries, start_entry, end_entry);

    if (R_SUCCEEDED(rangeRc) && total_entries > 0 && end_entry >= start_entry) {
        constexpr s32 kChunkSize = 64;
        std::vector<PdmAppletEvent> events(kChunkSize);
        std::unordered_map<u64, u64> activeStarts;

        // Scan the entire available range (up to 2048 entries) to ensure all active days and months are covered.
        s32 cur = std::max(start_entry, end_entry - 2048);
        DebugLog::log("[activity] scanning applet events from index %d to %d", cur, end_entry);
        s32 totalParsed = 0;
        s32 sessionsCount = 0;
        while (cur <= end_entry && !m_cancel.load()) {
            s32 count = std::min(kChunkSize, end_entry - cur + 1);
            s32 total_out = 0;
            const Result eqRc = pdmqryQueryAppletEvent(cur, true, events.data(), count, &total_out);
            if (R_FAILED(eqRc) || total_out <= 0)
                break;

            for (s32 i = 0; i < total_out; ++i) {
                const auto& ev = events[i];
                if (ev.program_id == 0) continue;

                u64 rawId = ev.program_id;
                u64 canonicalId = canonicalTitleId(rawId);

                std::string appName = resolveTitleName(canonicalId);
                if (appName.empty()) {
                    appName = resolveTitleName(rawId);
                }
                if (appName.empty()) {
                    auto nit = titleNames.find(canonicalId);
                    if (nit != titleNames.end()) appName = nit->second;
                    else {
                        auto rnit = titleNames.find(rawId);
                        if (rnit != titleNames.end()) appName = rnit->second;
                    }
                }

                bool isUtil = isUtilityOrLauncher(rawId, appName) || isUtilityOrLauncher(canonicalId, appName);
                u64 ts = toPosixTimestamp(ev.timestamp_user);

                if (isUtil) {
                    // Utility, launcher, or Home Menu took foreground: close any active game session at ts
                    for (auto it = activeStarts.begin(); it != activeStarts.end(); ) {
                        u64 prevStart = it->second;
                        if (ts >= prevStart) {
                            u64 dur = std::min<u64>(ts - prevStart, 1800); // 30 min max for backgrounded
                            if (dur > 0) {
                                int y = 0, m = 0, d = 0;
                                timestampToDate(prevStart, y, m, d);
                                int dayKey = makeDayKey(y, m, d);
                                auto& dayRecord = m_dailyRecords[dayKey];
                                dayRecord.year = y;
                                dayRecord.month = m;
                                dayRecord.day = d;
                                dayRecord.totalPlaytimeSeconds += dur;
                                auto tit = std::find_if(dayRecord.titles.begin(), dayRecord.titles.end(),
                                                        [id = it->first](const DailyTitleEntry& e) {
                                                            return e.titleId == id;
                                                        });
                                if (tit != dayRecord.titles.end()) {
                                    tit->playtimeSeconds += dur;
                                } else {
                                    DailyTitleEntry te;
                                    te.titleId = it->first;
                                    std::string tname = resolveTitleName(it->first);
                                    if (tname.empty()) {
                                        auto nit = titleNames.find(it->first);
                                        tname = (nit != titleNames.end()) ? nit->second : "";
                                    }
                                    te.titleName = tname;
                                    te.playtimeSeconds = dur;
                                    te.launches = 1;
                                    dayRecord.titles.push_back(te);
                                }
                                sessionsCount++;
                            }
                        }
                        it = activeStarts.erase(it);
                    }
                    continue;
                }

                if (ev.event_type == PdmAppletEventType_Launch || ev.event_type == PdmAppletEventType_InFocus) {
                    for (auto it = activeStarts.begin(); it != activeStarts.end(); ) {
                        if (it->first != canonicalId) {
                            u64 prevStart = it->second;
                            if (ts >= prevStart) {
                                u64 dur = std::min<u64>(ts - prevStart, 1800);
                                if (dur > 0) {
                                    int y = 0, m = 0, d = 0;
                                    timestampToDate(prevStart, y, m, d);
                                    int dayKey = makeDayKey(y, m, d);
                                    auto& dayRecord = m_dailyRecords[dayKey];
                                    dayRecord.year = y;
                                    dayRecord.month = m;
                                    dayRecord.day = d;
                                    dayRecord.totalPlaytimeSeconds += dur;
                                    auto tit = std::find_if(dayRecord.titles.begin(), dayRecord.titles.end(),
                                                            [id = it->first](const DailyTitleEntry& e) {
                                                                return e.titleId == id;
                                                            });
                                    if (tit != dayRecord.titles.end()) {
                                        tit->playtimeSeconds += dur;
                                    } else {
                                        DailyTitleEntry te;
                                        te.titleId = it->first;
                                        std::string tname = resolveTitleName(it->first);
                                        if (tname.empty()) {
                                            auto nit = titleNames.find(it->first);
                                            tname = (nit != titleNames.end()) ? nit->second : "";
                                        }
                                        te.titleName = tname;
                                        te.playtimeSeconds = dur;
                                        te.launches = 1;
                                        dayRecord.titles.push_back(te);
                                    }
                                    sessionsCount++;
                                }
                            }
                            it = activeStarts.erase(it);
                        } else {
                            ++it;
                        }
                    }

                    if (ev.event_type == PdmAppletEventType_Launch) {
                        int y = 0, m = 0, d = 0;
                        timestampToDate(ts, y, m, d);
                        int dayKey = makeDayKey(y, m, d);
                        auto& dayRecord = m_dailyRecords[dayKey];
                        auto tit = std::find_if(dayRecord.titles.begin(), dayRecord.titles.end(),
                                                [id = canonicalId](const DailyTitleEntry& e) {
                                                    return e.titleId == id;
                                                });
                        if (tit != dayRecord.titles.end()) {
                            tit->launches += 1;
                        }
                    }

                    if (activeStarts.find(canonicalId) == activeStarts.end()) {
                        activeStarts[canonicalId] = ts;
                    }
                } else if (ev.event_type == PdmAppletEventType_Exit || ev.event_type == PdmAppletEventType_OutOfFocus ||
                           ev.event_type == PdmAppletEventType_OutOfFocus4 || ev.event_type == PdmAppletEventType_Exit5 ||
                           ev.event_type == PdmAppletEventType_Exit6) {
                    auto it = activeStarts.find(canonicalId);
                    if (it != activeStarts.end()) {
                        u64 startTs = it->second;
                        activeStarts.erase(it);
                        if (ts >= startTs) {
                            u64 duration = std::min<u64>(ts - startTs, 3600 * 2);
                            if (duration > 0) {
                                int y = 0, m = 0, d = 0;
                                timestampToDate(startTs, y, m, d);
                                int dayKey = makeDayKey(y, m, d);

                                auto& dayRecord = m_dailyRecords[dayKey];
                                dayRecord.year = y;
                                dayRecord.month = m;
                                dayRecord.day = d;
                                dayRecord.totalPlaytimeSeconds += duration;

                                auto tit = std::find_if(dayRecord.titles.begin(), dayRecord.titles.end(),
                                                        [id = canonicalId](const DailyTitleEntry& e) {
                                                            return e.titleId == id;
                                                        });
                                if (tit != dayRecord.titles.end()) {
                                    tit->playtimeSeconds += duration;
                                } else {
                                    DailyTitleEntry te;
                                    te.titleId = canonicalId;
                                    std::string tname = resolveTitleName(canonicalId);
                                    if (tname.empty()) {
                                        auto nit = titleNames.find(canonicalId);
                                        tname = (nit != titleNames.end()) ? nit->second : "";
                                    }
                                    te.titleName = tname;
                                    te.playtimeSeconds = duration;
                                    te.launches = 1;
                                    dayRecord.titles.push_back(te);
                                }
                                sessionsCount++;
                            }
                        }
                    }
                }
            }
            cur += total_out;
        }
        DebugLog::log("[activity] applet events parsed: %d events, %d sessions created",
                      totalParsed, sessionsCount);
    }
    pdmqryExit();
#else
    (void)titleNames;
#endif
}

} // namespace switchu::activity
