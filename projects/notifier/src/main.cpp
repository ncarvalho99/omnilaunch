// OmniLaunch Notifier: in-game Discord direct-message toasts (homenx_todo J1).
//
// A separate, short-lived system module. The OmniLaunch daemon starts it only
// while a game is running and the player turned the option on, and terminates
// it when the game ends, before sleep and before any power action. It never
// runs on HOME: the menu has its own Discord session there.
//
// Kept out of the daemon on purpose (design review 2026-11-13): a TLS or JSON
// fault here ends this process only; the daemon, which is the console's HOME
// and lifecycle authority, is untouched. Its memory comes from its own heap and
// is returned when it exits.
//
// What it does, every kPollSeconds:
//   GET /users/@me/channels           (DMs and their last_message_id)
//   GET /channels/{id}/messages?after (only for DMs that moved, max 3)
// and writes one Ultrahand toast per new message from someone else:
//   sdmc:/config/ultrahand/notifications/omnilaunch-discord-<n>.notify
// Toasts it wrote on the previous round are removed, so message text does not
// stay on the card. No gateway, no sending, nothing marked read on Discord.

#include <switch.h>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

extern "C" {
u32 __nx_applet_type = AppletType_None;
u32 __nx_fs_num_sessions = 1;
TimeServiceType __nx_time_service_type = TimeServiceType_System;

// libcurl + mbedtls-free (libnx ssl service) transfers stay well under this.
constexpr size_t kHeapSize = 0x300000; // 3 MiB
alignas(0x1000) static char g_heap[kHeapSize];

void __libnx_initheap(void) {
    extern char* fake_heap_start;
    extern char* fake_heap_end;
    fake_heap_start = g_heap;
    fake_heap_end = g_heap + kHeapSize;
}

void __appInit(void) {
    if (R_FAILED(smInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_SM));
    if (R_FAILED(fsInitialize())) diagAbortWithResult(MAKERESULT(Module_Libnx, LibnxError_InitFail_FS));
    fsdevMountSdmc();
    timeInitialize();
    // sm stays open: nifm, bsd and ssl are looked up later, in main().
}

void __appExit(void) {
    timeExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
}
}

