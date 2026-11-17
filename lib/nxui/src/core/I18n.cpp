#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

namespace nxui {

namespace {

class JsonFlatParser {
public:
    JsonFlatParser(const std::string& src,
                   std::unordered_map<std::string, std::string>& out)
        : m_src(src), m_out(out) {}

    bool parse() {
        skipWs();
        if (!consume('{')) return false;
        parseObject("");
        return true;
    }

private:
    bool parseObject(const std::string& prefix) {
        skipWs();
        if (peek() == '}') {
            ++m_pos;
            return true;
        }

        while (m_pos < m_src.size()) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;

            skipWs();
            if (!consume(':')) return false;
            skipWs();

            const char c = peek();
            const std::string fullKey = prefix.empty() ? key : (prefix + "." + key);

            if (c == '"') {
                std::string value;
                if (!parseString(value)) return false;
                m_out[fullKey] = value;
            } else if (c == '{') {
                ++m_pos;
                if (!parseObject(fullKey)) return false;
            } else {
                skipValue();
            }

            skipWs();
            if (consume(',')) continue;
            if (consume('}')) return true;
            return false;
        }
        return false;
    }

    void skipValue() {
        int depthObj = 0;
        int depthArr = 0;
        bool inString = false;
        bool escaped = false;

        while (m_pos < m_src.size()) {
            char c = m_src[m_pos];
            if (inString) {
                ++m_pos;
                if (escaped) {
                    escaped = false;
                } else if (c == '\\') {
                    escaped = true;
                } else if (c == '"') {
                    inString = false;
                }
                continue;
            }

            if (c == '"') {
                inString = true;
                ++m_pos;
                continue;
            }
            if (c == '{') { ++depthObj; ++m_pos; continue; }
            if (c == '}') {
                if (depthObj == 0 && depthArr == 0) return;
                --depthObj;
                ++m_pos;
                continue;
            }
            if (c == '[') { ++depthArr; ++m_pos; continue; }
            if (c == ']') { --depthArr; ++m_pos; continue; }
            if (c == ',' && depthObj == 0 && depthArr == 0) return;

            ++m_pos;
        }
    }

