#include "home/HomePlaytime.hpp"
#include "core/Config.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/ThreadPool.hpp>
#ifdef SWITCHU_MENU
#include "core/PlayTime.hpp"
#endif
#include <chrono>

namespace home {

void HomePlaytime::start(nxui::ThreadPool& pool, std::vector<std::uint64_t> titleIds) {
    if (m_future.valid() || titleIds.empty())
        return;
    auto state = std::make_shared<State>();
    m_state = state;
    m_future = pool.submit([state, titleIds = std::move(titleIds)]() {
#ifdef SWITCHU_MENU
        state->playtime = switchu::menu::playtime::queryAll(titleIds, &state->cancelled);
#else
        (void)titleIds;
#endif
    });
}

int HomePlaytime::poll(AppConfig& config) {
    if (!m_future.valid() ||
        m_future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return -1;
    auto state = std::move(m_state);
    bool failed = false;
    try {
        m_future.get();
    } catch (...) {
        // The pool refusing work during shutdown; the cache stays as it was.
        failed = true;
    }
    int changed = 0;
    if (!failed && state) {
        for (const auto& [titleId, nanoseconds] : state->playtime)
            changed += config.setPlaytime(titleId, nanoseconds) ? 1 : 0;
    }
    DebugLog::log("[playtime] refresh done titles=%zu changed=%d",
                  state ? state->playtime.size() : static_cast<std::size_t>(0), changed);
    // Nothing is saved here. Every launch writes the config before the
    // handoff, and that write carries this cache with it.
    return changed;
}

void HomePlaytime::cancel() {
    if (m_state)
        m_state->cancelled.store(true);
}

void HomePlaytime::shutdown() {
    cancel();
    if (m_future.valid()) {
        try { m_future.get(); } catch (...) {}
    }
    m_state.reset();
}

} // namespace home
