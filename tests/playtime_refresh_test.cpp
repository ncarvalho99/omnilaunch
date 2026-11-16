#include "home/HomePlaytime.hpp"
#include "core/Config.hpp"
#include "core/PlayTime.hpp"
#include <nxui/core/ThreadPool.hpp>
#include <atomic>
#include <cassert>
#include <chrono>
#include <thread>
#include <iostream>
using namespace std::chrono_literals;
namespace { std::atomic<bool> entered{false}, release{false}; }
namespace switchu::menu::playtime {
Profile currentProfile(const AppConfig& config) {
    return {config.defaultProfileUid == "b" ? 2ULL : 1ULL, 0};
}
std::string profileKey(Profile user) { return std::to_string(user[0]); }
std::vector<std::pair<std::uint64_t,std::uint64_t>> queryAll(
    const std::vector<std::uint64_t>& ids, const std::atomic<bool>*, Profile user) {
    entered = true;
    while (!release.load()) std::this_thread::sleep_for(1ms);
    // Even an uncancellable IPC that returns a result must not publish stale data.
    std::vector<std::pair<std::uint64_t,std::uint64_t>> out;
    for (auto id : ids) out.emplace_back(id, user[0] * 100);
    return out;
}
}
template<class F> void eventually(F f) {
    for (int n=0; n<3000; ++n) {
        if (f()) return;
        std::this_thread::sleep_for(1ms);
    }
    assert(false && "condition did not become true");
}
int main() {
    nxui::ThreadPool pool(1);
    home::HomePlaytime refresh;
    AppConfig config;
    config.defaultProfileEnabled = true;
    config.defaultProfileUid = "a";
    config.setPlaytime(10, 999);
    assert(refresh.syncProfile(config));
    assert(config.playtime.empty());
    assert(refresh.start(pool, {10}, 5));
    eventually([]{return entered.load();});
    config.defaultProfileUid = "b";
    assert(refresh.syncProfile(config));
    release = true;
    int result = -1;
    eventually([&]{result = refresh.poll(config); return result >= 0;});
    assert(result == 0 && !config.hasPlaytime(10) && config.playtimeCheckedSequence == 0);
    assert(refresh.start(pool, {10,20}, 6));
    eventually([&]{result = refresh.poll(config); return result >= 0;});
    assert(result == 2 && config.playtimeOf(10) == 200 && config.playtimeCheckedSequence == 6);
    // A preference change immediately before completion is checked again at adopt,
    // even before the next periodic sync tick.
    entered = false; release = false;
    assert(refresh.start(pool, {10}, 7));
    eventually([]{return entered.load();});
    config.defaultProfileUid = "a";
    release = true;
    eventually([&]{result = refresh.poll(config); return result >= 0;});
    assert(result == 0 && config.playtimeOf(10) == 200 && config.playtimeCheckedSequence == 6);
    // Cancellation with the same profile likewise cannot confirm returned IPCs.
    assert(refresh.syncProfile(config));
    entered = false; release = false;
    assert(refresh.start(pool, {10}, 8));
    eventually([]{return entered.load();});
    refresh.cancel(); release = true;
    eventually([&]{result = refresh.poll(config); return result >= 0;});
    assert(!config.hasPlaytime(10) && config.playtimeCheckedSequence == 0);
    refresh.shutdown();
    std::cout << "playtime refresh stale/cancelled result tests PASS\n";
}
