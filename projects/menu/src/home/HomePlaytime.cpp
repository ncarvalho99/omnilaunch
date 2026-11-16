#include "home/HomePlaytime.hpp"
#include "core/Config.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/ThreadPool.hpp>
#ifdef SWITCHU_MENU
#include "core/PlayTime.hpp"
#endif
#include <chrono>

namespace home {

bool HomePlaytime::start(nxui::ThreadPool& pool, std::vector<std::uint64_t> titleIds,
                         std::uint64_t checkedSequence) {
    if (m_future.valid() || titleIds.empty())
        return false;
    auto state = std::make_shared<State>();
    state->profile = m_profile;
    state->requested = titleIds.size();
    state->checkedSequence = checkedSequence;
    m_state = state;
    m_future = pool.submit([state, titleIds = std::move(titleIds)]() {
#ifdef SWITCHU_MENU
        state->playtime = switchu::menu::playtime::queryAll(titleIds, &state->cancelled, state->profile);
#else
        (void)titleIds;
#endif
    });
    return true;
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
#ifdef SWITCHU_MENU
    if (state && (state->profile != m_profile ||
        state->profile != switchu::menu::playtime::currentProfile(config))) {
        DebugLog::log("[playtime] discarded result for a profile left behind");
        failed = true;
    }
#endif
    if (!failed && state && !state->cancelled.load()) {
        for (const auto& [titleId, nanoseconds] : state->playtime)
            changed += config.setPlaytime(titleId, nanoseconds) ? 1 : 0;
        // Advanced only when every title answered: a stopped or failed batch
        // leaves the opened-since titles to be asked again next time.
        if (state->playtime.size() == state->requested &&
            state->checkedSequence > config.playtimeCheckedSequence)
            config.playtimeCheckedSequence = state->checkedSequence;
    }
    DebugLog::log("[playtime] refresh done titles=%zu changed=%d",
                  state ? state->playtime.size() : static_cast<std::size_t>(0), changed);
    // Nothing is saved here. Every launch writes the config before the
    // handoff, and that write carries this cache with it.
    return changed;
}

bool HomePlaytime::syncProfile(AppConfig& config) {
#ifdef SWITCHU_MENU
    const auto now = std::chrono::steady_clock::now();
    const std::string preference = config.defaultProfileEnabled ? config.defaultProfileUid : "";
    if (m_profileReady && preference == m_preference &&
        now - m_profileChecked < std::chrono::seconds(3)) return false;
    m_profileChecked = now;
    m_preference = preference;
    const auto user = switchu::menu::playtime::currentProfile(config);
    const bool changed = !m_profileReady || user != m_profile;
    m_profileReady = true;
    m_profile = user;
    const bool invalidated = config.selectPlaytimeProfile(switchu::menu::playtime::profileKey(user));
    if (changed || invalidated) {
        cancel();
        DebugLog::log("[playtime] profile cache invalidated; full refresh required");
        return true;
    }
#else
    (void)config;
#endif
    return false;
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
