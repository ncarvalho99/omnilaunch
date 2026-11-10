#pragma once

#include <atomic>
#include <cstdint>
#include <list>
#include <stdexcept>
#include <string>
#include <functional>
#include <vector>

namespace themeshop::http {

using ProgressCallback = std::function<void(std::uint64_t downloaded,
                                            std::uint64_t total)>;

bool initialize();
void shutdown();
bool isInitialized();

// Terminal cancel: the menu is handing control to another applet/application
// or exiting. Aborts every request and refuses new ones for the rest of the
// process. Partial files are removed by getToFile(); transition latency takes
// priority over a background transfer.
void cancelPendingRequests();

// Frontend scope. Each request belongs to the scope open when it started.
// closeFrontendScope() aborts that frontend's requests and refuses new ones;
// openFrontendScope() lets the next frontend use the network again. Neither
// clears a terminal cancel. HomeCore calls these around each frontend.
void openFrontendScope();
void closeFrontendScope();

// The process-wide scope above, shared with NetService (HomeNX G1) so one
// frontend switch / launch hand-off cancels every network user the same way.
class CancelScope;
CancelScope& cancelScope();

// Thrown when a request stops because it was cancelled (frontend scope,
// terminal cancel, or the per-transfer flag below). Lets callers tell a user
// cancel from a real failure without re-reading a flag that may have been
// raised after the failure.
struct Cancelled : std::runtime_error {
    Cancelled() : std::runtime_error("HTTP request cancelled") {}
};

// Per-transfer cancel for every request made on this thread while it is
// alive (getBytes/getText/postJson/getToFile). A worker that runs a whole
// install sets it once instead of threading a flag through every call.
class ThreadCancelGuard {
public:
    explicit ThreadCancelGuard(const std::atomic<bool>* cancel);
    ~ThreadCancelGuard();
    ThreadCancelGuard(const ThreadCancelGuard&) = delete;
    ThreadCancelGuard& operator=(const ThreadCancelGuard&) = delete;
private:
    const std::atomic<bool>* m_previous;
};

std::vector<std::uint8_t> getBytes(const std::string& url,
                                   const std::list<std::string>& headers = {},
                                   const ProgressCallback& onProgress = {});

// Grava a resposta direto em disco, sem passar pela memoria.
//
// getBytes acumula o corpo inteiro em um ostringstream e ainda o copia duas
// vezes ate virar vetor -- para um pacote de tema de 40 MB isso passa de 150 MB
// de pico, e o download morre com "Failed writing body". Serve para JSON, nao
// para arquivos grandes.
//
// Devolve o total de bytes escritos. Lanca em erro de HTTP ou de escrita.
std::uint64_t getToFile(const std::string& url,
                        const std::string& destinationPath,
                        const std::function<void(std::uint64_t, std::uint64_t)>& onProgress = {},
                        // Optional per-transfer cancel (user pressed Cancel/HOME). Checked by
                        // the transfer callbacks; no retry after it is set.
                        const std::atomic<bool>* cancel = nullptr);

std::string getText(const std::string& url,
                    const std::list<std::string>& headers = {});

// Connection test (HomeNX G4, requirement C-NET-7) against the project's own
// server, whose /speedtest/ endpoints require the client key. It lives here
// because this module is the only code allowed to send that key; the URLs are
// fixed below and never come from the caller.
// Blocking: run it on a worker. Cancelled by the frontend/terminal scope and
// by `cancel`.
enum class SpeedProbeKind { Ping, Download, Upload };
struct SpeedProbeResult {
    bool ok = false;
    bool cancelled = false;
    long status = 0;
    std::uint64_t bytes = 0;  ///< payload moved (download/upload)
    double seconds = 0.0;     ///< transfer time, excluding connection setup
    double rttMs = 0.0;       ///< ping: best request round trip on a warm connection
    std::string error;
    /// Kilobits per second, or -1 when the run moved too little to tell.
    int kbps() const {
        if (!ok || bytes < 64 * 1024 || seconds <= 0.0) return -1;
        return static_cast<int>(static_cast<double>(bytes) * 8.0 / 1000.0 / seconds);
    }
};
SpeedProbeResult speedProbe(SpeedProbeKind kind, long limitMs, const std::atomic<bool>* cancel = nullptr);

std::string postJson(const std::string& url,
                     const std::string& jsonBody,
                     const std::list<std::string>& headers = {});

} // namespace themeshop::http