    bool parseString(std::string& out) {
        if (!consume('"')) return false;
        out.clear();

        while (m_pos < m_src.size()) {
            char c = m_src[m_pos++];
            if (c == '"') return true;
            if (c == '\\') {
                if (m_pos >= m_src.size()) return false;
                char e = m_src[m_pos++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    void skipWs() {
        while (m_pos < m_src.size() && std::isspace((unsigned char)m_src[m_pos]))
            ++m_pos;
    }

    char peek() const {
        if (m_pos >= m_src.size()) return '\0';
        return m_src[m_pos];
    }

    bool consume(char expected) {
        if (peek() != expected) return false;
        ++m_pos;
        return true;
    }

    const std::string& m_src;
    std::unordered_map<std::string, std::string>& m_out;
    size_t m_pos = 0;
};

} // namespace

I18n& I18n::instance() {
    static I18n inst;
    return inst;
}

// Background language work runs off core 0 (the render thread's core), as
// the menu's worker pool does.
static void i18nLeaveRenderCore(s32 core) {
    svcSetThreadCoreMask(CUR_THREAD_HANDLE, core, UINT64_C(1) << core);
}

void I18n::prefetch(const std::string& basePath, const std::string& languageOverride) {
    if (m_basePath != basePath)
        m_fallbackFuture = {};
    m_basePath = basePath;
    startFallbackLoad();

    // A thread that cannot be created leaves the work to initialize(), on the
    // calling thread, as before.
    if (m_systemTag.empty() && !m_systemFuture.valid()) {
        // The query has taken 3-375 ms on a menu start; the parse of the
        // language it names another 30-170 ms. Both overlap the rest of start-up.
        try {
            m_systemFuture = std::async(std::launch::async, [basePath]() {
                i18nLeaveRenderCore(2);
                std::string tag = detectSystemLanguageTag();
                loadJsonMapFromFile(basePath + "/" + closestShippedTag(basePath, tag) + ".json");
                return tag;
            }).share();
        } catch (...) {
            m_systemFuture = {};
        }
    }

    const std::string chosen = normalizeTag(languageOverride);
    if (!chosen.empty() && chosen != "auto") {
        // The parse lands in the process cache that setLanguage() reads.
        try {
            m_overrideFuture = std::async(std::launch::async,
                                          [path = basePath + "/" + closestShippedTag(basePath, chosen) + ".json"]() {
                i18nLeaveRenderCore(1);
                loadJsonMapFromFile(path);
            }).share();
        } catch (...) {
            m_overrideFuture = {};
        }
    }
}

const std::string& I18n::systemLanguageTag() {
    if (m_systemTag.empty()) {
        if (m_systemFuture.valid()) {
            m_systemTag = m_systemFuture.get();
            m_systemFuture = {};
        } else {
            m_systemTag = detectSystemLanguageTag();
        }
    }
    return m_systemTag;
}

void I18n::startFallbackLoad() {
    // Same path as the last start: the parsed map is already in hand (or on
    // its way), nothing to do.
    if (m_fallbackFuture.valid())
        return;
    const std::string path = buildLanguagePath(m_fallbackTag);
    const std::string englishPath = buildLanguagePath("en-US");
    auto load = [path, englishPath]() {
        auto map = loadJsonMapFromFile(path);
        if (!map && englishPath != path)
            map = loadJsonMapFromFile(englishPath);
        return map;
    };
    try {
        m_fallbackFuture = std::async(std::launch::async, [load]() {
            i18nLeaveRenderCore(1);
            return load();
        }).share();
    } catch (...) {
        // No thread: parse it here, as before.
        std::promise<std::shared_ptr<const Map>> done;
        done.set_value(load());
        m_fallbackFuture = done.get_future().share();
    }
}

const I18n::Map& I18n::fallbackMap() const {
    static const Map kEmpty;
    if (!m_fallbackFuture.valid())
        return kEmpty;
    // Normally finished long before the first miss; otherwise this waits for
    // the rest of one ~70 ms parse, once.
    const auto& map = m_fallbackFuture.get();
    return map ? *map : kEmpty;
}

bool I18n::initialize(const std::string& basePath, const std::string& fallbackTag) {
    std::string nextFallback = normalizeTag(fallbackTag);
    if (nextFallback.empty()) nextFallback = "en-US";
    if (basePath != m_basePath || nextFallback != m_fallbackTag)
        m_fallbackFuture = {};
    m_basePath = basePath;
    m_fallbackTag = nextFallback;
    startFallbackLoad();
    return setLanguage(systemLanguageTag());
}

bool I18n::setLanguageAuto() {
    return setLanguage(systemLanguageTag());
}

bool I18n::setLanguage(const std::string& tag) {
    const std::string previousTag = m_activeTag;

    std::string normalized = normalizeTag(tag);
    if (normalized.empty() || normalized == "auto") {
        normalized = systemLanguageTag();
    }

    normalized = closestShippedTag(m_basePath, normalized);

    // A parse prefetch() or the fallback load started is waited for here.
    std::shared_ptr<const Map> nextMap = loadJsonMapFromFile(buildLanguagePath(normalized));
    if (!nextMap) {
        normalized = m_fallbackTag;
        nextMap = loadJsonMapFromFile(buildLanguagePath(normalized));
    }

    m_activeTag = normalized;
    m_activeMap = std::move(nextMap);

    if (m_activeTag != previousTag)
        notifyLanguageChanged();

    return (m_activeMap && !m_activeMap->empty()) || m_fallbackFuture.valid();
}

std::string I18n::tr(std::string_view key, std::string_view fallback) const {
    const std::string k(key);
    if (m_activeMap) {
        auto it = m_activeMap->find(k);
        if (it != m_activeMap->end()) return it->second;
    }

    const Map& fbMap = fallbackMap();
    auto fb = fbMap.find(k);
    if (fb != fbMap.end()) return fb->second;

    if (!fallback.empty()) return std::string(fallback);
    return std::string(key);
}

int I18n::addLanguageChangedListener(LanguageChangedCb cb) {
    if (!cb) return -1;
    int id = m_nextListenerId++;
    m_languageListeners[id] = std::move(cb);
    return id;
}

void I18n::removeLanguageChangedListener(int id) {
    if (id < 0) return;
    m_languageListeners.erase(id);
}

void I18n::notifyLanguageChanged() {
    auto listeners = m_languageListeners;
    for (auto& kv : listeners) {
        if (kv.second) kv.second();
    }
}

std::string I18n::normalizeTag(const std::string& tag) {
    if (tag.empty()) return "";

    std::string out;
    out.reserve(tag.size());

    for (char c : tag) {
        if (c == '_') out.push_back('-');
        else if (!std::isspace((unsigned char)c)) out.push_back(c);
    }

    if (out.size() == 2) {
        out[0] = (char)std::tolower((unsigned char)out[0]);
        out[1] = (char)std::tolower((unsigned char)out[1]);
        return out;
    }

    auto dash = out.find('-');
    if (dash != std::string::npos && dash + 2 < out.size()) {
        std::string lang = out.substr(0, dash);
        std::string region = out.substr(dash + 1);
        std::transform(lang.begin(), lang.end(), lang.begin(), [](char ch) {
            return (char)std::tolower((unsigned char)ch);
        });
        std::transform(region.begin(), region.end(), region.begin(), [](char ch) {
            return (char)std::toupper((unsigned char)ch);
        });
        return lang + "-" + region;
    }

    return out;
}

std::string I18n::closestShippedTag(const std::string& basePath, const std::string& normalizedTag) {
    // Regional variants with no file of their own read the closest language
    // that ships instead of dropping to English. The detected tag itself stays
    // exact, since System Settings names the console's language with it.
    static const std::pair<const char*, const char*> kClosest[] = {
        {"es-419", "es-ES"}, {"pt-PT", "pt-BR"}, {"fr-CA", "fr-FR"}, {"en-GB", "en-US"},
    };
    std::error_code existsEc;
    for (const auto& [variant, closest] : kClosest) {
        if (normalizedTag == variant &&
            !std::filesystem::exists(basePath + "/" + normalizedTag + ".json", existsEc))
            return closest;
    }
    return normalizedTag;
}

std::string I18n::buildLanguagePath(const std::string& normalizedTag) const {
    return m_basePath + "/" + normalizedTag + ".json";
}

std::shared_ptr<const I18n::Map> I18n::loadJsonMapFromFile(const std::string& path) {
    // Parsed maps are kept per path for the process: the menu initialises
    // I18n again on every frontend switch, and reading and parsing the same
    // 100 KB files off the card each time was part of that switch's frame.
    // The language files only change with a menu update, which restarts it.
    // Shared, never copied: a copy of a 1700-key map is itself a few ms.
    // A parse already running on another thread (I18n::prefetch) is waited
    // for, not repeated. A missing or broken file is not cached.
    using Entry = std::shared_future<std::shared_ptr<const Map>>;
    static std::mutex cacheMutex;
    static std::unordered_map<std::string, Entry> cache;
    std::promise<std::shared_ptr<const Map>> promise;
    Entry existing;
    {
        std::lock_guard<std::mutex> lk(cacheMutex);
        auto it = cache.find(path);
        if (it != cache.end())
            existing = it->second;
        else
            cache.emplace(path, promise.get_future().share());
    }
    if (existing.valid()) {
        // Waited for outside the lock: the parse holding it may still run.
        return existing.get();
    }

    std::shared_ptr<Map> out;
    try {
        std::ifstream f(path);
        if (f.is_open()) {
            std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            f.close();
            out = std::make_shared<Map>();
            JsonFlatParser parser(src, *out);
            if (!parser.parse())
                out.reset();
        }
    } catch (...) {
        out.reset();   // waiters must always get an answer
    }
    if (!out) {
        std::lock_guard<std::mutex> lk(cacheMutex);
        cache.erase(path);
    }
    promise.set_value(out);
    return out;
}

static std::vector<std::string> scanI18nLanguageTags(const std::string& basePath) {
    std::vector<std::string> tags;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(basePath, ec)) {
        if (ec)
            break;

        const std::string name = entry.path().filename().string();
        if (name.size() <= 5 || name.substr(name.size() - 5) != ".json")
            continue;

        const std::string tag = name.substr(0, name.size() - 5);
        if (!tag.empty())
            tags.push_back(tag);
    }
    std::sort(tags.begin(), tags.end());
    tags.erase(std::unique(tags.begin(), tags.end()), tags.end());
    return tags;
}

std::vector<std::string> I18n::supportedLanguageTags() {
    std::vector<std::string> tags = scanI18nLanguageTags(instance().m_basePath);
    if (tags.empty() && instance().m_basePath != "romfs:/i18n")
        tags = scanI18nLanguageTags("romfs:/i18n");

    tags.erase(std::remove_if(tags.begin(), tags.end(), [](const std::string& tag) {
        return tag == "auto";
    }), tags.end());
    tags.insert(tags.begin(), "auto");
    return tags;
}

std::string I18n::detectSystemLanguageTag() {
    u64 langCode = 0;
    if (R_FAILED(setGetSystemLanguage(&langCode)))
        return "en-US";

    SetLanguage lang = SetLanguage_ENUS;
    if (R_FAILED(setMakeLanguage(langCode, &lang)))
        return "en-US";

    switch (lang) {
        case SetLanguage_JA:    return "ja-JP";
        case SetLanguage_ENUS:  return "en-US";
        case SetLanguage_FR:    return "fr-FR";
        case SetLanguage_DE:    return "de-DE";
        case SetLanguage_IT:    return "it-IT";
        case SetLanguage_ES:    return "es-ES";
        case SetLanguage_ZHCN:  return "zh-CN";
        case SetLanguage_KO:    return "ko-KR";
        case SetLanguage_NL:    return "nl-NL";
        case SetLanguage_PT:    return "pt-PT";
        case SetLanguage_RU:    return "ru-RU";
        case SetLanguage_ZHTW:  return "zh-TW";
        case SetLanguage_ENGB:  return "en-GB";
        case SetLanguage_FRCA:  return "fr-CA";
        case SetLanguage_ES419: return "es-419";
        case SetLanguage_ZHHANS:return "zh-CN";
        case SetLanguage_ZHHANT:return "zh-TW";
        case SetLanguage_PTBR:  return "pt-BR";
        default:                return "en-US";
    }
}

} // namespace nxui
