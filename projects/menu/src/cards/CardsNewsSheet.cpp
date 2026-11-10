#include "cards/CardsNewsSheet.hpp"
#include "core/AudioManager.hpp"
#include "core/DebugLog.hpp"
#include "home/HomeCore.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <set>
#include <sstream>

// Adapted from upstream/homenx @ 0448cd4, projects/cards/src/CardsActivity.cpp:6220-6433.
// See integration/provenance/homenx-intake.md.

namespace cards {

namespace {

const nxui::Rect kCard{120.f, 60.f, 1040.f, 600.f};
constexpr float kSheetTop = 146.f;
constexpr float kSheetBottom = 636.f;
constexpr float kVisible = kSheetBottom - kSheetTop;
constexpr float kListX = 144.f;
constexpr float kListW = 310.f;
constexpr float kRowH = 72.f;
constexpr float kArticleX = 492.f;
constexpr float kArticleW = 628.f;
constexpr float kPicH = kArticleW * 9.f / 16.f;
constexpr float kTitleLineH = 30.f;
constexpr float kBodyLineH = 24.f;
constexpr int kPictureMaxSide = 720;

std::vector<std::string> wrapText(nxui::Font& font, const std::string& text, float maxWidth) {
    std::vector<std::string> lines;
    if (text.empty()) return lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t breakAt = text.find('\n', start);
        const std::string paragraph =
            text.substr(start, breakAt == std::string::npos ? std::string::npos : breakAt - start);
        if (paragraph.empty()) {
            lines.push_back("");
        } else {
            std::istringstream stream(paragraph);
            std::string word, current;
            while (stream >> word) {
                if (current.empty()) { current = word; continue; }
                const std::string candidate = current + " " + word;
                if (font.measure(candidate).x <= maxWidth) {
                    current = candidate;
                } else {
                    lines.push_back(current);
                    current = word;
                }
            }
            if (!current.empty()) lines.push_back(current);
        }
        if (breakAt == std::string::npos) break;
        start = breakAt + 1;
    }
    return lines;
}

} // namespace

CardsNewsSheet::~CardsNewsSheet() {
    clearTextures();
}

void CardsNewsSheet::cancelDecodes() {
    // Pending decodes only write into their own shared image; abandoning them is safe.
    for (auto& d : m_decodes)
        d.cancelled->store(true);
    m_decodes.clear();
}

void CardsNewsSheet::clearTextures() {
    cancelDecodes();
    m_pictures.clear();
}

