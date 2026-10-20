#include "GameGalleryScreen.hpp"

#include "core/DebugLog.hpp"
#include "themeshop/ThemeHttp.hpp"

#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/ThreadPool.hpp>

#include <algorithm>
#include <chrono>

namespace {

constexpr const char* kGridDimensions[] = {
    "600x900", "342x482", "660x930", "512x512", "1024x1024", "460x215", "920x430",
};
constexpr int kGridDimensionCount = sizeof(kGridDimensions) / sizeof(kGridDimensions[0]);
constexpr const char* kHeroDimensions[] = {"1920x620", "3840x1240", "1600x650"};
constexpr int kHeroDimensionCount = sizeof(kHeroDimensions) / sizeof(kHeroDimensions[0]);

const char* phaseLabel(GameGalleryClient::Phase phase) {
    switch (phase) {
        case GameGalleryClient::Phase::Loading: return "dialog.gallery_loading";
        case GameGalleryClient::Phase::Failed: return "dialog.gallery_error";
        case GameGalleryClient::Phase::Ready: return "dialog.gallery_empty";
        case GameGalleryClient::Phase::Idle: return "dialog.gallery_loading";
    }
    return "dialog.gallery_loading";
}

} // namespace

GameGalleryScreen::GameGalleryScreen()
    : TabbedOverlayScreen(ScreenMode::ThemeShop) {
    setPinBottomTabEnabled(false);
}

void GameGalleryScreen::openForGame(std::uint64_t titleId, std::string title) {
    m_titleId = titleId;
    m_title = std::move(title);
    m_selected = 0;
    m_page = 0;
    m_fullscreen = false;
    m_seenRevision = 0;
    m_seenTab = -1;
    clearPreviews();
    show();
    // show() rebuilds the base action map. Add Gallery's Plus action only
    // afterwards; registering it in the constructor made the visible hint a
    // promise with no live callback.
    addAction(static_cast<uint64_t>(nxui::Button::Plus), [this]() {
        requestApplySelected();
    });
    addAction(static_cast<uint64_t>(nxui::Button::Minus), [this]() {
        requestResetArtwork();
    });
    m_focusArea = FocusArea::Content;
    refreshCategory();
}

void GameGalleryScreen::buildTabs() {
    auto& i18n = nxui::I18n::instance();
    m_tabs.clear();
    m_tabs.push_back({i18n.tr("dialog.gallery_grids", "Covers / Grids"), {}});
    m_tabs.push_back({i18n.tr("dialog.gallery_heroes", "Backgrounds / Heroes"), {}});
}

void GameGalleryScreen::refreshCategory() {
    if (!m_pool || m_title.empty())
        return;
    m_seenTab = m_tabIndex;
    m_selected = 0;
    m_page = 0;
    m_fullscreen = false;
    clearPreviews();
    m_client.load(*m_pool, m_title, m_tabIndex == 0
        ? GameGalleryClient::Category::Grids : GameGalleryClient::Category::Heroes,
        currentDimensions());
}

std::string GameGalleryScreen::currentDimensions() const {
    if (m_tabIndex == 0)
        return kGridDimensions[std::clamp(m_gridDimensionIndex, 0, kGridDimensionCount - 1)];
    return kHeroDimensions[std::clamp(m_heroDimensionIndex, 0, kHeroDimensionCount - 1)];
}

void GameGalleryScreen::cycleDimensions() {
    if (m_tabIndex == 0)
        m_gridDimensionIndex = (m_gridDimensionIndex + 1) % kGridDimensionCount;
    else
        m_heroDimensionIndex = (m_heroDimensionIndex + 1) % kHeroDimensionCount;
    DebugLog::log("[gallery] dimensions changed category=%s dimensions=%s",
                  m_tabIndex == 0 ? "cover" : "background", currentDimensions().c_str());
    refreshCategory();
    if (m_navSfxCb) m_navSfxCb();
}

void GameGalleryScreen::clearPreviews() {
    if (m_gpu && !m_previews.empty())
        m_gpu->waitIdle();
    m_previews.clear();
}

