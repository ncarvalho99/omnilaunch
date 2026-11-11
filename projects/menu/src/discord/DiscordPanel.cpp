#include "discord/DiscordPanel.hpp"
#include "discord/DiscordApi.hpp"
#include "home/HomeCore.hpp"
#include "net/NetService.hpp"

#include <nxui/core/Animation.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <sstream>

namespace discord {

namespace {

// Panel text through the locale files; the English literal is the fallback.
inline std::string T(const char* key, const char* fallback) {
    return nxui::I18n::instance().tr(key, fallback);
}

inline void drawLabel(nxui::Renderer& ren, nxui::Font* font, float x, float y,
                      const std::string& text, const nxui::Color& color) {
    if (font && !text.empty()) {
        ren.drawText(text, {x, y}, font, color);
    }
}

constexpr float kPanelX = 16.f;
constexpr float kPanelY = 16.f;
constexpr float kPanelW = 460.f;
constexpr float kPanelH = 688.f;
constexpr float kRailW  = 72.f;
constexpr float kTopH   = 56.f;
constexpr float kMeH    = 64.f;
constexpr float kInputH = 56.f;
constexpr float kAnimDur = 0.22f;

inline nxui::Color rgb(int r, int g, int b, float a = 1.f) {
    return nxui::Color(r / 255.f, g / 255.f, b / 255.f, a);
}
inline nxui::Color bgPrimary(float a = 1.f)   { return rgb(49, 51, 56, a); }
inline nxui::Color bgSecondary(float a = 1.f) { return rgb(43, 45, 49, a); }
inline nxui::Color bgTertiary(float a = 1.f)  { return rgb(30, 31, 34, a); }
inline nxui::Color bgUser(float a = 1.f)      { return rgb(35, 36, 40, a); }
inline nxui::Color bgSelected(float a = 1.f)  { return rgb(64, 66, 73, a); }
inline nxui::Color textNormal(float a = 1.f)  { return rgb(219, 222, 225, a); }
inline nxui::Color textMuted(float a = 1.f)   { return rgb(148, 155, 164, a); }
inline nxui::Color blurple(float a = 1.f)     { return rgb(88, 101, 242, a); }
inline nxui::Color green(float a = 1.f)       { return rgb(35, 165, 90, a); }
inline nxui::Color red(float a = 1.f)         { return rgb(242, 63, 67, a); }

std::string cleanText(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (static_cast<unsigned char>(c) >= 32 || c == '\n') {
            out += c;
        }
    }
    return out;
}

static std::vector<std::string> wrapLines(nxui::Font* font, const std::string& text, float maxW) {
    std::vector<std::string> lines;
    if (!font || text.empty()) return lines;
    std::istringstream stream(text);
    std::string paragraph;
    while (std::getline(stream, paragraph)) {
        if (paragraph.empty()) {
            lines.push_back("");
            continue;
        }
        std::string current;
        std::istringstream words(paragraph);
        std::string word;
        while (words >> word) {
            std::string test = current.empty() ? word : current + " " + word;
            if (font->measure(test).x <= maxW) {
                current = test;
            } else {
                if (!current.empty()) lines.push_back(current);
                current = word;
            }
        }
        if (!current.empty()) lines.push_back(current);
    }
    return lines;
}

} // namespace

const nxui::Texture* DiscordPanel::requestImage(const std::string& url, int maxSide) const {
    if (url.empty() || !m_core) return nullptr;
    auto it = m_images.find(url);
    if (it != m_images.end()) {
        if (it->second.ready && it->second.texture.valid())
            return &it->second.texture;
        return nullptr;
    }
    if (m_images.size() > 160) return nullptr;
    auto& slot = m_images[url];
    slot.requested = true;
    slot.decoded = std::make_shared<nxui::DecodedImage>();
    auto dec = slot.decoded;
    try {
        slot.future = m_core->threadPool().submit([this, url, maxSide, dec]() {
            try {
                net::Request req;
                req.url = url;
                req.timeoutMs = 12000;
                req.attempts = 1;
                auto res = m_core->net().request(req);
                if (res.ok() && !res.body.empty()) {
                    *dec = nxui::Texture::decodeMemory(reinterpret_cast<const uint8_t*>(res.body.data()),
                                                       res.body.size(), maxSide);
                }
            } catch (...) {}
        });
    } catch (...) {
        slot.failed = true;
    }
    return nullptr;
}

void DiscordPanel::pumpImages(nxui::GpuDevice& gpu, nxui::Renderer& ren) const {
    for (auto& [url, slot] : m_images) {
        if (slot.requested && !slot.ready && !slot.failed && slot.future.valid()) {
            if (slot.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                try { slot.future.get(); } catch (...) {}
                if (slot.decoded && slot.decoded->valid()) {
                    if (slot.texture.loadFromPixels(gpu, ren, slot.decoded->rgba.data(),
                                                    slot.decoded->width, slot.decoded->height)) {
                        slot.ready = true;
                    } else {
                        slot.failed = true;
                    }
                } else {
                    slot.failed = true;
                }
            }
        }
    }
}

