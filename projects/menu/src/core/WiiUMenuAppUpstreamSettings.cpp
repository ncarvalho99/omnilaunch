#include "WiiUMenuApp.hpp"

#include <switchu/control_cache.hpp>
#include <switchu/title_footprint.hpp>
#include "themeshop/ThemePackageInstaller.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "widgets/GlossyIcon.hpp"
#include "DebugLog.hpp"
#include "NsService.hpp"

#include <nxui/core/I18n.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fmt/format.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>
#include <utility>

namespace {

bool removeDirectoryRecursive(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    return !ec;
}
bool pathExists(const std::string& path) {
    if (path.empty())
        return false;

    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string joinPath(const std::string& base, const std::string& name) {
    if (base.empty())
        return name;
    if (name.empty())
        return base;
    if (base.back() == '/')
        return base + name;
    return base + "/" + name;
}

bool isAbsoluteThemePath(const std::string& path) {
    return (!path.empty() && path.front() == '/') || (path.find(":/") != std::string::npos);
}

std::string trimSlashes(std::string path) {
    while (!path.empty() && path.front() == '/')
        path.erase(path.begin());
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    return path;
}

bool readJsonString(const nlohmann::json& obj, const char* key, std::string& out) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string())
        return false;
    out = it->get<std::string>();
    return !out.empty();
}

std::string installedThemePreviewPath(const ThemePreset& preset) {
    if (preset.installPath.empty())
        return {};

    auto resolveLocal = [&](const std::string& raw) -> std::string {
        if (raw.empty() || raw.rfind("http://", 0) == 0 || raw.rfind("https://", 0) == 0)
            return {};
        std::string path = isAbsoluteThemePath(raw) ? raw : joinPath(preset.installPath, trimSlashes(raw));
        return pathExists(path) ? path : std::string();
    };

    nlohmann::json manifest;
    std::ifstream input(joinPath(preset.installPath, "theme.json"));
    if (input) {
        try {
            input >> manifest;
        } catch (...) {
            manifest = nlohmann::json{};
        }
    }

    if (manifest.is_object()) {
        std::string raw;
        if ((readJsonString(manifest, "cover", raw)
             || readJsonString(manifest, "screenshot", raw)
             || readJsonString(manifest, "thumbnail", raw))) {
            std::string path = resolveLocal(raw);
            if (!path.empty())
                return path;
        }

        auto previewIt = manifest.find("preview");
        if (previewIt != manifest.end() && previewIt->is_object()) {
            raw.clear();
            if ((readJsonString(*previewIt, "cover", raw)
                 || readJsonString(*previewIt, "screenshot", raw)
                 || readJsonString(*previewIt, "thumbnail", raw))) {
                std::string path = resolveLocal(raw);
                if (!path.empty())
                    return path;
            }

            auto screenshotsIt = previewIt->find("screenshots");
            if (screenshotsIt != previewIt->end() && screenshotsIt->is_array()) {
                for (const auto& value : *screenshotsIt) {
                    if (!value.is_string())
                        continue;
                    std::string path = resolveLocal(value.get<std::string>());
                    if (!path.empty())
                        return path;
                }
            }
        }

        auto screenshotsIt = manifest.find("screenshots");
        if (screenshotsIt != manifest.end() && screenshotsIt->is_array()) {
            for (const auto& value : *screenshotsIt) {
                if (!value.is_string())
                    continue;
                std::string path = resolveLocal(value.get<std::string>());
                if (!path.empty())
                    return path;
            }
        }
    }

    static constexpr const char* kFallbacks[] = {
        "preview/cover.png",
        "preview/screenshot.png",
        "preview/cover.jpg",
        "preview/screenshot.jpg",
        "screenshots/cover.png",
        "screenshots/screenshot.png",
        "cover.png",
        "screenshot.png",
    };
    for (const char* fallback : kFallbacks) {
        std::string path = joinPath(preset.installPath, fallback);
        if (pathExists(path))
            return path;
    }

    return {};
}

const char* safeLogPath(const std::string& path) {
    return path.empty() ? "<empty>" : path.c_str();
}

const char* themeSourceName(ThemePresetSource source) {
    switch (source) {
        case ThemePresetSource::BuiltIn: return "builtin";
        case ThemePresetSource::UserPreset: return "user";
        case ThemePresetSource::InstalledPackage: return "package";
        default: return "unknown";
    }
}

WaraWaraBackground::Layout toBackgroundLayout(ThemeBackgroundLayout layout) {
    switch (layout) {
        case ThemeBackgroundLayout::Grid:
            return WaraWaraBackground::Layout::Grid;
        case ThemeBackgroundLayout::Floating:
        default:
            return WaraWaraBackground::Layout::Floating;
    }
}

WaraWaraBackground::ShapeSet toBackgroundShapeSet(ThemeBackgroundShapeSet shapeSet) {
    switch (shapeSet) {
        case ThemeBackgroundShapeSet::Circle:
            return WaraWaraBackground::ShapeSet::Circle;
        case ThemeBackgroundShapeSet::Triangle:
            return WaraWaraBackground::ShapeSet::Triangle;
        case ThemeBackgroundShapeSet::Square:
            return WaraWaraBackground::ShapeSet::Square;
        case ThemeBackgroundShapeSet::Diamond:
            return WaraWaraBackground::ShapeSet::Diamond;
        case ThemeBackgroundShapeSet::Hexagon:
            return WaraWaraBackground::ShapeSet::Hexagon;
        case ThemeBackgroundShapeSet::Mixed:
        default:
            return WaraWaraBackground::ShapeSet::Mixed;
    }
}

