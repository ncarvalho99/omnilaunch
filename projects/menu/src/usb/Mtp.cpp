#include "Mtp.hpp"
#include <core/DebugLog.hpp>

#if defined(__SWITCH__)
#include <haze.hpp>
#include <haze/transfer_status.hpp>
#include <atomic>
#include <cstring>

namespace switchu::usb {

namespace {

    class StopConsumer : public haze::EventConsumer {
    public:
        haze::EventReactor* reactor = nullptr;
        void ProcessEvent() override {
            if (reactor) {
                reactor->SetResult(haze::ResultStopRequested());
            }
        }
    };

    haze::PtpObjectHeap g_heap;
    haze::EventReactor  g_reactor;
    haze::PtpResponder  g_responder;
    StopConsumer        g_stop;
    UEvent              g_stop_event;
    Thread              g_thread;
    bool                g_started = false;
    std::atomic<bool>   g_serving{false};
    std::atomic<bool>   g_connected{false};

    const char* stateName(u32 s) {
        switch (s) {
        case UsbState_Detached: return "detached";
        case UsbState_Attached: return "attached";
        case UsbState_Powered: return "powered";
        case UsbState_Default: return "default";
        case UsbState_Address: return "address";
        case UsbState_Configured: return "configured";
        case UsbState_Suspended: return "suspended";
        default: return "unknown";
        }
    }

    // Each USB state change the host causes, so "no computer answered" can be
    // told apart from a link that never leaves "detached".
    void onUsbState(u32 s) {
        if (s == UsbState_Configured) {
            UsbDeviceSpeed speed{};
            const Result rc = usbDsGetSpeed(&speed);
            DebugLog::log("[mtp] usb state %s speed=%d rc=0x%X", stateName(s), static_cast<int>(speed), rc);
        } else {
            DebugLog::log("[mtp] usb state %s", stateName(s));
        }
    }

    void run(void*) {
        if (R_FAILED(haze::LoadDeviceProperties())) {
            DebugLog::log("[mtp] LoadDeviceProperties failed");
            return;
        }

        // Fails when something else still holds USB; the menu runs without file transfer
        haze::SetUsbStateHook(onUsbState);
        bool usb30 = false;
        const Result usb30Rc = setsysGetUsb30EnableFlag(&usb30);
        if (const auto rc = g_responder.Initialize(&g_reactor, &g_heap).GetValue(); R_FAILED(rc)) {
            DebugLog::log("[mtp] Responder Initialize failed rc=0x%X (USB in use?)", rc);
            g_responder.Finalize();
            return;
        }

        g_stop.reactor = &g_reactor;
        if (g_reactor.AddConsumer(&g_stop, waiterForUEvent(&g_stop_event))) {
            g_serving = true;
            UsbState st = UsbState_Detached;
            const Result stRc = usbDsGetState(&st);
            DebugLog::log("[mtp] MTP server active and listening on USB (state=%s rc=0x%X usb30=%d rc=0x%X)",
                          stateName(st), stRc, usb30 ? 1 : 0, usb30Rc);
            const auto loopRc = g_responder.LoopProcess().GetValue();
            DebugLog::log("[mtp] loop ended rc=0x%X", loopRc);
            g_serving = false;
            g_reactor.RemoveConsumer(&g_stop);
            DebugLog::log("[mtp] MTP server stopped");
        }
        g_responder.Finalize();
    }

} // namespace

void mtpStart() {
    if (g_started) return;
    ueventCreate(&g_stop_event, false);
    g_reactor.SetResult(haze::ResultSuccess());

    // Core 2: priority 0x2E, 64 KB stack
    if (R_FAILED(threadCreate(&g_thread, run, nullptr, nullptr, 0x10000, 0x2E, 2)) &&
        R_FAILED(threadCreate(&g_thread, run, nullptr, nullptr, 0x10000, 0x2E, -2))) {
        DebugLog::log("[mtp] threadCreate failed");
        return;
    }

    threadStart(&g_thread);
    g_started = true;
    DebugLog::log("[mtp] thread started");
}

void mtpStop() {
    if (!g_started) return;
    ueventSignal(&g_stop_event);
    threadWaitForExit(&g_thread);
    threadClose(&g_thread);
    g_started = false;
    g_serving = false;
    g_connected = false;
    DebugLog::log("[mtp] thread joined and closed");
}

bool mtpRunning() { return g_started; }

bool mtpConnected() {
    UsbState st = UsbState_Detached;
    const bool c = g_serving && R_SUCCEEDED(usbDsGetState(&st)) && st == UsbState_Configured;
    g_connected = c;
    return c;
}

void mtpGetStatus(MtpStatus& out) {
    const auto& s = haze::GetTransferStatus();
    out.connected = g_connected;
    out.op        = static_cast<MtpOp>(s.op.load(std::memory_order_acquire));
    out.done      = s.done.load(std::memory_order_relaxed);
    out.total     = s.total.load(std::memory_order_relaxed);
    out.bytes     = s.bytes.load(std::memory_order_relaxed);
    out.received  = s.received.load(std::memory_order_relaxed);
    out.sent      = s.sent.load(std::memory_order_relaxed);
    out.deleted   = s.deleted.load(std::memory_order_relaxed);

    char name[sizeof(out.name)];
    if (haze::TransferName(name, sizeof(name))) {
        std::memcpy(out.name, name, sizeof(name));
    }
}

} // namespace switchu::usb

#else // !__SWITCH__

namespace switchu::usb {

void mtpStart() {}
void mtpStop() {}
bool mtpConnected() { return false; }
bool mtpRunning() { return false; }
void mtpGetStatus(MtpStatus& out) { out = {}; }

} // namespace switchu::usb

#endif
