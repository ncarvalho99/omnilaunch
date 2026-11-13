#include "discord/DiscordGateway.hpp"

#include "core/DebugLog.hpp"
#include "net/NetService.hpp"
#include "net/WebSocket.hpp"

#include <nlohmann/json.hpp>

#include <chrono>

namespace discord {

namespace {

constexpr const char* kGatewayDefaultUrl = "wss://gateway.discord.gg/?v=10&encoding=json";

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

DiscordGateway::DiscordGateway() = default;

DiscordGateway::~DiscordGateway() {
    stop();
}

void DiscordGateway::start(net::NetService& net, const std::string& token) {
    stop();
    // Resume is only for the same account: another token Identifies afresh.
    if (token != m_token) {
        std::lock_guard<std::mutex> lk(m_mu);
        m_sessionId.clear();
        m_resumeGatewayUrl.clear();
        m_lastSeq = -1;
        m_lastError.clear();
    }
    m_token = token;
    m_stop.store(false);
    m_state.store(State::Connecting);
    m_thread = std::thread([this, &net]() {
        run(net);
        m_state.store(State::Disconnected);
    });
}

void DiscordGateway::stop() {
    m_stop.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_state.store(State::Disconnected);
    // Events of a stopped connection belong to the session that ended.
    std::lock_guard<std::mutex> lk(m_mu);
    m_inEvents.clear();
}

DiscordGateway::State DiscordGateway::state() const {
    return m_state.load();
}

bool DiscordGateway::isConnected() const {
    return m_state.load() == State::Connected;
}

std::string DiscordGateway::lastError() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_lastError;
}

void DiscordGateway::setState(State s, const std::string& err) {
    m_state.store(s);
    if (!err.empty()) {
        std::lock_guard<std::mutex> lk(m_mu);
        m_lastError = err;
    }
}

std::vector<DiscordGateway::Event> DiscordGateway::drainEvents() {
    std::lock_guard<std::mutex> lk(m_mu);
    std::vector<Event> out(m_inEvents.begin(), m_inEvents.end());
    m_inEvents.clear();
    return out;
}

void DiscordGateway::run(net::NetService& net) {
    while (!m_stop.load()) {
        setState(State::Connecting);
        std::string targetUrl = m_resumeGatewayUrl.empty() ? kGatewayDefaultUrl : m_resumeGatewayUrl;
        if (targetUrl.find("?") == std::string::npos) {
            targetUrl += "/?v=10&encoding=json";
        }

        net::WebSocket ws(net);
        std::string wsErr;
        std::vector<std::string> headers = {
            "Origin: https://discord.com",
            "User-Agent: Mozilla/5.0 (Nintendo Switch; Share/1.0.0) OmniLaunch/1.2",
        };

        DebugLog::log("[discord-gw] connecting to %s...", targetUrl.c_str());
        if (!ws.connect(targetUrl, headers, wsErr, &m_stop)) {
            DebugLog::log("[discord-gw] connect failed: %s", wsErr.c_str());
            setState(State::Failed, wsErr);
            // Wait before reconnecting
            for (int i = 0; i < 50 && !m_stop.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }

        setState(State::Handshaking);
        std::int64_t heartbeatInterval = 41250;
        std::int64_t nextHeartbeat = 0;
        bool identified = false;

        while (!m_stop.load() && ws.isOpen()) {
            std::vector<net::ws::Message> msgs;
            if (ws.wait(msgs, 400, &m_stop)) {
                // An exception escaping this thread terminates the HOME Menu,
                // so a payload the parser rejects only drops that batch.
                try {
                for (const auto& msg : msgs) {
                    if (msg.binary) continue;
                    const auto j = nlohmann::json::parse(msg.data, nullptr, false);
                    if (j.is_discarded() || !j.is_object()) continue;

                    const int op = j.value("op", -1);
                    if (j.contains("s") && j["s"].is_number_integer()) {
                        m_lastSeq = j["s"].get<std::int64_t>();
                    }

                    if (op == 10) { // Hello
                        if (j.contains("d") && j["d"].is_object()) {
                            heartbeatInterval = j["d"].value("heartbeat_interval", 41250);
                        }
                        nextHeartbeat = nowMs() + heartbeatInterval / 2;

                        // Identify or Resume
                        if (!m_sessionId.empty() && m_lastSeq >= 0) {
                            nlohmann::json resume;
                            resume["op"] = 6;
                            resume["d"]["token"] = m_token;
                            resume["d"]["session_id"] = m_sessionId;
                            resume["d"]["seq"] = m_lastSeq;
                            ws.sendText(resume.dump());
                            DebugLog::log("[discord-gw] sent Resume for session %s seq %lld", m_sessionId.c_str(),
                                          static_cast<long long>(m_lastSeq));
                        } else {
                            nlohmann::json ident;
                            ident["op"] = 2;
                            ident["d"]["token"] = m_token;
                            ident["d"]["properties"]["os"] = "Nintendo Switch";
                            ident["d"]["properties"]["browser"] = "OmniLaunch";
                            ident["d"]["properties"]["device"] = "Nintendo Switch";
                            ws.sendText(ident.dump());
                            DebugLog::log("[discord-gw] sent Identify");
                        }
                        identified = true;
                    } else if (op == 1) { // Heartbeat requested
                        nlohmann::json hb;
                        hb["op"] = 1;
                        if (m_lastSeq < 0) hb["d"] = nullptr;
                        else hb["d"] = m_lastSeq;
                        ws.sendText(hb.dump());
                    } else if (op == 0) { // Dispatch
                        const std::string t = (j.contains("t") && j["t"].is_string()) ? j["t"].get<std::string>() : std::string();
                        if (t == "READY") {
                            setState(State::Connected);
                            if (j.contains("d") && j["d"].is_object()) {
                                m_sessionId = j["d"].value("session_id", "");
                                m_resumeGatewayUrl = j["d"].value("resume_gateway_url", "");
                            }
                            DebugLog::log("[discord-gw] READY session=%s", m_sessionId.c_str());
                        } else if (t == "RESUMED") {
                            setState(State::Connected);
                            DebugLog::log("[discord-gw] RESUMED successfully");
                        }

                        // Queue dispatch for main thread processing
                        std::lock_guard<std::mutex> lk(m_mu);
                        m_inEvents.push_back(Event{t, msg.data});
                        if (m_inEvents.size() > 200) {
                            m_inEvents.pop_front();
                        }
                    } else if (op == 7) { // Reconnect
                        DebugLog::log("[discord-gw] server requested reconnect");
                        ws.close();
                        break;
                    } else if (op == 9) { // Invalid Session
                        const bool resumable = j.contains("d") && j["d"].is_boolean() && j["d"].get<bool>();
                        DebugLog::log("[discord-gw] invalid session (resumable=%d)", resumable);
                        if (!resumable) {
                            m_sessionId.clear();
                            m_lastSeq = -1;
                        }
                        ws.close();
                        break;
                    }
                }
                } catch (const std::exception& e) {
                    DebugLog::log("[discord-gw] dropped gateway payload: %s", e.what());
                }
            }

            if (identified && nextHeartbeat != 0 && nowMs() >= nextHeartbeat) {
                nlohmann::json hb;
                hb["op"] = 1;
                if (m_lastSeq < 0) hb["d"] = nullptr;
                else hb["d"] = m_lastSeq;
                ws.sendText(hb.dump());
                nextHeartbeat = nowMs() + heartbeatInterval;
            }
        }

        if (m_stop.load()) break;
        // Pause briefly before reconnect
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    }
}

} // namespace discord
