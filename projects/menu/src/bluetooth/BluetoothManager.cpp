#include "BluetoothManager.hpp"
#include "core/DebugLog.hpp"
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace bluetooth {

#ifdef SWITCHU_MENU

namespace {

constexpr s32 kMaxDiscovered = 15;
constexpr s32 kMaxPaired = 10;

Event g_connectionEvent;
UEvent g_stopEvent;

std::vector<BtmAudioDevice> g_pairedDevices;
std::recursive_mutex g_pairedLock;
std::atomic_bool g_pairedChanged = false;

BtmAudioDevice g_connectedDevice = {};
std::recursive_mutex g_connectedLock;
std::atomic_bool g_connectedChanged = false;

std::vector<BtmAudioDevice> g_discoveredDevices;
std::recursive_mutex g_discoveredLock;
std::atomic_bool g_discoveredChanged = false;

std::atomic_bool g_threadRunning = false;
std::atomic_int g_pendingOps = 0;
std::atomic_bool g_discovering = false;
std::atomic_bool g_initialized = false;
std::atomic_uint g_discoveryPollCount = 0;

Thread g_thread;
alignas(0x1000) u8 g_threadStack[32 * 1024];

bool UpdateList(std::vector<BtmAudioDevice>& dst, const std::vector<BtmAudioDevice>& src, std::atomic_bool& changed) {
    const bool listChanged = src.size() != dst.size()
        || (!dst.empty() && memcmp(dst.data(), src.data(), sizeof(BtmAudioDevice) * dst.size()) != 0);
    dst = src;
    if (listChanged)
        changed.store(true, std::memory_order_release);
    return listChanged;
}

std::string SanitizedDeviceName(const BtmAudioDevice& device) {
    size_t len = 0;
    while (len < sizeof(device.name) && device.name[len] != '\0')
        ++len;

    std::string name(device.name, device.name + len);
    name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char ch) {
        return ch < 0x20 || ch == 0x7F;
    }), name.end());

    if (!name.empty())
        return name;

    char fallback[32] = {};
    std::snprintf(fallback, sizeof(fallback), "%02X:%02X:%02X:%02X:%02X:%02X",
                  device.addr.address[0], device.addr.address[1], device.addr.address[2],
                  device.addr.address[3], device.addr.address[4], device.addr.address[5]);
    return fallback;
}

void ReloadConnected() {
    std::lock_guard<std::recursive_mutex> lk(g_connectedLock);
    BtdrvAddress prevAddr = g_connectedDevice.addr;
    g_connectedDevice = {};

    s32 count = 0;
    Result rc = btmsysGetConnectedAudioDevices(&g_connectedDevice, 1, &count);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] GetConnectedAudioDevices failed: 0x%x", rc);
        return;
    }
    count = std::clamp(count, 0, 1);
    if (!AddressesEqual(g_connectedDevice.addr, prevAddr)) {
        g_connectedChanged = true;
        if (count > 0)
            DebugLog::log("[bt] Connected: %s", g_connectedDevice.name);
        else
            DebugLog::log("[bt] No connected audio device");
    }
}

void ReloadPaired() {
    std::lock_guard<std::recursive_mutex> lk(g_pairedLock);
    BtmAudioDevice buf[kMaxPaired] = {};
    s32 count = 0;
    Result rc = btmsysGetPairedAudioDevices(buf, kMaxPaired, &count);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] GetPairedAudioDevices failed: 0x%x", rc);
        return;
    }
    count = std::clamp(count, 0, kMaxPaired);
    std::vector<BtmAudioDevice> list(buf, buf + count);
    if (UpdateList(g_pairedDevices, list, g_pairedChanged))
        DebugLog::log("[bt] Paired devices changed (%d)", count);
}

s32 ReloadDiscovered() {
    std::lock_guard<std::recursive_mutex> lk(g_discoveredLock);
    BtmAudioDevice buf[kMaxDiscovered] = {};
    s32 count = 0;
    Result rc = btmsysGetDiscoveredAudioDevice(buf, kMaxDiscovered, &count);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] GetDiscoveredAudioDevice failed: 0x%x", rc);
        return -1;
    }
    count = std::clamp(count, 0, kMaxDiscovered);
    std::vector<BtmAudioDevice> list(buf, buf + count);
    if (UpdateList(g_discoveredDevices, list, g_discoveredChanged))
        DebugLog::log("[bt] Discovered devices changed (%d)", count);
    return count;
}