WaraWaraBackground::Symmetry toBackgroundSymmetry(ThemeBackgroundSymmetry symmetry) {
    switch (symmetry) {
        case ThemeBackgroundSymmetry::MirrorX:
            return WaraWaraBackground::Symmetry::MirrorHorizontal;
        case ThemeBackgroundSymmetry::MirrorY:
            return WaraWaraBackground::Symmetry::MirrorVertical;
        case ThemeBackgroundSymmetry::Quad:
            return WaraWaraBackground::Symmetry::Quad;
        case ThemeBackgroundSymmetry::None:
        default:
            return WaraWaraBackground::Symmetry::None;
    }
}

std::string sourceLabel(ThemePresetSource source) {
    auto& i18n = nxui::I18n::instance();
    switch (source) {
        case ThemePresetSource::BuiltIn:
            return i18n.tr("themeshop.source.builtin", "Built-in");
        case ThemePresetSource::UserPreset:
            return i18n.tr("themeshop.source.custom", "Custom");
        case ThemePresetSource::InstalledPackage:
            return i18n.tr("themeshop.source.community", "Community");
    }

    return i18n.tr("themeshop.source.unknown", "Unknown");
}

std::string defaultThemeRef() {
    return "builtin:Default Light";
}

} // namespace


void WiiUMenuApp::editSteamGridDbApiKey() {
    auto& i18n = nxui::I18n::instance();
    requestTextEntry(i18n.tr("settings.steamgriddb.api_key", "API key"),
                     // Not masked. The key is stored in plain text in
                     // config.json on the card, so hiding it while typing
                     // protects nothing and left no way to check a mistyped
                     // key against a failed download.
                     "SteamGridDB", m_config.steamGridDbApiKey, 192, false,
                     [this](const std::string& typed) {
        m_config.steamGridDbApiKey = typed;
        m_config.save();
        if (m_themeShop) {
            m_themeShop->setSteamGridDbViewFlags(m_config.steamGridDbShowInGrid, m_config.steamGridDbShowInDynamicLine, m_config.steamGridDbShowInFolders);
            m_themeShop->setSteamGridDbState(m_config.steamGridDbEnabled,
                                            !m_config.steamGridDbApiKey.empty());
            m_themeShop->refreshCurrentTabWidgets();
            m_themeShop->requestToast(m_config.steamGridDbApiKey.empty()
                ? nxui::I18n::instance().tr("settings.steamgriddb.key_cleared", "API key cleared.")
                : nxui::I18n::instance().tr("settings.steamgriddb.key_saved", "API key saved."));
            focusManager().setFocus(m_themeShop.get());
        }

    });
}

void WiiUMenuApp::startSteamGridDbScrape() {
    // No key is required: an empty key routes the manager through this fork's
    // gallery service. A key only adds the logo source the proxy does not have.
    if (m_steamGridDbBrowseFuture.valid() || m_steamGridDbApplyFuture.valid()) {
        const auto msg = nxui::I18n::instance().tr(
            "settings.steamgriddb.already_running", "A SteamGridDB operation is already running.");
        if (m_themeShop && m_themeShop->isActive())
            m_themeShop->requestToast(msg);
        else if (m_settings)
            m_settings->requestToast(msg);
        return;
    }
    if (!m_steamGridDb.start(m_config.steamGridDbApiKey, m_allApps)) {
        const auto msg = nxui::I18n::instance().tr(
            "settings.steamgriddb.start_failed", "The artwork scan could not be started.");
        if (m_themeShop && m_themeShop->isActive())
            m_themeShop->requestToast(msg);
        else if (m_settings)
            m_settings->requestToast(msg);
        return;
    }
    m_steamGridDbWasRunning = true;
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->show(nxui::I18n::instance().tr(
            "settings.steamgriddb.download_title", "Downloading artwork"),
            nxui::I18n::instance().tr(
                "settings.steamgriddb.download_start", "Searching SteamGridDB..."), 0.f);
        focusManager().setFocus(m_progressDialog.get());
    }
    const auto startedMsg = nxui::I18n::instance().tr(
        "settings.steamgriddb.started", "SteamGridDB scan started.");
    if (m_themeShop && m_themeShop->isActive())
        m_themeShop->requestToast(startedMsg);
    else if (m_settings)
        m_settings->requestToast(startedMsg);
}

