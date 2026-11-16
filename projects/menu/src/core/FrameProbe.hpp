#pragma once
// Named time marks inside one render-thread frame, reported only when that
// frame turns out slow (see the slow-frame hook in main.cpp). Render thread
// only; a mark is one tick read and an array store, so it stays in release.
#include <cstdint>
#include <cstdio>
#include <switch.h>

namespace switchu::menu::frame_probe {

struct Mark {
    const char* name;
    std::uint64_t tick;
};

constexpr int kMaxMarks = 48;

struct State {
    Mark marks[kMaxMarks];
    int count = 0;
    std::uint64_t begin = 0;
};

inline State& state() {
    static State s;
    return s;
}

inline void begin() {
    auto& s = state();
    s.count = 0;
    s.begin = armGetSystemTick();
}

// `name` must be a string literal: only the pointer is kept.
inline void mark(const char* name) {
    auto& s = state();
    if (s.count < kMaxMarks)
        s.marks[s.count++] = {name, armGetSystemTick()};
}

// The marks of this frame that took at least `minMs` since the previous one,
// as "name=12.3 name=40.1", written into `out`.
// Marks taken before `frameStart` belong to an older frame (another frontend
// is running and never calls begin()), so they are not reported.
inline void formatSlowSteps(char* out, std::size_t size, double minMs,
                            std::uint64_t frameStart) {
    auto& s = state();
    std::size_t used = 0;
    out[0] = '\0';
    if (s.begin < frameStart)
        return;
    std::uint64_t previous = s.begin;
    for (int i = 0; i < s.count && used + 1 < size; ++i) {
        const double ms = armTicksToNs(s.marks[i].tick - previous) / 1'000'000.0;
        previous = s.marks[i].tick;
        if (ms < minMs)
            continue;
        const int n = std::snprintf(out + used, size - used, "%s%s=%.1f",
                                    used ? " " : "", s.marks[i].name, ms);
        if (n <= 0)
            break;
        used += static_cast<std::size_t>(n);
    }
}

} // namespace switchu::menu::frame_probe
