#pragma once

#include <switch.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace switchu::services {

class NtpClient {
public:
    using SyncCallback = std::function<void(bool success, uint64_t timestamp)>;

    // Queries configured NTP servers (0.pool.ntp.org, 1.pool.ntp.org, 2.pool.ntp.org,
    // 3.pool.ntp.org, pool.ntp.org, time.google.com, time.cloudflare.com) and returns POSIX timestamp (or 0 on failure).
    static uint64_t queryNetworkTime(int timeoutSeconds = 2);

    // Asynchronously queries NTP servers on a native Horizon thread, updates daemon clock if successful,
    // and invokes callback.
    static void syncAsync(SyncCallback callback = nullptr);

    // Wait for worker thread to finish if running and release resources.
    static void cleanup();

    // Drops the pending completion callback without stopping the query, after
    // waiting for one that is already running. An owner whose callback
    // captured it calls this before being destroyed (a live frontend switch
    // destroys Classic and its Settings screen while a sync may be in flight).
    static void detachCallback();

    static const std::vector<std::string>& serverList();
};

} // namespace switchu::services