DiscordPanel::DiscordPanel() {
    setRect({0.f, 0.f, 1280.f, 720.f});
    setVisible(false);
}

DiscordPanel::~DiscordPanel() = default;

void DiscordPanel::show() {
    m_active = true;
    m_view = View::Home;
    m_sel = 0;
    m_railSel = 0;
    m_column = 1;
    m_scroll = 0.f;
    m_railScroll = 0.f;
    m_chatScroll = 0.f;
    setVisible(true);
    setFocusable(true);
    if (m_service && m_core) {
        m_service->activate(*m_core);
    }
    m_lastState = m_service ? m_service->state() : DiscordService::State::SignedOut;
    m_lastVersion = m_service ? m_service->version() : 0;
    m_lastView = m_view;
    m_lastRailSel = m_railSel;
    rebuildRows();
    bindInput();
}

void DiscordPanel::hide() {
    if (!m_active) return;
    m_active = false;
    clearActions();
    setFocusable(false);
    if (m_cb.onClosed) m_cb.onClosed();
}

void DiscordPanel::openChat(Snowflake channelId, Snowflake guildId, const std::string& name) {
    m_view = View::Chat;
    m_chatId = channelId;
    m_chatGuildId = guildId;
    m_chatName = name;
    m_chatScroll = 0.f;
    if (m_service && m_core) {
        m_service->fetchMessagesAsync(*m_core, channelId);
    }
}

void DiscordPanel::bindInput() {
    clearActions();
    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (m_view == View::Chat) {
            m_chatScroll = std::max(0.f, m_chatScroll - 40.f);
        } else {
            if (listFocused()) move(-1); else moveRail(-1);
        }
    });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (m_view == View::Chat) {
            m_chatScroll += 40.f;
        } else {
            if (listFocused()) move(+1); else moveRail(+1);
        }
    });
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (m_view == View::Home && railShown() && m_column == 1) {
            m_column = 0;
            if (m_cb.onNavigate) m_cb.onNavigate();
        }
    });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (m_view == View::Home && railShown() && m_column == 0) {
            m_column = 1;
            if (m_cb.onNavigate) m_cb.onNavigate();
        }
    });

    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_view == View::Chat) {
            if (m_cb.onReply && m_chatId != 0) m_cb.onReply(m_chatId, m_chatName);
            return;
        }
        if (!listFocused()) {
            m_column = 1;
            m_sel = 0;
            m_scroll = 0.f;
            rebuildRows();
            if (m_cb.onNavigate) m_cb.onNavigate();
            return;
        }
        activateRow();
    });

    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() { back(); });

    addAction(static_cast<uint64_t>(nxui::Button::X), [this]() {
        if (m_view == View::Chat && m_chatId != 0) {
            if (m_cb.onReply) m_cb.onReply(m_chatId, m_chatName);
        } else if (m_view == View::Home && m_service && m_service->state() == DiscordService::State::SignedOut) {
            if (m_cb.onUserTokenLogin) m_cb.onUserTokenLogin();
        } else if (m_view == View::Home && m_service && m_service->state() == DiscordService::State::Ready) {
            openAccounts();
        }
    });

    addAction(static_cast<uint64_t>(nxui::Button::Y), [this]() {
        if (m_view == View::Home && m_service && m_service->state() == DiscordService::State::Ready) {
            openAccounts();
        }
    });
}

void DiscordPanel::back() {
    if (m_view == View::Chat || m_view == View::Accounts) {
        m_view = View::Home;
        rebuildRows();
        if (m_cb.onNavigate) m_cb.onNavigate();
        return;
    }
    if (m_service && m_service->state() == DiscordService::State::QrLogin) {
        m_service->cancelQrLogin();
        rebuildRows();
        if (m_cb.onNavigate) m_cb.onNavigate();
        return;
    }
    hide();
}

void DiscordPanel::openAccounts() {
    m_view = View::Accounts;
    m_sel = 0;
    m_scroll = 0.f;
    rebuildRows();
    if (m_cb.onNavigate) m_cb.onNavigate();
}

bool DiscordPanel::railShown() const {
    return m_view == View::Home && m_service && m_service->state() == DiscordService::State::Ready;
}