void WiiUMenuApp::openSteamGridDbPicker(GameOptionsScreen::ArtworkKind kind,
                                        const std::string& requestedQuery) {
    if (!m_gameOptions || m_gameOptionsTitleId == 0) return;
    if (kind == GameOptionsScreen::ArtworkKind::Logo
        && m_config.steamGridDbApiKey.empty()) {
        // Heroes and icons come from this fork's gallery service without a key;
        // logos have no source there and still need the player's own key.
        m_gameOptions->requestToast(nxui::I18n::instance().tr(
            "settings.steamgriddb.need_key_logo",
            "Logos need a SteamGridDB API key. Configure one in Settings."));
        return;
    }
    SteamGridDbManager::ArtworkKind managerKind = SteamGridDbManager::ArtworkKind::Hero;
    if (kind == GameOptionsScreen::ArtworkKind::Logo)
        managerKind = SteamGridDbManager::ArtworkKind::Logo;
    else if (kind == GameOptionsScreen::ArtworkKind::Icon)
        managerKind = SteamGridDbManager::ArtworkKind::Icon;
    else if (kind == GameOptionsScreen::ArtworkKind::Cover)
        managerKind = SteamGridDbManager::ArtworkKind::Cover;

    std::string title;
    std::string defaultQuery;
    for (const auto& appEntry : m_allApps) {
        if (appEntry.titleId == m_gameOptionsTitleId) {
            title = appEntry.title;
            defaultQuery = appEntry.steamGridDbTitle();
            break;
        }
    }
    if (title.empty() || m_steamGridDbBrowseFuture.valid()
        || m_steamGridDbApplyFuture.valid() || m_steamGridDb.running()) {
        m_gameOptions->requestToast(nxui::I18n::instance().tr(
            "settings.steamgriddb.already_running", "A SteamGridDB operation is already running."));
        return;
    }
    const std::string query = requestedQuery.empty() ? defaultQuery : requestedQuery;
    m_steamGridDbPicker->showLoading(m_gameOptionsTitleId, title, query, managerKind);
    focusManager().setFocus(m_steamGridDbPicker.get());
    m_steamGridDbBrowseFuture = std::async(std::launch::async,
        [apiKey = m_config.steamGridDbApiKey, titleId = m_gameOptionsTitleId,
         title, query, managerKind]() {
            return SteamGridDbManager::browse(apiKey, titleId, title, query, managerKind);
        });
}

void WiiUMenuApp::openImagePinSteamGridDbPicker(int targetSlot, const nxui::Rect& anchor,
                                                switchu::widgets::WidgetSize size,
                                                const std::string& requestedQuery) {
    if (m_steamGridDbBrowseFuture.valid() || m_steamGridDbApplyFuture.valid() || m_steamGridDb.running())
        return;
    if (requestedQuery.empty()) return;
    m_imagePinTargetSlot = targetSlot;
    m_imagePinSize = size;
    m_imagePinAnchor = anchor;
    const std::string query = requestedQuery;
    const std::uint64_t pickerId = switchu::widgets::widgetTitleId(
        static_cast<std::uint32_t>(targetSlot + 1));
    m_steamGridDbPicker->showLoading(pickerId, query, query,
                                     SteamGridDbManager::ArtworkKind::Hero);
    focusManager().setFocus(m_steamGridDbPicker.get());
    m_steamGridDbBrowseFuture = std::async(std::launch::async,
        [apiKey = m_config.steamGridDbApiKey, pickerId, query]() {
            return SteamGridDbManager::browse(apiKey, pickerId, query, query,
                                              SteamGridDbManager::ArtworkKind::Hero);
        });
}

void WiiUMenuApp::editSteamGridDbPickerQuery() {
    if (!m_steamGridDbPicker || !m_steamGridDbPicker->isActive()) return;
    auto& i18n = nxui::I18n::instance();
    const auto kind = m_steamGridDbPicker->artworkKind();
    requestTextEntry(i18n.tr("settings.steamgriddb.scan", "Search artwork"),
                     "SteamGridDB", m_steamGridDbPicker->query(), 128, false,
                     [this, kind](const std::string& typed) {
        if (typed.empty()) return;
        if (m_imagePinTargetSlot >= 0) {
            openImagePinSteamGridDbPicker(m_imagePinTargetSlot, m_imagePinAnchor,
                                          m_imagePinSize, typed);
            return;
        }
        const auto mappedKind = kind == SteamGridDbManager::ArtworkKind::Logo
            ? GameOptionsScreen::ArtworkKind::Logo
            : kind == SteamGridDbManager::ArtworkKind::Icon
                ? GameOptionsScreen::ArtworkKind::Icon : GameOptionsScreen::ArtworkKind::Hero;
        openSteamGridDbPicker(mappedKind, typed);
    });
}

void WiiUMenuApp::applyImagePinSteamGridDbCandidate(
    const SteamGridDbManager::BrowseResult&,
    const SteamGridDbManager::Candidate& candidate) {
    if (m_steamGridDbApplyFuture.valid() || candidate.url.empty()) return;
    auto progress = std::make_shared<SteamGridDbApplyProgressShared>();
    progress->message = "Downloading pin image...";
    m_steamGridDbApplyProgress = progress;
    m_steamGridDbApplyProgressUiRevision = 0;
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->show("Downloading image pin", progress->message, 0.f);
        focusManager().setFocus(m_progressDialog.get());
    }
    m_imagePinApplyPending = true;
    const int targetSlot = m_imagePinTargetSlot;
    const auto size = m_imagePinSize;
    m_steamGridDbApplyFuture = std::async(std::launch::async, [candidate, progress, targetSlot, size]() {
        SteamGridDbManager::ApplyResult result;
        result.titleId = static_cast<std::uint64_t>(targetSlot);
        try {
            std::error_code ec;
            std::filesystem::create_directories(switchu::widgets::WidgetStore::kAssetRoot, ec);
            const std::string filename = "steamgriddb_pin_" + std::to_string(candidate.id) + ".png";
            const std::string fullPath = std::string(switchu::widgets::WidgetStore::kAssetRoot) + "/" + filename;
            auto bytes = themeshop::http::getBytes(candidate.url);
            if (bytes.empty()) throw std::runtime_error("Empty image data");
            std::ofstream out(fullPath, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            if (!out.good()) throw std::runtime_error("Could not save image file");
            result.success = true;
            result.message = filename;
        } catch (const std::exception& ex) {
            result.message = ex.what();
        }
        return result;
    });
}

