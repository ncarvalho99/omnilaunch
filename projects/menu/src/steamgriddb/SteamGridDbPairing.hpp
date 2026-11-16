#pragma once
// SteamGridDB API key by phone: the console asks the project's gallery service
// for a short pairing, shows its URL as a QR code, and polls until the phone
// has sent a key (checked against SteamGridDB by the service) or the pairing
// expires. The key is handed over once and never logged.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace steamgriddb {

class KeyPairing {
public:
    enum class Phase { Idle, Requesting, ShowQr, Received, Expired, Failed };

    struct Snapshot {
        Phase phase = Phase::Idle;
        std::string url;
        int qrSize = 0;
        std::vector<std::uint8_t> qrModules; // size * size, 1 = dark
        std::string key;                     // only in Received
        std::string error;
    };

    KeyPairing() = default;
    ~KeyPairing();
    KeyPairing(const KeyPairing&) = delete;
    KeyPairing& operator=(const KeyPairing&) = delete;

    void start();
    // Stops polling and drops the pairing on the server. Joins the worker.
    void stop();

    Snapshot snapshot() const;
    // The received key, once; Idle afterwards.
    bool takeKey(std::string& out);

private:
    void run();
    void set(Phase phase, const std::string& error = {});

    std::thread m_thread;
    std::atomic<bool> m_stop{false};
    mutable std::mutex m_mu;
    Snapshot m_snap;
    std::string m_id;
};

} // namespace steamgriddb