void GameGalleryScreen::requestApplySelected() {
    if (!m_active || m_animating || !m_fullscreen || !m_applyArtworkCb
        || m_selected < 0 || m_selected >= (int)m_snapshot.assets.size()) {
        DebugLog::log("[gallery] apply ignored active=%d animating=%d fullscreen=%d callback=%d selected=%d assets=%zu",
                      m_active ? 1 : 0, m_animating ? 1 : 0, m_fullscreen ? 1 : 0,
                      m_applyArtworkCb ? 1 : 0, m_selected, m_snapshot.assets.size());
        return;
    }

    const auto& asset = m_snapshot.assets[(size_t)m_selected];
    auto it = m_previews.find(asset.url);
    if (it == m_previews.end()) {
        DebugLog::log("[gallery] apply waiting: original preview not queued");
        requestToast(nxui::I18n::instance().tr("dialog.gallery_wait_download",
                                                "Wait for the image to finish loading."));
        return;
    }

    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    {
        std::lock_guard<std::mutex> lock(it->second->mutex);
        if ((it->second->phase != PreviewPhase::Downloaded && it->second->phase != PreviewPhase::Ready)
            || it->second->bytes.empty()) {
            DebugLog::log("[gallery] apply waiting: original preview phase=%d bytes=%zu",
                          static_cast<int>(it->second->phase), it->second->bytes.size());
            requestToast(nxui::I18n::instance().tr("dialog.gallery_wait_download",
                                                    "Wait for the image to finish loading."));
            return;
        }
        bytes = std::make_shared<const std::vector<std::uint8_t>>(it->second->bytes);
    }

    DebugLog::log("[gallery] apply requested category=%s bytes=%zu",
                  m_snapshot.category == GameGalleryClient::Category::Grids ? "cover" : "background",
                  bytes->size());
    m_applyArtworkCb(m_titleId, m_title, m_snapshot.category, asset, std::move(bytes));
}

void GameGalleryScreen::requestResetArtwork() {
    if (!m_active || m_animating || !m_resetArtworkCb || m_titleId == 0)
        return;
    DebugLog::log("[gallery] restore requested category=%s",
                  m_tabIndex == 0 ? "cover" : "background");
    m_resetArtworkCb(m_titleId, m_title, m_tabIndex == 0
        ? GameGalleryClient::Category::Grids : GameGalleryClient::Category::Heroes);
}

int GameGalleryScreen::visibleStart() const {
    return m_page * kPerPage;
}

int GameGalleryScreen::visibleEnd() const {
    return std::min((int)m_snapshot.assets.size(), visibleStart() + kPerPage);
}

void GameGalleryScreen::clampSelection() {
    if (m_snapshot.assets.empty()) {
        m_selected = 0;
        m_page = 0;
        return;
    }
    m_selected = std::clamp(m_selected, 0, (int)m_snapshot.assets.size() - 1);
    m_page = std::clamp(m_page, 0, ((int)m_snapshot.assets.size() - 1) / kPerPage);
    if (m_selected < visibleStart() || m_selected >= visibleEnd())
        m_page = m_selected / kPerPage;
}

void GameGalleryScreen::queuePreview(const std::string& url, int maxSide) {
    if (!m_pool || url.empty()) return;
    auto& slot = m_previews[url];
    if (!slot) slot = std::make_shared<PreviewState>();

    int active = 0;
    for (const auto& entry : m_previews) {
        std::lock_guard<std::mutex> lock(entry.second->mutex);
        if (entry.second->phase == PreviewPhase::Loading) ++active;
    }

    {
        std::lock_guard<std::mutex> lock(slot->mutex);
        if (slot->phase == PreviewPhase::Ready || slot->phase == PreviewPhase::Downloaded ||
            slot->phase == PreviewPhase::Loading || slot->phase == PreviewPhase::Failed)
            return;
        if (active >= 3) return;
        slot->phase = PreviewPhase::Loading;
        slot->maxSide = maxSide;
    }

    slot->future = m_pool->submit([slot, url]() {
        try {
            auto bytes = themeshop::http::getBytes(url);
            std::lock_guard<std::mutex> lock(slot->mutex);
            slot->bytes = std::move(bytes);
            slot->phase = slot->bytes.empty() ? PreviewPhase::Failed : PreviewPhase::Downloaded;
        } catch (...) {
            std::lock_guard<std::mutex> lock(slot->mutex);
            slot->bytes.clear();
            slot->phase = PreviewPhase::Failed;
        }
    });
}

