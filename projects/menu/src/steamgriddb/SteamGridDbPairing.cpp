#include "steamgriddb/SteamGridDbPairing.hpp"

#include "core/DebugLog.hpp"
#include "third_party/qrcodegen.hpp"
#include "themeshop/ThemeHttp.hpp"

#include <nlohmann/json.hpp>

#include <chrono>

namespace steamgriddb {
namespace {

constexpr const char* kService = "https://gallery.nclabs.dev";
constexpr int kPollSeconds = 3;
constexpr int kMaxSeconds = 10 * 60;

bool validId(const std::string& id) {
    if (id.size() != 10) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '2' && c <= '9'))) return false;
    return true;
}

bool validKey(const std::string& key) {
    if (key.size() < 16 || key.size() > 64) return false;
    for (char c : key)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))) return false;
    return true;
}

} // namespace

KeyPairing::~KeyPairing() {
    stop();
}

void KeyPairing::start() {
    stop();
    m_stop.store(false);
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_snap = Snapshot{};
        m_snap.phase = Phase::Requesting;
        m_id.clear();
    }
    m_thread = std::thread([this]() { run(); });
}

void KeyPairing::stop() {
    m_stop.store(true);
    if (m_thread.joinable())
        m_thread.join();
}

KeyPairing::Snapshot KeyPairing::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mu);
    Snapshot copy = m_snap;
    copy.key.clear(); // only takeKey() hands it out
    return copy;
}

bool KeyPairing::takeKey(std::string& out) {
    std::lock_guard<std::mutex> lk(m_mu);
    if (m_snap.phase != Phase::Received || m_snap.key.empty())
        return false;
    out = std::move(m_snap.key);
    m_snap = Snapshot{};
    return true;
}

void KeyPairing::set(Phase phase, const std::string& error) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_snap.phase = phase;
    m_snap.error = error;
}

void KeyPairing::run() {
    themeshop::http::ThreadCancelGuard guard(&m_stop);
    std::string id;
    try {
        const auto created = nlohmann::json::parse(
            themeshop::http::postJson(std::string(kService) + "/v1/pair", "{}"));
        id = created.value("id", "");
        const std::string url = created.value("url", "");
        if (!validId(id) || url.rfind(std::string(kService) + "/pair/", 0) != 0)
            throw std::runtime_error("unexpected pairing answer");

        const auto qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
        const int size = qr.getSize();
        std::vector<std::uint8_t> modules(static_cast<std::size_t>(size * size), 0);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x)
                modules[static_cast<std::size_t>(y * size + x)] = qr.getModule(x, y) ? 1 : 0;
        {
            std::lock_guard<std::mutex> lk(m_mu);
            m_id = id;
            m_snap.url = url;
            m_snap.qrSize = size;
            m_snap.qrModules = std::move(modules);
            m_snap.phase = Phase::ShowQr;
        }
        DebugLog::log("[sgdb-pair] pairing open");
    } catch (const themeshop::http::Cancelled&) {
        return;
    } catch (const std::exception& ex) {
        DebugLog::log("[sgdb-pair] could not open a pairing: %s", ex.what());
        set(Phase::Failed, ex.what());
        return;
    }

    const auto started = std::chrono::steady_clock::now();
    while (!m_stop.load()) {
        for (int i = 0; i < kPollSeconds * 10 && !m_stop.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (m_stop.load())
            break;
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(kMaxSeconds)) {
            set(Phase::Expired);
            return;
        }
        try {
            const auto polled = nlohmann::json::parse(
                themeshop::http::getText(std::string(kService) + "/v1/pair/" + id));
            if (polled.value("status", "") != "ready")
                continue;
            const std::string key = polled.value("key", "");
            if (!validKey(key)) {
                set(Phase::Failed, "invalid key received");
                return;
            }
            std::lock_guard<std::mutex> lk(m_mu);
            m_snap.key = key;
            m_snap.phase = Phase::Received;
            DebugLog::log("[sgdb-pair] key received (%zu chars)", key.size());
            return;
        } catch (const themeshop::http::Cancelled&) {
            break;
        } catch (const std::exception& ex) {
            const std::string what = ex.what();
            if (what.find("HTTP error 404") != std::string::npos) {
                set(Phase::Expired);
                return;
            }
            DebugLog::log("[sgdb-pair] poll failed: %s", what.c_str());
        }
    }

    // Cancelled on the console: the service forgets the pairing at its expiry.
}

} // namespace steamgriddb