void WiiUMenuApp::applySteamGridDbCandidate(
    const SteamGridDbManager::BrowseResult& browse,
    const SteamGridDbManager::Candidate& candidate) {
    if (m_steamGridDbApplyFuture.valid() || m_steamGridDb.running()) return;
    auto progress = std::make_shared<SteamGridDbApplyProgressShared>();
    progress->message = nxui::I18n::instance().tr(
        "settings.steamgriddb.download_start", "Starting download...");
    m_steamGridDbApplyProgress = progress;
    m_steamGridDbApplyProgressUiRevision = 0;
    if (m_progressDialog) {
        m_progressDialog->setTheme(&m_theme);
        m_progressDialog->show(nxui::I18n::instance().tr(
            "settings.steamgriddb.download_title", "Downloading artwork"),
            progress->message, 0.f);
        focusManager().setFocus(m_progressDialog.get());
    }
    m_steamGridDbApplyFuture = std::async(std::launch::async, [browse, candidate, progress]() {
        return SteamGridDbManager::applyCandidate(browse, candidate,
            [progress](const std::string& message, float value) {
                std::lock_guard<std::mutex> lock(progress->mutex);
                progress->message = message;
                progress->progress01 = value;
                ++progress->revision;
            });
    });
}

void WiiUMenuApp::syncSteamGridDb() {
    if (m_steamGridDbBrowseFuture.valid()
        && m_steamGridDbBrowseFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        auto result = m_steamGridDbBrowseFuture.get();
        if (m_steamGridDbPicker && m_steamGridDbPicker->isActive()
            && m_steamGridDbPicker->titleId() == result.titleId) {
            m_steamGridDbPicker->setResult(std::move(result));
            focusManager().setFocus(m_steamGridDbPicker.get());
        }
    }
    if (m_steamGridDbApplyProgress) {
        std::string message;
        float progress01 = 0.f;
        std::uint64_t revision = 0;
        {
            std::lock_guard<std::mutex> lock(m_steamGridDbApplyProgress->mutex);
            message = m_steamGridDbApplyProgress->message;
            progress01 = m_steamGridDbApplyProgress->progress01;
            revision = m_steamGridDbApplyProgress->revision;
        }
        if (revision != m_steamGridDbApplyProgressUiRevision && m_progressDialog) {
            m_steamGridDbApplyProgressUiRevision = revision;
            m_progressDialog->updateState(message, progress01);
        }
        if (m_progressDialog && m_progressDialog->isActive()
            && focusManager().current() != m_progressDialog.get())
            focusManager().setFocus(m_progressDialog.get());
    }
    if (m_steamGridDbApplyFuture.valid()
        && m_steamGridDbApplyFuture.wait_for(std::chrono::seconds(0))
            == std::future_status::ready) {
        const auto result = m_steamGridDbApplyFuture.get();
        if (m_imagePinTargetSlot >= 0) {
            const int targetSlot = m_imagePinTargetSlot;
            const auto size = m_imagePinSize;
            m_imagePinTargetSlot = -1;
            m_imagePinApplyPending = false;
            if (m_progressDialog) m_progressDialog->hide();
            if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
                m_steamGridDbPicker->hide();
            m_steamGridDbApplyProgress.reset();
            m_steamGridDbApplyProgressUiRevision = 0;
            if (result.success) {
                createWidget(targetSlot, switchu::widgets::WidgetType::ImagePin,
                             size, "widget:" + result.message);
            }
            return;
        }
        if (result.success) {
            // Wide tiles and recent widgets can still be referenced by frames
            // already submitted to deko3d. Drain them before destroying their
            // old textures after a manual artwork replacement.
            DebugLog::log("[steamgriddb-ui] draining GPU before replacing artwork title=%016lX",
                          static_cast<unsigned long>(result.titleId));
            app().gpu().waitIdle();
#ifdef NXUI_BACKEND_DEKO3D
            app().renderer().reclaimReleasedTextureSlotsAfterIdle();
#endif
            m_gameArtwork.erase(result.titleId);
            if (m_recentWidgetAssetTitleId == result.titleId) {
                m_recentWidgetAssetTitleId = 0;
                m_recentWidgetHero.reset();
                m_recentWidgetLogo.reset();
                m_recentWidgetIcon.reset();
            }
            if (result.kind == SteamGridDbManager::ArtworkKind::Icon && m_grid) {
                // A folder showing this game as its cover (PoloNX #100) redraws too.
                m_folderCoverCache.erase(result.titleId);
                m_iconStreamer.reloadTitle(result.titleId, m_grid->currentPage(),
                                           m_grid->iconsPerPage(), app().gpu(),
                                           app().renderer(), m_grid->allIcons());
                applyFolderCoversToIcons();
            } else if (result.kind == SteamGridDbManager::ArtworkKind::Cover && m_grid) {
                m_grid->clearFlowCovers();
            } else {
                showFocusedSteamGridDbArtwork(true);
            }
            if (m_grid && m_openFolderId == 0 &&
                gameGridSize(result.titleId, AppLayoutMode::Grid) !=
                    switchu::widgets::WidgetSize{1, 1})
                applyDisplayModel(buildRootFolderModel(), result.titleId, false);
        }
        if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
            m_steamGridDbPicker->setMessage(result.message, false);
        if (m_progressDialog) m_progressDialog->hide();
        m_steamGridDbApplyProgress.reset();
        m_steamGridDbApplyProgressUiRevision = 0;
        if (m_steamGridDbPicker && m_steamGridDbPicker->isActive())
            focusManager().setFocus(m_steamGridDbPicker.get());
    }

    const auto state = m_steamGridDb.status();
    if (state.revision != m_steamGridDbUiRevision) {
        m_steamGridDbUiRevision = state.revision;

        if (state.running && m_steamGridDbWasRunning && m_progressDialog) {
            m_progressDialog->updateState(
                state.message.empty() ? state.currentTitle : state.message,
                state.progress01);
            if (focusManager().current() != m_progressDialog.get())
                focusManager().setFocus(m_progressDialog.get());
        }
        if (state.lastCompletedTitleId != 0
            && state.lastCompletedTitleId != m_steamGridDbLastCompletedTitleId) {
            m_steamGridDbLastCompletedTitleId = state.lastCompletedTitleId;
            m_gameArtwork.erase(state.lastCompletedTitleId);
            ensureGameArtwork(state.lastCompletedTitleId);
            syncGameArtworkTextures(state.lastCompletedTitleId);
            if (m_grid && m_grid->focusManager().current()) {
                auto* focused = m_grid->focusManager().current();
                if (focused->tag() == "glossy_icon"
                    && static_cast<GlossyIcon*>(focused)->titleId() == state.lastCompletedTitleId) {
                    showFocusedSteamGridDbArtwork(true);
                }
            }
        }
    }

    if (m_steamGridDbWasRunning && !state.running && state.finished) {
        m_steamGridDbWasRunning = false;
        if (m_progressDialog) m_progressDialog->hide();
        if (m_steamGridDbBackdrop)
            m_steamGridDbBackdrop->invalidateArtworkCaches();
        showFocusedSteamGridDbArtwork(true);
        // Any completed scan writes new files under steamgriddb/, and nothing in
        // the running menu re-reads them: the streamer keeps the textures it has
        // already uploaded, and ensureRecentWidgetAssets() refuses to look again
        // at a title it has tried once. Both caches and wide game artwork are
        // invalidated here so the artwork appears without restarting the menu.
        if (state.matched > 0) {
            m_recentWidgetAssetTitleId = 0;
            m_gameArtwork.clear();
            if (m_grid) {
                for (const auto& icon : m_grid->allIcons()) {
                    if (!icon || icon->entryKind() != GridEntryKind::Application) continue;
                    bool isExpanded = (icon->gridSpanColumns() > 1 || icon->gridSpanRows() > 1);
                    if (m_appLayoutMode == AppLayoutMode::Metro) {
                        auto it = m_config.metroTileSpans.find(icon->titleId());
                        if (it != m_config.metroTileSpans.end()) {
                            isExpanded = (it->second.first > 1 || it->second.second > 1);
                        }
                    }
                    if (isExpanded) {
                        ensureGameArtwork(icon->titleId());
                    }
                }
                // forceReload releases the pool texture used by the panel header;
                // detach it first so the overlay never renders a stale pointer.
                if (m_gameOptions) m_gameOptions->setGameIcon(nullptr);
                const StreamPump pump = gridStreamPump();
                m_iconStreamer.forceReload(pump.page, pump.perPage,
                                           app().gpu(), app().renderer(),
                                           m_grid->allIcons());
            }
        }
        if (state.selectedKind != SteamGridDbManager::ArtworkKind::None
            && m_gameOptions && m_gameOptions->isActive()) {
            m_gameOptions->requestToast(state.message, 3.2f);
        }
        if (m_settings && m_settings->isActive()) {
            // A reason, when there is one, beats a count: "9 missing" was all
            // the run said when the console was offline.
            m_settings->requestToast(!state.message.empty()
                ? state.message
                : std::to_string(state.matched) + " artwork sets found, "
                  + std::to_string(state.failed) + " missing.", 3.2f);
        }
    }
}

