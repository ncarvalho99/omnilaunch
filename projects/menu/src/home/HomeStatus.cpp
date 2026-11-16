#include "home/HomeStatus.hpp"
#ifdef SWITCHU_MENU
#include <switch.h>
#include <switchu/hardware_status.hpp>
#include <sys/statvfs.h>
#endif

namespace home {

StatusReading HomeStatus::read(bool freeSpace, bool gpuClock) {
    StatusReading out;
#ifdef SWITCHU_MENU
    const auto thermal = switchu::getThermalReading();
    if (thermal.hasSoc) out.soc = thermal.soc;
    if (gpuClock) out.gpuMhz = switchu::getGpuClockMhz();
    struct statvfs space{};
    if (freeSpace && statvfs("sdmc:/", &space) == 0) {
        out.freeBytes = static_cast<std::uint64_t>(space.f_bavail) * space.f_frsize;
        out.freeValid = true;
    }
    SetSysSleepSettings plan{};
    if (R_SUCCEEDED(setsysGetSleepSettings(&plan))) {
        out.handheldSleep = plan.handheld_sleep_plan;
        out.consoleSleep = plan.console_sleep_plan;
        out.sleepValid = true;
    }
#else
    (void)freeSpace; (void)gpuClock;
#endif
    return out;
}

void HomeStatus::poll(nxui::ThreadPool& pool, Clock::time_point now, bool freeSpace, bool gpuClock) {
    if (m_future.valid()) {
        if (m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            return;
        try { m_future.get(); m_latest = *m_result; } catch (...) {}
        m_result.reset();
        // Delay from completion: a slow service never builds a polling backlog.
        m_next = now + std::chrono::seconds(3);
    }
    if (now < m_next || !pool.acceptingTasks()) return;
    auto result = std::make_shared<StatusReading>();
    try {
        m_future = pool.submit([sample = m_sampler, result, freeSpace, gpuClock] { *result = sample(freeSpace, gpuClock); });
        m_result = std::move(result);
    } catch (...) {
        m_next = now + std::chrono::seconds(3);
    }
}

} // namespace home
