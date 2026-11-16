#pragma once
#include <nxui/core/ThreadPool.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>

namespace home {

struct StatusReading {
    float soc = -1.f;
    std::uint32_t gpuMhz = 0;
    std::uint64_t freeBytes = 0;
    bool freeValid = false;
    bool sleepValid = false;
    int handheldSleep = 0;
    int consoleSleep = 0;
};

// Process-owned snapshot. Only the worker touches Horizon/SD; a frame polls
// readiness and reads the last completed value. At most one request is queued.
class HomeStatus {
public:
    using Clock = std::chrono::steady_clock;
    using Sampler = std::function<StatusReading(bool freeSpace, bool gpuClock)>;
    explicit HomeStatus(Sampler sampler = read) : m_sampler(std::move(sampler)) {}
    void poll(nxui::ThreadPool& pool, Clock::time_point now = Clock::now(),
              bool freeSpace = false, bool gpuClock = false);
    const StatusReading& latest() const { return m_latest; }
private:
    static StatusReading read(bool freeSpace, bool gpuClock);
    Sampler m_sampler;
    StatusReading m_latest;
    Clock::time_point m_next{};
    std::future<void> m_future;
    std::shared_ptr<StatusReading> m_result;
};

} // namespace home
