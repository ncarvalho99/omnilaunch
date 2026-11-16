#pragma once

// Time service sessions on a budget.
//
// Adapted from HomeNX (ad3333m/homenx, GPL-3.0) commit 8d0fd8b,
// projects/common/include/switchu/time_budget.hpp, by frcjules.
//
// The console's time service only has a few sessions for every process
// together, and libnx's timeInitialize() takes 6 at once (the service, four
// clocks, the time zone service). With a few sysmodules running only ~3 are
// left by the time HOME starts: timeInitialize() then fails with 0x64274
// (time: ResultOutOfMemory), and with it the clock, the time zone and time().
//
// HOME only reads the user clock and converts with the time zone, so it opens
// just those two sessions, straight into libnx's own (timeGetCurrentTime(),
// timeToCalendarTimeWithMyRule(), localtime() then work as usual), and tries
// again until it gets them. Calls that need the service itself or another
// clock (setting the clock) open them for that call only (FullScope).
#include <switch.h>

#include <initializer_list>

extern "C" {
extern TimeServiceType __nx_time_service_type;
void __libnx_init_time(void);
}

namespace switchu::timebudget {

inline const char* portName() {
    switch (__nx_time_service_type) {
        case TimeServiceType_Menu: return "time:a";
        case TimeServiceType_System: return "time:s";
        case TimeServiceType_Repair: return "time:r";
        case TimeServiceType_SystemUser: return "time:su";
        default: return "time:u";
    }
}

inline Result openObject(Service* srv, u32 cmd, Service* out) {
    return serviceDispatch(srv, cmd, .out_num_objects = 1, .out_objects = out);
}

// Keeps the user clock and the time zone service open, trying at most every
// 2 s until both are. True once the time is available. Main thread only:
// __libnx_init_time() sets TZ for localtime().
inline bool poll() {
    Service* clock = timeGetServiceSession_SystemClock(TimeType_UserSystemClock);
    Service* zone = timeGetServiceSession_TimeZoneService();
    if (serviceIsActive(zone) && serviceIsActive(clock)) return true;
    static u64 nextTry = 0;
    const u64 now = armGetSystemTick();
    if (now < nextTry) return false;
    nextTry = now + armNsToTicks(2'000'000'000ULL);

    Service srv{};
    if (R_FAILED(smGetService(&srv, portName()))) return false;
    if (!serviceIsActive(clock)) openObject(&srv, 0, clock);
    if (!serviceIsActive(zone)) openObject(&srv, 3, zone);
    serviceClose(&srv);
    if (!serviceIsActive(zone) || !serviceIsActive(clock)) return false;
    __libnx_init_time();
    return true;
}

// For a process that only needs time() and localtime() (they count from the
// boot time libnx reads in __libnx_init_time()): opens the user clock and the
// time zone service just long enough to read it again. Rate-limited to once
// every 2 s unless `force`. True when the boot time was read.
inline bool refreshBootTime(bool force = false) {
    static u64 nextTry = 0;
    const u64 now = armGetSystemTick();
    if (!force && now < nextTry) return false;
    nextTry = now + armNsToTicks(2'000'000'000ULL);

    Service* clock = timeGetServiceSession_SystemClock(TimeType_UserSystemClock);
    Service* zone = timeGetServiceSession_TimeZoneService();
    bool ownClock = false;
    bool ownZone = false;
    if (!serviceIsActive(clock) || !serviceIsActive(zone)) {
        Service srv{};
        if (R_FAILED(smGetService(&srv, portName()))) return false;
        ownClock = !serviceIsActive(clock) && R_SUCCEEDED(openObject(&srv, 0, clock));
        ownZone = !serviceIsActive(zone) && R_SUCCEEDED(openObject(&srv, 3, zone));
        serviceClose(&srv);
    }
    const bool ok = serviceIsActive(clock) && serviceIsActive(zone);
    if (ok) __libnx_init_time();
    if (ownZone) serviceClose(zone);
    if (ownClock) serviceClose(clock);
    return ok;
}

// The time service, the three clocks and the time zone service, whichever are
// not open, open for this scope (setting the clock, reading it back).
class FullScope {
public:
    FullScope() {
        Service* srv = timeGetServiceSession();
        if (serviceIsActive(srv) || R_FAILED(smGetService(srv, portName()))) return;
        m_opened = true;
        constexpr u32 kGetClock[3] = {0, 1, 4}; // user, network, local (IStaticService)
        for (TimeType type : {TimeType_UserSystemClock, TimeType_NetworkSystemClock, TimeType_LocalSystemClock}) {
            Service* clock = timeGetServiceSession_SystemClock(type);
            if (!serviceIsActive(clock) && R_SUCCEEDED(openObject(srv, kGetClock[type], clock)))
                m_clocks[type] = true;
        }
        Service* zone = timeGetServiceSession_TimeZoneService();
        m_zone = !serviceIsActive(zone) && R_SUCCEEDED(openObject(srv, 3, zone));
    }
    ~FullScope() {
        if (!m_opened) return;
        if (m_zone) serviceClose(timeGetServiceSession_TimeZoneService());
        for (TimeType type : {TimeType_UserSystemClock, TimeType_NetworkSystemClock, TimeType_LocalSystemClock})
            if (m_clocks[type]) serviceClose(timeGetServiceSession_SystemClock(type));
        serviceClose(timeGetServiceSession());
    }
    FullScope(const FullScope&) = delete;
    FullScope& operator=(const FullScope&) = delete;

private:
    bool m_opened = false;
    bool m_clocks[3] = {};
    bool m_zone = false;
};

} // namespace switchu::timebudget