void GameGalleryScreen::syncPreviewUploads() {
    if (!m_gpu || !m_renderer) return;
    int uploads = 0;
    for (auto& entry : m_previews) {
        auto& state = entry.second;
        if (state->future.valid() && state->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try { state->future.get(); } catch (...) {}
        }
        std::vector<std::uint8_t> bytes;
        int maxSide = 512;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (uploads >= 1 || state->phase != PreviewPhase::Downloaded) continue;
            bytes = state->bytes;
            maxSide = state->maxSide;
        }
        if (bytes.empty()) continue;
        ++uploads;
        nxui::Texture texture;
        const bool ok = texture.loadFromMemory(*m_gpu, *m_renderer, bytes.data(), bytes.size(), maxSide);
        std::lock_guard<std::mutex> lock(state->mutex);
        if (ok) {
            state->texture = std::move(texture);
            state->phase = PreviewPhase::Ready;
        } else {
            state->bytes.clear();
            state->phase = PreviewPhase::Failed;
        }
    }
}

const nxui::Texture* GameGalleryScreen::previewTexture(const std::string& url) const {
    auto it = m_previews.find(url);
    if (it == m_previews.end()) return nullptr;
    std::lock_guard<std::mutex> lock(it->second->mutex);
    return it->second->phase == PreviewPhase::Ready && it->second->texture.valid()
        ? &it->second->texture : nullptr;
}

GameGalleryScreen::PreviewPhase GameGalleryScreen::previewPhase(const std::string& url) const {
    auto it = m_previews.find(url);
    if (it == m_previews.end()) return PreviewPhase::Idle;
    std::lock_guard<std::mutex> lock(it->second->mutex);
    return it->second->phase;
}

nxui::Rect GameGalleryScreen::cardRect(const nxui::Rect& content, int localIndex) const {
    const float gap = 16.f;
    const float header = 88.f;
    const float w = (content.width - gap * (kColumns + 1)) / kColumns;
    const float h = (content.height - header - gap * (kRows + 1)) / kRows;
    const int col = localIndex % kColumns;
    const int row = localIndex / kColumns;
    return {content.x + gap + col * (w + gap), content.y + header + gap + row * (h + gap), w, h};
}

nxui::Rect GameGalleryScreen::fitTexture(const nxui::Rect& rect, const nxui::Texture& texture) {
    const float scale = std::min(rect.width / (float)texture.width(), rect.height / (float)texture.height());
    const float width = texture.width() * scale;
    const float height = texture.height() * scale;
    return {rect.x + (rect.width - width) * 0.5f, rect.y + (rect.height - height) * 0.5f, width, height};
}

void GameGalleryScreen::updateCustomContent(float) {
    if (!m_showing) {
        m_fullscreen = false;
        return;
    }
    if (m_seenTab != m_tabIndex)
        refreshCategory();

    const auto next = m_client.snapshot();
    if (next.revision != m_seenRevision) {
        m_seenRevision = next.revision;
        m_snapshot = next;
        m_selected = 0;
        m_page = 0;
        clearPreviews();
    } else {
        m_snapshot = next;
    }
    clampSelection();

    if (m_snapshot.phase == GameGalleryClient::Phase::Ready) {
        for (int i = visibleStart(); i < visibleEnd(); ++i)
            queuePreview(m_snapshot.assets[(size_t)i].thumb, 512);
        if (m_fullscreen && !m_snapshot.assets.empty())
            queuePreview(m_snapshot.assets[(size_t)m_selected].url, 1024);
    }
    syncPreviewUploads();
}

bool GameGalleryScreen::handleCustomPressA() {
    if (m_focusArea == FocusArea::Tabs) {
        m_focusArea = FocusArea::Content;
        return true;
    }
    if (m_snapshot.phase == GameGalleryClient::Phase::Ready && !m_snapshot.assets.empty()) {
        m_fullscreen = !m_fullscreen;
        if (m_fullscreen)
            queuePreview(m_snapshot.assets[(size_t)m_selected].url, 1024);
        if (m_activateSfxCb) m_activateSfxCb();
        return true;
    }
    return false;
}

bool GameGalleryScreen::handleCustomPressB() {
    if (m_fullscreen) {
        m_fullscreen = false;
        return true;
    }
    // Let the shared overlay behavior take over: B moves from artwork back to
    // the tab selector first, then closes only when the selector is focused.
    return false;
}

bool GameGalleryScreen::handleCustomPressX() {
    if (m_fullscreen)
        return false;
    cycleDimensions();
    return true;
}