void DiscordPanel::move(int dir) {
    if (m_rows.empty()) return;
    int next = m_sel;
    for (int step = 0; step < static_cast<int>(m_rows.size()); ++step) {
        next += dir;
        if (next < 0) { next = 0; break; }
        if (next >= static_cast<int>(m_rows.size())) { next = static_cast<int>(m_rows.size()) - 1; break; }
        // Skip non-interactive rows
        if (m_rows[next].kind != Row::Category && m_rows[next].kind != Row::Info && m_rows[next].kind != Row::Error) {
            break;
        }
    }
    if (next != m_sel && next >= 0 && next < static_cast<int>(m_rows.size())) {
        if (m_rows[next].kind != Row::Category && m_rows[next].kind != Row::Info && m_rows[next].kind != Row::Error) {
            m_sel = next;
            if (m_cb.onNavigate) m_cb.onNavigate();
        }
    }
}

void DiscordPanel::moveRail(int dir) {
    if (!m_service) return;
    const int count = 1 + static_cast<int>(m_service->guilds().size());
    int next = m_railSel + dir;
    if (next < 0) next = 0;
    if (next >= count) next = count - 1;
    if (next != m_railSel) {
        m_railSel = next;
        m_sel = 0;
        m_scroll = 0.f;
        const float selY = m_railSel == 0 ? 0.f : (66.f + (m_railSel - 1) * 56.f);
        const float visibleH = kPanelH - kMeH - 32.f;
        if (selY < m_railScroll) {
            m_railScroll = selY;
        } else if (selY + 48.f > m_railScroll + visibleH) {
            m_railScroll = selY + 48.f - visibleH;
        }
        rebuildRows();
        if (m_cb.onNavigate) m_cb.onNavigate();
    }
}

void DiscordPanel::activateRow() {
    if (m_rows.empty() || m_sel < 0 || m_sel >= static_cast<int>(m_rows.size())) return;
    const auto& r = m_rows[m_sel];
    if (m_cb.onActivate) m_cb.onActivate();

    if (r.kind == Row::Button) {
        if (r.action == 1 && m_service && m_core) { // QR login
            m_service->startQrLogin(*m_core);
        } else if (r.action == 2) { // Token login
            if (m_cb.onUserTokenLogin) m_cb.onUserTokenLogin();
        } else if (r.action == 3 && m_service) { // Sign out
            m_service->signOut();
            m_view = View::Home;
            rebuildRows();
        }
    } else if (r.kind == Row::Dm || r.kind == Row::Channel) {
        openChat(r.id, (m_railSel > 0 && m_service) ? m_service->guilds()[m_railSel - 1].id : 0, r.text);
    } else if (r.kind == Row::Account && m_service && m_core) {
        m_service->switchAccount(*m_core, snowflakeToString(r.id));
        m_view = View::Home;
        rebuildRows();
    }
}

