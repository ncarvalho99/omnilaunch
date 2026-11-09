#include "cards/GameTdbIndex.hpp"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <vector>

namespace cards {

namespace {

struct Row {
    std::string_view key;
    std::string_view id;
    char region = 'A';
};

std::mutex s_mutex;
std::string s_buffer;
std::vector<Row> s_rows;
bool s_loaded = false;

const char* const kCandidatePaths[] = {
    "romfs:/data/gametdb.tsv",
    "sdmc:/switch/OmniLaunch/data/gametdb.tsv",
    "romfs/data/gametdb.tsv",
    "data/gametdb.tsv",
};

bool tryLoadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
        return false;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size <= 0 || size > (8u << 20))
        return false;
    in.seekg(0, std::ios::beg);
    s_buffer.resize(static_cast<std::size_t>(size));
    in.read(s_buffer.data(), size);
    if (!in.good()) {
        s_buffer.clear();
        return false;
    }

    s_rows.clear();
    std::size_t start = 0;
    while (start < s_buffer.size()) {
        std::size_t end = s_buffer.find('\n', start);
        if (end == std::string::npos)
            end = s_buffer.size();
        std::size_t lineEnd = end;
        if (lineEnd > start && s_buffer[lineEnd - 1] == '\r')
            --lineEnd;
        if (lineEnd > start) {
            std::size_t t1 = s_buffer.find('\t', start);
            if (t1 != std::string::npos && t1 < lineEnd) {
                std::size_t t2 = s_buffer.find('\t', t1 + 1);
                if (t2 != std::string::npos && t2 < lineEnd && t2 + 1 < lineEnd) {
                    Row r;
                    r.key = std::string_view(s_buffer.data() + start, t1 - start);
                    r.id = std::string_view(s_buffer.data() + t1 + 1, t2 - (t1 + 1));
                    r.region = s_buffer[t2 + 1];
                    if (!r.key.empty() && !r.id.empty())
                        s_rows.push_back(r);
                }
            }
        }
        start = end + 1;
    }
    s_loaded = !s_rows.empty();
    return s_loaded;
}

void ensureLoaded(const std::string& explicitPath) {
    if (s_loaded)
        return;
    if (!explicitPath.empty() && tryLoadFile(explicitPath))
        return;
    for (const char* p : kCandidatePaths) {
        if (tryLoadFile(p))
            return;
    }
}

} // namespace

std::string GameTdbIndex::nameKey(std::string_view name) {
    static const char kFold[] =
        "aaaaaa ceeeeiiii nooooo  uuuuy  aaaaaa ceeeeiiii nooooo  uuuuy yaaaaaaccccccccdd  eeeeeeeeeegggggggghh  "
        "iiiiiiiii iijjkk llllllll  nnnnnnn  oooooo  rrrrrrsssssssstttt  uuuuuuuuuuuuwwyyyzzzzzzs";
    std::string out;
    out.reserve(name.size());
    for (std::size_t i = 0; i < name.size();) {
        const unsigned char c = static_cast<unsigned char>(name[i]);
        if (c < 0x80) {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z')) {
                out.push_back(static_cast<char>(c));
            } else if (c >= 'A' && c <= 'Z') {
                out.push_back(static_cast<char>(c + ('a' - 'A')));
            } else {
                if (!out.empty() && out.back() != ' ')
                    out.push_back(' ');
            }
            ++i;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < name.size()) {
                const unsigned char c2 = static_cast<unsigned char>(name[i + 1]);
                const std::uint32_t cp = ((c & 0x1F) << 6) | (c2 & 0x3F);
                if (cp >= 0xC0 && cp < 0x180) {
                    char folded = kFold[cp - 0xC0];
                    if (folded != ' ') {
                        out.push_back(folded);
                    } else if (!out.empty() && out.back() != ' ') {
                        out.push_back(' ');
                    }
                } else if (!out.empty() && out.back() != ' ') {
                    out.push_back(' ');
                }
                i += 2;
            } else {
                ++i;
            }
        } else if ((c & 0xF0) == 0xE0) {
            i += (i + 2 < name.size()) ? 3 : 1;
            if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
        } else if ((c & 0xF8) == 0xF0) {
            i += (i + 3 < name.size()) ? 4 : 1;
            if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
        } else {
            ++i;
            if (!out.empty() && out.back() != ' ')
                out.push_back(' ');
        }
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

std::optional<GameTdbIndex::Entry> GameTdbIndex::lookup(std::string_view title, const std::string& tsvPath) {
    const std::string key = nameKey(title);
    if (key.empty())
        return std::nullopt;

    std::lock_guard<std::mutex> lock(s_mutex);
    ensureLoaded(tsvPath);
    if (!s_loaded || s_rows.empty())
        return std::nullopt;

    auto it = std::lower_bound(s_rows.begin(), s_rows.end(), key,
                               [](const Row& r, std::string_view k) { return r.key < k; });
    if (it != s_rows.end() && it->key == key) {
        return Entry{std::string(it->id), it->region};
    }
    return std::nullopt;
}

std::string GameTdbIndex::coverUrl(std::string_view gid, char region, int attempt) {
    if (gid.empty() || attempt < 0 || attempt >= 3)
        return {};
    static const char* const kOrder[3][3] = {
        {"US", "EN", "JA"}, // 0: US or All
        {"EN", "US", "JA"}, // 1: Europe
        {"JA", "US", "EN"}  // 2: Japan
    };
    const int pick = (region == 'E') ? 1 : (region == 'J') ? 2 : 0;
    const char* regStr = kOrder[pick][attempt];
    return "https://art.gametdb.com/switch/coverM/" + std::string(regStr) + "/" + std::string(gid) + ".jpg";
}

void GameTdbIndex::unload() {
    std::lock_guard<std::mutex> lock(s_mutex);
    s_rows.clear();
    s_buffer.clear();
    s_loaded = false;
}

bool GameTdbIndex::isLoaded() {
    std::lock_guard<std::mutex> lock(s_mutex);
    return s_loaded;
}

} // namespace cards