void CardsNewsSheet::open(home::HomeCore& core, CardsChrome& chrome, float now) {
    if (m_open) return;
    m_open = true;
    m_in = 0.f;
    m_dragging = false;
    if (m_loading) return; // shown again; the running load lands in update()

    if (!m_items.empty() && now - m_loadedAt < kCacheSec) {
        showItem(std::clamp(m_selected, 0, static_cast<int>(m_items.size()) - 1), chrome);
        m_shownSinceLoad = true;
        return;
    }

    // (Re)load. The previous list stays out of view until the new one lands.
    clearTextures();
    m_items.clear();
    m_selected = 0;
    m_listScroll = 0.f;
    m_articleScroll = 0.f;
    m_shownSinceLoad = false;
    m_loading = true;

    auto state = std::make_shared<LoadState>();
    m_loadState = state;
    try {
        m_loadFuture = core.threadPool().submit([state]() {
            std::vector<NewsItem> items;
            std::string error;
            try {
                Result rc = 0;
                news::NewsLoadStats stats;
                std::vector<news::NewsArticle> raw = news::loadNewsArticles(rc, 40, &stats);
                const std::set<std::string> read = news::loadReadArticleIds();
                for (auto& a : raw) {
                    NewsItem item;
                    item.id = std::move(a.id);
                    item.title = a.subject.empty() ? a.topic : a.subject;
                    item.topic = a.topic;
                    item.body = std::move(a.body);
                    item.footer = std::move(a.footer);
                    if (a.publishedAt > 0) {
                        const std::time_t t = static_cast<std::time_t>(a.publishedAt);
                        std::tm tmv{};
                        if (localtime_r(&t, &tmv)) {
                            char when[48];
                            if (std::strftime(when, sizeof(when), "%d/%m/%Y", &tmv) > 0)
                                item.topic += (item.topic.empty() ? "" : "   ") + std::string(when);
                        }
                    }
                    item.unread = !a.read && read.find(item.id) == read.end();
                    if (!a.image.empty())
                        item.picture = std::make_shared<const std::vector<std::uint8_t>>(std::move(a.image));
                    items.push_back(std::move(item));
                }
                if (R_FAILED(rc) && items.empty()) {
                    char code[16];
                    std::snprintf(code, sizeof(code), "0x%X", static_cast<unsigned>(rc));
                    error = code;
                }
                // Ask the console to receive newer news for the next reading.
                // On this worker, so pool shutdown drains it.
                news::requestNewsUpdate(120);
            } catch (const std::exception& e) {
                DebugLog::log("[news] load failed: %s", e.what());
                items.clear();
                error = "exception";
            } catch (...) {
                items.clear();
                error = "exception";
            }
            std::lock_guard<std::mutex> lk(state->mu);
            state->items = std::move(items);
            state->errorCode = std::move(error);
            state->ready = true;
        });
    } catch (...) {
        // Pool shutting down: nothing to load.
        m_loading = false;
        m_loadState.reset();
        m_emptyMsg = nxui::I18n::instance().tr("cards.news_empty", "There is no news saved on this console yet.");
    }
}

void CardsNewsSheet::close() {
    if (!m_open) return;
    m_open = false;
    m_dragging = false;
    // Banners are cheap to decode again; free them while News is closed.
    clearTextures();
}

