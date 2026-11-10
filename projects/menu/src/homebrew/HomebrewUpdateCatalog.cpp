// Adapted from upstream/homenx @ 0448cd4, projects/updater/src/Catalog.cpp (GPL-3.0).
#include "homebrew/HomebrewUpdateCatalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>

namespace hbupdate {

namespace {

// GitHub projects of well-known homebrew, by squashed NACP name (HomeNX list,
// minus the HomeNX author's own projects).
const std::map<std::string, std::string> kKnown = {
    {"sphaira", "ITotalJustice/sphaira"},
    {"jksv", "J-D-K/JKSV"},
    {"goldleaf", "XorTroll/Goldleaf"},
    {"nxshell", "joel16/NX-Shell"},
    {"moonlight", "XITRIX/Moonlight-Switch"},
    {"nxmp", "proconsule/nxmp"},
    {"linkalho", "rdmrocha/linkalho"},
    {"nxthemesinstaller", "exelix11/SwitchThemeInjector"},
    {"awooinstaller", "Huntereb/Awoo-Installer"},
    {"hekatetoolbox", "WerWolv/Hekate-Toolbox"},
    {"edizon", "WerWolv/EdiZon"},
    {"pplay", "Cpasjuste/pplay"},
    {"ftpd", "mtheall/ftpd"},
    {"nxdumptool", "DarkMatterCore/nxdumptool"},
    {"nxactivitylog", "tallbl0nde/NX-Activity-Log"},
};

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string fileName(const std::string& path) {
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

bool endsWith(const std::string& s, const char* suffix) {
    const std::string t(suffix);
    return s.size() >= t.size() && s.compare(s.size() - t.size(), t.size(), t) == 0;
}

std::vector<long long> numbers(const std::string& v) {
    std::vector<long long> out;
    long long cur = -1;
    for (char c : v) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            if (cur < 100000000000000LL) // longer digit runs saturate instead of overflowing
                cur = (cur < 0 ? 0 : cur * 10) + (c - '0');
        } else if (cur >= 0) {
            out.push_back(cur);
            cur = -1;
        }
    }
    if (cur >= 0) out.push_back(cur);
    return out;
}

std::string str(const nlohmann::json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

std::string squash(const std::string& s) {
    std::string out;
    for (unsigned char c : s)
        if (std::isalnum(c)) out += static_cast<char>(std::tolower(c));
    return out;
}

bool isNewer(const std::string& latest, const std::string& installed) {
    if (latest.empty()) return false;
    const auto a = numbers(latest), b = numbers(installed);
    if (a.empty() || b.empty()) return lower(latest) != lower(installed);
    for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
        const long long x = i < a.size() ? a[i] : 0, y = i < b.size() ? b[i] : 0;
        if (x != y) return x > y;
    }
    return false;
}

std::string knownRepo(const std::string& name) {
    auto it = kKnown.find(squash(name));
    return it == kKnown.end() ? std::string() : it->second;
}

std::vector<StorePackage> parseStore(const std::string& json) {
    std::vector<StorePackage> out;
    // Keep only the fields used; descriptions, changelogs and screenshots are
    // most of the 1.6 MB and are dropped as they are read.
    const auto keep = [](int, nlohmann::json::parse_event_t event, nlohmann::json& parsed) {
        if (event != nlohmann::json::parse_event_t::key) return true;
        const auto& k = parsed.get_ref<const std::string&>();
        return k == "packages" || k == "name" || k == "title" || k == "version" || k == "binary";
    };
    const auto j = nlohmann::json::parse(json, keep, false);
    if (j.is_discarded() || !j.is_object()) return out;
    auto pk = j.find("packages");
    if (pk == j.end() || !pk->is_array()) return out;
    out.reserve(pk->size());
    for (const auto& p : *pk) {
        if (!p.is_object()) continue;
        StorePackage s{str(p, "name"), str(p, "title"), str(p, "version"), str(p, "binary")};
        if (s.name.empty() || s.binary.empty() || !endsWith(lower(s.binary), ".nro")) continue;
        // Package names become part of a URL: letters, digits, '-', '_', '.' only.
        if (!std::all_of(s.name.begin(), s.name.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '-' || c == '_' || c == '.';
            }) || s.name.find("..") != std::string::npos)
            continue;
        out.push_back(std::move(s));
    }
    return out;
}