bool WiiUMenuApp::steamGridDbArtworkAllowedHere() const {
    if (!m_config.steamGridDbEnabled)
        return false;
    if (m_openFolderId != 0)
        return m_config.steamGridDbShowInFolders;
    if (isCarouselLayout())
        return m_config.steamGridDbShowInDynamicLine;
    return m_config.steamGridDbShowInGrid;
}

// Applied once per frame but only acts when the answer changes (folder
// opened/closed, view switched, a flag toggled), so it costs one compare.
void WiiUMenuApp::syncSteamGridDbVisibility() {
    if (!m_steamGridDbBackdrop)
        return;
    const int allowed = steamGridDbArtworkAllowedHere() ? 1 : 0;
    if (allowed == m_steamGridDbShownState)
        return;
    m_steamGridDbShownState = allowed;
    m_steamGridDbBackdrop->setEnabled(allowed != 0);
    if (allowed)
        showFocusedSteamGridDbArtwork(true);
}

void WiiUMenuApp::showFocusedSteamGridDbArtwork(bool forceReload) {
    if (!m_steamGridDbBackdrop || !steamGridDbArtworkAllowedHere()) return;
    std::uint64_t titleId = 0;
    if (m_grid) {
        auto* current = m_grid->focusManager().current();
        if (current && current->tag() == "glossy_icon") {
            const int focused = m_grid->focusedGlobalIndex();
            if (focused >= 0 && focused < m_model.count()
                && m_model.at(focused).isApplication())
                titleId = static_cast<GlossyIcon*>(current)->titleId();
        }
    }

    static std::uint64_t s_lastFocusedTitleId = 0xFFFFFFFFFFFFFFFFULL;
    if (!forceReload && titleId == s_lastFocusedTitleId) return;
    s_lastFocusedTitleId = titleId;

    std::vector<std::uint64_t> nearbyTitleIds;
    if (m_grid) {
        const int focused = m_grid->focusedGlobalIndex();
        for (int distance = 1; focused >= 0 && nearbyTitleIds.size() < 4
                               && distance < m_model.count(); ++distance) {
            for (int direction : {1, -1}) {
                const int index = focused + direction * distance;
                if (index < 0 || index >= m_model.count()) continue;
                const auto& entry = m_model.at(index);
                if (!entry.isApplication() || entry.titleId == 0) continue;
                nearbyTitleIds.push_back(entry.titleId);
                if (nearbyTitleIds.size() >= 4) break;
            }
        }
    }
    // Zero is meaningful: it clears artwork when focus moves to a folder,
    // an empty slot or any non-application widget.
    m_steamGridDbBackdrop->showTitle(titleId, forceReload);
    m_steamGridDbBackdrop->setPreloadTitles(std::move(nearbyTitleIds));
}

