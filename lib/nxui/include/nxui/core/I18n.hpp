#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>
#include <future>
#include <memory>

namespace nxui {

class I18n {
public:
    using LanguageChangedCb = std::function<void()>;

    static I18n& instance();

    bool initialize(const std::string& basePath = "romfs:/i18n",
                    const std::string& fallbackTag = "en-US");

    bool setLanguageAuto();
    bool setLanguage(const std::string& tag);

    std::string tr(std::string_view key, std::string_view fallback = "") const;

    int  addLanguageChangedListener(LanguageChangedCb cb);
    void removeLanguageChangedListener(int id);

    const std::string& activeLanguageTag() const { return m_activeTag; }
    const std::string& fallbackLanguageTag() const { return m_fallbackTag; }

    // Asks the system settings service every time (an IPC round trip).
    static std::string detectSystemLanguageTag();
    static std::vector<std::string> supportedLanguageTags();

    // Starts, on background threads, the work initialize() would do on the
    // calling thread: the system language query and the parse of the language
    // files it will need (the console language, `languageOverride` unless it
    // is "auto"/empty, and the en-US fallback). Call once, early, from the
    // thread that later calls initialize(); initialize() then waits only for
    // whatever has not finished yet.
    void prefetch(const std::string& basePath, const std::string& languageOverride);

    // The console language as used by initialize(), setLanguageAuto() and
    // setLanguage("auto"): asked once per process (or taken from prefetch()).
    // The menu is a new process after every return, so a console language
    // change is still seen on the next start.
    const std::string& systemLanguageTag();

    // Temporary start-up timing of the last initialize() (microseconds).
    struct InitTiming {
        uint64_t fallbackUs = 0;   // starting the background fallback (en-US) load
        uint64_t detectUs = 0;     // system language query
        uint64_t activeUs = 0;     // active language map load
        uint64_t notifyUs = 0;     // language-changed listeners
        uint32_t listeners = 0;
        uint32_t cacheHits = 0;    // maps served from the process cache
        uint32_t entries = 0;      // keys in the active map
    };
    const InitTiming& lastInitTiming() const { return m_initTiming; }

private:
    I18n() = default;

    static std::string normalizeTag(const std::string& tag);
    // Regional variant without its own file -> closest shipped language.
    static std::string closestShippedTag(const std::string& basePath, const std::string& normalizedTag);
    std::string buildLanguagePath(const std::string& normalizedTag) const;

    using Map = std::unordered_map<std::string, std::string>;

    // Parsed maps are shared per path for the process; null when the file is
    // missing or does not parse.
    static std::shared_ptr<const Map> loadJsonMapFromFile(const std::string& path,
                                                          bool* cacheHit = nullptr);
    const Map& fallbackMap() const;
    void startFallbackLoad();
    void notifyLanguageChanged();

    std::string m_basePath = "romfs:/i18n";
    std::string m_fallbackTag = "en-US";
    std::string m_activeTag = "en-US";
    std::string m_systemTag;   // first detection; empty = not known yet
    std::shared_future<std::string> m_systemFuture; // prefetch(): detection + parse
    std::shared_future<void> m_overrideFuture;      // prefetch(): override parse

    // The fallback (en-US) map is only read for keys the active language
    // lacks, so it is parsed on a background thread while start-up goes on.
    std::shared_future<std::shared_ptr<const Map>> m_fallbackFuture;
    std::shared_ptr<const Map> m_activeMap;
    std::unordered_map<int, LanguageChangedCb> m_languageListeners;
    int m_nextListenerId = 1;
    InitTiming m_initTiming;
    uint32_t m_cacheHits = 0;
};

} // namespace nxui