namespace {

constexpr int kPollSeconds = 120;          // owner choice 2026-11-13: >= 2 min
constexpr int kFirstPollSeconds = 20;      // let the game finish starting first
constexpr int kMaxToastsPerRound = 3;
constexpr int kMaxChannelsPerRound = 3;
constexpr size_t kMaxBody = 256 * 1024;
constexpr const char* kAccounts = "sdmc:/config/OmniLaunch/discord_accounts.json";
constexpr const char* kState = "sdmc:/config/OmniLaunch/notifier_state.json";
constexpr const char* kLog = "sdmc:/config/OmniLaunch/notifier.log";
constexpr const char* kToastDir = "sdmc:/config/ultrahand/notifications";
constexpr const char* kToastPrefix = "omnilaunch-discord-";
constexpr const char* kCertDir = "sdmc:/switch/OmniLaunch/certs/";
constexpr const char* kRoots[] = {"gts-root-r4.pem", "gts-root-r1.pem", "isrg-root-x1.pem",
                                  "isrg-root-x2.pem", "gts-root-r3.pem", "globalsign-root-r3.pem"};

// The daemon ends this process with pm (a hard stop); nothing sets this today.
// It is kept so a cooperative stop can be added without touching the loops.
std::atomic<bool> g_stop{false};

// Short log; never the token or any message text.
void logLine(const char* fmt, ...) {
    FILE* f = std::fopen(kLog, "a");
    if (!f) return;
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::fprintf(f, "[%04d-%02d-%02d %02d:%02d:%02dZ] ", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                 tm.tm_hour, tm.tm_min, tm.tm_sec);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

void trimLog() {
    std::error_code ec;
    if (std::filesystem::file_size(kLog, ec) > 64 * 1024 && !ec)
        std::filesystem::remove(kLog, ec);
}

std::string readFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return s.size() > kMaxBody ? std::string() : s;
}

std::string str(const nlohmann::json& o, const char* key) {
    const auto it = o.find(key);
    return (it != o.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::uint64_t snow(const nlohmann::json& o, const char* key) {
    const std::string s = str(o, key);
    return s.empty() ? 0 : std::strtoull(s.c_str(), nullptr, 10);
}

std::string currentToken(std::string& outUserId) {
    const auto j = nlohmann::json::parse(readFile(kAccounts), nullptr, false);
    if (!j.is_object()) return {};
    const std::string current = str(j, "current");
    const auto it = j.find("accounts");
    if (it == j.end() || !it->is_array()) return {};
    for (const auto& a : *it) {
        if (!a.is_object()) continue;
        if (!current.empty() && str(a, "id") != current) continue;
        outUserId = str(a, "id");
        return str(a, "token");
    }
    return {};
}

// ---- HTTP -------------------------------------------------------------------
struct Sink {
    std::string body;
    bool overflow = false;
};

size_t onBody(char* p, size_t size, size_t n, void* user) {
    auto* s = static_cast<Sink*>(user);
    const size_t len = size * n;
    if (s->body.size() + len > kMaxBody) {
        s->overflow = true;
        return 0;
    }
    s->body.append(p, len);
    return len;
}

int onProgress(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return g_stop.load() ? 1 : 0;
}

// GET with the account token. Returns the HTTP status (0 on transport failure).
long httpGet(CURL* c, const std::string& url, const std::string& token, std::string& out, long& retryAfter) {
    Sink sink;
    retryAfter = 0;
    curl_easy_reset(c);
    struct curl_slist* hdr = nullptr;
    const std::string auth = "Authorization: " + token;
    hdr = curl_slist_append(hdr, auth.c_str());
    hdr = curl_slist_append(hdr, "Accept: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "Mozilla/5.0 (Nintendo Switch; Share/1.0.0) OmniLaunch/1.3");
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT_MS, 8000L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 15000L);
    curl_easy_setopt(c, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(c, CURLOPT_BUFFERSIZE, 16384L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, onBody);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, onProgress);
    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_PEER_FAILED_VERIFICATION || rc == CURLE_SSL_CACERT_BADFILE) {
        // Console store refused: try the bundled roots (still verifying).
        for (const char* root : kRoots) {
            sink = Sink{};
            curl_easy_setopt(c, CURLOPT_CAINFO, (std::string(kCertDir) + root).c_str());
            rc = curl_easy_perform(c);
            if (rc == CURLE_OK) break;
        }
    }
    long status = 0;
    if (rc == CURLE_OK) {
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        if (status == 429) {
            const auto j = nlohmann::json::parse(sink.body, nullptr, false);
            if (j.is_object() && j.contains("retry_after") && j["retry_after"].is_number())
                retryAfter = static_cast<long>(j["retry_after"].get<double>()) + 1;
        }
    } else {
        logLine("http %s", curl_easy_strerror(rc));
    }
    curl_slist_free_all(hdr);
    out = std::move(sink.body);
    return status;
}

// ---- State ------------------------------------------------------------------
// Per DM: newest message id already notified (or already there at start).
struct State {
    std::string userId;
    std::map<std::uint64_t, std::uint64_t> seen;
    std::vector<std::string> toasts; // files written last round
};

State loadState() {
    State s;
    const auto j = nlohmann::json::parse(readFile(kState), nullptr, false);
    if (!j.is_object()) return s;
    s.userId = str(j, "user");
    if (j.contains("seen") && j["seen"].is_object())
        for (auto it = j["seen"].begin(); it != j["seen"].end(); ++it)
            if (it.value().is_string())
                s.seen[std::strtoull(it.key().c_str(), nullptr, 10)] =
                    std::strtoull(it.value().get<std::string>().c_str(), nullptr, 10);
    if (j.contains("toasts") && j["toasts"].is_array())
        for (const auto& t : j["toasts"])
            if (t.is_string()) s.toasts.push_back(t.get<std::string>());
    return s;
}

void saveState(const State& s) {
    nlohmann::json j;
    j["user"] = s.userId;
    j["seen"] = nlohmann::json::object();
    for (const auto& [ch, id] : s.seen) j["seen"][std::to_string(ch)] = std::to_string(id);
    j["toasts"] = s.toasts;
    const std::string tmp = std::string(kState) + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out << j.dump();
    }
    std::error_code ec;
    std::filesystem::rename(tmp, kState, ec);
}

// Every toast this module wrote (exact prefix and extension, inside the
// notifications folder only). Scanning the folder rather than trusting the
// saved list also removes toasts left by a run the daemon killed.
void removeOldToasts(State& s) {
    std::error_code ec;
    std::vector<std::filesystem::path> ours;
    for (std::filesystem::directory_iterator it(kToastDir, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.rfind(kToastPrefix, 0) == 0 && name.size() > 7 &&
            name.compare(name.size() - 7, 7, ".notify") == 0)
            ours.push_back(it->path());
    }
    for (const auto& p : ours) std::filesystem::remove(p, ec);
    s.toasts.clear();
}

// Cut to maxBytes without splitting a UTF-8 letter; newlines become spaces.
std::string clip(std::string text, size_t maxBytes) {
    for (char& ch : text)
        if (ch == '\n' || ch == '\r' || ch == '\t') ch = ' ';
    if (text.size() <= maxBytes) return text;
    size_t cut = maxBytes > 3 ? maxBytes - 3 : 0; // room for "..."
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut) + "...";
}

