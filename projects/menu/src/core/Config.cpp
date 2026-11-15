#include "Config.hpp"
#include <cstdlib>
#include <cstdio>
#include "FolderStore.hpp"
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <mutex>
#include <system_error>

namespace {

bool isGamePortPlatform(const std::string& platform) {
    static constexpr const char* kPlatforms[] = {
        "nintendo-switch", "pc", "playstation-5", "playstation-4", "xbox-series-x",
        "xbox-one", "xbox-360", "playstation-3", "playstation-2", "playstation",
        "gamecube", "wii", "wii-u", "nintendo-64", "super-nintendo", "nes",
        "game-boy-advance", "nintendo-ds", "nintendo-3ds", "psp", "playstation-vita",
        "dreamcast", "sega-genesis",
    };
    for (const char* candidate : kPlatforms)
        if (platform == candidate) return true;
    return false;
}

template <typename T>
void readJsonOpt(const nlohmann::json& j, const char* key, T& out) {
    auto it = j.find(key);
    if (it != j.end() && !it->is_null()) {
        try {
            out = it->get<T>();
        } catch (...) {
        }
    }
}

} // namespace

bool AppConfig::load() {
    // The previous copy is tried when the current one is missing or unreadable.
    // A crash used to cost the player every setting they had ever chosen --
    // including tutorialCompleted, which is why the tutorial greeted them again
    // after the theme crash. Losing the last change is acceptable; losing the
    // whole file is not.
    nlohmann::json j;
    bool parsed = false;
    for (const char* path : {kConfigPath, kBackupPath}) {
        std::ifstream f(path);
        if (!f.is_open()) continue;
        try {
            f >> j;
            parsed = true;
            break;
        } catch (...) {
            // Truncated or half-written: fall through to the backup.
        }
    }
    if (!parsed) return false;

    readJsonOpt(j, "musicEnabled", musicEnabled);
    readJsonOpt(j, "themeMusicEnabled", themeMusicEnabled);
    readJsonOpt(j, "customBgmEnabled", customBgmEnabled);
    readJsonOpt(j, "customBgmShuffle", customBgmShuffle);
    readJsonOpt(j, "bgmRepeat", bgmRepeat);
    readJsonOpt(j, "audioSourcePreference", audioSourcePreference);
    readJsonOpt(j, "musicVolume", musicVolume);
    readJsonOpt(j, "sfxVolume", sfxVolume);
    readJsonOpt(j, "gridColumns", gridColumns);
    readJsonOpt(j, "gridRows", gridRows);
    readJsonOpt(j, "iconScalePercent", iconScalePercent);
    readJsonOpt(j, "sidebarScalePercent", sidebarScalePercent);
    readJsonOpt(j, "dynamicPages", dynamicPages);
    {
        std::string mode;
        readJsonOpt(j, "appLayoutMode", mode);
        if (mode == "dynamic_line" || mode == "line")
            appLayoutMode = AppLayoutMode::DynamicLine;
        else if (mode == "flow")
            appLayoutMode = AppLayoutMode::Flow;
        else if (mode == "shelf")
            appLayoutMode = AppLayoutMode::Shelf;
        else if (mode == "deck")
            appLayoutMode = AppLayoutMode::Deck;
        else if (mode == "cover")
            appLayoutMode = AppLayoutMode::Cover;
        else if (mode == "xmb")
            appLayoutMode = AppLayoutMode::Xmb;
        else if (mode == "list")
            appLayoutMode = AppLayoutMode::List;
        else if (mode == "metro" || mode == "tiles")
            appLayoutMode = AppLayoutMode::Metro;
        else if (mode == "cards") {
            // Legacy IconGrid cards mode (HomeNX Part 3 MVP): migrate to Grid,
            // as Cards is now an independent frontend (HomeStyle::Cards, D1/D2).
            appLayoutMode = AppLayoutMode::Grid;
        }
        else if (mode == "grid")
            appLayoutMode = AppLayoutMode::Grid;
    }
    if (j.contains("metroTileSpans") && j["metroTileSpans"].is_object()) {
        for (auto it = j["metroTileSpans"].begin(); it != j["metroTileSpans"].end(); ++it) {
            try {
                std::uint64_t tid = std::stoull(it.key(), nullptr, 16);
                if (it.value().is_array() && it.value().size() == 2) {
                    metroTileSpans[tid] = {it.value()[0].get<int>(), it.value()[1].get<int>()};
                }
            } catch (...) {}
        }
    }
    readJsonOpt(j, "actionHintStyle", actionHintStyle);
    readJsonOpt(j, "viewSwitchButton", viewSwitchButton);
    if (viewSwitchButton != "Minus" && viewSwitchButton != "-")
        viewSwitchButton = "R";
    if (j.contains("sidebarOrder") && j["sidebarOrder"].is_array()) {
        sidebarOrder.clear();
        for (const auto& el : j["sidebarOrder"]) {
            if (el.is_number_integer())
                sidebarOrder.push_back(el.get<int>());
        }
        // Must be a permutation of 0..9: a duplicate would show one button
        // twice and hide another.
        std::vector<int> sorted = sidebarOrder;
        std::sort(sorted.begin(), sorted.end());
        bool permutation = sorted.size() == 10;
        for (int i = 0; permutation && i < 10; ++i)
            permutation = sorted[static_cast<std::size_t>(i)] == i;
        if (!permutation)
            sidebarOrder = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    }
    readJsonOpt(j, "uiLanguageOverride", uiLanguageOverride);
    readJsonOpt(j, "mtpEnabled", mtpEnabled);
    readJsonOpt(j, "soundPreset", soundPreset);
    readJsonOpt(j, "iconPack", iconPack);
    readJsonOpt(j, "defaultProfileEnabled", defaultProfileEnabled);
    readJsonOpt(j, "defaultProfileUid", defaultProfileUid);
    readJsonOpt(j, "tutorialCompleted", tutorialCompleted);
    {
        std::string style;
        readJsonOpt(j, "homeStyle", style);
        homeStyle = home::parseHomeStyle(style);
    }
    readJsonOpt(j, "cardsSort", cardsSort);
    readJsonOpt(j, "cardsLaunchStyle", cardsLaunchStyle);
    readJsonOpt(j, "cardsAskUser", cardsAskUser);
    cards = CardsPrefs{};
    if (auto it = j.find("cards"); it != j.end() && it->is_object()) {
        const auto& c = *it;
        readJsonOpt(c, "sort", cardsSort);
        readJsonOpt(c, "launchStyle", cardsLaunchStyle);
        readJsonOpt(c, "askUser", cardsAskUser);
        readJsonOpt(c, "pageOrder", cards.pageOrder);
        readJsonOpt(c, "hiddenPages", cards.hiddenPages);
        readJsonOpt(c, "settingsOrder", cards.settingsOrder);
        readJsonOpt(c, "tutorialDone", cards.tutorialDone);
        readJsonOpt(c, "accent", cards.accent);
        readJsonOpt(c, "glass", cards.glass);
        readJsonOpt(c, "haze", cards.haze);
        readJsonOpt(c, "bgMotion", cards.bgMotion);
        readJsonOpt(c, "motion", cards.motion);
        readJsonOpt(c, "boxArt", cards.boxArt);
        readJsonOpt(c, "titlePics", cards.titlePics);
        auto readIds = [&c](const char* key, std::vector<std::uint64_t>& out) {
            out.clear();
            auto f = c.find(key);
            if (f == c.end() || !f->is_array())
                return;
            for (const auto& v : *f) {
                if (!v.is_string())
                    continue;
                const std::string s = v.get<std::string>();
                if (s.size() != 16 || s.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                    continue;
                const std::uint64_t id = std::strtoull(s.c_str(), nullptr, 16);
                if (id != 0 && std::find(out.begin(), out.end(), id) == out.end())
                    out.push_back(id);
            }
        };
        readIds("gamesOrder", cards.gamesOrder);
        readIds("mediaOrder", cards.mediaOrder);
        readIds("mediaTitles", cards.mediaTitles);
        readIds("iconTitles", cards.iconTitles);
        readIds("coverBackgroundTitles", cards.coverBackgroundTitles);
        readIds("plainBackgroundTitles", cards.plainBackgroundTitles);
        readIds("logoTitles", cards.logoTitles);
        std::vector<std::uint64_t> panel;
        readIds("panelTitle", panel);
        cards.panelTitleId = panel.empty() ? 0 : panel.front();
        readJsonOpt(c, "panelButtonEnabled", cards.panelButtonEnabled);
        readJsonOpt(c, "showNextendo", cards.showNextendo);
        readJsonOpt(c, "showFree", cards.showFree);
        readJsonOpt(c, "showTemp", cards.showTemp);
        readJsonOpt(c, "lockScreen", cards.lockScreen);
        readJsonOpt(c, "lockMusic", cards.lockMusic);
        readJsonOpt(c, "shuffle", cards.shuffle);
        readJsonOpt(c, "sounds", cards.sounds);
        readJsonOpt(c, "perfLog", cards.perfLog);
        readJsonOpt(c, "wallpaper", cards.wallpaper);
        readJsonOpt(c, "shuffleWall", cards.shuffleWall);
    }
    cards.normalize();
    readJsonOpt(j, "lastUpdateCheckDay", lastUpdateCheckDay);
    readJsonOpt(j, "nextendoPath", nextendoPath);
    readJsonOpt(j, "clockUse12Hour", clockUse12Hour);
    readJsonOpt(j, "backgroundBlur", backgroundBlur);
    readJsonOpt(j, "glassSharpness", glassSharpness);
    readJsonOpt(j, "solidPanels", solidPanels);
    readJsonOpt(j, "panelColor", panelColor);
    readJsonOpt(j, "cornerRoundness", cornerRoundness);
    cornerRoundness = std::clamp(cornerRoundness, 0.f, 1.f);
    readJsonOpt(j, "customTextColor", customTextColor);
    readJsonOpt(j, "textColor", textColor);
    readJsonOpt(j, "backgroundSpeed", backgroundSpeed);
    readJsonOpt(j, "backgroundSpeedChosen", backgroundSpeedChosen);
    readJsonOpt(j, "accessibilityEnabled", accessibilityEnabled);
    readJsonOpt(j, "accessibilitySpeakHints", accessibilitySpeakHints);
    readJsonOpt(j, "accessibilitySpeakContextEveryFocus", accessibilitySpeakContextEveryFocus);
    readJsonOpt(j, "accessibilitySpeakPosition", accessibilitySpeakPosition);
    readJsonOpt(j, "accessibilitySpeechRate", accessibilitySpeechRate);
    readJsonOpt(j, "steamGridDbEnabled", steamGridDbEnabled);
    readJsonOpt(j, "steamGridDbOpacity", steamGridDbOpacity);
    readJsonOpt(j, "steamGridDbShowInGrid", steamGridDbShowInGrid);
    readJsonOpt(j, "steamGridDbShowInDynamicLine", steamGridDbShowInDynamicLine);
    readJsonOpt(j, "steamGridDbShowInFolders", steamGridDbShowInFolders);
    readJsonOpt(j, "steamGridDbApiKey", steamGridDbApiKey);
    readJsonOpt(j, "ytdlBackendUrl", ytdlBackendUrl);
    readJsonOpt(j, "themePreset", themePreset);
    readJsonOpt(j, "lastPageTitleId", lastPageTitleId);
    readJsonOpt(j, "sortMode", sortMode);
    readJsonOpt(j, "lastOpenedSequence", lastOpenedSequence);
    readJsonOpt(j, "playtimeCheckedSequence", playtimeCheckedSequence);
    lastOpened.clear();
    if (auto it = j.find("lastOpened"); it != j.end() && it->is_object()) {
        for (auto& [k, v] : it->items()) {
            if (!v.is_number_unsigned()) continue;
            lastOpened.emplace_back(std::strtoull(k.c_str(), nullptr, 16),
                                    v.get<std::uint64_t>());
        }
    }
    playtime.clear();
    if (auto it = j.find("playtime"); it != j.end() && it->is_object()) {
        for (auto& [k, v] : it->items()) {
            if (!v.is_number_unsigned()) continue;
            // Zero is kept: it means "asked, never played", which is what stops
            // the title being asked about again at every boot.
            const std::uint64_t ns = v.get<std::uint64_t>();
            playtime.emplace_back(std::strtoull(k.c_str(), nullptr, 16), ns);
        }
    }
    gamePortPlatforms.clear();
    if (auto it = j.find("gamePorts"); it != j.end() && it->is_object()) {
        for (auto& [k, v] : it->items()) {
            if (!v.is_string()) continue;
            const std::string platform = v.get<std::string>();
            if (!isGamePortPlatform(platform)) continue;
            gamePortPlatforms.emplace_back(std::strtoull(k.c_str(), nullptr, 16), platform);
        }
    }
    gamePortSearchTitles.clear();
    if (auto it = j.find("gamePortSearchTitles"); it != j.end() && it->is_object()) {
        for (auto& [k, v] : it->items()) {
            if (!v.is_string()) continue;
            const std::string title = v.get<std::string>();
            if (title.empty()) continue;
            gamePortSearchTitles.emplace_back(std::strtoull(k.c_str(), nullptr, 16), title);
        }
    }
    customTitles.clear();
    if (auto it = j.find("customTitles"); it != j.end() && it->is_object()) {
        for (auto& [k, v] : it->items()) {
            if (!v.is_string()) continue;
            const std::string title = v.get<std::string>();
            if (title.empty()) continue;
            customTitles.emplace_back(std::strtoull(k.c_str(), nullptr, 16), title);
        }
    }
    favoriteTitleIds.clear();
    if (auto it = j.find("favorites"); it != j.end() && it->is_array()) {
        for (auto& v : *it) {
            if (v.is_string()) {
                const std::string s = v.get<std::string>();
                if (!s.empty()) {
                    favoriteTitleIds.push_back(std::strtoull(s.c_str(), nullptr, 16));
                }
            } else if (v.is_number_unsigned()) {
                favoriteTitleIds.push_back(v.get<std::uint64_t>());
            }
        }
    }
    steamGridDbKnownTitles.clear();
    if (auto it = j.find("steamGridDbKnownTitles"); it != j.end() && it->is_array()) {
        for (auto& v : *it) {
            if (v.is_string()) {
                const std::string s = v.get<std::string>();
                if (!s.empty()) {
                    steamGridDbKnownTitles.push_back(std::strtoull(s.c_str(), nullptr, 16));
                }
            } else if (v.is_number_unsigned()) {
                steamGridDbKnownTitles.push_back(v.get<std::uint64_t>());
            }
        }
    }
    readJsonOpt(j, "autoThemeMode", autoThemeMode);
    readJsonOpt(j, "autoThemeDayPreset", autoThemeDayPreset);
    readJsonOpt(j, "autoThemeNightPreset", autoThemeNightPreset);
    readJsonOpt(j, "autoThemeDayStartHour", autoThemeDayStartHour);
    readJsonOpt(j, "autoThemeNightStartHour", autoThemeNightStartHour);
    readJsonOpt(j, "autoThemeGeoResolved", autoThemeGeoResolved);
    readJsonOpt(j, "autoThemeGeoLat", autoThemeGeoLat);
    readJsonOpt(j, "autoThemeGeoLon", autoThemeGeoLon);
    readJsonOpt(j, "autoThemeGeoCity", autoThemeGeoCity);
    readJsonOpt(j, "folderStyle", folderStyle);
    readJsonOpt(j, "folderShowCover", folderShowCover);

    if (musicVolume < 0.f) musicVolume = 0.f;
    if (musicVolume > 1.f) musicVolume = 1.f;
    if (sfxVolume   < 0.f) sfxVolume   = 0.f;
    if (sfxVolume   > 1.f) sfxVolume   = 1.f;
    gridColumns = std::clamp(gridColumns, 3, 8);
    gridRows = std::clamp(gridRows, 2, 5);
    iconScalePercent = std::clamp(iconScalePercent, 80, 130);
    sidebarScalePercent = std::clamp(sidebarScalePercent, 80, 130);
    if (sortMode < 0 || sortMode >= kSortModeCount) sortMode = 0;
    if (cardsSort < 0 || cardsSort >= kCardsSortCount) cardsSort = 0;
    if (cardsLaunchStyle < 0 || cardsLaunchStyle >= kCardsLaunchStyleCount) cardsLaunchStyle = 0;
    if (actionHintStyle != "panel" && actionHintStyle != "capsules")
        actionHintStyle = "capsules";
    if (uiLanguageOverride.empty()) uiLanguageOverride = "auto";
    if (soundPreset.empty()) soundPreset = "wiiu";
    if (!defaultProfileEnabled) defaultProfileUid.clear();
    accessibilitySpeechRate = std::clamp(accessibilitySpeechRate, 120, 320);
    if (themePreset.empty()) themePreset = "Default Dark";
    // PoloNX #100 also remaps a 9-style table from its own development builds
    // when folderShowCover is absent; no build of this fork ever wrote one.
    folderStyle = std::clamp(folderStyle, 0, switchu::folders::kFolderStyleCount - 1);

    if (autoThemeMode != "off" && autoThemeMode != "manual" && autoThemeMode != "geo")
        autoThemeMode = "off";
    autoThemeDayStartHour = std::clamp(autoThemeDayStartHour, 0, 23);
    autoThemeNightStartHour = std::clamp(autoThemeNightStartHour, 0, 23);
    if (autoThemeGeoLat < -90.0 || autoThemeGeoLat > 90.0
        || autoThemeGeoLon < -180.0 || autoThemeGeoLon > 180.0) {
        autoThemeGeoResolved = false;
        autoThemeGeoLat = 0.0;
        autoThemeGeoLon = 0.0;
        autoThemeGeoCity.clear();
    }

    return true;
}

bool AppConfig::save() const {
    // Saves come from the UI thread and from pool workers (snapshots). They
    // share the staging and backup paths, so one runs at a time.
    static std::mutex s_saveMutex;
    std::lock_guard<std::mutex> saveLock(s_saveMutex);
    std::error_code ec;
    std::filesystem::create_directory("sdmc:/config", ec);
    ec.clear();
    std::filesystem::create_directory(kConfigDir, ec);

    nlohmann::json j;
    // In safe mode a value still equal to the in-memory override was not chosen
    // by the player, so the value they had before is what must stay on disk.
    if (m_safe) {
        if (musicEnabled) m_touchedMusic = true;
        if (customBgmEnabled) m_touchedBgm = true;
        if (soundPreset != "wiiu") m_touchedSound = true;
        if (themePreset != kSafeTheme) m_touchedTheme = true;
    }
    j["musicEnabled"] = (m_safe && !m_touchedMusic) ? m_savedMusic : musicEnabled;
    j["themeMusicEnabled"] = themeMusicEnabled;
    j["iconPack"] = iconPack;
    j["customBgmEnabled"] = (m_safe && !m_touchedBgm) ? m_savedCustomBgm : customBgmEnabled;
    j["customBgmShuffle"] = customBgmShuffle;
    j["bgmRepeat"] = bgmRepeat;
    j["audioSourcePreference"] = audioSourcePreference;
    j["musicVolume"] = musicVolume;
    j["sfxVolume"] = sfxVolume;
    j["gridColumns"] = std::clamp(gridColumns, 3, 8);
    j["gridRows"] = std::clamp(gridRows, 2, 5);
    j["iconScalePercent"] = std::clamp(iconScalePercent, 80, 130);
    j["sidebarScalePercent"] = std::clamp(sidebarScalePercent, 80, 130);
    j["dynamicPages"] = dynamicPages;
    // Stable strings, never enum ordinals: an installation written by a build
    // that knows Flow/Shelf/Deck must still read correctly on one that does not.
    j["appLayoutMode"] = appLayoutMode == AppLayoutMode::DynamicLine ? "dynamic_line"
                       : appLayoutMode == AppLayoutMode::Flow        ? "flow"
                       : appLayoutMode == AppLayoutMode::Shelf       ? "shelf"
                       : appLayoutMode == AppLayoutMode::Deck        ? "deck"
                       : appLayoutMode == AppLayoutMode::Cover       ? "cover"
                       : appLayoutMode == AppLayoutMode::Xmb         ? "xmb"
                       : appLayoutMode == AppLayoutMode::List        ? "list"
                       : appLayoutMode == AppLayoutMode::Metro       ? "metro"
                                                                     : "grid";
    if (!metroTileSpans.empty()) {
        nlohmann::json spansObj = nlohmann::json::object();
        for (const auto& kv : metroTileSpans) {
            char tidHex[32];
            std::snprintf(tidHex, sizeof(tidHex), "%016llx", static_cast<unsigned long long>(kv.first));
            spansObj[tidHex] = {kv.second.first, kv.second.second};
        }
        j["metroTileSpans"] = spansObj;
    }
    j["actionHintStyle"] = actionHintStyle == "panel" ? "panel" : "capsules";
    j["viewSwitchButton"] = (viewSwitchButton == "Minus" || viewSwitchButton == "-") ? "Minus" : "R";
    j["sidebarOrder"] = sidebarOrder;
    j["uiLanguageOverride"] = uiLanguageOverride;
    j["mtpEnabled"] = mtpEnabled;
    j["soundPreset"] = (m_safe && !m_touchedSound) ? m_savedSound : soundPreset;
    j["defaultProfileEnabled"] = defaultProfileEnabled;
    j["defaultProfileUid"] = defaultProfileEnabled ? defaultProfileUid : std::string();
    j["tutorialCompleted"] = tutorialCompleted;
    j["homeStyle"] = home::homeStyleName(homeStyle);
    {
        auto ids = [](const std::vector<std::uint64_t>& in) {
            nlohmann::json arr = nlohmann::json::array();
            char key[17];
            for (std::uint64_t tid : in) {
                std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)tid);
                arr.push_back(std::string(key));
            }
            return arr;
        };
        nlohmann::json c = nlohmann::json::object();
        c["sort"] = cardsSort;
        c["launchStyle"] = cardsLaunchStyle;
        c["askUser"] = cardsAskUser;
        c["pageOrder"] = cards.pageOrder;
        c["hiddenPages"] = cards.hiddenPages;
        c["gamesOrder"] = ids(cards.gamesOrder);
        c["mediaOrder"] = ids(cards.mediaOrder);
        c["mediaTitles"] = ids(cards.mediaTitles);
        c["iconTitles"] = ids(cards.iconTitles);
        c["coverBackgroundTitles"] = ids(cards.coverBackgroundTitles);
        c["plainBackgroundTitles"] = ids(cards.plainBackgroundTitles);
        c["logoTitles"] = ids(cards.logoTitles);
        c["panelTitle"] = ids(cards.panelTitleId ? std::vector<std::uint64_t>{cards.panelTitleId}
                                               : std::vector<std::uint64_t>{});
        c["panelButtonEnabled"] = cards.panelButtonEnabled;
        c["showNextendo"] = cards.showNextendo;
        c["showFree"] = cards.showFree;
        c["showTemp"] = cards.showTemp;
        c["lockScreen"] = cards.lockScreen;
        c["lockMusic"] = cards.lockMusic;
        c["shuffle"] = cards.shuffle;
        c["sounds"] = cards.sounds;
        c["perfLog"] = cards.perfLog;
        c["wallpaper"] = cards.wallpaper;
        c["shuffleWall"] = cards.shuffleWall;
        c["settingsOrder"] = cards.settingsOrder;
        c["tutorialDone"] = cards.tutorialDone;
        c["accent"] = cards.accent;
        c["glass"] = cards.glass;
        c["haze"] = cards.haze;
        c["bgMotion"] = cards.bgMotion;
        c["motion"] = cards.motion;
        c["boxArt"] = cards.boxArt;
        c["titlePics"] = cards.titlePics;
        j["cards"] = std::move(c);
    }
    j["lastUpdateCheckDay"] = lastUpdateCheckDay;
    j["nextendoPath"] = nextendoPath;
    j["clockUse12Hour"] = clockUse12Hour;
    j["backgroundBlur"] = backgroundBlur;
    j["glassSharpness"] = glassSharpness;
    j["solidPanels"] = solidPanels;
    j["panelColor"] = panelColor;
    j["cornerRoundness"] = cornerRoundness;
    j["customTextColor"] = customTextColor;
    j["textColor"] = textColor;
    j["backgroundSpeed"] = backgroundSpeed;
    j["backgroundSpeedChosen"] = backgroundSpeedChosen;
    j["accessibilityEnabled"] = accessibilityEnabled;
    j["accessibilitySpeakHints"] = accessibilitySpeakHints;
    j["accessibilitySpeakContextEveryFocus"] = accessibilitySpeakContextEveryFocus;
    j["accessibilitySpeakPosition"] = accessibilitySpeakPosition;
    j["accessibilitySpeechRate"] = std::clamp(accessibilitySpeechRate, 120, 320);
    j["steamGridDbEnabled"] = steamGridDbEnabled;
    j["steamGridDbOpacity"] = std::clamp(steamGridDbOpacity, 0.f, 1.f);
    j["steamGridDbShowInGrid"] = steamGridDbShowInGrid;
    j["steamGridDbShowInDynamicLine"] = steamGridDbShowInDynamicLine;
    j["steamGridDbShowInFolders"] = steamGridDbShowInFolders;
    j["steamGridDbApiKey"] = steamGridDbApiKey;
    j["ytdlBackendUrl"] = ytdlBackendUrl;
    j["themePreset"] = (m_safe && !m_touchedTheme) ? m_savedTheme : themePreset;
    j["folderStyle"] = std::clamp(folderStyle, 0, switchu::folders::kFolderStyleCount - 1);
    j["folderShowCover"] = folderShowCover;
    j["lastPageTitleId"] = lastPageTitleId;
    j["sortMode"] = sortMode;
    j["lastOpenedSequence"] = lastOpenedSequence;
    j["playtimeCheckedSequence"] = playtimeCheckedSequence;
    {
        nlohmann::json opened = nlohmann::json::object();
        char key[17];
        for (const auto& e : lastOpened) {
            std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)e.first);
            opened[key] = e.second;
        }
        j["lastOpened"] = std::move(opened);
    }
    {
        nlohmann::json played = nlohmann::json::object();
        char key[17];
        for (const auto& e : playtime) {
            std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)e.first);
            played[key] = e.second;
        }
        j["playtime"] = std::move(played);
    }
    {
        nlohmann::json ports = nlohmann::json::object();
        char key[17];
        for (const auto& port : gamePortPlatforms) {
            std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)port.first);
            ports[key] = port.second;
        }
        j["gamePorts"] = std::move(ports);
    }
    {
        nlohmann::json searchTitles = nlohmann::json::object();
        char key[17];
        for (const auto& entry : gamePortSearchTitles) {
            std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)entry.first);
            searchTitles[key] = entry.second;
        }
        j["gamePortSearchTitles"] = std::move(searchTitles);
    }
    {
        nlohmann::json titles = nlohmann::json::object();
        char key[17];
        for (const auto& entry : customTitles) {
            std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)entry.first);
            titles[key] = entry.second;
        }
        j["customTitles"] = std::move(titles);
    }
    {
        nlohmann::json favorites = nlohmann::json::array();
        char key[17];
        for (std::uint64_t tid : favoriteTitleIds) {
            std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)tid);
            favorites.push_back(std::string(key));
        }
        j["favorites"] = std::move(favorites);
    }
    {
        nlohmann::json known = nlohmann::json::array();
        char key[17];
        for (std::uint64_t tid : steamGridDbKnownTitles) {
            std::snprintf(key, sizeof(key), "%016llX", (unsigned long long)tid);
            known.push_back(std::string(key));
        }
        j["steamGridDbKnownTitles"] = std::move(known);
    }
    j["autoThemeMode"] = (autoThemeMode == "manual" || autoThemeMode == "geo") ? autoThemeMode : "off";
    j["autoThemeDayPreset"] = autoThemeDayPreset;
    j["autoThemeNightPreset"] = autoThemeNightPreset;
    j["autoThemeDayStartHour"] = std::clamp(autoThemeDayStartHour, 0, 23);
    j["autoThemeNightStartHour"] = std::clamp(autoThemeNightStartHour, 0, 23);
    j["autoThemeGeoResolved"] = autoThemeGeoResolved;
    j["autoThemeGeoLat"] = autoThemeGeoLat;
    j["autoThemeGeoLon"] = autoThemeGeoLon;
    j["autoThemeGeoCity"] = autoThemeGeoCity;

    // Written beside the real file and swapped in, never over it. Truncating
    // the live config and then dying mid-write is how a crash used to reset
    // every setting to its default: load() found a half-written file, failed to
    // parse it, and started from scratch. The staging file absorbs that risk.
    const std::string staging = std::string(kConfigPath) + ".part";

    {
        std::ofstream f(staging, std::ios::trunc);
        if (!f.is_open()) return false;
        f << j.dump(2);
        f.flush();
        if (!f.good()) {
            f.close();
            std::remove(staging.c_str());
            return false;
        }
    }

    // The outgoing copy becomes the backup rather than being deleted, so the
    // window in which neither file is complete costs nothing.
    std::remove(kBackupPath);
    std::rename(kConfigPath, kBackupPath);   // absent on the very first save
    if (std::rename(staging.c_str(), kConfigPath) != 0) {
        std::remove(staging.c_str());
        return false;
    }
    // No commit here: save() is submitted to the thread pool, so this ran on a
    // worker while the main thread was also writing. Committing an fs session
    // from two threads at once is its own hazard, and the author's build —
    // which does not corrupt — commits nowhere.
    return true;
}

void AppConfig::CardsPrefs::normalize() {
    // Pages offered in this build. Homebrew (1) active from Part F.
    static constexpr int kListed[] = {kGames, kHomebrew, kNintendo, kMedia};
    auto listed = [](int p) {
        for (int k : kListed)
            if (k == p) return true;
        return false;
    };
    std::vector<int> order;
    for (int p : pageOrder)
        if (listed(p) && std::find(order.begin(), order.end(), p) == order.end())
            order.push_back(p);
    for (int k : kListed)
        if (std::find(order.begin(), order.end(), k) == order.end())
            order.push_back(k);
    pageOrder = std::move(order);

    std::vector<int> hidden;
    for (int p : hiddenPages)
        if (listed(p) && std::find(hidden.begin(), hidden.end(), p) == hidden.end())
            hidden.push_back(p);
    if (hidden.size() >= pageOrder.size()) // one page always shows
        hidden.erase(std::remove(hidden.begin(), hidden.end(), pageOrder.front()), hidden.end());
    hiddenPages = std::move(hidden);

    if (accent < 0 || accent >= 7) accent = 1;
    if (glass < 0 || glass >= 3) glass = 1;
    haze = std::clamp(haze, 0, 80);
}
