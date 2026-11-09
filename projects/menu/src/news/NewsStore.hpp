#pragma once
// Console HOME menu news store (HomeNX G3, requirements C-NET-5, C-UI-6).
// Reads the console's saved news articles via Horizon's news:a service (libnx)
// and parses binary MessagePack article payloads.
// Attribution: adapted from upstream/homenx @ 0448cd4, projects/menu/src/news/NewsStore.{hpp,cpp}.
// See integration/provenance/homenx-intake.md.

#include <cstdint>
#include <string>
#include <vector>
#include <set>

#ifdef __SWITCH__
#include <switch.h>
#else
using Result = std::uint32_t;
#endif

namespace news {

struct NewsArticle {
    std::string topic;             // Channel / topic name (e.g. "Nintendo Switch")
    std::string subject;           // Headline / title
    std::string body;              // Article text
    std::string footer;            // Optional footer text
    std::string linkUrl;           // Browser link URL if any
    std::string id;                // 24-byte news_id string
    bool read = false;             // Console read flag
    std::uint64_t publishedAt = 0; // POSIX timestamp seconds
    std::vector<std::uint8_t> image;// Raw JPEG image bytes
};

struct NewsLoadStats {
    unsigned records = 0;   // In console news database
    unsigned readFails = 0; // Articles that could not be read
    unsigned parseFails = 0;// Articles read but could not be parsed
    Result lastReadRc = 0;
};

// MessagePack article payload parser (pure logic, fully testable on host).
bool parseNewsArticleFromMsgpack(const std::vector<std::uint8_t>& bytes, NewsArticle& out);

// Loads saved console news articles (newest first). Max articles default 40.
std::vector<NewsArticle> loadNewsArticles(Result& rc, int maxArticles = 40, NewsLoadStats* stats = nullptr);

// Asks the console to receive news now (rate limited by minIntervalSec).
// Blocking: call it from a worker thread, never the UI thread.
void requestNewsUpdate(int minIntervalSec = 600);

// Read tracking helpers on SD card.
std::set<std::string> loadReadArticleIds(const std::string& path = "sdmc:/config/OmniLaunch/news_read.txt");
bool markArticleRead(const std::string& id, const std::string& path = "sdmc:/config/OmniLaunch/news_read.txt");

} // namespace news
