// clock_gettime() for the HOME menu process.
//
// Adapted from HomeNX (ad3333m/homenx, GPL-3.0) commit d0826c5,
// projects/menu/src/platform/clock_gettime_fix.cpp, by frcjules. The same
// failure is described by PoloNX/SwitchU commit 2853ccc (Tomas-Uribe).
//
// libnx derives CLOCK_MONOTONIC from the wall clock it seeds once at process
// start. When the menu starts before the time service answers, or the service
// has no session to spare, that seed stays "unavailable" and clock_gettime()
// fails for the life of the process. libcurl times every transfer with it, so
// each request ended with "Timeout was reached" within milliseconds (Discord QR
// stuck on "Getting a code...", empty Theme Shop catalogue), and
// std::chrono::steady_clock read garbage.
//
// CLOCK_MONOTONIC now comes from the system tick, which never fails. Other
// clocks use libnx when it works, then the user/network/local system clocks,
// and finally the tick so time still moves.
#include <switch.h>

#include <cerrno>
#include <ctime>

extern "C" int __syscall_clock_gettime(clockid_t clk, struct timespec* ts);

extern "C" int clock_gettime(clockid_t clk, struct timespec* ts) {
    if (!ts) {
        errno = EFAULT;
        return -1;
    }
    const u64 ns = armTicksToNs(armGetSystemTick());
    if (clk != CLOCK_MONOTONIC) {
        const int savedErrno = errno;
        if (__syscall_clock_gettime(clk, ts) == 0)
            return 0;
        errno = savedErrno;
        if (clk == CLOCK_REALTIME) {
            u64 sec = 0;
            Result rc = timeGetCurrentTime(TimeType_UserSystemClock, &sec);
            if (R_FAILED(rc)) rc = timeGetCurrentTime(TimeType_NetworkSystemClock, &sec);
            if (R_FAILED(rc)) rc = timeGetCurrentTime(TimeType_LocalSystemClock, &sec);
            if (R_SUCCEEDED(rc)) {
                ts->tv_sec = static_cast<time_t>(sec);
                ts->tv_nsec = static_cast<long>(ns % 1000000000ULL);
                return 0;
            }
        }
    }
    ts->tv_sec = static_cast<time_t>(ns / 1000000000ULL);
    ts->tv_nsec = static_cast<long>(ns % 1000000000ULL);
    return 0;
}