bool GameGalleryScreen::handleCustomNavUp() {
    if (m_fullscreen || m_focusArea != FocusArea::Content) return m_fullscreen;
    if (m_selected >= kColumns) m_selected -= kColumns;
    else {
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
    }
    return true;
}

bool GameGalleryScreen::handleCustomNavDown() {
    if (m_fullscreen || m_focusArea != FocusArea::Content) return m_fullscreen;
    if (m_selected + kColumns < (int)m_snapshot.assets.size()) m_selected += kColumns;
    clampSelection();
    return true;
}

bool GameGalleryScreen::handleCustomNavLeft() {
    if (m_fullscreen || m_focusArea != FocusArea::Content) return m_fullscreen;
    if (m_selected % kColumns > 0) {
        --m_selected;
    } else {
        // The tab selector sits visually to the left of the artwork grid.
        // Left from its first column is therefore a direct, predictable route
        // to Grids/Heroes, without forcing the player to navigate to row one.
        m_focusArea = FocusArea::Tabs;
        if (m_navSfxCb) m_navSfxCb();
    }
    return true;
}

bool GameGalleryScreen::handleCustomNavRight() {
    if (m_fullscreen) return true;
    if (m_focusArea == FocusArea::Tabs) {
        // Custom gallery tabs contain no stock setting rows, so the shared
        // overlay cannot discover a focusable item here. Right explicitly
        // enters the visible artwork grid, matching its physical direction.
        m_focusArea = FocusArea::Content;
        if (m_navSfxCb) m_navSfxCb();
        return true;
    }
    if (m_selected + 1 < (int)m_snapshot.assets.size() && m_selected % kColumns < kColumns - 1) ++m_selected;
    return true;
}

bool GameGalleryScreen::handleCustomTouch(nxui::Input& input, const nxui::Rect&,
                                          const nxui::Rect&, const nxui::Rect& content) {
    if (m_fullscreen) {
        // A simple tap is the touch equivalent of B from full-screen preview.
        if (input.touchUp())
            m_fullscreen = false;
        return input.isTouching() || input.touchDown() || input.touchUp();
    }

    auto assetAt = [&](float x, float y) {
        if (!content.contains(x, y)) return -1;
        for (int i = visibleStart(); i < visibleEnd(); ++i) {
            if (cardRect(content, i - visibleStart()).contains(x, y))
                return i;
        }
        return -1;
    };

    if (input.touchDown()) {
        m_touchAsset = assetAt(input.touchX(), input.touchY());
        // Let the base class own the tab bar, but reserve custom content.
        return m_touchAsset >= 0 || content.contains(input.touchX(), input.touchY());
    }
    if (input.touchUp()) {
        const int releasedAsset = assetAt(input.touchX(), input.touchY());
        const bool activate = m_touchAsset >= 0 && releasedAsset == m_touchAsset;
        m_touchAsset = -1;
        if (!activate) return content.contains(input.touchX(), input.touchY());

        if (releasedAsset == m_selected) {
            m_fullscreen = true;
            queuePreview(m_snapshot.assets[(size_t)m_selected].url, 1024);
            if (m_activateSfxCb) m_activateSfxCb();
        } else {
            m_selected = releasedAsset;
            m_focusArea = FocusArea::Content;
            if (m_navSfxCb) m_navSfxCb();
        }
        return true;
    }
    return m_touchAsset >= 0;
}

std::string GameGalleryScreen::currentAccessibilitySummary() const {
    auto& i18n = nxui::I18n::instance();
    if (m_fullscreen) return i18n.tr("dialog.gallery_fullscreen", "Fullscreen preview. Press B to return.");
    if (m_snapshot.phase == GameGalleryClient::Phase::Loading)
        return i18n.tr("dialog.gallery_loading", "Loading artwork...");
    if (m_snapshot.phase == GameGalleryClient::Phase::Failed)
        return i18n.tr("dialog.gallery_error", "Artwork could not be loaded.");
    if (m_snapshot.assets.empty())
        return i18n.tr("dialog.gallery_empty", "No compatible artwork was found.");
    return m_snapshot.matchedName + ". " + std::to_string(m_selected + 1) + " "
        + i18n.tr("accessibility.context.of", "of") + " " + std::to_string(m_snapshot.assets.size());
}

