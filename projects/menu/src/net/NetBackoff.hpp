#pragma once
// Retry policy for NetService requests (HomeNX G1, C-NET-1). Pure; covered by
// tests/net_test.cpp.

#include <algorithm>
#include <cstdint>

namespace net {

/// HTTP statuses worth another attempt: rate limit and server-side failures.
inline bool retryableStatus(long status) {
    return status == 408 || status == 425 || status == 429 || status == 500 ||
           status == 502 || status == 503 || status == 504;
}

/// Delay before attempt `attempt` (1 = the first retry). Exponential from
/// 500 ms, capped at 8 s; a server Retry-After (seconds) wins up to 30 s.
inline std::uint32_t backoffMs(int attempt, double retryAfterSeconds = 0.0) {
    if (retryAfterSeconds > 0.0)
        return static_cast<std::uint32_t>(std::min(retryAfterSeconds, 30.0) * 1000.0);
    const int shift = std::clamp(attempt - 1, 0, 4);
    return std::min<std::uint32_t>(500u << shift, 8000u);
}

} // namespace net