void DiscordPanel::rebuildRows() {
    m_rows.clear();
    if (!m_service) return;

    const auto st = m_service->state();

    if (m_view == View::Accounts) {
        for (const auto& a : m_service->accounts().accounts()) {
            Row r;
            r.kind = Row::Account;
            r.text = a.name();
            r.detail = "@" + a.username;
            r.id = stringToSnowflake(a.id);
            r.selected = (a.id == m_service->accounts().currentId());
            m_rows.push_back(std::move(r));
        }

        Row addQr;
        addQr.kind = Row::Button;
        addQr.text = "+ Add another account (QR Code)";
        addQr.detail = T("discord_ui.qr_detail", "Fast login via Discord mobile app");
        addQr.action = 1;
        m_rows.push_back(std::move(addQr));

        Row addTok;
        addTok.kind = Row::Button;
        addTok.text = "+ Add account via Token";
        addTok.detail = T("discord_ui.token_detail", "Enter an account token manually via keyboard");
        addTok.action = 2;
        m_rows.push_back(std::move(addTok));

        Row out;
        out.kind = Row::Button;
        out.text = T("discord_ui.sign_out", "Sign out current account");
        out.action = 3;
        out.danger = true;
        m_rows.push_back(std::move(out));

        if (m_sel < 0 || m_sel >= static_cast<int>(m_rows.size())) m_sel = 0;
        return;
    }

    if (st == DiscordService::State::SignedOut) {
        Row qr;
        qr.kind = Row::Button;
        qr.text = T("discord_ui.sign_in_qr", "Sign in with QR Code");
        qr.detail = T("discord_ui.qr_detail", "Fast login via Discord mobile app");
        qr.action = 1;
        qr.primary = true;
        m_rows.push_back(std::move(qr));

        Row tok;
        tok.kind = Row::Button;
        tok.text = T("discord_ui.sign_in_token", "Sign in with User Token");
        tok.detail = T("discord_ui.token_detail", "Enter an account token manually via keyboard");
        tok.action = 2;
        m_rows.push_back(std::move(tok));

        Row info;
        info.kind = Row::Info;
        info.text = T("discord_ui.tip", "Tip: Open Discord on your phone > Settings > Scan QR Code to connect instantly.");
        m_rows.push_back(std::move(info));
    } else if (st == DiscordService::State::Connecting) {
        Row c;
        c.kind = Row::Info;
        c.text = T("discord_ui.connecting", "Connecting to Discord...");
        m_rows.push_back(std::move(c));
    } else if (st == DiscordService::State::Failed) {
        Row err;
        err.kind = Row::Error;
        err.text = T("discord_ui.failed", "Connection failed");
        err.detail = m_service->error().empty() ? T("discord_ui.failed_detail", "Could not connect to Discord") : m_service->error();
        m_rows.push_back(std::move(err));

        Row retry;
        retry.kind = Row::Button;
        retry.text = T("discord_ui.retry_qr", "Try QR Sign in again");
        retry.action = 1;
        m_rows.push_back(std::move(retry));

        Row tok;
        tok.kind = Row::Button;
        tok.text = T("discord_ui.sign_in_token", "Sign in with User Token");
        tok.detail = T("discord_ui.token_detail", "Enter an account token manually via keyboard");
        tok.action = 2;
        m_rows.push_back(std::move(tok));
    } else if (st == DiscordService::State::Ready) {
        if (m_railSel == 0) { // DMs
            for (const auto& d : m_service->dms()) {
                Row r;
                r.kind = Row::Dm;
                r.text = d.name.empty() ? T("discord_ui.dm", "Direct Message") : d.name;
                r.detail = d.topic.empty() ? T("discord_ui.dm", "Direct Message") : d.topic;
                r.id = d.id;
                m_rows.push_back(std::move(r));
            }
            if (m_rows.empty()) {
                Row empty;
                empty.kind = Row::Info;
                empty.text = T("discord_ui.no_dms", "No direct messages yet.");
                m_rows.push_back(std::move(empty));
            }
        } else { // Guild channels
            const auto& guilds = m_service->guilds();
            const std::size_t gIdx = static_cast<std::size_t>(m_railSel - 1);
            if (gIdx < guilds.size()) {
                const auto& g = guilds[gIdx];
                if (g.channels.empty() && m_core) {
                    m_service->fetchGuildChannelsAsync(*m_core, g.id);
                    Row loading;
                    loading.kind = Row::Info;
                    loading.text = T("discord_ui.loading_channels", "Loading channels...");
                    m_rows.push_back(std::move(loading));
                } else {
                    for (const auto& c : g.channels) {
                        if (c.type == ChannelType::GuildCategory) {
                            Row cat;
                            cat.kind = Row::Category;
                            cat.text = c.name;
                            for (char& ch : cat.text) ch = (char)std::toupper((unsigned char)ch);
                            m_rows.push_back(std::move(cat));
                        } else if (c.type == ChannelType::GuildText || c.type == ChannelType::GuildAnnouncement) {
                            Row r;
                            r.kind = Row::Channel;
                            r.text = "# " + c.name;
                            r.detail = c.topic;
                            r.id = c.id;
                            m_rows.push_back(std::move(r));
                        }
                    }
                    if (m_rows.empty()) {
                        Row empty;
                        empty.kind = Row::Info;
                        empty.text = T("discord_ui.no_channels", "No text channels in this server.");
                        m_rows.push_back(std::move(empty));
                    }
                }
            }
        }
    }

    // Ensure m_sel points to a valid interactive row
    if (m_sel < 0 || m_sel >= static_cast<int>(m_rows.size()) ||
        m_rows[m_sel].kind == Row::Category || m_rows[m_sel].kind == Row::Info || m_rows[m_sel].kind == Row::Error) {
        m_sel = -1;
        for (std::size_t i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].kind != Row::Category && m_rows[i].kind != Row::Info && m_rows[i].kind != Row::Error) {
                m_sel = static_cast<int>(i);
                break;
            }
        }
    }
}

nxui::Rect DiscordPanel::panelRect() const {
    const float x = -kPanelW + (kPanelX + kPanelW) * m_anim;
    return {x, kPanelY, kPanelW, kPanelH};
}

void DiscordPanel::onUpdate(float dt) {
    if (m_active) {
        m_anim = std::min(1.f, m_anim + dt / kAnimDur);
    } else {
        m_anim = std::max(0.f, m_anim - dt / kAnimDur);
        if (m_anim <= 0.001f) {
            setVisible(false);
        }
    }

    if (!m_active || !m_service) return;

    const auto st = m_service->state();
    const auto ver = m_service->version();
    if (st != m_lastState || ver != m_lastVersion || m_view != m_lastView || m_railSel != m_lastRailSel) {
        m_lastState = st;
        m_lastVersion = ver;
        m_lastView = m_view;
        m_lastRailSel = m_railSel;
        rebuildRows();
    }
}