void GameGalleryScreen::drawCustomContent(nxui::Renderer& ren, const nxui::Rect&, const nxui::Rect& content, float opacity) {
    if (!m_theme || !m_font || !m_smallFont) return;
    auto& i18n = nxui::I18n::instance();
    const nxui::Color primary = m_theme->textPrimary.withAlpha(opacity);
    const nxui::Color secondary = m_theme->textSecondary.withAlpha(0.84f * opacity);

    const std::string heading = i18n.tr("dialog.gallery_title", "Gallery") + std::string(" — ") + m_title;
    ren.drawText(heading, {content.x + 22.f, content.y + 20.f}, m_font, primary, 1.0f);
    std::string sub = m_snapshot.matchedName.empty() ? m_title : m_snapshot.matchedName;
    if (m_snapshot.gameId != 0) sub += "  •  SteamGridDB " + std::to_string(m_snapshot.gameId);
    sub += "  |  " + i18n.tr("dialog.gallery_dimension", "Dimension") + ": " + currentDimensions();
    ren.drawText(sub, {content.x + 22.f, content.y + 53.f}, m_smallFont, secondary, 0.82f);

    if (m_fullscreen && !m_snapshot.assets.empty()) {
        // TabbedOverlayScreen renders its shared cursor after custom content.
        // Hide its last card position while one image owns the whole content
        // area, otherwise that stale selection outline overlays the artwork.
        m_focusCursor.setOpacity(0.f);
        const auto& asset = m_snapshot.assets[(size_t)m_selected];
        nxui::Rect preview = content.shrunk(34.f);
        if (const auto* texture = previewTexture(asset.url)) {
            ren.drawTextureRounded(texture, fitTexture(preview, *texture), 18.f, nxui::Color::white().withAlpha(opacity));
        } else {
            ren.drawRoundedRect(preview, m_theme->panelBase.withAlpha(0.18f * opacity), 18.f);
            ren.drawText(i18n.tr("dialog.gallery_loading", "Loading artwork..."),
                         {preview.x + 28.f, preview.y + preview.height * 0.5f}, m_font, primary, 0.9f);
        }
        return;
    }

    if (m_snapshot.phase != GameGalleryClient::Phase::Ready || m_snapshot.assets.empty()) {
        const std::string key = phaseLabel(m_snapshot.phase);
        const std::string text = m_snapshot.phase == GameGalleryClient::Phase::Failed
            ? i18n.tr(key, "Artwork could not be loaded.")
            : (m_snapshot.phase == GameGalleryClient::Phase::Ready
                ? i18n.tr(key, "No compatible artwork was found.")
                : i18n.tr(key, "Loading artwork..."));
        ren.drawText(text, {content.x + 24.f, content.y + 128.f}, m_font, primary, 0.92f);
        return;
    }

    for (int i = visibleStart(); i < visibleEnd(); ++i) {
        const int local = i - visibleStart();
        const nxui::Rect card = cardRect(content, local);
        const auto& asset = m_snapshot.assets[(size_t)i];
        const bool selected = i == m_selected && m_focusArea == FocusArea::Content;
        ren.drawRoundedRect(card, m_theme->panelBase.withAlpha((selected ? 0.18f : 0.08f) * opacity), 16.f);
        ren.drawRoundedRectOutline(card, (selected ? m_theme->cursorNormal : m_theme->panelBorder)
                                  .withAlpha((selected ? 0.75f : 0.18f) * opacity), 16.f, selected ? 2.f : 1.f);
        nxui::Rect art = card.shrunk(8.f);
        art.height -= 30.f;
        if (const auto* texture = previewTexture(asset.thumb)) {
            ren.drawTextureRounded(texture, fitTexture(art, *texture), 11.f, nxui::Color::white().withAlpha(opacity));
        } else {
            ren.drawRoundedRect(art, m_theme->background.withAlpha(0.32f * opacity), 11.f);
            if (previewPhase(asset.thumb) == PreviewPhase::Loading)
                ren.drawText("…", {art.x + art.width * 0.48f, art.y + art.height * 0.45f}, m_font, secondary, 1.1f);
        }
        const std::string by = asset.author.empty() ? asset.style : asset.author;
        if (!by.empty())
            ren.drawText(by, {card.x + 10.f, card.bottom() - 22.f}, m_smallFont, secondary, 0.68f);
        if (selected)
            m_focusCursor.moveTo(card.expanded(2.f), 14.f, 0.08f);
    }
}