// btm connect/disconnect/unpair calls can block for seconds (or never return
// while the headset is out of range), so they run on one persistent worker
// instead of the UI thread. The worker is joined, never detached: detach()
// throws when the thread already finished, and that aborted the whole menu.
struct BtOp {
    const char* what;
    std::function<Result()> run;
};
std::thread* g_opThread = nullptr;
std::mutex g_opMutex;
std::condition_variable g_opCv;
std::deque<BtOp> g_ops;
bool g_opStop = false;

void OpWorker() {
    for (;;) {
        BtOp op;
        {
            std::unique_lock<std::mutex> lk(g_opMutex);
            g_opCv.wait(lk, [] { return g_opStop || !g_ops.empty(); });
            if (g_ops.empty())
                return; // stop requested and nothing left to run
            op = std::move(g_ops.front());
            g_ops.pop_front();
        }
        const auto start = std::chrono::steady_clock::now();
        const Result rc = op.run();
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        DebugLog::log("[bt] %s finished rc=0x%x in %lld ms", op.what, rc, static_cast<long long>(ms));
        g_connectedChanged = true;
        g_discoveredChanged = true;
        g_pairedChanged = true;
        g_pendingOps--;
    }
}

void RunAsync(const char* what, std::function<Result()> op) {
    std::lock_guard<std::mutex> lk(g_opMutex);
    if (!g_opThread)
        g_opThread = new std::thread(OpWorker);
    g_pendingOps++;
    g_ops.push_back({what, std::move(op)});
    g_opCv.notify_one();
}

void StopOpWorker() {
    std::thread* worker = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_opMutex);
        g_opStop = true;
        g_opCv.notify_all();
        worker = g_opThread;
    }
    if (!worker)
        return;
    // A connect that never returns must not hang shutdown: wait briefly, and
    // if the worker is still busy leave it to die with the process.
    for (int i = 0; i < 60 && g_pendingOps > 0; ++i)
        svcSleepThread(50'000'000);
    if (g_pendingOps == 0) {
        worker->join();
        delete worker;
        std::lock_guard<std::mutex> lk(g_opMutex);
        g_opThread = nullptr;
        g_opStop = false;
    }
}

void ThreadFunc(void*) {
    DebugLog::log("[bt] Thread started");
    Result rc = btmsysAcquireAudioDeviceConnectionEvent(&g_connectionEvent);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] AcquireAudioDeviceConnectionEvent failed: 0x%x", rc);
        return;
    }

    while (g_threadRunning) {
        s32 waitIndex = -1;
        rc = waitMulti(&waitIndex, 500'000'000,
                       waiterForEvent(&g_connectionEvent),
                       waiterForUEvent(&g_stopEvent));
        if (!g_threadRunning || (R_SUCCEEDED(rc) && waitIndex == 1))
            break;
        if (R_FAILED(rc) && rc != KERNELRESULT(TimedOut)) {
            DebugLog::log("[bt] connection event wait error: 0x%x", rc);
        } else {
            ReloadConnected();
            ReloadPaired();
            if (g_discovering) {
                bool serviceDiscovering = true;
                const Result stateRc = btmsysIsDiscoveryingAudioDevice(&serviceDiscovering);
                const s32 count = ReloadDiscovered();
                const unsigned poll = g_discoveryPollCount.fetch_add(1, std::memory_order_relaxed) + 1;
                if (poll == 1 || (poll % 10) == 0) {
                    DebugLog::log("[bt] Discovery poll active=%d results=%d status=0x%x",
                                  serviceDiscovering ? 1 : 0, count, stateRc);
                }
                if (R_SUCCEEDED(stateRc) && !serviceDiscovering) {
                    g_discovering = false;
                    g_discoveredChanged.store(true, std::memory_order_release);
                    DebugLog::log("[bt] Discovery completed (%d result(s))", count);
                }
            }
        }
    }

    DebugLog::log("[bt] Thread exiting");
    eventClose(&g_connectionEvent);
}

} // anonymous namespace

