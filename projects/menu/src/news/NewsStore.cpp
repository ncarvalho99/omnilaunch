#include "news/NewsStore.hpp"
#include "core/DebugLog.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>

// Attribution: adapted from upstream/homenx @ 0448cd4, projects/menu/src/news/NewsStore.cpp.
// Uses libnx services/news.h. Article files are msgpack (subject.text, body.text, topic_name, images as bin).

namespace news {

namespace {

std::string textOf(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    if (it->is_object()) {
        auto t = it->find("text");
        if (t != it->end() && t->is_string()) return t->get<std::string>();
    }
    return {};
}

std::vector<std::uint8_t> binaryOf(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_binary()) return {};
    const auto& bin = it->get_binary();
    return std::vector<std::uint8_t>(bin.begin(), bin.end());
}

} // namespace

bool parseNewsArticleFromMsgpack(const std::vector<std::uint8_t>& bytes, NewsArticle& a) {
    if (bytes.empty()) return false;
    auto j = nlohmann::json::from_msgpack(bytes, true, false);
    if (j.is_discarded()) {
        // Locally stored articles (LS ids) come back as a fixed 512 KiB buffer:
        // the msgpack map followed by padding, which a strict parse rejects
        // (G3 console run 2026-11-10). Read the first value and ignore the rest.
        j = nlohmann::json::from_msgpack(bytes, false, false);
    }
    if (j.is_discarded() || !j.is_object()) return false;

    a.subject = textOf(j, "subject");
    a.topic = textOf(j, "topic_name");
    a.footer = textOf(j, "footer");
    if (auto it = j.find("published_at"); it != j.end() && it->is_number())
        a.publishedAt = it->get<std::uint64_t>();
    if (a.publishedAt > 100000000000ull) a.publishedAt /= 1000; // milliseconds to seconds

    if (auto body = j.find("body"); body != j.end() && body->is_object()) {
        a.body = textOf(*body, "text");
        a.image = binaryOf(*body, "main_image");
    }
    if (a.image.empty()) a.image = binaryOf(j, "list_image");
    if (auto more = j.find("more"); more != j.end() && more->is_object())
        if (auto br = more->find("browser"); br != more->end() && br->is_object())
            a.linkUrl = textOf(*br, "url");

    if (a.subject.empty() && a.body.empty()) {
        // Fallback for non-standard or altered Nintendo layout schemas:
        // Search recursively for title/subject candidate, longest body text, and embedded JPEG binary.
        std::string title, longest;
        std::vector<std::uint8_t> jpeg;
        std::function<void(const nlohmann::json&, const std::string&)> walk = [&](const nlohmann::json& v,
                                                                                 const std::string& key) {
            if (v.is_string()) {
                const std::string& text = v.get_ref<const std::string&>();
                if (text.rfind("http", 0) == 0) return;
                if (title.empty() && (key.find("subject") != std::string::npos || key.find("title") != std::string::npos))
                    title = text;
                if (text.size() > longest.size()) longest = text;
            } else if (v.is_binary()) {
                const auto& bin = v.get_binary();
                if (jpeg.empty() && bin.size() > 2 && bin[0] == 0xFF && bin[1] == 0xD8)
                    jpeg.assign(bin.begin(), bin.end());
            } else if (v.is_object()) {
                for (auto it = v.begin(); it != v.end(); ++it) walk(it.value(), key + "/" + it.key());
            } else if (v.is_array()) {
                for (const auto& e : v) walk(e, key);
            }
        };
        walk(j, "");
        a.subject = title;
        if (longest != title) a.body = longest;
        if (a.image.empty()) a.image = std::move(jpeg);
    }
    return !a.subject.empty() || !a.body.empty();
}

std::set<std::string> loadReadArticleIds(const std::string& path) {
    std::set<std::string> read;
    std::ifstream in(path);
    if (!in.is_open()) return read;
    for (std::string line; std::getline(in, line);) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (!line.empty()) read.insert(line);
    }
    return read;
}

