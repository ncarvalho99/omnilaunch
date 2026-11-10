#include "discord/DiscordRemoteAuth.hpp"

#include "core/DebugLog.hpp"
#include "discord/DiscordApi.hpp"
#include "net/NetService.hpp"
#include "net/NetUtil.hpp"
#include "net/WebSocket.hpp"
#include "third_party/qrcodegen.hpp"

#include <nlohmann/json.hpp>

#ifdef __SWITCH__
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>
#endif

#include <chrono>
#include <cstring>
#include <vector>

namespace discord {

namespace {

constexpr const char* kRemoteAuthGatewayUrl = "wss://remote-auth-gateway.discord.gg/?v=2";

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::vector<std::uint8_t> base64Decode(const std::string& in) {
    std::vector<std::uint8_t> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++)
        T["ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[i]] = i;
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (c == '=') break;
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<std::uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string base64Url(const std::uint8_t* data, std::size_t size) {
    std::string s = net::base64(data, size);
    std::string res;
    res.reserve(s.size());
    for (char c : s) {
        if (c == '+') res += '-';
        else if (c == '/') res += '_';
        else if (c != '=') res += c;
    }
    return res;
}

} // namespace

struct DiscordRemoteAuth::RsaKey {
#ifdef __SWITCH__
    mbedtls_pk_context pk;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_context entropy;
#endif
    bool ok = false;
    std::string spkiB64;

    RsaKey() {
#ifdef __SWITCH__
        mbedtls_pk_init(&pk);
        mbedtls_ctr_drbg_init(&drbg);
        mbedtls_entropy_init(&entropy);
#endif
    }

    ~RsaKey() {
#ifdef __SWITCH__
        mbedtls_pk_free(&pk);
        mbedtls_ctr_drbg_free(&drbg);
        mbedtls_entropy_free(&entropy);
#endif
    }

    bool generate() {
#ifdef __SWITCH__
        static const unsigned char kPers[] = "omnilaunch-remote-auth";
        if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, kPers, sizeof(kPers) - 1) != 0)
            return false;
        if (mbedtls_pk_setup(&pk, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA)) != 0)
            return false;
        mbedtls_rsa_context* rsa = mbedtls_pk_rsa(pk);
        if (mbedtls_rsa_gen_key(rsa, mbedtls_ctr_drbg_random, &drbg, 2048, 65537) != 0)
            return false;
        mbedtls_rsa_set_padding(rsa, MBEDTLS_RSA_PKCS_V21, MBEDTLS_MD_SHA256);
        unsigned char der[1024];
        int len = mbedtls_pk_write_pubkey_der(&pk, der, sizeof(der));
        if (len <= 0) return false;
        spkiB64 = net::base64(der + sizeof(der) - len, static_cast<std::size_t>(len));
        ok = true;
        return true;
#else
        ok = false;
        return false;
#endif
    }

    bool decrypt(const std::string& b64, std::vector<std::uint8_t>& out) {
#ifdef __SWITCH__
        std::vector<std::uint8_t> in = base64Decode(b64);
        mbedtls_rsa_context* rsa = mbedtls_pk_rsa(pk);
        if (in.size() != mbedtls_rsa_get_len(rsa)) return false;
        out.assign(512, 0);
        size_t olen = 0;
#if MBEDTLS_VERSION_MAJOR >= 3
        int rc = mbedtls_rsa_rsaes_oaep_decrypt(rsa, mbedtls_ctr_drbg_random, &drbg, nullptr, 0, &olen, in.data(),
                                                out.data(), out.size());
#else
        int rc = mbedtls_rsa_rsaes_oaep_decrypt(rsa, mbedtls_ctr_drbg_random, &drbg, MBEDTLS_RSA_PRIVATE, nullptr, 0,
                                                &olen, in.data(), out.data(), out.size());
#endif
        if (rc != 0) {
            DebugLog::log("[discord-ra] rsa decrypt failed: -0x%04x", -rc);
            return false;
        }
        out.resize(olen);
        return true;
#else
        (void)b64;
        (void)out;
        return false;
#endif
    }
};