void WiiUMenuApp::createGameOptions() {
    if (m_gameOptions) return;
    m_gameOptions = std::make_shared<GameOptionsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_gameOptions);
    m_gameOptions->setFont(&m_fontNormal);
    m_gameOptions->setSmallFont(&m_fontSmall);
    m_gameOptions->setTheme(&m_theme);
    m_gameOptions->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_gameOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                     m_config.accessibilitySpeakPosition);
    m_gameOptions->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_gameOptions->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_gameOptions->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_gameOptions->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_gameOptions->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary, forceRepeat, forceContext);
        });
    m_gameOptions->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::GameOptions);
        if (focusTitle(m_gameOptionsTitleId)) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    });
}

void WiiUMenuApp::createFolderOptions() {
    if (m_folderOptions) return;
    m_folderOptions = std::make_shared<FolderOptionsScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_folderOptions);
    m_folderOptions->setFont(&m_fontNormal);
    m_folderOptions->setSmallFont(&m_fontSmall);
    m_folderOptions->setTheme(&m_theme);
    m_folderOptions->setAccessibilityVoiceEnabled(m_config.accessibilityEnabled);
    m_folderOptions->setAccessibilitySpeechPreferences(m_config.accessibilitySpeakHints,
                                                       m_config.accessibilitySpeakPosition);
    m_folderOptions->onNavigateSfx([this]() { m_audio.playSfx(Sfx::Navigate); });
    m_folderOptions->onActivateSfx([this]() { m_audio.playSfx(Sfx::Activate); });
    m_folderOptions->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_folderOptions->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_folderOptions->onAccessibilityStructuredAnnouncement(
        [this](const std::string& context, const std::string& position,
               const std::string& summary, bool forceRepeat, bool forceContext) {
            m_accessibility.announceStructuredFocus(context, position, summary,
                                                    forceRepeat, forceContext);
        });
    m_folderOptions->onClosed([this]() {
        m_navigator.routeDidClose(switchu::navigation::Route::FolderOptions);
        if (focusTitle(folderTitleId(m_folderOptionsId))) {
            if (auto* focused = m_grid->focusManager().current())
                focusManager().setFocus(focused);
        }
    });
}

void WiiUMenuApp::createControllerTest() {
    if (m_controllerTest) return;
    m_controllerTest = std::make_shared<ControllerTestScreen>();
    if (m_overlayLayer) m_overlayLayer->addChild(m_controllerTest);
    m_controllerTest->setFont(&m_fontNormal);
    m_controllerTest->setSmallFont(&m_fontSmall);
    m_controllerTest->setTheme(&m_theme);
    m_controllerTest->setInput(&app().input());
    m_controllerTest->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
    m_controllerTest->onAccessibilityAnnouncement([this](const std::string& text) {
        m_accessibility.announce(text);
    });
    m_controllerTest->onClosed([this]() {
        if (m_navigator.route() == switchu::navigation::Route::ControllerTest) {
            if (m_settings && m_settings->isActive())
                m_navigator.navigate(switchu::navigation::Route::Settings);
            else
                m_navigator.resetToHome();
        }
        if (m_settings && m_settings->isActive())
            focusManager().setFocus(m_settings.get());
    });
}