bool markArticleRead(const std::string& id, const std::string& path) {
    if (id.empty()) return false;
    std::error_code ec;
    std::filesystem::path p(path);
    if (p.has_parent_path())
        std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::app);
    if (!out.is_open()) return false;
    out << id << '\n';
    out.flush();
    return static_cast<bool>(out);
}

#ifdef __SWITCH__

namespace {

constexpr u32 kMaxRecords = 300;
constexpr u64 kMaxArticleBytes = 8u * 1024u * 1024u;
// Kept article pictures (encoded JPEG) across one load; past it, text only.
constexpr std::size_t kMaxImageBytesTotal = 12u * 1024u * 1024u;

// newsInitialize/newsExit pair that also runs when the load unwinds.
struct NewsSession {
    Result rc;
    NewsSession() : rc(newsInitialize(NewsServiceType_Administrator)) {}
    ~NewsSession() { if (R_SUCCEEDED(rc)) newsExit(); }
};

Result getList(NewsDatabaseService& db, NewsRecord* records, u32& count) {
    static const char* const attempts[][2] = {
        {"", "received_at DESC"}, {"", ""}, {"1=1", "received_at DESC"}, {"1=1", ""}, {"1", ""},
    };
    Result rc = 0, firstRc = 0;
    int best = -1;
    u32 bestCount = 0;
    for (int i = 0; i < (int)(sizeof(attempts) / sizeof(attempts[0])); ++i) {
        u32 n = 0;
        rc = newsDatabaseGetList(&db, records, kMaxRecords, attempts[i][0], attempts[i][1], &n, 0);
        DebugLog::log("[news] GetList where='%s' order='%s' rc=0x%X count=%u", attempts[i][0], attempts[i][1], rc, n);
        if (i == 0) firstRc = rc;
        if (R_SUCCEEDED(rc) && (best < 0 || n > bestCount)) {
            best = i;
            bestCount = n;
        }
    }
    count = 0;
    if (best < 0) return firstRc;
    if (best == (int)(sizeof(attempts) / sizeof(attempts[0])) - 1 && R_SUCCEEDED(rc)) {
        count = bestCount;
        return 0;
    }
    rc = newsDatabaseGetList(&db, records, kMaxRecords, attempts[best][0], attempts[best][1], &count, 0);
    return rc;
}

Result readArticle(NewsRecord& record, std::vector<std::uint8_t>& out) {
    NewsDataService data;
    Result rc = newsCreateNewsDataService(&data);
    if (R_FAILED(rc)) return rc;
    rc = newsDataOpenWithNewsRecord(&data, &record);
    u64 size = 0;
    if (R_SUCCEEDED(rc)) rc = newsDataGetSize(&data, &size);
    if (R_SUCCEEDED(rc) && (size == 0 || size > kMaxArticleBytes))
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    if (R_SUCCEEDED(rc)) {
        out.resize(size);
        u64 done = 0;
        while (R_SUCCEEDED(rc) && done < size) {
            u64 got = 0;
            rc = newsDataRead(&data, &got, done, out.data() + done, std::min<u64>(size - done, 0x40000));
            if (got == 0) break;
            done += got;
        }
        out.resize(done);
    }
    newsDataClose(&data);
    return rc;
}

} // namespace