bool writeToast(State& s, const std::string& title, const std::string& text) {
    static unsigned serial = 0;
    char name[64];
    std::snprintf(name, sizeof(name), "%s%llu-%u.notify", kToastPrefix,
                  static_cast<unsigned long long>(armGetSystemTick() / 19200), serial++);
    nlohmann::json j;
    j["title"] = clip(title, 48);
    j["text"] = clip(text, 120);
    j["font_size"] = 20;
    j["priority"] = 20;
    j["duration"] = 5000;
    j["show_time"] = "false";
    std::ofstream out(std::string(kToastDir) + "/" + name, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << j.dump();
    if (!out) return false;
    s.toasts.push_back(name);
    return true;
}

std::string authorName(const nlohmann::json& m) {
    if (!m.contains("author") || !m["author"].is_object()) return "Discord";
    const auto& a = m["author"];
    std::string n = str(a, "global_name");
    if (n.empty()) n = str(a, "username");
    return n.empty() ? std::string("Discord") : n;
}

std::string messageSummary(const nlohmann::json& m) {
    std::string text = str(m, "content");
    if (!text.empty()) return text;
    if (m.contains("attachments") && m["attachments"].is_array() && !m["attachments"].empty())
        return "[image/file]";
    if (m.contains("sticker_items") && m["sticker_items"].is_array() && !m["sticker_items"].empty())
        return "[sticker]";
    if (m.contains("embeds") && m["embeds"].is_array() && !m["embeds"].empty())
        return "[link]";
    return {};
}

// One polling round. Returns seconds to wait before the next one.
int pollOnce(CURL* c, State& s, const std::string& token, const std::string& myId, bool firstRound) {
    removeOldToasts(s);
    std::string body;
    long retry = 0;
    const long st = httpGet(c, "https://discord.com/api/v10/users/@me/channels", token, body, retry);
    if (st == 401 || st == 403) {
        logLine("token refused (%ld): stopping until the next game", st);
        return -1;
    }
    if (st == 429) return std::max<long>(retry, kPollSeconds);
    if (st != 200) return kPollSeconds;
    const auto list = nlohmann::json::parse(body, nullptr, false);
    if (!list.is_array()) return kPollSeconds;

    // DMs that moved since we last looked, newest first.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> moved; // channel, last message
    for (const auto& ch : list) {
        if (!ch.is_object()) continue;
        const int type = ch.contains("type") && ch["type"].is_number() ? ch["type"].get<int>() : -1;
        if (type != 1 && type != 3) continue; // DM, group DM
        const std::uint64_t id = snow(ch, "id"), last = snow(ch, "last_message_id");
        if (id == 0 || last == 0) continue;
        auto it = s.seen.find(id);
        if (firstRound) {
            // Everything already there when the game started is not news.
            std::uint64_t& mark = s.seen[id];
            mark = std::max(mark, last);
            continue;
        }
        if (it == s.seen.end()) {
            // A conversation that started during the game: its messages are new.
            // seen 0 = take its newest messages (limit=5 below).
            s.seen[id] = 0;
            moved.push_back({id, last});
            continue;
        }
        if (last > it->second) moved.push_back({id, last});
    }
    std::sort(moved.begin(), moved.end(), [](auto& a, auto& b) { return a.second > b.second; });
    if (moved.size() > static_cast<size_t>(kMaxChannelsPerRound)) moved.resize(kMaxChannelsPerRound);

    int toasts = 0;
    for (const auto& [chId, last] : moved) {
        if (g_stop.load()) break;
        std::string url = "https://discord.com/api/v10/channels/" + std::to_string(chId) + "/messages?limit=5";
        if (s.seen[chId] != 0) url += "&after=" + std::to_string(s.seen[chId]);
        const long ms = httpGet(c, url, token, body, retry);
        if (ms == 429) return std::max<long>(retry, kPollSeconds);
        if (ms != 200) continue;
        const auto msgs = nlohmann::json::parse(body, nullptr, false);
        if (!msgs.is_array()) continue;
        // Oldest first.
        std::vector<const nlohmann::json*> ordered;
        for (const auto& m : msgs) if (m.is_object()) ordered.push_back(&m);
        std::sort(ordered.begin(), ordered.end(),
                  [](const nlohmann::json* a, const nlohmann::json* b) { return snow(*a, "id") < snow(*b, "id"); });
        for (const auto* m : ordered) {
            const std::uint64_t mid = snow(*m, "id");
            const std::string author = m->contains("author") && (*m)["author"].is_object()
                ? str((*m)["author"], "id") : std::string();
            if (author != myId && toasts < kMaxToastsPerRound) {
                const std::string text = messageSummary(*m);
                if (!text.empty() && writeToast(s, authorName(*m), text)) ++toasts;
            }
            s.seen[chId] = std::max(s.seen[chId], mid);
        }
        s.seen[chId] = std::max(s.seen[chId], last);
    }
    if (toasts > 0) logLine("round: %d toast(s)", toasts);
    return kPollSeconds;
}

// ---- Waiting -----------------------------------------------------------------
// No PSC registration on purpose: a registered module must acknowledge every
// sleep request, and a transfer in progress (up to 15 s) would hold the console
// awake. The daemon terminates this process before sleep and power actions.
void waitSeconds(int seconds) {
    for (int i = 0; i < seconds * 2 && !g_stop.load(); ++i)
        svcSleepThread(500'000'000ULL);
}
bool enabled() {
    std::error_code ec;
    return std::filesystem::exists("sdmc:/config/OmniLaunch/ingame_discord", ec) &&
           std::filesystem::is_directory(kToastDir, ec);
}

} // namespace