void Initialize() {
    if (g_initialized) return;

    Result rc = btmsysInitialize();
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] btmsysInitialize failed: 0x%x", rc);
        return;
    }

    ReloadPaired();
    ReloadConnected();

    bool radioEnabled = false;
    Result radioRc = btmsysGetRadioOnOff(&radioEnabled);
    bool configuredEnabled = false;
    Result configuredRc = setsysGetBluetoothEnableFlag(&configuredEnabled);
    DebugLog::log("[bt] Radio runtime=%d (0x%x) configured=%d (0x%x)",
                  radioEnabled ? 1 : 0, radioRc,
                  configuredEnabled ? 1 : 0, configuredRc);

    ueventCreate(&g_stopEvent, false);
    g_threadRunning = true;
    rc = threadCreate(&g_thread, ThreadFunc, nullptr, g_threadStack, sizeof(g_threadStack), 0x2C, 2);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] threadCreate failed: 0x%x", rc);
        g_threadRunning = false;
        btmsysExit();
        return;
    }
    rc = threadStart(&g_thread);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] threadStart failed: 0x%x", rc);
        g_threadRunning = false;
        threadClose(&g_thread);
        btmsysExit();
        return;
    }

    g_initialized = true;
    DebugLog::log("[bt] Initialized");
}

void Finalize() {
    if (!g_initialized) return;

    if (g_discovering) StopDiscovery();

    StopOpWorker();
    g_threadRunning = false;
    // UEvent is user-mode only, so it does not consume the kernel handle that
    // made the original stop Event fail with ResultLimitReached. waitMulti
    // registers the waiter before sleeping, avoiding the cancellation race of
    // calling svcCancelSynchronization directly.
    ueventSignal(&g_stopEvent);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    btmsysExit();
    g_initialized = false;
    DebugLog::log("[bt] Finalized");
}

bool IsAvailable() { return g_initialized; }

bool IsRadioEnabled() {
    if (g_initialized) {
        bool enabled = false;
        Result rc = btmsysGetRadioOnOff(&enabled);
        if (R_SUCCEEDED(rc))
            return enabled;
        DebugLog::log("[bt] GetRadioOnOff failed: 0x%x", rc);
    }

    bool enabled = false;
    Result rc = setsysGetBluetoothEnableFlag(&enabled);
    if (R_FAILED(rc))
        DebugLog::log("[bt] GetBluetoothEnableFlag failed: 0x%x", rc);
    return enabled;
}

Result SetRadioEnabled(bool enabled) {
    if (!g_initialized)
        return setsysSetBluetoothEnableFlag(enabled);

    if (!enabled && g_discovering)
        StopDiscovery();

    const Result runtimeRc = enabled ? btmsysEnableRadio() : btmsysDisableRadio();
    const Result configuredRc = setsysSetBluetoothEnableFlag(enabled);
    DebugLog::log("[bt] Radio -> %d runtime=0x%x configured=0x%x",
                  enabled ? 1 : 0, runtimeRc, configuredRc);
    return R_FAILED(runtimeRc) ? runtimeRc : configuredRc;
}

std::vector<BtmAudioDevice> ListPairedAudioDevices() {
    std::lock_guard<std::recursive_mutex> lk(g_pairedLock);
    return g_pairedDevices;
}

bool HasPairedChanges() {
    return g_pairedChanged.exchange(false, std::memory_order_acq_rel);
}

BtmAudioDevice GetConnectedAudioDevice() {
    std::lock_guard<std::recursive_mutex> lk(g_connectedLock);
    return g_connectedDevice;
}

bool HasConnectedChanges() {
    return g_connectedChanged.exchange(false, std::memory_order_acq_rel);
}

std::vector<BtmAudioDevice> ListDiscoveredAudioDevices() {
    std::lock_guard<std::recursive_mutex> lk(g_discoveredLock);
    return g_discoveredDevices;
}

bool HasDiscoveredChanges() {
    return g_discoveredChanged.exchange(false, std::memory_order_acq_rel);
}

Result ConnectAudioDevice(const BtmAudioDevice& device) {
    g_connectedChanged = true;
    g_discoveredChanged = true;
    g_pairedChanged = true;
    const BtdrvAddress addr = device.addr;
    RunAsync("connect", [addr]() { return btmsysConnectAudioDevice(addr); });
    return 0;
}