void DiscordPanel::onRender(nxui::Renderer& ren) {
    if (!isVisible() || m_anim <= 0.001f) return;
    pumpImages(ren.gpu(), ren);
    const float a = m_anim;

    // Dimmed background
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.45f * a});

    const nxui::Rect p = panelRect();
    ren.drawRoundedRect(p, bgSecondary(a), 18.f);
    ren.drawRoundedRectOutline(p, {1.f, 1.f, 1.f, 0.12f * a}, 18.f, 1.5f);

    if (!m_font) return;

    if (m_view == View::Chat) {
        drawChat(ren, p, a);
        return;
    }

    if (m_service && m_service->state() == DiscordService::State::QrLogin) {
        drawQrView(ren, p, a);
        return;
    }

    const bool hasRail = railShown();
    if (hasRail) {
        drawRail(ren, p, a);
    }

    const float listX = p.x + (hasRail ? kRailW : 0.f);
    const float listW = p.width - (hasRail ? kRailW : 0.f);
    const nxui::Rect listArea{listX, p.y + kTopH, listW, p.height - kTopH - (hasRail ? kMeH : 0.f)};

    // Top title bar
    std::string titleText = "Discord";
    if (m_view == View::Accounts) {
        titleText = T("discord_ui.accounts", "Accounts");
    } else if (hasRail) {
        if (m_railSel == 0) {
            titleText = T("discord_ui.dms", "Direct Messages");
        } else if (m_service) {
            const auto& guilds = m_service->guilds();
            const std::size_t gIdx = static_cast<std::size_t>(m_railSel - 1);
            if (gIdx < guilds.size()) {
                titleText = guilds[gIdx].name;
            }
        }
    }

    drawLabel(ren, m_font, listX + 20.f, p.y + 16.f, titleText, textNormal(a));
    ren.drawRect({listX, p.y + kTopH - 1.f, listW, 1.f}, {1.f, 1.f, 1.f, 0.08f * a});

    drawRows(ren, listArea, a);

    if (hasRail) {
        drawMe(ren, p, a);
    }
}

void DiscordPanel::drawRail(nxui::Renderer& ren, const nxui::Rect& p, float a) {
    const nxui::Rect rail{p.x, p.y, kRailW, p.height - kMeH};
    ren.drawRoundedRect(rail, bgTertiary(a), 18.f);
    m_railHits.clear();

    const nxui::Rect railClip{p.x, p.y + 8.f, kRailW, p.height - kMeH - 16.f};
    ren.pushClipRect(railClip);

    float y = p.y + 14.f - m_railScroll;
    const float cx = p.x + kRailW * 0.5f;

    // Direct messages top button
    const nxui::Rect dmHit{p.x + 12.f, y, 48.f, 48.f};
    const bool dmSel = (m_railSel == 0);
    const bool dmFocused = (m_column == 0 && dmSel);
    ren.drawRoundedRect(dmHit, dmSel ? blurple(a) : bgPrimary(a), dmSel ? 16.f : 24.f);
    if (dmFocused) {
        ren.drawRoundedRectOutline(dmHit, {1.f, 1.f, 1.f, 0.7f * a}, 16.f, 2.f);
    }
    if (m_smallFont) {
        drawLabel(ren, m_smallFont, cx - 12.f, y + 14.f, "DM", nxui::Color(1.f, 1.f, 1.f, a));
    }
    m_railHits.push_back({0, dmHit});
    y += 56.f;

    // Divider
    ren.drawRect({p.x + 20.f, y, 32.f, 2.f}, {1.f, 1.f, 1.f, 0.12f * a});
    y += 10.f;

    // Guilds
    if (m_service) {
        const auto& guilds = m_service->guilds();
        for (std::size_t i = 0; i < guilds.size(); ++i) {
            const nxui::Rect gHit{p.x + 12.f, y, 48.f, 48.f};
            const bool sel = (m_railSel == static_cast<int>(i + 1));
            const bool focused = (m_column == 0 && sel);
            ren.drawRoundedRect(gHit, sel ? blurple(a) : bgPrimary(a), sel ? 16.f : 24.f);
            if (focused) {
                ren.drawRoundedRectOutline(gHit, {1.f, 1.f, 1.f, 0.7f * a}, 16.f, 2.f);
            }
            const nxui::Texture* gIcon = requestImage(guilds[i].iconUrl(64), 48);
            if (gIcon) {
                ren.drawTextureRounded(gIcon, gHit, sel ? 16.f : 24.f, {1.f, 1.f, 1.f, a});
            } else if (m_smallFont) {
                const std::string inits = guilds[i].initials();
                const float textW = m_smallFont->measure(inits).x;
                drawLabel(ren, m_smallFont, cx - textW * 0.5f, y + 14.f, inits, nxui::Color(1.f, 1.f, 1.f, a));
            }
            m_railHits.push_back({static_cast<int>(i + 1), gHit});
            y += 56.f;
        }
    }

    ren.popClipRect();
}