void WiiUMenuApp::retryPendingBackgroundImage() {
    if (m_pendingBackgroundImagePath.empty() || !m_background)
        return;
    if (m_backgroundImageRetryFrames > 0) {
        --m_backgroundImageRetryFrames;
        return;
    }

    const std::string expectedPath = resolveThemeAssetPath(
        m_effectivePreset, m_effectivePreset.background.imagePath);
    if (expectedPath != m_pendingBackgroundImagePath) {
        DebugLog::log("[theme-apply] background retry cancelled after theme changed");
        m_pendingBackgroundImagePath.clear();
        return;
    }

    ++m_backgroundImageRetryAttempts;
    const bool exists = pathExists(m_pendingBackgroundImagePath);
    bool loaded = false;
    if (exists) {
        app().gpu().waitIdle();
        loaded = m_background->loadImage(app().gpu(), app().renderer(),
                                         m_pendingBackgroundImagePath);
    }
    DebugLog::log("[theme-apply] background retry %d/3: path=%s exists=%d loaded=%d",
                  m_backgroundImageRetryAttempts,
                  safeLogPath(m_pendingBackgroundImagePath),
                  exists ? 1 : 0, loaded ? 1 : 0);

    if (loaded) {
        m_backgroundImageLoaded = true;
        m_loadedBackgroundImagePath = m_pendingBackgroundImagePath;
        m_pendingBackgroundImagePath.clear();
        m_backgroundImageRetryAttempts = 0;
        return;
    }

    if (m_backgroundImageRetryAttempts >= 3) {
        DebugLog::log("[theme-apply] background retry exhausted: path=%s",
                      safeLogPath(m_pendingBackgroundImagePath));
        m_pendingBackgroundImagePath.clear();
        return;
    }
    m_backgroundImageRetryFrames = 2 << (m_backgroundImageRetryAttempts - 1);
}


void WiiUMenuApp::startSoftwareDeletion(uint64_t titleId, const std::string& title,
                                        bool closeGameOptionsOnSuccess) {
    if (m_softwareDeleteFuture.valid()
        && m_softwareDeleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return;
    }

    auto& i18n = nxui::I18n::instance();
    m_softwareDeleteTitle = title;
    m_softwareDeleteTitleId = titleId;
    m_softwareDeleteClosesGameOptions = closeGameOptionsOnSuccess;
    m_dialogReturnFocus = closeGameOptionsOnSuccess
        ? static_cast<nxui::Widget*>(m_gameOptions.get())
        : static_cast<nxui::Widget*>(m_settings.get());
    // The confirmation is done with. It was left on screen and, because
    // showing it raised it to the end of the overlay tree, it was drawn over
    // the progress dialog while input went to the progress dialog instead:
    // the menu looked frozen on the confirmation for the whole delete, and
    // came back to that same dialog afterwards with nothing said.
    if (m_dialog && m_dialog->isActive())
        m_dialog->hide();
    if (m_progressDialog) {
        raiseOverlay(m_progressDialog);
        m_progressDialog->show(
            i18n.tr("game.uninstall_progress_title", "Deleting software"),
            i18n.tr("game.uninstall_progress", "Please wait. Do not turn off the console.")
                + std::string("\n") + title,
            -1.f);
        focusManager().setFocus(m_progressDialog.get());
    }

    DebugLog::log("[software-delete] queued title=%016llX",
                  static_cast<unsigned long long>(titleId));
    m_softwareDeleteResult = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    m_softwareDeleteSdRemoved = 0;
    m_softwareDeleteSdFailure.clear();
    m_softwareDeleteDone.store(0);
    m_softwareDeleteTotal.store(0);
    m_softwareDeleteFuture = m_threadPool.submit([this, titleId]() {
        // Counted before anything is removed so the bar has a denominator. The
        // walk only reads directory entries, and it is what turns a long silent
        // wait into a bar that moves.
        m_softwareDeleteTotal.store(switchu::titles::countSdFootprint(titleId));
        const Result nsRc = switchu::menu::ensureNsService("software-delete");
        if (R_SUCCEEDED(nsRc)) {
            m_softwareDeleteResult = nsDeleteApplicationCompletely(titleId);
        } else {
            m_softwareDeleteResult = nsRc;
        }
        // ns owns only what it installed. Anything copied onto the card by hand
        // has to be removed here or it stays, which is how a deleted port kept
        // its whole romfs.
        const auto report =
            switchu::titles::removeSdFootprint(titleId, &m_softwareDeleteDone);
        m_softwareDeleteSdRemoved = report.removed;
        m_softwareDeleteSdFailure = report.firstFailure;
    });
}