Result DisconnectAudioDevice(const BtmAudioDevice& device) {
    {
        std::lock_guard<std::recursive_mutex> lk(g_connectedLock);
        g_connectedDevice = {};
    }
    g_connectedChanged = true;
    const BtdrvAddress addr = device.addr;
    RunAsync("disconnect", [addr]() { return btmsysDisconnectAudioDevice(addr); });
    return 0;
}

Result UnpairAudioDevice(const BtmAudioDevice& device) {
    g_pairedChanged = true;
    g_discoveredChanged = true;
    const BtdrvAddress addr = device.addr;
    RunAsync("unpair", [addr]() { return btmsysRemoveAudioDevicePairing(addr); });
    return 0;
}

std::string DeviceName(const BtmAudioDevice& device) {
    return SanitizedDeviceName(device);
}

void StartDiscovery() {
    if (!g_initialized)
        return;

    bool radioEnabled = false;
    Result rc = btmsysGetRadioOnOff(&radioEnabled);
    if (R_FAILED(rc)) {
        DebugLog::log("[bt] Discovery radio query failed: 0x%x", rc);
        return;
    }
    if (!radioEnabled) {
        bool configuredEnabled = false;
        Result configuredRc = setsysGetBluetoothEnableFlag(&configuredEnabled);
        if (R_FAILED(configuredRc) || !configuredEnabled) {
            DebugLog::log("[bt] Discovery blocked: radio disabled configured=%d status=0x%x",
                          configuredEnabled ? 1 : 0, configuredRc);
            return;
        }
        rc = btmsysEnableRadio();
        if (R_FAILED(rc)) {
            DebugLog::log("[bt] Discovery radio enable failed: 0x%x", rc);
            return;
        }
        radioEnabled = true;
    }

    {
        std::lock_guard<std::recursive_mutex> lk(g_discoveredLock);
        if (!g_discoveredDevices.empty()) {
            g_discoveredDevices.clear();
            g_discoveredChanged.store(true, std::memory_order_release);
        }
    }

    rc = btmsysStartAudioDeviceDiscovery();
    if (R_FAILED(rc))
        DebugLog::log("[bt] StartDiscovery failed: 0x%x", rc);
    else {
        g_discovering = true;
        g_discoveryPollCount = 0;
        g_discoveredChanged.store(true, std::memory_order_release);
        const s32 count = ReloadDiscovered();
        DebugLog::log("[bt] Discovery started radio=%d initial_results=%d",
                      radioEnabled ? 1 : 0, count);
    }
}

void StopDiscovery() {
    Result rc = btmsysStopAudioDeviceDiscovery();
    if (R_FAILED(rc))
        DebugLog::log("[bt] StopDiscovery failed: 0x%x", rc);
    else
        DebugLog::log("[bt] Discovery stopped");
    g_discovering = false;
    g_discoveredChanged.store(true, std::memory_order_release);
}

bool IsDiscovering() { return g_discovering; }

#else

void Initialize() {}
void Finalize() {}
bool IsAvailable() { return false; }
bool IsRadioEnabled() {
    bool enabled = false;
    setsysGetBluetoothEnableFlag(&enabled);
    return enabled;
}
Result SetRadioEnabled(bool enabled) { return setsysSetBluetoothEnableFlag(enabled); }
std::vector<BtmAudioDevice> ListPairedAudioDevices() { return {}; }
bool HasPairedChanges() { return false; }
BtmAudioDevice GetConnectedAudioDevice() { return {}; }
bool HasConnectedChanges() { return false; }
std::vector<BtmAudioDevice> ListDiscoveredAudioDevices() { return {}; }
bool HasDiscoveredChanges() { return false; }
Result ConnectAudioDevice(const BtmAudioDevice&) { return 0; }
Result DisconnectAudioDevice(const BtmAudioDevice&) { return 0; }
Result UnpairAudioDevice(const BtmAudioDevice&) { return 0; }
std::string DeviceName(const BtmAudioDevice&) { return {}; }
void StartDiscovery() {}
void StopDiscovery() {}
bool IsDiscovering() { return false; }

#endif

}
