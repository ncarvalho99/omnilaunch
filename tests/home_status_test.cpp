#include "home/HomeStatus.hpp"
#include <atomic>
#include <cassert>
#include <future>
#include <iostream>
#include <thread>

int main() {
    using namespace std::chrono_literals;
    nxui::ThreadPool pool(1);
    std::promise<void> entered, release;
    auto gate = release.get_future().share();
    std::atomic<int> calls{0};
    home::HomeStatus status([&](bool, bool) {
        ++calls;
        entered.set_value();
        gate.wait();
        home::StatusReading r;
        r.soc = 42.f; r.freeBytes = 123; r.freeValid = true;
        return r;
    });
    auto now = home::HomeStatus::Clock::now();
    status.poll(pool, now);
    entered.get_future().wait();
    auto frame = std::async(std::launch::async, [&] {
        for (int i = 0; i < 100; ++i) status.poll(pool, now + 30s);
    });
    const bool returned = frame.wait_for(200ms) == std::future_status::ready;
    // Always release, even if a regression makes the frame wait.
    release.set_value();
    frame.get();
    assert(returned && calls == 1);
    pool.waitForIdle();
    status.poll(pool, now + 30s);
    assert(status.latest().freeValid && status.latest().freeBytes == 123);
    assert(status.latest().soc == 42.f && calls == 1);
    status.poll(pool, now + 32s);
    assert(calls == 1); // interval begins at completion, not submission
    pool.shutdown();
    status.poll(pool, now + 60s); // shutdown is not a new request
    assert(calls == 1);
    nxui::ThreadPool retryPool(1);
    int attempts=0;
    home::HomeStatus retry([&](bool,bool) {
        if (++attempts==1) throw std::runtime_error("service unavailable");
        home::StatusReading r; r.sleepValid=true; r.handheldSleep=3; return r;
    });
    retry.poll(retryPool,now); retryPool.waitForIdle(); retry.poll(retryPool,now);
    assert(!retry.latest().sleepValid);
    retry.poll(retryPool,now+std::chrono::seconds(3)); retryPool.waitForIdle();
    retry.poll(retryPool,now+std::chrono::seconds(3));
    assert(retry.latest().sleepValid && retry.latest().handheldSleep==3);
    retryPool.shutdown();
    std::cout << "home status blocked-worker/nonblocking poll/failure recovery PASS\n";
}
