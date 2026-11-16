#pragma once
#include <array>
#include <cstdint>
#include <map>

namespace switchu::menu::playtime {
using Profile = std::array<std::uint64_t, 2>;
struct PlayEvent {
    enum class Kind { Launch, Account, InFocus, OutOfFocus, Exit, Reset };
    Kind kind;
    std::uint64_t title = 0;
    Profile user{};
    std::uint64_t steady = 0;
};
struct PlayTotal {
    std::uint64_t seconds = 0;
    std::uint32_t launches = 0;
    bool complete = true;
};

// A streaming interpretation of PDM's raw play events (HomeNX #32 concept).
// Only closed, profile-owned sessions contribute; another applet cannot close
// the application's session. No event list or open-session estimate is kept.
class EventTotals {
public:
    explicit EventTotals(Profile user) : m_user(user) {}
    void push(const PlayEvent& event) {
        if (event.kind == PlayEvent::Kind::Reset) { m_open = false; return; }
        if (event.kind == PlayEvent::Kind::Launch) {
            close();
            m_open = event.title != 0;
            m_title = event.title;
            m_mine = m_focused = m_sawInterval = false;
            m_played = 0;
            return;
        }
        if (!m_open) return;
        if (event.kind == PlayEvent::Kind::Account) {
            if (event.user == m_user) m_mine = true;
            return;
        }
        if (event.title != m_title) return;
        if (event.kind == PlayEvent::Kind::InFocus && !m_focused) {
            m_focused = true;
            m_focusAt = event.steady;
        } else if (event.kind == PlayEvent::Kind::OutOfFocus && m_focused) {
            if (event.steady > m_focusAt) {
                m_played += event.steady - m_focusAt;
                m_sawInterval = true;
            }
            m_focused = false;
        } else if (event.kind == PlayEvent::Kind::Exit) {
            // An unmatched focus interval is not a measured duration.
            close();
        }
    }
    const std::map<std::uint64_t, PlayTotal>& values() const { return m_values; }
private:
    void close() {
        if (m_open && m_mine) {
            auto& total = m_values[m_title];
            total.complete = total.complete && m_sawInterval && !m_focused;
            total.seconds += m_played;
            ++total.launches;
        }
        m_open = false;
    }
    Profile m_user;
    std::map<std::uint64_t, PlayTotal> m_values;
    std::uint64_t m_title = 0, m_focusAt = 0, m_played = 0;
    bool m_open = false, m_mine = false, m_focused = false, m_sawInterval = false;
};
} // namespace switchu::menu::playtime