void DiscordPanel::drawRows(nxui::Renderer& ren, const nxui::Rect& area, float a) {
    m_hits.clear();

    // Compute row heights and tops
    std::vector<float> rowHeights(m_rows.size());
    std::vector<float> rowTops(m_rows.size());
    float curTop = 8.f;
    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        rowTops[i] = curTop;
        float h = 46.f;
        if (m_rows[i].kind == Row::Category) {
            h = 36.f;
        } else if (!m_rows[i].detail.empty()) {
            h = 60.f;
        }
        rowHeights[i] = h;
        curTop += h + 6.f;
    }
    const float totalH = curTop;
    const float maxScroll = std::max(0.f, totalH - area.height + 10.f);

    // Auto-scroll to selected row if needed
    if (m_sel >= 0 && m_sel < static_cast<int>(m_rows.size())) {
        const float selTop = rowTops[m_sel];
        const float selBottom = selTop + rowHeights[m_sel];
        if (selTop < m_scroll) {
            m_scroll = selTop;
        } else if (selBottom > m_scroll + area.height) {
            m_scroll = selBottom - area.height;
        }
    }
    m_scroll = std::clamp(m_scroll, 0.f, maxScroll);

    ren.pushClipRect(area);

    for (std::size_t i = 0; i < m_rows.size(); ++i) {
        const auto& r = m_rows[i];
        const float y = area.y + rowTops[i] - m_scroll;
        const float h = rowHeights[i];
        if (y + h < area.y - 10.f || y > area.bottom() + 10.f) continue;

        const nxui::Rect rowRect{area.x + 8.f, y, area.width - 16.f, h};
        const bool focused = listFocused() && (m_sel == static_cast<int>(i));

        if (r.kind == Row::Category) {
            if (m_smallFont) {
                drawLabel(ren, m_smallFont, rowRect.x + 10.f, y + 12.f, r.text, textMuted(a));
            }
            continue;
        }

        if (r.kind == Row::Info) {
            if (m_smallFont) {
                drawLabel(ren, m_smallFont, rowRect.x + 12.f, y + 14.f, r.text, textMuted(a));
            }
            continue;
        }

        if (r.kind == Row::Error) {
            ren.drawRoundedRect(rowRect, rgb(242, 63, 67, 0.15f * a), 8.f);
            drawLabel(ren, m_font, rowRect.x + 12.f, y + 10.f, r.text, red(a));
            if (!r.detail.empty() && m_smallFont) {
                drawLabel(ren, m_smallFont, rowRect.x + 12.f, y + 32.f, r.detail, textMuted(a));
            }
            continue;
        }

        if (focused || r.selected) {
            ren.drawRoundedRect(rowRect, r.danger ? rgb(242, 63, 67, 0.25f * a) : bgSelected(a), 8.f);
            ren.drawRoundedRectOutline(rowRect, {1.f, 1.f, 1.f, 0.18f * a}, 8.f, 1.f);
        }

        if (!r.detail.empty()) {
            drawLabel(ren, m_font, rowRect.x + 12.f, y + 10.f, r.text, r.danger ? red(a) : textNormal(a));
            if (m_smallFont) {
                drawLabel(ren, m_smallFont, rowRect.x + 12.f, y + 34.f, r.detail, textMuted(a));
            }
        } else {
            drawLabel(ren, m_font, rowRect.x + 12.f, y + 12.f, r.text, r.danger ? red(a) : textNormal(a));
        }

        m_hits.push_back({static_cast<int>(i), rowRect});
    }

    ren.popClipRect();
}

void DiscordPanel::drawMe(nxui::Renderer& ren, const nxui::Rect& p, float a) {
    const nxui::Rect me{p.x, p.y + p.height - kMeH, p.width, kMeH};
    ren.drawRoundedRect(me, bgUser(a), 18.f);

    const auto u = m_service ? m_service->currentUser() : User{};
    const std::string name = u.id != 0 ? u.displayName() : T("discord_ui.signed_in", "Signed in");
    const std::string tag = u.id != 0 ? "@" + u.username : "";

    // Avatar circle
    const float cx = me.x + 32.f, cy = me.y + me.height * 0.5f;
    ren.drawCircle({cx, cy}, 18.f, blurple(a), 24);
    // Online dot
    ren.drawCircle({cx + 12.f, cy + 12.f}, 5.f, green(a), 16);

    drawLabel(ren, m_font, me.x + 62.f, me.y + 12.f, name, textNormal(a));
    if (m_smallFont) {
        drawLabel(ren, m_smallFont, me.x + 62.f, me.y + 36.f, tag + T("discord_ui.y_accounts", "  (Y: Accounts)"), textMuted(a));
    }
}