const StorePackage* matchStore(const std::vector<StorePackage>& store, const Installed& app) {
    const std::string prefix = "sdmc:";
    std::string rel = app.path.rfind(prefix, 0) == 0 ? app.path.substr(prefix.size()) : app.path;
    rel = lower(rel);
    for (const auto& p : store)
        if (lower(p.binary) == rel) return &p;
    const std::string key = squash(app.name);
    if (key.empty()) return nullptr;
    for (const auto& p : store)
        if (squash(p.title) == key) return &p;
    return nullptr;
}

Offer storeOffer(const StorePackage& pkg) {
    Offer o;
    o.latest = pkg.version;
    o.source = "Homebrew App Store";
    o.url = "https://switch.cdn.fortheusers.org/zips/" + pkg.name + ".zip";
    o.zipEntry = pkg.binary;
    if (!o.zipEntry.empty() && o.zipEntry[0] == '/') o.zipEntry.erase(0, 1);
    return o;
}

bool githubOffer(const std::string& json, const std::string& repo, const Installed& app,
                 std::int64_t installedSize, Offer& out) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;
    const std::string want = lower(fileName(app.path));
    std::string any, named;
    std::int64_t anySize = -1, namedSize = -1;
    auto assets = j.find("assets");
    if (assets == j.end() || !assets->is_array()) return false;
    for (const auto& a : *assets) {
        if (!a.is_object()) continue;
        const std::string name = lower(str(a, "name"));
        const std::string url = str(a, "browser_download_url");
        if (!endsWith(name, ".nro") || url.rfind("https://", 0) != 0) continue;
        const std::int64_t size = a.value("size", static_cast<std::int64_t>(-1));
        if (any.empty()) {
            any = url;
            anySize = size;
        }
        if (name == want) {
            named = url;
            namedSize = size;
        }
    }
    if (any.empty()) return false;
    std::string tag = str(j, "tag_name");
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    out = Offer{};
    out.url = named.empty() ? any : named;
    out.source = "GitHub " + repo;
    out.latest = tag;
    if (numbers(tag).empty()) {
        // No version number: offered only when the asset is not the file we have.
        const std::int64_t size = named.empty() ? anySize : namedSize;
        if (size <= 0 || installedSize < 0 || size == installedSize) return false;
        out.latest = tag.empty() ? std::string("latest") : tag;
    }
    return true;
}

State parseState(const std::string& json) {
    State s;
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return s;
    if (auto it = j.find("checkedAt"); it != j.end() && it->is_number_integer()) s.checkedAt = it->get<std::int64_t>();
    if (auto it = j.find("apps"); it != j.end() && it->is_object())
        for (auto a = it->begin(); a != it->end(); ++a) {
            if (!a.value().is_object()) continue;
            Offer o{str(a.value(), "latest"), str(a.value(), "source"), str(a.value(), "url"),
                    str(a.value(), "zipEntry")};
            if (o.url.rfind("https://", 0) == 0) s.offers[a.key()] = o;
        }
    if (auto it = j.find("installed"); it != j.end() && it->is_object())
        for (auto a = it->begin(); a != it->end(); ++a)
            if (a.value().is_string()) s.installed[a.key()] = a.value().get<std::string>();
    return s;
}

std::string dumpState(const State& state) {
    nlohmann::json j = {{"checkedAt", state.checkedAt}, {"apps", nlohmann::json::object()},
                        {"installed", nlohmann::json::object()}};
    for (const auto& [path, o] : state.offers)
        j["apps"][path] = {{"latest", o.latest}, {"source", o.source}, {"url", o.url}, {"zipEntry", o.zipEntry}};
    for (const auto& [path, v] : state.installed) j["installed"][path] = v;
    return j.dump(1);
}

const Offer* pendingOffer(const State& state, const Installed& app) {
    auto it = state.offers.find(app.path);
    if (it == state.offers.end()) return nullptr;
    const Offer& o = it->second;
    auto had = state.installed.find(app.path);
    if (had != state.installed.end() && had->second == o.latest) return nullptr;
    if (o.latest == "latest") return &o; // size-based GitHub offer, decided at check time
    return isNewer(o.latest, app.version) ? &o : nullptr;
}

bool managedPath(const std::string& path) {
    const std::string p = lower(path);
    return p.find("/switch/omnilaunch/") != std::string::npos || p == "sdmc:/hbmenu.nro";
}

} // namespace hbupdate