std::vector<NewsArticle> loadNewsArticles(Result& rc, int maxArticles, NewsLoadStats* stats) {
    NewsLoadStats local;
    NewsLoadStats& st = stats ? *stats : local;
    std::vector<NewsArticle> articles;
    NewsSession session;
    rc = session.rc;
    if (R_FAILED(rc)) {
        DebugLog::log("[news] newsInitialize rc=0x%X", rc);
        return articles;
    }
    NewsDatabaseService db;
    rc = newsCreateNewsDatabaseService(&db);
    if (R_SUCCEEDED(rc)) {
        std::vector<NewsRecord> records(kMaxRecords); // per call: reentrant
        u32 count = 0;
        rc = getList(db, records.data(), count);
        newsDatabaseClose(&db);
        count = std::min(count, kMaxRecords);
        st.records = count;
        std::sort(records.begin(), records.begin() + count, [](const NewsRecord& a, const NewsRecord& b) {
            return a.received_at > b.received_at;
        });
        std::size_t imageBytes = 0;
        bool headLogged = false;
        for (u32 i = 0; i < count && (int)articles.size() < maxArticles; ++i) {
            std::vector<std::uint8_t> bytes;
            Result r = readArticle(records[i], bytes);
            NewsArticle a;
            bool parsed = true;
            if (R_FAILED(r)) {
                ++st.readFails;
                st.lastReadRc = r;
            } else if (!parseNewsArticleFromMsgpack(bytes, a)) {
                ++st.parseFails;
                parsed = false;
                r = MAKERESULT(Module_Libnx, LibnxError_BadInput);
            }
            if (R_SUCCEEDED(r)) {
                a.id.assign(records[i].news_id,
                            std::find(records[i].news_id, records[i].news_id + sizeof(records[i].news_id), '\0'));
                a.read = records[i].read != 0;
                if (a.publishedAt == 0 && records[i].received_at > 0)
                    a.publishedAt = static_cast<std::uint64_t>(records[i].received_at);
                if (imageBytes + a.image.size() > kMaxImageBytesTotal)
                    a.image.clear();
                imageBytes += a.image.size();
                a.image.shrink_to_fit();
                articles.push_back(std::move(a));
            } else {
                DebugLog::log("[news] article %.24s rc=0x%X bytes=%d", records[i].news_id, r, (int)bytes.size());
                if (parsed == false && !headLogged && !bytes.empty()) {
                    headLogged = true;
                    // First parse failure only: the leading bytes tell the layout apart.
                    char head[3 * 16 + 1] = {};
                    const std::size_t n = std::min<std::size_t>(16, bytes.size());
                    for (std::size_t k = 0; k < n; ++k)
                        std::snprintf(head + k * 3, 4, "%02X ", bytes[k]);
                    DebugLog::log("[news] unparsed head: %s", head);
                }
            }
        }
    }
    std::stable_sort(articles.begin(), articles.end(),
                     [](const NewsArticle& a, const NewsArticle& b) { return a.publishedAt > b.publishedAt; });
    DebugLog::log("[news] loaded %d articles rc=0x%X", (int)articles.size(), rc);
    return articles;
}

void requestNewsUpdate(int minIntervalSec) {
    constexpr const char* kStamp = "sdmc:/config/OmniLaunch/news_requested.txt";
    const long long now = static_cast<long long>(std::time(nullptr));
    long long last = 0;
    {
        std::ifstream in(kStamp);
        in >> last;
    }
    if (now >= last && now - last < minIntervalSec) return;

    std::error_code ec;
    std::filesystem::create_directories("sdmc:/config/OmniLaunch", ec);
    std::ofstream(kStamp, std::ios::trunc) << now;

    {
        NewsSession session;
        if (R_FAILED(session.rc)) return;
        std::set<std::string> topics{"nx_news"};
        NewsDatabaseService db;
        if (R_SUCCEEDED(newsCreateNewsDatabaseService(&db))) {
            std::vector<NewsRecord> records(kMaxRecords);
            u32 count = 0;
            if (R_SUCCEEDED(getList(db, records.data(), count))) {
                for (u32 i = 0; i < std::min(count, kMaxRecords); ++i) {
                    const NewsTopicName& topic = records[i].topic_id;
                    std::string name(topic.name, std::find(topic.name, topic.name + sizeof(topic.name), '\0'));
                    if (!name.empty()) topics.insert(name);
                }
            }
            newsDatabaseClose(&db);
        }
        for (const std::string& topic : topics) {
            const Result rc = newsRequestImmediateReception(topic.c_str());
            DebugLog::log("[news] request reception '%s' rc=0x%X", topic.c_str(), rc);
        }
    }
}

#else

std::vector<NewsArticle> loadNewsArticles(Result& rc, int maxArticles, NewsLoadStats* stats) {
    (void)maxArticles;
    (void)stats;
    rc = 0;
    return {};
}

void requestNewsUpdate(int minIntervalSec) {
    (void)minIntervalSec;
}

#endif

} // namespace news
