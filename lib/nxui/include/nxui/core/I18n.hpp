#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <functional>

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

    static std::string detectSystemLanguageTag();
    static std::vector<std::string> supportedLanguageTags();

    // Temporary start-up timing of the last initialize() (microseconds).
    struct InitTiming {
        uint64_t fallbackUs = 0;   // fallback (en-US) map load
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
    std::string buildLanguagePath(const std::string& normalizedTag) const;

    bool loadJsonMapFromFile(const std::string& path,
                             std::unordered_map<std::string, std::string>& out) const;
    void notifyLanguageChanged();

    std::string m_basePath = "romfs:/i18n";
    std::string m_fallbackTag = "en-US";
    std::string m_activeTag = "en-US";

    std::unordered_map<std::string, std::string> m_fallbackMap;
    std::unordered_map<std::string, std::string> m_activeMap;
    std::unordered_map<int, LanguageChangedCb> m_languageListeners;
    int m_nextListenerId = 1;
    InitTiming m_initTiming;
    mutable uint32_t m_cacheHits = 0;
};

} // namespace nxui