void WiiUMenuApp::syncSoftwareDeletion() {
    if (!m_softwareDeleteFuture.valid())
        return;
    if (m_softwareDeleteFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        // Still running. Report how far the sweep has got; before the count
        // finishes there is no denominator, so the dialog keeps its spinner.
        if (m_progressDialog && m_progressDialog->isActive()) {
            auto& progressI18n = nxui::I18n::instance();
            const std::uint64_t total = m_softwareDeleteTotal.load();
            const std::uint64_t done = m_softwareDeleteDone.load();
            std::string message =
                progressI18n.tr("game.uninstall_progress",
                                "Please wait. Do not turn off the console.")
                + std::string("\n") + m_softwareDeleteTitle;
            if (total > 0) {
                message += fmt::format("\n{}/{}", done, total);
                m_progressDialog->updateState(
                    message,
                    std::min(1.f, static_cast<float>(done)
                                      / static_cast<float>(total)));
            } else {
                m_progressDialog->updateState(message, -1.f);
            }
        }
        return;
    }

    Result rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    try {
        m_softwareDeleteFuture.get();
        rc = m_softwareDeleteResult;
    } catch (const std::exception& ex) {
        DebugLog::log("[software-delete] worker exception: %s", ex.what());
    } catch (...) {
        DebugLog::log("[software-delete] worker exception: unknown");
    }

    if (m_progressDialog)
        m_progressDialog->hide();

    auto& i18n = nxui::I18n::instance();
    // A port that was only ever copied to the card has nothing registered with
    // ns, so the ns call fails while the removal succeeds. Treating the ns
    // result as the whole answer is what let "deleted" be reported for a title
    // whose files were all still there.
    const bool removedFromCard = m_softwareDeleteSdRemoved > 0;
    const bool sweepFailed = !m_softwareDeleteSdFailure.empty();
    DebugLog::log("[software-delete] ns rc=0x%08X sd-removed=%d sd-failure=%s",
                  rc, m_softwareDeleteSdRemoved,
                  m_softwareDeleteSdFailure.empty()
                      ? "none" : m_softwareDeleteSdFailure.c_str());
    if (!sweepFailed && (R_SUCCEEDED(rc) || removedFromCard)) {
        DebugLog::log("[software-delete] complete: %s", m_softwareDeleteTitle.c_str());
        // A folder keeps title ids, not entries, so a deleted title stayed a
        // member of whatever folder it was filed in. The folder tile then drew a
        // preview cell for a title with no icon behind it, which is the flat
        // coloured square left behind after deleting a game from inside a folder.
        if (const std::uint32_t folderId =
                m_folderStore.folderForTitle(m_softwareDeleteTitleId);
            folderId != 0) {
            m_folderStore.removeTitle(folderId, m_softwareDeleteTitleId);
            m_folderStore.save();
        }
        std::replace(m_layoutSlots.begin(), m_layoutSlots.end(),
                     m_softwareDeleteTitleId, std::uint64_t{0});
        m_layoutDirty = true;
        // Drop its cached name and icon now (PoloNX #118), so a reinstall with a
        // different icon is not stuck on the old one. The daemon forgets titles
        // that leave the catalogue too, but only from its next rebuild.
        switchu::control_cache::forget(m_softwareDeleteTitleId);
        if (m_config.hasCustomTitle(m_softwareDeleteTitleId)) {
            m_config.setCustomTitle(m_softwareDeleteTitleId, "");
            m_config.save();
        }
        if (m_gameDetails && m_gameDetails->isActive() &&
            m_gameDetails->titleId() == m_softwareDeleteTitleId) {
            m_gameDetails->hide();
        }
        m_audio.playSfx(Sfx::ConfirmPositive);
        if (m_softwareDeleteClosesGameOptions && m_gameOptions) {
            m_gameOptions->hide();
        } else if (m_settings && m_settings->isActive()) {
            m_settings->requestToast(
                i18n.tr("settings.storage.uninstall_success", "Uninstalled successfully."), 2.8f);
            m_settings->rebuildCurrentTab();
            focusManager().setFocus(m_settings.get());
        } else if (m_dialog) {
            // Deleting from the grid or the dossier leaves no overlay to return
            // to, and saying nothing is what made a finished delete look like a
            // hang. The queued refresh below takes the tile away.
            raiseOverlay(m_dialog);
            m_dialog->show(
                i18n.tr("dialog.delete_software", "Delete software?"),
                i18n.tr("dialog.delete_software_done", "The software was deleted."),
                {{i18n.tr("button.ok", "OK"), [this]() {}, true}},
                0, {});
            focusManager().setFocus(m_dialog.get());
        }
        m_refreshQueued = true;
        m_deferredRefreshFrames = std::max(m_deferredRefreshFrames, 3);
    } else {
        DebugLog::log("[software-delete] failed rc=0x%08X module=%u description=%u",
                      rc, R_MODULE(rc), R_DESCRIPTION(rc));
        const std::string details = sweepFailed
            ? i18n.tr("settings.storage.uninstall_failed_files",
                      "Some files could not be removed from the SD card.")
                  + std::string("\n") + m_softwareDeleteSdFailure
            : fmt::format(
                  "{}\nResult: 0x{:08X} (module {}, description {})",
                  i18n.tr("settings.storage.uninstall_failed",
                          "Failed to uninstall the selected title."),
                  rc, R_MODULE(rc), R_DESCRIPTION(rc));
        if (m_dialog) {
            m_dialog->show(
                i18n.tr("settings.storage.uninstall_failed_title", "Uninstall Failed"),
                details,
                {{i18n.tr("button.ok", "OK"), []() {}, true}});
            focusManager().setFocus(m_dialog.get());
        }
    }

    m_softwareDeleteTitle.clear();
    m_softwareDeleteClosesGameOptions = false;
}
