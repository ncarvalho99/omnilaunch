#include "cards/CardsAchievements.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <system_error>

namespace cards::achievements {

float progress(const Milestone& m, const Play& p) {
    const double have = m.hours ? static_cast<double>(p.seconds) / 3600.0 : static_cast<double>(p.launches);
    const double f = have / static_cast<double>(m.goal);
    return f >= 1.0 ? 1.f : static_cast<float>(f);
}

std::string key(std::uint64_t titleId, const char* id) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%016llX %s", static_cast<unsigned long long>(titleId), id);
    return buf;
}

LoadStatus readStream(std::istream& in, Earned& out) {
    Earned pending;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream words(line);
        std::string a, b, trailing;
        long long c = 0;
        if (!(words >> a >> b >> c) || (words >> trailing) || a.size() != 16 ||
            a.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            return LoadStatus::Error;
        bool known = false;
        for (const auto& m : kPerGame) if (b == m.id) known = true;
        if (!known) return LoadStatus::Error;
        pending.when[a + " " + b] = c;
    }
    if (in.bad() || !in.eof()) return LoadStatus::Error;
    out = std::move(pending);
    return LoadStatus::Complete;
}

LoadStatus loadChecked(const std::string& path, Earned& out) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) return LoadStatus::Error;
    if (!exists) return LoadStatus::Missing;
    if (!std::filesystem::is_regular_file(path, ec) || ec) return LoadStatus::Error;
    std::ifstream in(path);
    if (!in) return LoadStatus::Error;
    return readStream(in, out);
}

Earned load(const std::string& path) {
    Earned out;
    loadChecked(path, out);
    return out;
}

bool save(const std::string& path, const Earned& e) {
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out)
            return false;
        out << "# OmniLaunch Cards achievements: <title id> <milestone> <posix seconds>\n";
        for (const auto& [k, when] : e.when)
            out << k << ' ' << when << '\n';
        out.flush();
        if (!out) return false;
        out.close();
        if (!out) return false;
    }
    // rename() replaces the destination atomically on POSIX; on FAT (the SD
    // card) it can fail when the target exists, so keep the old file as .bak
    // until the new one is in place, and put it back if the move fails.
    std::filesystem::rename(tmp, path, ec);
    if (!ec)
        return true;
    const std::string bak = path + ".bak";
    std::error_code ec2;
    if (std::filesystem::exists(path, ec2)) {
        // Never discard the sole recoverable copy when only .bak exists.
        std::filesystem::remove(bak, ec2);
        if (ec2) return false;
        std::filesystem::rename(path, bak, ec2);
        if (ec2) return false; // old destination is untouched
    }
    ec.clear();
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (!std::filesystem::exists(path, ec2))
            std::filesystem::rename(bak, path, ec2);
        return false; // preserve .tmp/.bak for recovery rather than deleting evidence
    }
    // Keep .bak until the next successful replacement.

    return true;
}

std::vector<const Milestone*> recordAndSave(const std::string& path, std::uint64_t titleId, const Play& play,
                                            std::int64_t now, bool& ok) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    ok = true;
    Earned e;
    const auto status = loadChecked(path, e);
    // Never overwrite from an uncertain or partial read. A missing main
    // file can recover a completely read backup (FAT rename interruption).
    if (status == LoadStatus::Error) { ok = false; return {}; }
    if (status == LoadStatus::Missing) {
        const auto backup = loadChecked(path + ".bak", e);
        if (backup == LoadStatus::Error) { ok = false; return {}; }
    }
    auto fresh = record(e, titleId, play, now);
    if (!fresh.empty())
        ok = save(path, e);
    return fresh;
}

std::vector<const Milestone*> record(Earned& e, std::uint64_t titleId, const Play& play, std::int64_t now) {
    std::vector<const Milestone*> fresh;
    for (const auto& m : kPerGame) {
        if (progress(m, play) < 1.f)
            continue;
        const std::string k = key(titleId, m.id);
        if (e.when.count(k))
            continue;
        e.when[k] = now;
        fresh.push_back(&m);
    }
    return fresh;
}

} // namespace cards::achievements