void CardsNewsSheet::finishLoad(CardsChrome& chrome, float now) {
    if (!m_loading || !m_loadState) return;
    std::vector<NewsItem> items;
    std::string error;
    {
        std::lock_guard<std::mutex> lk(m_loadState->mu);
        if (!m_loadState->ready) {
            // The worker always publishes; a finished future without a result
            // means the task never ran its body (pool stopped).
            if (!m_loadFuture.valid() ||
                m_loadFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            error = "exception";
        } else {
            items = std::move(m_loadState->items);
            error = std::move(m_loadState->errorCode);
        }
    }
    if (m_loadFuture.valid()) {
        try { m_loadFuture.get(); } catch (...) {}
    }
    m_loadState.reset();
    m_loading = false;
    m_items = std::move(items);
    m_loadedAt = now;

    auto& i18n = nxui::I18n::instance();
    if (error.empty()) {
        m_emptyMsg = i18n.tr("cards.news_empty", "There is no news saved on this console yet.");
    } else {
        m_emptyMsg = i18n.tr("cards.news_read_failed", "The console news could not be read ({code}).");
        const std::string tag = "{code}";
        const auto pos = m_emptyMsg.find(tag);
        if (pos != std::string::npos) m_emptyMsg.replace(pos, tag.size(), error);
        m_loadedAt = -1e9f; // an error is not cached: the next open tries again
    }
    DebugLog::log("[news] sheet: %d articles%s%s", static_cast<int>(m_items.size()), error.empty() ? "" : " error=",
                  error.c_str());
    (void)chrome;
}

void CardsNewsSheet::showItem(int index, CardsChrome& chrome) {
    if (index < 0 || index >= static_cast<int>(m_items.size())) return;
    m_selected = index;
    m_articleScroll = 0.f;
    NewsItem& item = m_items[static_cast<std::size_t>(index)];
    if (item.titleLines.empty() && item.bodyLines.empty()) {
        item.titleLines = wrapText(chrome.labelFont(), item.title, kArticleW);
        const std::string text = item.footer.empty() ? item.body : item.body + "\n\n" + item.footer;
        item.bodyLines = wrapText(chrome.smallFont(), text, kArticleW);
    }
    // Only an article actually on screen counts as read, and only once saved.
    if (m_open && item.unread && !item.id.empty() && news::markArticleRead(item.id))
        item.unread = false;
}

void CardsNewsSheet::pumpPictures(nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool) {
    // Uploads (main thread): at most one per frame.
    for (auto it = m_decodes.begin(); it != m_decodes.end();) {
        if (!it->future.valid() || it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }
        try { it->future.get(); } catch (...) {}
        const int idx = it->index;
        if (idx >= 0 && idx < static_cast<int>(m_items.size())) {
            auto tex = std::make_unique<nxui::Texture>();
            if (it->image->valid() && tex->loadFromDecoded(gpu, ren, *it->image))
                m_pictures[idx] = std::move(tex);
            else
                m_items[static_cast<std::size_t>(idx)].pictureFailed = true;
        }
        m_decodes.erase(it);
        break;
    }

    if (!m_open || m_items.empty()) return;
    const NewsItem& item = m_items[static_cast<std::size_t>(m_selected)];
    if (!item.picture || item.pictureFailed || m_pictures.count(m_selected)) return;
    for (const auto& d : m_decodes)
        if (d.index == m_selected) return;

    // Keep the few nearest pictures; the farthest goes.
    while (static_cast<int>(m_pictures.size()) >= kMaxPictures) {
        auto far = m_pictures.begin();
        for (auto it = m_pictures.begin(); it != m_pictures.end(); ++it)
            if (std::abs(it->first - m_selected) > std::abs(far->first - m_selected)) far = it;
        m_pictures.erase(far);
    }
    Decode d;
    d.index = m_selected;
    d.image = std::make_shared<nxui::DecodedImage>();
    d.cancelled = std::make_shared<std::atomic<bool>>(false);
    auto image = d.image;
    auto cancelled = d.cancelled;
    auto bytes = item.picture;
    try {
        d.future = pool.submit([image, cancelled, bytes]() {
            if (cancelled->load()) return;
            *image = nxui::Texture::decodeMemory(bytes->data(), bytes->size(), kPictureMaxSide);
        });
    } catch (...) {
        return; // pool shutting down
    }
    m_decodes.push_back(std::move(d));
}

void CardsNewsSheet::layout() {
    if (m_items.empty()) return;
    const float want = std::clamp(kRowH * static_cast<float>(m_selected) - kVisible * 0.5f + kRowH * 0.5f, 0.f,
                                  std::max(0.f, kRowH * static_cast<float>(m_items.size()) - kVisible));
    m_listScroll += (want - m_listScroll) * 0.25f;

    const NewsItem& item = m_items[static_cast<std::size_t>(m_selected)];
    float content = 0.f;
    if (m_pictures.count(m_selected)) content += kPicH + 18.f;
    content += kTitleLineH * static_cast<float>(item.titleLines.size()) + 4.f + 32.f;
    content += kBodyLineH * static_cast<float>(item.bodyLines.size());
    m_articleContent = content;
    const float most = std::max(0.f, content - kVisible + 24.f);
    m_articleScroll = std::clamp(m_articleScroll, 0.f, most);
}

void CardsNewsSheet::update(float dt, nxui::GpuDevice& gpu, nxui::Renderer& ren, nxui::ThreadPool& pool,
                            CardsChrome& chrome, float now) {
    finishLoad(chrome, now);
    if (m_open && !m_loading && !m_shownSinceLoad) {
        m_shownSinceLoad = true;
        if (!m_items.empty())
            showItem(std::clamp(m_selected, 0, static_cast<int>(m_items.size()) - 1), chrome);
    }
    if (m_open)
        m_in = std::min(1.f, m_in + dt * 6.f);
    else
        m_in = std::max(0.f, m_in - dt * 6.f);
    pumpPictures(gpu, ren, pool);
    layout();
}

bool CardsNewsSheet::handleInput(nxui::Input& in, float dt, CardsChrome& chrome, AudioManager& audio) {
    if (!m_open) return false;
    using B = nxui::Button;
    const int count = static_cast<int>(m_items.size());

    if (in.isDown(B::B) || in.isDown(B::Minus)) {
        close();
        audio.playSfx(Sfx::ModalHide);
        return true;
    }
    if ((in.isDown(B::DUp) || in.isDown(B::LStickU)) && m_selected > 0) {
        audio.playSfx(Sfx::Navigate);
        showItem(m_selected - 1, chrome);
        return true;
    }
    if ((in.isDown(B::DDown) || in.isDown(B::LStickD)) && m_selected + 1 < count) {
        audio.playSfx(Sfx::Navigate);
        showItem(m_selected + 1, chrome);
        return true;
    }

    float speed = 0.f;
    if (in.isHeld(B::ZR) || in.isHeld(B::R) || in.isHeld(B::RStickD)) speed += 700.f;
    if (in.isHeld(B::ZL) || in.isHeld(B::L) || in.isHeld(B::RStickU)) speed -= 700.f;
    if (speed != 0.f) m_articleScroll = std::max(0.f, m_articleScroll + speed * dt);

    const float x = in.touchX(), y = in.touchY();
    if (in.touchDown()) {
        m_dragging = false;
        m_touchY = y;
        m_touchScroll = m_articleScroll;
    }
    if (in.isTouching() && x > kArticleX - 20.f && kCard.contains(x, y)) {
        if (std::abs(y - m_touchY) > 10.f) m_dragging = true;
        if (m_dragging) m_articleScroll = std::max(0.f, m_touchScroll - (y - m_touchY));
    }
    if (in.touchUp()) {
        if (!kCard.contains(x, y)) {
            close();
            audio.playSfx(Sfx::ModalHide);
            return true;
        }
        if (!m_dragging && x < kArticleX - 20.f && y >= kSheetTop) {
            const int row = static_cast<int>((y - kSheetTop + m_listScroll) / kRowH);
            if (row >= 0 && row < count && row != m_selected) {
                audio.playSfx(Sfx::Navigate);
                showItem(row, chrome);
            }
        }
        m_dragging = false;
    }
    return true;
}

void CardsNewsSheet::render(nxui::Renderer& ren, CardsChrome& chrome, float time) const {
    if (m_in <= 0.f) return;
    const float a = m_in;
    const nxui::Color white = CardsChrome::kWhite.withAlpha(a);
    const nxui::Color muted{0.8f, 0.82f, 0.88f, 0.85f * a};
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.52f * a});
    ren.drawRoundedRect(kCard, {0.08f, 0.09f, 0.13f, 0.92f * a}, 34.f);
    ren.drawRoundedRectOutline(kCard, {1.f, 1.f, 1.f, 0.16f * a}, 34.f, 1.5f);

    auto& i18n = nxui::I18n::instance();
    chrome.icon(ren, "news", kCard.x + 44.f, kCard.y + 46.f, 30.f, white);
    chrome.textMid(ren, chrome.titleFont(), i18n.tr("cards.news_title", "News"), kCard.x + 80.f, kCard.y + 46.f,
                   white);
    const std::string hint = i18n.tr("cards.news_hint", "Up/Down Choose    ZL/ZR Scroll    B Back");
    chrome.textMid(ren, chrome.smallFont(), hint, kCard.right() - chrome.smallFont().measure(hint).x - 44.f,
                   kCard.y + 46.f, {1.f, 1.f, 1.f, 0.65f * a});

    auto centred = [&](const std::string& s, const nxui::Color& c) {
        auto& f = chrome.labelFont();
        const std::string t = chrome.fit(f, s, kCard.width - 80.f);
        chrome.textMid(ren, f, t, kCard.x + (kCard.width - f.measure(t).x) * 0.5f, kCard.y + kCard.height * 0.5f,
                       c);
    };
    if (m_loading) {
        centred(i18n.tr("cards.news_loading", "Loading news..."), white);
        return;
    }
    if (m_items.empty()) {
        centred(m_emptyMsg, muted);
        return;
    }

    // Lines not yet in the font cache are rationed (see header); in drawing
    // order, so the list and the article's top fill in first.
    m_newLines = 0;
    auto text = [&](const std::string& s, const nxui::Vec2& at, nxui::Font& f, const nxui::Color& c) {
        if (s.empty()) return;
        if (!f.cached(s)) {
            if (m_newLines >= kNewLinesPerFrame) return;
            ++m_newLines;
        }
        ren.drawText(s, at, &f, c);
    };

    // List.
    ren.pushClipRect({kListX - 6.f, kSheetTop - 4.f, kListW + 12.f, kVisible + 8.f});
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        const nxui::Rect row{kListX, kSheetTop + kRowH * static_cast<float>(i) - m_listScroll, kListW, kRowH - 8.f};
        if (row.bottom() < kSheetTop || row.y > kSheetBottom) continue;
        const NewsItem& n = m_items[i];
        const bool on = static_cast<int>(i) == m_selected;
        ren.drawRoundedRect(row, {1.f, 1.f, 1.f, (on ? 0.16f : 0.05f) * a}, 16.f);
        if (on)
            ren.drawRoundedRectOutline(row, CardsChrome::accent((0.8f + 0.2f * std::sin(time * 5.f)) * a), 16.f, 2.f);
        if (n.unread)
            ren.drawCircle({row.x + 14.f, row.y + 20.f}, 4.5f, CardsChrome::kGreen.withAlpha(a), 16);
        const float tx = row.x + 28.f;
        const float tw = row.right() - 14.f - tx;
        text(chrome.fit(chrome.labelFont(), n.title, tw), {tx, row.y + 8.f}, chrome.labelFont(), white);
        text(chrome.fit(chrome.smallFont(), n.topic, tw), {tx, row.y + 36.f}, chrome.smallFont(), muted);
    }
    ren.popClipRect();
    ren.drawRect({kListX + kListW + 16.f, kSheetTop, 1.5f, kVisible}, {1.f, 1.f, 1.f, 0.12f * a});

    // Article.
    const NewsItem& item = m_items[static_cast<std::size_t>(m_selected)];
    ren.pushClipRect({kArticleX - 6.f, kSheetTop - 4.f, kArticleW + 12.f, kVisible + 8.f});
    float y = kSheetTop - m_articleScroll;
    auto pic = m_pictures.find(m_selected);
    if (pic != m_pictures.end() && pic->second && pic->second->valid()) {
        CardsChrome::drawCover(ren, pic->second.get(), {kArticleX, y, kArticleW, kPicH}, 16.f, white);
        y += kPicH + 18.f;
    }
    for (const std::string& line : item.titleLines) {
        if (y > kSheetTop - kTitleLineH && y < kSheetBottom)
            text(line, {kArticleX, y}, chrome.labelFont(), white);
        y += kTitleLineH;
    }
    y += 4.f;
    if (y > kSheetTop - 24.f && y < kSheetBottom)
        text(item.topic, {kArticleX, y}, chrome.smallFont(), muted);
    y += 32.f;
    for (const std::string& line : item.bodyLines) {
        if (y > kSheetTop - kBodyLineH && y < kSheetBottom)
            text(line, {kArticleX, y}, chrome.smallFont(), CardsChrome::kWhite.withAlpha(0.92f * a));
        y += kBodyLineH;
    }
    ren.popClipRect();

    const float most = std::max(0.f, m_articleContent - kVisible + 24.f);
    if (most > 0.f && m_articleContent > 0.f) {
        const float trackX = kCard.right() - 14.f;
        ren.drawRoundedRect({trackX, kSheetTop, 4.f, kVisible}, {1.f, 1.f, 1.f, 0.08f * a}, 2.f);
        const float barH = std::max(40.f, kVisible * kVisible / m_articleContent);
        const float barY = kSheetTop + (kVisible - barH) * std::clamp(m_articleScroll / most, 0.f, 1.f);
        ren.drawRoundedRect({trackX, barY, 4.f, barH}, CardsChrome::kWhite.withAlpha(0.5f * a), 2.f);
    }
}

} // namespace cards