int main(int, char**) {
    trimLog();
    if (!enabled()) {
        logLine("off or Ultrahand notifications folder missing: exit");
        return 0;
    }
    std::string myId;
    const std::string token = currentToken(myId);
    if (token.empty()) {
        logLine("no Discord account: exit");
        return 0;
    }
    waitSeconds(kFirstPollSeconds);

    State state = loadState();
    if (state.userId != myId) state = State{}; // another account: start fresh
    state.userId = myId;

    bool netUp = false, sslUp = false, curlUp = false;
    CURL* curl = nullptr;
    if (!g_stop.load() && R_SUCCEEDED(nifmInitialize(NifmServiceType_User))) {
        SocketInitConfig cfg = *socketGetDefaultInitConfig();
        // One session and small buffers: the game owns the network.
        cfg.num_bsd_sessions = 1;
        cfg.tcp_tx_buf_size = 0x4000;
        cfg.tcp_rx_buf_size = 0x8000;
        cfg.tcp_tx_buf_max_size = 0x10000;
        cfg.tcp_rx_buf_max_size = 0x20000;
        cfg.udp_tx_buf_size = 0x2400;
        cfg.udp_rx_buf_size = 0xA500;
        cfg.sb_efficiency = 2;
        netUp = R_SUCCEEDED(socketInitialize(&cfg));
        if (netUp) sslUp = R_SUCCEEDED(sslInitialize(1));
        if (sslUp) curlUp = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
        if (curlUp) curl = curl_easy_init();
        if (!curl) logLine("network init failed (net=%d ssl=%d curl=%d)", netUp, sslUp, curlUp);
    }

    bool first = true;
    while (curl && !g_stop.load()) {
        NifmInternetConnectionStatus status{};
        u32 strength = 0;
        NifmInternetConnectionType type{};
        int wait = kPollSeconds;
        if (R_SUCCEEDED(nifmGetInternetConnectionStatus(&type, &strength, &status)) &&
            status == NifmInternetConnectionStatus_Connected) {
            wait = pollOnce(curl, state, token, myId, first);
            first = false;
            saveState(state);
        }
        if (wait < 0) break;
        waitSeconds(wait);
    }

    // Leave the card clean: our toasts go, the state is written once, and the
    // card is committed once (not per toast).
    removeOldToasts(state);
    saveState(state);
    fsdevCommitDevice("sdmc");

    if (curl) curl_easy_cleanup(curl);
    if (curlUp) curl_global_cleanup();
    if (sslUp) sslExit();
    if (netUp) socketExit();
    nifmExit();
    logLine("exit");
    return 0;
}
