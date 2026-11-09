#include "news/NewsStore.hpp"
#include <nlohmann/json.hpp>
#include <cassert>
#include <cstdio>
#include <iostream>
#include <vector>

int main() {
    std::cout << "Starting news_test..." << std::endl;

    // 1. Standard article msgpack parsing
    {
        nlohmann::json j;
        j["subject"] = "Super Mario Odyssey Update";
        j["topic_name"] = "Nintendo Switch";
        j["body"] = {{"text", "A new software update is available for Super Mario Odyssey."}};
        j["footer"] = "Nintendo Co., Ltd.";
        j["published_at"] = 1700000000ULL;
        std::vector<std::uint8_t> binImg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0x4A, 0x46, 0x49, 0x46};
        j["body"]["main_image"] = nlohmann::json::binary(binImg);
        j["more"] = {{"browser", {{"url", "https://nintendo.com"}}}};
        auto bytes = nlohmann::json::to_msgpack(j);

        news::NewsArticle a;
        bool ok = news::parseNewsArticleFromMsgpack(bytes, a);
        assert(ok);
        assert(a.subject == "Super Mario Odyssey Update");
        assert(a.topic == "Nintendo Switch");
        assert(a.body == "A new software update is available for Super Mario Odyssey.");
        assert(a.footer == "Nintendo Co., Ltd.");
        assert(a.publishedAt == 1700000000ULL);
        assert(a.image == binImg);
        assert(a.linkUrl == "https://nintendo.com");
    }

    // 2. Millisecond published_at timestamp conversion
    {
        nlohmann::json j;
        j["subject"] = "Zelda News";
        j["body"] = {{"text", "Tears of the Kingdom event."}};
        j["published_at"] = 1700000000000ULL; // milliseconds
        auto bytes = nlohmann::json::to_msgpack(j);

        news::NewsArticle a;
        bool ok = news::parseNewsArticleFromMsgpack(bytes, a);
        assert(ok);
        assert(a.publishedAt == 1700000000ULL); // converted to seconds
    }

    // 3. Fallback heuristic layout parsing
    {
        nlohmann::json j;
        j["title"] = "Non-standard Schema Title";
        j["details_long"] = "A very long detailed body describing an exciting tournament.";
        std::vector<std::uint8_t> altJpeg = {0xFF, 0xD8, 0xAA, 0xBB};
        j["custom_pic"] = nlohmann::json::binary(altJpeg);
        auto bytes = nlohmann::json::to_msgpack(j);

        news::NewsArticle a;
        bool ok = news::parseNewsArticleFromMsgpack(bytes, a);
        assert(ok);
        assert(a.subject == "Non-standard Schema Title");
        assert(a.body == "A very long detailed body describing an exciting tournament.");
        assert(a.image == altJpeg);
    }

    // 4. Corrupted and empty inputs
    {
        news::NewsArticle a;
        assert(!news::parseNewsArticleFromMsgpack({}, a));
        assert(!news::parseNewsArticleFromMsgpack({0x01, 0x02, 0x03}, a));
    }

    // 5. Read tracking on SD path
    {
        const std::string tmpRead = "/tmp/test_news_read.txt";
        std::remove(tmpRead.c_str());

        auto set1 = news::loadReadArticleIds(tmpRead);
        assert(set1.empty());

        assert(news::markArticleRead("art_001", tmpRead));
        auto set2 = news::loadReadArticleIds(tmpRead);
        assert(set2.size() == 1);
        assert(set2.count("art_001") == 1);
        assert(set2.count("art_002") == 0);

        assert(news::markArticleRead("art_002", tmpRead));
        auto set3 = news::loadReadArticleIds(tmpRead);
        assert(set3.size() == 2);
        assert(set3.count("art_001") == 1);
        assert(set3.count("art_002") == 1);

        std::remove(tmpRead.c_str());
    }

    // 6. Host loadNewsArticles stub
    {
        Result rc = 12345;
        news::NewsLoadStats stats;
        auto articles = news::loadNewsArticles(rc, 20, &stats);
        assert(rc == 0);
        assert(articles.empty());
        assert(stats.records == 0);
    }

    std::cout << "news_test: all passed" << std::endl;
    return 0;
}