void DiscordPanel::drawQrView(nxui::Renderer& ren, const nxui::Rect& p, float a) {
    drawLabel(ren, m_font, p.x + 24.f, p.y + 36.f, T("discord_ui.scan_qr", "Scan QR Code"), textNormal(a));
    if (m_smallFont) {
        drawLabel(ren, m_smallFont, p.x + 24.f, p.y + 64.f,
                  T("discord_ui.scan_hint", "Scan with Discord mobile app (User Settings > Scan QR Code)"), textMuted(a));
    }

    const auto snap = m_service ? m_service->remoteAuth().snapshot() : DiscordRemoteAuth::Snapshot{};
    if (snap.qrSize > 0 && !snap.qrModules.empty()) {
        const float qrBoxSize = 260.f;
        const float qx = p.x + (p.width - qrBoxSize) * 0.5f;
        const float qy = p.y + 104.f;

        // White background box for QR code
        ren.drawRoundedRect({qx - 10.f, qy - 10.f, qrBoxSize + 20.f, qrBoxSize + 20.f},
                            nxui::Color(1.f, 1.f, 1.f, a), 12.f);

        const float cell = qrBoxSize / static_cast<float>(snap.qrSize);
        for (int y = 0; y < snap.qrSize; ++y) {
            for (int x = 0; x < snap.qrSize; ++x) {
                if (snap.qrModules[static_cast<std::size_t>(y * snap.qrSize + x)]) {
                    ren.drawRect({qx + x * cell, qy + y * cell, cell + 0.5f, cell + 0.5f},
                                 nxui::Color(0.08f, 0.09f, 0.12f, a));
                }
            }
        }

        if (m_smallFont) {
            const std::string status = snap.username.empty()
                ? T("discord_ui.waiting_scan", "Waiting for scan from phone...")
                : T("discord_ui.confirm_phone", "Confirm login on phone: @") + snap.username;
            drawLabel(ren, m_smallFont, p.x + 24.f, p.y + 400.f, status, textNormal(a));
            drawLabel(ren, m_smallFont, p.x + 24.f, p.y + 430.f, T("discord_ui.press_b_cancel", "Press (B) to Cancel"), textMuted(a));
        }
    } else {
        drawLabel(ren, m_font, p.x + 100.f, p.y + 240.f, T("discord_ui.generating_qr", "Generating QR Code..."), textMuted(a));
        if (m_smallFont) {
            drawLabel(ren, m_smallFont, p.x + 24.f, p.y + 430.f, T("discord_ui.press_b_cancel", "Press (B) to Cancel"), textMuted(a));
        }
    }
}