DiscordRemoteAuth::DiscordRemoteAuth() = default;

DiscordRemoteAuth::~DiscordRemoteAuth() {
    stop();
}

void DiscordRemoteAuth::start(net::NetService& net) {
    stop();
    m_stop.store(false);
    m_done.store(false);
    {
        std::lock_guard<std::mutex> lk(m_mu);
        m_snap = Snapshot{};
        m_snap.phase = Phase::Preparing;
    }
    m_thread = std::thread([this, &net]() {
        run(net);
        m_done.store(true);
    });
}

void DiscordRemoteAuth::stop() {
    m_stop.store(true);
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_done.store(true);
}

DiscordRemoteAuth::Snapshot DiscordRemoteAuth::snapshot() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_snap;
}

DiscordRemoteAuth::Phase DiscordRemoteAuth::phase() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return m_snap.phase;
}

bool DiscordRemoteAuth::isDone() const {
    return m_done.load();
}

void DiscordRemoteAuth::setPhase(Phase p, const std::string& err) {
    std::lock_guard<std::mutex> lk(m_mu);
    m_snap.phase = p;
    if (!err.empty()) m_snap.error = err;
}

void DiscordRemoteAuth::run(net::NetService& net) {
    DebugLog::log("[discord-ra] generating RSA-2048 key...");
    RsaKey key;
    if (!key.generate()) {
        DebugLog::log("[discord-ra] RSA key generation failed");
        setPhase(Phase::Failed, "Failed to generate security key.");
        return;
    }

    net::WebSocket ws(net);
    std::string wsErr;
    std::vector<std::string> headers = {
        "Origin: https://discord.com",
    };

    DebugLog::log("[discord-ra] connecting to %s...", kRemoteAuthGatewayUrl);
    if (!ws.connect(kRemoteAuthGatewayUrl, headers, wsErr, &m_stop)) {
        DebugLog::log("[discord-ra] connection failed: %s", wsErr.c_str());
        setPhase(Phase::Failed, "Failed to connect to Discord Remote Auth.");
        return;
    }

    std::int64_t nextHeartbeat = 0;
    std::int64_t heartbeatInterval = 41250;
    bool proofSent = false;

    while (!m_stop.load() && ws.isOpen()) {
        std::vector<net::ws::Message> msgs;
        if (ws.wait(msgs, 500, &m_stop)) {
            for (const auto& msg : msgs) {
                if (msg.binary) continue;
                const auto j = nlohmann::json::parse(msg.data, nullptr, false);
                if (j.is_discarded() || !j.is_object()) continue;

                const std::string op = j.value("op", "");
                if (op == "hello") {
                    heartbeatInterval = j.value("heartbeat_interval", 41250);
                    nextHeartbeat = nowMs() + heartbeatInterval;
                    const int timeout = j.value("timeout_ms", 120000);
                    {
                        std::lock_guard<std::mutex> lk(m_mu);
                        m_snap.expiresMs = nowMs() + timeout;
                    }
                    nlohmann::json init;
                    init["op"] = "init";
                    init["encoded_public_key"] = key.spkiB64;
                    ws.sendText(init.dump());
                } else if (op == "nonce_proof") {
                    std::vector<std::uint8_t> nonce;
                    if (!key.decrypt(j.value("encrypted_nonce", ""), nonce)) {
                        setPhase(Phase::Failed, "Login handshake decryption failed.");
                        ws.close();
                        return;
                    }
                    if (m_hashedProof) {
#ifdef __SWITCH__
                        unsigned char digest[32] = {};
#if MBEDTLS_VERSION_MAJOR >= 3
                        mbedtls_sha256(nonce.data(), nonce.size(), digest, 0);
#else
                        mbedtls_sha256_ret(nonce.data(), nonce.size(), digest, 0);
#endif
                        nlohmann::json proof;
                        proof["op"] = "nonce_proof";
                        proof["proof"] = base64Url(digest, 32);
                        ws.sendText(proof.dump());
#endif
                    } else {
                        nlohmann::json proof;
                        proof["op"] = "nonce_proof";
                        proof["nonce"] = base64Url(nonce.data(), nonce.size());
                        ws.sendText(proof.dump());
                    }
                    proofSent = true;
                } else if (op == "pending_remote_init") {
                    const std::string fp = j.value("fingerprint", "");
                    const std::string url = "https://discord.com/ra/" + fp;
                    DebugLog::log("[discord-ra] QR URL ready: %s", url.c_str());

                    // Generate QR matrix
                    const auto qr = qrcodegen::QrCode::encodeText(url.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);
                    const int size = qr.getSize();
                    std::vector<std::uint8_t> modules(static_cast<std::size_t>(size * size), 0);
                    for (int y = 0; y < size; ++y) {
                        for (int x = 0; x < size; ++x) {
                            modules[static_cast<std::size_t>(y * size + x)] = qr.getModule(x, y) ? 1 : 0;
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lk(m_mu);
                        m_snap.qrUrl = url;
                        m_snap.qrSize = size;
                        m_snap.qrModules = std::move(modules);
                        m_snap.phase = Phase::ShowQr;
                        m_snap.error.clear();
                    }
                } else if (op == "pending_ticket") {
                    std::vector<std::uint8_t> payload;
                    if (key.decrypt(j.value("encrypted_user_payload", ""), payload)) {
                        const std::string s(payload.begin(), payload.end());
                        // Colon-separated: userId:discriminator:avatar:username
                        std::vector<std::string> parts;
                        std::stringstream ss(s);
                        std::string p;
                        while (std::getline(ss, p, ':')) parts.push_back(p);
                        std::lock_guard<std::mutex> lk(m_mu);
                        if (parts.size() >= 4) {
                            m_snap.userId = parts[0];
                            m_snap.avatar = (parts[2] == "0") ? "" : parts[2];
                            m_snap.username = parts[3];
                            for (std::size_t i = 4; i < parts.size(); ++i) {
                                m_snap.username += ":" + parts[i];
                            }
                        }
                        m_snap.phase = Phase::Confirm;
                        DebugLog::log("[discord-ra] scanned by phone, user=%s", m_snap.username.c_str());
                    }
                } else if (op == "pending_login") {
                    setPhase(Phase::LoggingIn);
                    const std::string ticket = j.value("ticket", "");
                    std::string encryptedToken, exchangeErr;
                    if (api::exchangeRemoteAuthTicket(net, ticket, encryptedToken, exchangeErr)) {
                        std::vector<std::uint8_t> tokenBytes;
                        if (key.decrypt(encryptedToken, tokenBytes)) {
                            std::string token(tokenBytes.begin(), tokenBytes.end());
                            std::lock_guard<std::mutex> lk(m_mu);
                            m_snap.token = std::move(token);
                            m_snap.phase = Phase::Done;
                            DebugLog::log("[discord-ra] login successful for user %s", m_snap.username.c_str());
                            ws.close();
                            return;
                        }
                    }
                    setPhase(Phase::Failed, exchangeErr.empty() ? "Could not decrypt session token." : exchangeErr);
                    ws.close();
                    return;
                } else if (op == "cancel") {
                    DebugLog::log("[discord-ra] login cancelled on phone");
                    setPhase(Phase::Cancelled);
                    ws.close();
                    return;
                }
            }
        }

        if (nextHeartbeat != 0 && nowMs() >= nextHeartbeat) {
            ws.sendText("{\"op\":\"heartbeat\"}");
            nextHeartbeat = nowMs() + heartbeatInterval;
        }
    }

    if (m_snap.phase == Phase::Preparing && proofSent) {
        m_hashedProof = !m_hashedProof; // retry alternate proof format if needed
    }
}

} // namespace discord
