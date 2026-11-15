#include "PreviewDiskCache.hpp"

#include "core/DebugLog.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>

namespace themeshop::previewcache {

namespace {

std::mutex g_writeMutex; // one writer / pruner at a time

std::string fileFor(const std::string& url, const std::string& token) {
    // FNV-1a 64: a stable name, not a security boundary.
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&](const std::string& s) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
    };
    mix(url);
    mix("|");
    mix(token);
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.img", (unsigned long long)h);
    return std::string(kDir) + "/" + name;
}

// A preview is a JPEG, PNG or WebP; anything else (an error page) is refused.
bool looksLikeImage(const std::vector<std::uint8_t>& b) {
    if (b.size() < 12)
        return false;
    if (b[0] == 0xFF && b[1] == 0xD8)
        return true; // JPEG
    if (b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G')
        return true;
    return b[0] == 'R' && b[1] == 'I' && b[2] == 'F' && b[3] == 'F' && b[8] == 'W' && b[9] == 'E';
}

void prune() {
    namespace fs = std::filesystem;
    struct Item { fs::path path; std::uint64_t size; fs::file_time_type time; };
    std::vector<Item> items;
    std::uint64_t total = 0;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(kDir, ec)) {
        if (ec)
            break;
        std::error_code fe;
        if (!e.is_regular_file(fe))
            continue;
        Item it{e.path(), (std::uint64_t)e.file_size(fe), e.last_write_time(fe)};
        total += it.size;
        items.push_back(std::move(it));
    }
    if (total <= kMaxBytes)
        return;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.time < b.time; });
    int removed = 0;
    for (const auto& it : items) {
        if (total <= kMaxBytes * 3 / 4)
            break;
        std::error_code re;
        if (fs::remove(it.path, re)) {
            total -= it.size;
            ++removed;
        }
    }
    DebugLog::log("[preview-cache] pruned %d file(s), %.1f MB left", removed, total / 1048576.0);
}

} // namespace

bool load(const std::string& url, const std::string& token, std::vector<std::uint8_t>& out) {
    const std::string path = fileFor(url, token);
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f)
        return false;
    std::vector<std::uint8_t> bytes;
    std::uint8_t buf[16384];
    std::size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        bytes.insert(bytes.end(), buf, buf + n);
    std::fclose(f);
    if (!looksLikeImage(bytes)) {
        std::remove(path.c_str());
        return false;
    }
    out = std::move(bytes);
    return true;
}

void store(const std::string& url, const std::string& token, const std::vector<std::uint8_t>& bytes) {
    if (!looksLikeImage(bytes))
        return;
    std::lock_guard<std::mutex> lk(g_writeMutex);
    std::error_code ec;
    std::filesystem::create_directories(kDir, ec);
    const std::string path = fileFor(url, token);
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f)
        return;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    std::fclose(f);
    if (!ok) {
        std::remove(tmp.c_str());
        return;
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return;
    }
    prune();
}

} // namespace themeshop::previewcache