void DiscordPanel::drawChat(nxui::Renderer& ren, const nxui::Rect& p, float a) {
    // Top bar
    m_backHit = {p.x + 12.f, p.y + 12.f, 96.f, 36.f};
    ren.drawRoundedRect(m_backHit, bgSelected(a), 6.f);
    if (m_smallFont) {
        drawLabel(ren, m_smallFont, m_backHit.x + 8.f, m_backHit.y + 10.f, T("discord_ui.back", "< Voltar (B)"), textNormal(a));
    }
    drawLabel(ren, m_font, p.x + 120.f, p.y + 16.f, m_chatName, textNormal(a));
    ren.drawRect({p.x, p.y + kTopH - 1.f, p.width, 1.f}, {1.f, 1.f, 1.f, 0.08f * a});

    // Message list area
    const float chatY = p.y + kTopH;
    const float chatH = p.height - kTopH - kInputH;
    const nxui::Rect chatArea{p.x + 8.f, chatY, p.width - 16.f, chatH};

    const auto msgs = m_service ? m_service->messages(m_chatId) : std::vector<Message>{};

    if (msgs.empty()) {
        if (m_smallFont) {
            drawLabel(ren, m_smallFont, p.x + 24.f, chatY + 24.f, T("discord_ui.no_messages", "Nenhuma mensagem ainda. Pressione A ou X para digitar!"), textMuted(a));
        }
    } else {
        struct MsgLayout {
            std::vector<std::string> lines;
            std::vector<std::string> images;
            float height = 0.f;
        };
        const float maxTextW = chatArea.width - 64.f;
        std::vector<MsgLayout> layouts(msgs.size());
        float totalMsgH = 16.f;
        for (std::size_t i = 0; i < msgs.size(); ++i) {
            auto& lay = layouts[i];
            const auto& m = msgs[i];
            lay.lines = wrapLines(m_smallFont, cleanText(m.content), maxTextW);
            for (const auto& att : m.attachmentUrls) {
                lay.images.push_back(att);
            }
            float h = 28.f;
            h += static_cast<float>(std::max<std::size_t>(1, lay.lines.size())) * 20.f;
            h += static_cast<float>(lay.images.size()) * 156.f;
            lay.height = h;
            totalMsgH += h + 8.f;
        }

        const float maxScroll = std::max(0.f, totalMsgH - chatH + 10.f);
        if (m_chatScroll > maxScroll) m_chatScroll = maxScroll;

        ren.pushClipRect(chatArea);

        float y = chatY + 10.f - m_chatScroll;
        for (std::size_t i = 0; i < msgs.size(); ++i) {
            const auto& m = msgs[i];
            const auto& lay = layouts[i];
            const float mh = lay.height;
            if (y + mh >= chatArea.y - 20.f && y <= chatArea.bottom() + 20.f) {
                // Avatar
                const nxui::Texture* avTex = requestImage(m.author.avatarUrl(64), 64);
                const nxui::Rect avRect{chatArea.x + 8.f, y + 4.f, 34.f, 34.f};
                if (avTex) {
                    ren.drawTextureRounded(avTex, avRect, 17.f, {1.f, 1.f, 1.f, a});
                } else {
                    ren.drawCircle({avRect.x + 17.f, avRect.y + 17.f}, 17.f, blurple(a), 24);
                    std::string init = m.author.displayName().substr(0, 1);
                    if (m_smallFont)
                        drawLabel(ren, m_smallFont, avRect.x + 12.f, avRect.y + 10.f, init, textNormal(a));
                }

                // Author name & time
                drawLabel(ren, m_font, chatArea.x + 50.f, y + 2.f, m.author.displayName(), blurple(a));
                if (m_smallFont && !m.timestamp.empty()) {
                    float nw = m_font ? m_font->measure(m.author.displayName()).x : 80.f;
                    drawLabel(ren, m_smallFont, chatArea.x + 50.f + nw + 10.f, y + 6.f, m.timestamp, textMuted(a));
                }

                // Content lines
                float ly = y + 24.f;
                for (const auto& line : lay.lines) {
                    if (m_smallFont) {
                        drawLabel(ren, m_smallFont, chatArea.x + 50.f, ly, line, textNormal(a));
                    }
                    ly += 20.f;
                }

                // Attachment images
                for (const auto& imgUrl : lay.images) {
                    const nxui::Rect imgBox{chatArea.x + 50.f, ly + 4.f, std::min(maxTextW, 260.f), 144.f};
                    const nxui::Texture* imgTex = requestImage(imgUrl, 320);
                    if (imgTex) {
                        ren.drawTextureRounded(imgTex, imgBox, 8.f, {1.f, 1.f, 1.f, a});
                    } else {
                        ren.drawRoundedRect(imgBox, bgTertiary(a), 8.f);
                        ren.drawRoundedRectOutline(imgBox, {1.f, 1.f, 1.f, 0.12f * a}, 8.f, 1.f);
                        if (m_smallFont) {
                            drawLabel(ren, m_smallFont, imgBox.x + 16.f, imgBox.y + imgBox.height * 0.5f - 8.f,
                                      T("discord_ui.loading_image", "[Carregando imagem...]"), textMuted(a));
                        }
                    }
                    ly += 154.f;
                }
            }
            y += mh + 8.f;
        }

        ren.popClipRect();
    }

    // Input bar at bottom
    m_replyHit = {p.x + 12.f, p.y + p.height - kInputH + 6.f, p.width - 24.f, 44.f};
    ren.drawRoundedRect(m_replyHit, bgTertiary(a), 8.f);
    ren.drawRoundedRectOutline(m_replyHit, {1.f, 1.f, 1.f, 0.12f * a}, 8.f, 1.f);
    if (m_smallFont) {
        drawLabel(ren, m_smallFont, m_replyHit.x + 14.f, m_replyHit.y + 12.f,
                  T("discord_ui.type_hint", "Pressione (A) ou (X) para digitar uma mensagem..."), textMuted(a));
        const nxui::Rect xKey{m_replyHit.right() - 32.f, m_replyHit.y + 10.f, 24.f, 24.f};
        ren.drawRoundedRect(xKey, bgSelected(a), 12.f);
        drawLabel(ren, m_smallFont, xKey.x + 7.f, xKey.y + 4.f, "X", textNormal(a));
    }
}

void DiscordPanel::handleTouch(const nxui::Input& in) {
    if (!m_active || m_anim < 0.9f) return;
    if (!in.touchDown()) return;

    const float tx = in.touchX();
    const float ty = in.touchY();
    const nxui::Rect p = panelRect();
    if (!p.contains(tx, ty)) {
        hide();
        return;
    }

    if (m_view == View::Chat) {
        if (m_backHit.contains(tx, ty)) {
            back();
            return;
        }
        if (m_replyHit.contains(tx, ty)) {
            if (m_cb.onReply) m_cb.onReply(m_chatId, m_chatName);
            return;
        }
    }

    for (const auto& hit : m_railHits) {
        if (hit.second.contains(tx, ty)) {
            m_railSel = hit.first;
            m_column = 1;
            m_sel = 0;
            m_scroll = 0.f;
            rebuildRows();
            if (m_cb.onNavigate) m_cb.onNavigate();
            return;
        }
    }

    for (const auto& hit : m_hits) {
        if (hit.second.contains(tx, ty)) {
            m_sel = hit.first;
            m_column = 1;
            activateRow();
            return;
        }
    }
}

} // namespace discord
