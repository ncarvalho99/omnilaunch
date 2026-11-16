#include "discord/DiscordPanel.hpp"
#include "core/ButtonGlyph.hpp"
#include "discord/DiscordApi.hpp"
#include "discord/DiscordPresentation.hpp"
#include "home/HomeCore.hpp"
#include "net/NetService.hpp"

#include <nxui/core/Animation.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>

#include <webp/decode.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <sstream>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace discord {

namespace {

// Panel text through the locale files; the English literal is the fallback.
inline std::string T(const char* key, const char* fallback) {
    return nxui::I18n::instance().tr(key, fallback);
}

inline void drawLabel(nxui::Renderer& ren, nxui::Font* font, float x, float y,
                      const std::string& text, const nxui::Color& color, float scale = 1.0f) {
    if (font && !text.empty()) {
        ren.drawText(text, {x, y}, font, color, scale);
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

// "2019-09-20T05:31:12.345+00:00" -> seconds since 1970 (UTC); -1 when not ISO.
static long long parseIsoUtc(const std::string& ts) {
    if (ts.size() < 19 || ts[4] != '-' || ts[7] != '-' || ts[10] != 'T') return -1;
    auto n = [&](std::size_t at, std::size_t len) { return std::atoi(ts.substr(at, len).c_str()); };
    int y = n(0, 4);
    const unsigned m = static_cast<unsigned>(n(5, 2)), d = static_cast<unsigned>(n(8, 2));
    // days_from_civil (Howard Hinnant)
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = static_cast<long long>(era) * 146097 + static_cast<long long>(doe) - 719468;
    long long secs = days * 86400 + n(11, 2) * 3600 + n(14, 2) * 60 + n(17, 2);
    // Discord sends +00:00; honour any other offset anyway.
    const auto tz = ts.find_first_of("+-", 19);
    if (tz != std::string::npos && ts.size() >= tz + 6) {
        const int off = n(tz + 1, 2) * 3600 + n(tz + 4, 2) * 60;
        secs += ts[tz] == '+' ? -off : off;
    }
    return secs;
}

// Discord timestamps are UTC; the phone shows them in the console's time zone.
static std::string formatDiscordTimestamp(const std::string& ts) {
    if (ts.empty()) return {};
    const long long utc = parseIsoUtc(ts);
    if (utc < 0) return ts;
    char buf[32];
#ifdef __SWITCH__
    TimeCalendarTime cal{};
    TimeCalendarAdditionalInfo info{};
    if (R_SUCCEEDED(timeToCalendarTimeWithMyRule(static_cast<u64>(utc), &cal, &info))) {
        std::snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d", cal.day, cal.month, cal.year, cal.hour,
                      cal.minute);
        return buf;
    }
#endif
    return ts.substr(8, 2) + "/" + ts.substr(5, 2) + "/" + ts.substr(0, 4) + " " + ts.substr(11, 5);
}

// "a few seconds", "10 minutes", "1 h 05 min" for a call record.
static std::string callDuration(long long seconds) {
    auto& t = nxui::I18n::instance();
    if (seconds < 60) return t.tr("discord_ui.call_seconds", "a few seconds");
    if (seconds < 3600) {
        const long long m = seconds / 60;
        return std::to_string(m) + " " + (m == 1 ? t.tr("discord_ui.call_minute", "minute")
                                                 : t.tr("discord_ui.call_minutes", "minutes"));
    }
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%lld h %02lld min", seconds / 3600, (seconds % 3600) / 60);
    return buf;
}

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

inline std::string fit(nxui::Font* font, const std::string& text, float maxW, float scale = 1.0f) {
    if (!font || text.empty()) return text;
    if (font->measure(text).x * scale <= maxW) return text;
    std::string s = text;
    while (!s.empty() && font->measure(s + "...").x * scale > maxW) {
        // Cut whole UTF-8 letters, never half of one.
        std::size_t cut = s.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.erase(cut);
    }
    return s.empty() ? std::string("...") : s + "...";
}

// One UTF-8 codepoint from [p, end); malformed bytes come back as U+FFFD.
uint32_t nextCodepoint(const char*& p, const char* end) {
    const auto b0 = static_cast<unsigned char>(*p++);
    if (b0 < 0x80) return b0;
    int extra = b0 >= 0xF0 ? 3 : b0 >= 0xE0 ? 2 : b0 >= 0xC0 ? 1 : -1;
    if (extra < 0) return 0xFFFD;
    uint32_t cp = b0 & (0x3F >> extra);
    for (int i = 0; i < extra; ++i) {
        if (p >= end || (static_cast<unsigned char>(*p) & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(*p++) & 0x3F);
    }
    return cp;
}

void appendCodepoint(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// Invisible joiners, variation selectors and skin-tone modifiers: emoji glue
// with nothing to draw once the emoji itself is gone.
bool invisibleMark(uint32_t cp) {
    return (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2060 && cp <= 0x2064) ||
           (cp >= 0xFE00 && cp <= 0xFE0F) || (cp >= 0x1F3FB && cp <= 0x1F3FF) ||
           (cp >= 0xE0020 && cp <= 0xE007F) || cp == 0x20E3 || cp == 0xFEFF;
}

} // namespace

// Server, channel and user names are full of emoji and decorative brackets the
// menu font does not have; SDL_ttf would draw each as an empty box. Leave out
// every letter the font cannot draw, and the spaces that leaves behind.
std::string DiscordPanel::clean(const std::string& text) const {
    if (!m_font || text.empty()) return text;
    std::string out;
    out.reserve(text.size());
    const char* p = text.data();
    const char* end = p + text.size();
    while (p < end) {
        const uint32_t cp = nextCodepoint(p, end);
        if (cp == '\n') { out += '\n'; continue; }
        if (cp < 0x20 || cp == 0xFFFD || invisibleMark(cp)) continue;
        bool ok = cp < 0x80;
        if (!ok) {
            auto it = m_glyphs.find(cp);
            if (it == m_glyphs.end()) it = m_glyphs.emplace(cp, m_font->hasGlyph(cp)).first;
            ok = it->second;
        }
        if (!ok) continue;
        if (cp == ' ' && (out.empty() || out.back() == ' ' || out.back() == '\n')) continue;
        appendCodepoint(out, cp);
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n')) out.pop_back();
    return out;
}

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
                    const auto* bytes = reinterpret_cast<const uint8_t*>(res.body.data());
                    *dec = nxui::Texture::decodeMemory(bytes, res.body.size(), maxSide);
                    int w = 0, h = 0;
                    // Discord's media proxy answers many images as WebP, which
                    // the stb decoder does not read.
                    if (!dec->valid() && WebPGetInfo(bytes, res.body.size(), &w, &h) && w > 0 && h > 0 &&
                        w <= 8192 && h <= 8192) {
                        if (uint8_t* rgba = WebPDecodeRGBA(bytes, res.body.size(), &w, &h)) {
                            int dw = w, dh = h;
                            if (maxSide > 0 && (w > maxSide || h > maxSide)) {
                                const float s = std::min(static_cast<float>(maxSide) / w, static_cast<float>(maxSide) / h);
                                dw = std::max(1, static_cast<int>(w * s));
                                dh = std::max(1, static_cast<int>(h * s));
                            }
                            dec->width = dw;
                            dec->height = dh;
                            dec->rgba.resize(static_cast<std::size_t>(dw) * dh * 4);
                            for (int y = 0; y < dh; ++y)
                                for (int x = 0; x < dw; ++x)
                                    std::memcpy(dec->rgba.data() + (static_cast<std::size_t>(y) * dw + x) * 4,
                                                rgba + (static_cast<std::size_t>(y * h / dh) * w + x * w / dw) * 4, 4);
                            WebPFree(rgba);
                        }
                    }
                }
            } catch (...) {}
        });
    } catch (...) {
        slot.failed = true;
    }
    return nullptr;
}

bool DiscordPanel::imageFailed(const std::string& url) const {
    const auto it = m_images.find(url);
    return it != m_images.end() && it->second.failed;
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

// OmniLaunch's button capsule (glyph + translated label), right-aligned at rightX.
void DiscordPanel::drawHint(nxui::Renderer& ren, nxui::Button btn, const std::string& label,
                            float rightX, float y, float a, float* outW) {
    if (!m_smallFont) return;
    nxui::Font& icons = m_iconFont && m_iconFont->valid() ? *m_iconFont : *m_smallFont;
    const ui::glyph::ActionHint h{ui::glyph::button(btn), label};
    const float w = ui::glyph::capsuleWidth(icons, *m_smallFont, h.icon, h.label);
    ui::glyph::drawCapsule(ren, icons, *m_smallFont, h, {rightX - w, y, w, 26.f}, a);
    if (outW) *outW = w;
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
    m_chatName = clean(name);
    m_chatScroll = 1e9f; // start at the newest message, as the phone does
    m_chatAtBottom = true;
    if (m_service && m_core) {
        m_service->fetchMessagesAsync(*m_core, channelId);
        m_service->markRead(channelId);
    }
}

void DiscordPanel::bindInput() {
    clearActions();
    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (m_view == View::Chat) {
            m_chatScroll = std::max(0.f, m_chatScroll - 40.f);
            m_chatAtBottom = false;
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
            r.text = clean(a.name());
            r.detail = "@" + a.username;
            r.avatarUrl = a.avatarUrl(64);
            r.status = "online";
            r.id = stringToSnowflake(a.id);
            r.selected = (a.id == m_service->accounts().currentId());
            m_rows.push_back(std::move(r));
        }

        Row addQr;
        addQr.kind = Row::Button;
        addQr.text = T("discord_ui.add_qr", "Add another account (QR Code)");
        addQr.detail = T("discord_ui.qr_detail", "Fast login via Discord mobile app");
        addQr.action = 1;
        m_rows.push_back(std::move(addQr));

        Row addTok;
        addTok.kind = Row::Button;
        addTok.text = T("discord_ui.add_token", "Add account via Token");
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
                r.text = clean(d.name);
                if (r.text.empty()) r.text = T("discord_ui.dm", "Direct Message");
                r.avatarUrl = d.avatarUrl;
                if (!d.recipientIds.empty()) {
                    r.status = m_service->presenceStatus(d.recipientIds[0]);
                } else {
                    r.status = "offline";
                }
                r.mentions = d.mentions;
                r.unread = (d.unreadCount > 0 || d.mentions > 0);
                if (d.recipientIds.size() > 1) {
                    r.detail = std::to_string(d.recipientIds.size() + 1) + " " + T("discord_ui.members", "members");
                } else if (r.status != "offline" && !r.status.empty()) {
                    r.detail = (r.status == "online") ? T("discord_ui.status_online", "Online")
                             : (r.status == "idle") ? T("discord_ui.status_idle", "Idle")
                             : (r.status == "dnd") ? T("discord_ui.status_dnd", "Do Not Disturb") : r.status;
                } else {
                    r.detail.clear();
                }
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
                    for (const auto& c : orderedTextChannels(g.channels)) {
                        if (c.type == ChannelType::GuildCategory) {
                            Row cat;
                            cat.kind = Row::Category;
                            cat.text = clean(c.name);
                            for (char& ch : cat.text) ch = (char)std::toupper((unsigned char)ch);
                            if (cat.text.empty()) continue;
                            m_rows.push_back(std::move(cat));
                        } else if (c.type == ChannelType::GuildText || c.type == ChannelType::GuildAnnouncement) {
                            Row r;
                            r.kind = Row::Channel;
                            r.text = clean(c.name);
                            r.id = c.id;
                            r.mentions = c.mentions;
                            r.unread = c.unreadCount > 0 || c.mentions > 0;
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
    if (m_view == View::Chat && m_chatId != 0 && ver != m_lastVersion)
        m_service->markRead(m_chatId); // what arrives in the open chat is read
    if (st != m_lastState || ver != m_lastVersion || m_view != m_lastView || m_railSel != m_lastRailSel) {
        m_lastState = m_service->state();
        m_lastVersion = m_service->version();
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
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.65f * a});

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
                titleText = clean(guilds[gIdx].name);
            }
        }
    }

    // Right side of the title bar: the button that leaves this view.
    float hintW = 0.f;
    const bool sub = m_view == View::Accounts ||
                     (m_service && m_service->state() == DiscordService::State::QrLogin);
    drawHint(ren, nxui::Button::B, sub ? T("hint.back", "Back") : T("hint.close", "Close"), p.right() - 12.f,
             p.y + (kTopH - 26.f) * 0.5f, a, &hintW);
    drawLabel(ren, m_font, listX + 18.f, p.y + 16.f, fit(m_font, titleText, listW - 40.f - hintW, 0.82f),
              textNormal(a), 0.82f);
    ren.drawRect({listX, p.y + kTopH - 1.f, listW, 1.f}, {1.f, 1.f, 1.f, 0.08f * a});

    drawRows(ren, listArea, a);

    if (hasRail) {
        drawMe(ren, p, a);
    }
}

void DiscordPanel::drawAvatar(nxui::Renderer& ren, const std::string& url, const std::string& name,
                              float x, float y, float size, float a) {
    if (!url.empty()) {
        const nxui::Texture* t = requestImage(url, static_cast<int>(size));
        if (t && t->valid()) {
            ren.drawTextureRounded(t, {x, y, size, size}, size * 0.5f, nxui::Color::white().withAlpha(a));
            return;
        }
    }
    static const int kColors[5][3] = {{88, 101, 242}, {116, 127, 141}, {59, 165, 93}, {250, 166, 26}, {237, 66, 69}};
    const auto* c = kColors[std::hash<std::string>{}(name) % 5];
    ren.drawCircle({x + size * 0.5f, y + size * 0.5f}, size * 0.5f, rgb(c[0], c[1], c[2], a), 32);
    if (name.empty() || !m_font) return;
    std::string letter = name.substr(0, 1);
    if (!letter.empty() && letter[0] >= 'a' && letter[0] <= 'z') letter[0] -= 32;
    const float s = size / 48.f;
    const nxui::Vec2 m = m_font->measure(letter);
    drawLabel(ren, m_font, x + (size - m.x * s) * 0.5f, y + (size - m.y * s) * 0.5f, letter, nxui::Color::white().withAlpha(a), s);
}

void DiscordPanel::drawStatus(nxui::Renderer& ren, const std::string& s, float cx, float cy, float r,
                              const nxui::Color& ring, float a) {
    ren.drawCircle({cx, cy}, r + 2.5f, ring, 24);
    if (s == "online") {
        ren.drawCircle({cx, cy}, r, green(a), 24);
    } else if (s == "idle") {
        ren.drawCircle({cx, cy}, r, rgb(240, 178, 50, a), 24);
        ren.drawCircle({cx - r * 0.4f, cy - r * 0.4f}, r * 0.6f, ring, 20);
    } else if (s == "dnd") {
        ren.drawCircle({cx, cy}, r, red(a), 24);
        ren.drawRoundedRect({cx - r * 0.6f, cy - r * 0.2f, r * 1.2f, r * 0.4f}, ring, r * 0.2f);
    } else {
        ren.drawCircle({cx, cy}, r, rgb(128, 132, 142, a), 24);
        ren.drawCircle({cx, cy}, r * 0.5f, ring, 20);
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
    const nxui::Texture* logoTex = requestImage("https://cdn.discordapp.com/embed/avatars/0.png", 64);
    if (!logoTex) logoTex = requestImage("romfs:/icons/cards/discord.png", 64);
    if (logoTex && logoTex->valid()) {
        ren.drawTextureRounded(logoTex, {dmHit.x + 8.f, dmHit.y + 8.f, 32.f, 32.f}, 12.f, nxui::Color::white().withAlpha(a));
    } else {
        const float r = 10.f;
        ren.drawCircle({cx - 7.f, y + 24.f}, r, nxui::Color::white().withAlpha(a), 20);
        ren.drawCircle({cx + 7.f, y + 24.f}, r, nxui::Color::white().withAlpha(a), 20);
        ren.drawRoundedRect({cx - 14.f, y + 17.f, 28.f, 14.f}, nxui::Color::white().withAlpha(a), 4.f);
        ren.drawCircle({cx - 6.f, y + 24.f}, 3.5f, dmSel ? blurple(a) : bgPrimary(a), 16);
        ren.drawCircle({cx + 6.f, y + 24.f}, 3.5f, dmSel ? blurple(a) : bgPrimary(a), 16);
    }
    int totalDmMentions = 0;
    bool dmUnread = false;
    if (m_service) {
        for (const auto& dm : m_service->dms()) {
            totalDmMentions += dm.mentions;
            dmUnread = dmUnread || dm.unreadCount > 0;
        }
    }
    if (dmUnread && !dmSel)
        ren.drawRoundedRect({p.x + 1.f, dmHit.y + 20.f, 4.f, 8.f}, nxui::Color::white().withAlpha(a), 2.f);
    if (totalDmMentions > 0) {
        const std::string n = totalDmMentions > 99 ? "99+" : std::to_string(totalDmMentions);
        const float nw = m_smallFont ? m_smallFont->measure(n).x * 0.7f : 14.f;
        const float bw = std::max(20.f, nw + 10.f);
        ren.drawRoundedRect({dmHit.right() - bw * 0.5f, dmHit.y - 4.f, bw, 18.f}, red(a), 9.f);
        if (m_smallFont) {
            drawLabel(ren, m_smallFont, dmHit.right() - bw * 0.5f + (bw - nw) * 0.5f, dmHit.y - 2.f, n, nxui::Color(1.f, 1.f, 1.f, a), 0.7f);
        }
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
            if (guilds[i].unreadCount > 0 && !sel) // unread pill left of the server
                ren.drawRoundedRect({p.x + 1.f, gHit.y + 20.f, 4.f, 8.f}, nxui::Color::white().withAlpha(a), 2.f);
            if (guilds[i].mentions > 0) {
                const std::string n = guilds[i].mentions > 99 ? "99+" : std::to_string(guilds[i].mentions);
                const float nw = m_smallFont ? m_smallFont->measure(n).x * 0.7f : 14.f;
                const float bw = std::max(20.f, nw + 10.f);
                ren.drawRoundedRect({gHit.right() - bw * 0.5f, gHit.y - 4.f, bw, 18.f}, red(a), 9.f);
                if (m_smallFont) {
                    drawLabel(ren, m_smallFont, gHit.right() - bw * 0.5f + (bw - nw) * 0.5f, gHit.y - 2.f, n, nxui::Color(1.f, 1.f, 1.f, a), 0.7f);
                }
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
        } else if (m_rows[i].kind == Row::Channel) {
            h = 40.f;
        } else if (m_rows[i].kind == Row::Info) {
            const auto lines = wrapLines(m_smallFont, m_rows[i].text, (area.width - 40.f) / 0.78f);
            h = 16.f + 20.f * static_cast<float>(std::max<std::size_t>(1, lines.size()));
        } else if (m_rows[i].kind == Row::Dm || m_rows[i].kind == Row::Account) {
            h = 52.f;
        } else if (!m_rows[i].detail.empty()) {
            h = 58.f;
        }
        rowHeights[i] = h;
        curTop += h + (m_rows[i].kind == Row::Channel ? 0.f : 6.f);
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

        if (r.kind == Row::Category) { // small muted label, as Discord's category headers
            if (m_smallFont) {
                drawLabel(ren, m_smallFont, rowRect.x + 6.f, y + 14.f,
                          fit(m_smallFont, r.text, rowRect.width - 12.f, 0.66f), textMuted(a), 0.66f);
            }
            continue;
        }

        if (r.kind == Row::Channel) { // "#" glyph column + name, Discord style
            if (focused) {
                ren.drawRoundedRect({rowRect.x, y + 2.f, rowRect.width, h - 4.f}, bgSelected(a), 6.f);
                ren.drawRoundedRectOutline({rowRect.x, y + 2.f, rowRect.width, h - 4.f}, {1.f, 1.f, 1.f, 0.18f * a}, 6.f, 1.f);
            }
            const float cy = y + h * 0.5f;
            const nxui::Color ic = (focused || r.unread) ? textNormal(a) : textMuted(a);
            drawLabel(ren, m_font, rowRect.x + 11.f, cy - 12.f, "#", ic, 0.78f);
            const float tw = rowRect.width - 44.f - (r.mentions > 0 ? 36.f : 0.f);
            drawLabel(ren, m_font, rowRect.x + 34.f, cy - 11.f, fit(m_font, r.text, tw, 0.7f), ic, 0.7f);
            if (r.mentions > 0) {
                const std::string n = r.mentions > 99 ? "99+" : std::to_string(r.mentions);
                const float nw = m_smallFont ? m_smallFont->measure(n).x * 0.7f : 14.f;
                const float bw = std::max(20.f, nw + 10.f);
                ren.drawRoundedRect({rowRect.right() - 8.f - bw, cy - 10.f, bw, 20.f}, red(a), 10.f);
                if (m_smallFont)
                    drawLabel(ren, m_smallFont, rowRect.right() - 8.f - bw + (bw - nw) * 0.5f, cy - 8.f, n,
                              nxui::Color::white().withAlpha(a), 0.7f);
            }
            if (r.unread) // Discord's white pill at the left edge
                ren.drawRoundedRect({area.x + 2.f, cy - 4.f, 4.f, 8.f}, nxui::Color::white().withAlpha(a), 2.f);
            m_hits.push_back({static_cast<int>(i), rowRect});
            continue;
        }

        if (r.kind == Row::Info) {
            if (m_smallFont) {
                const auto lines = wrapLines(m_smallFont, r.text, (rowRect.width - 24.f) / 0.78f);
                float ly = y + 8.f;
                for (const auto& line : lines) {
                    drawLabel(ren, m_smallFont, rowRect.x + 12.f, ly, line, textMuted(a), 0.78f);
                    ly += 20.f;
                }
            }
            continue;
        }

        if (r.kind == Row::Error) {
            ren.drawRoundedRect(rowRect, rgb(242, 63, 67, 0.15f * a), 8.f);
            drawLabel(ren, m_font, rowRect.x + 12.f, y + 8.f, fit(m_font, r.text, rowRect.width - 24.f, 0.76f), red(a), 0.76f);
            if (!r.detail.empty() && m_smallFont) {
                drawLabel(ren, m_smallFont, rowRect.x + 12.f, y + 33.f,
                          fit(m_smallFont, r.detail, rowRect.width - 24.f, 0.70f), textMuted(a), 0.70f);
            }
            continue;
        }

        if (r.kind == Row::Dm || r.kind == Row::Account) {
            if (focused || r.selected) {
                ren.drawRoundedRect(rowRect, bgSelected(a), 8.f);
                ren.drawRoundedRectOutline(rowRect, {1.f, 1.f, 1.f, 0.18f * a}, 8.f, 1.f);
            }
            const float av = 36.f;
            const float ax = rowRect.x + 8.f;
            const float ay = rowRect.y + (rowRect.height - av) * 0.5f;
            drawAvatar(ren, r.avatarUrl, r.text, ax, ay, av, a);
            if (!r.status.empty())
                drawStatus(ren, r.status, ax + av - 4.f, ay + av - 4.f, 5.f, (focused || r.selected) ? bgSelected(a) : bgSecondary(a), a);
            const float tx = ax + av + 10.f;
            const float badgeW = (r.mentions > 0 ? 38.f : 8.f);
            const float textW = rowRect.width - (tx - rowRect.x) - badgeW;
            if (!r.detail.empty()) {
                drawLabel(ren, m_font, tx, rowRect.y + 7.f, fit(m_font, r.text, textW, 0.76f), textNormal(a), 0.76f);
                if (m_smallFont) {
                    drawLabel(ren, m_smallFont, tx, rowRect.y + 28.f, fit(m_smallFont, r.detail, textW, 0.70f), textMuted(a), 0.70f);
                }
            } else {
                drawLabel(ren, m_font, tx, rowRect.y + 16.f, fit(m_font, r.text, textW, 0.76f), textNormal(a), 0.76f);
            }
            if (r.mentions > 0) {
                const std::string n = r.mentions > 99 ? "99+" : std::to_string(r.mentions);
                const float nw = m_smallFont ? m_smallFont->measure(n).x * 0.7f : 14.f;
                const float bw = std::max(20.f, nw + 10.f);
                const float cy = rowRect.y + rowRect.height * 0.5f;
                ren.drawRoundedRect({rowRect.right() - 8.f - bw, cy - 10.f, bw, 20.f}, red(a), 10.f);
                if (m_smallFont) {
                    drawLabel(ren, m_smallFont, rowRect.right() - 8.f - bw + (bw - nw) * 0.5f, cy - 8.f, n, nxui::Color::white().withAlpha(a), 0.7f);
                }
            } else if (r.unread) {
                const float cy = rowRect.y + rowRect.height * 0.5f;
                ren.drawRoundedRect({area.x + 2.f, cy - 4.f, 4.f, 8.f}, nxui::Color::white().withAlpha(a), 2.f);
            }
            m_hits.push_back({static_cast<int>(i), rowRect});
            continue;
        }

        if (focused || r.selected) {
            ren.drawRoundedRect(rowRect, r.danger ? rgb(242, 63, 67, 0.25f * a) : bgSelected(a), 8.f);
            ren.drawRoundedRectOutline(rowRect, {1.f, 1.f, 1.f, 0.18f * a}, 8.f, 1.f);
        }

        const float bw = rowRect.width - 24.f;
        if (!r.detail.empty()) {
            drawLabel(ren, m_font, rowRect.x + 12.f, y + 8.f, fit(m_font, r.text, bw, 0.76f),
                      r.danger ? red(a) : textNormal(a), 0.76f);
            if (m_smallFont) {
                drawLabel(ren, m_smallFont, rowRect.x + 12.f, y + 33.f, fit(m_smallFont, r.detail, bw, 0.70f),
                          textMuted(a), 0.70f);
            }
        } else {
            drawLabel(ren, m_font, rowRect.x + 12.f, y + 12.f, fit(m_font, r.text, bw, 0.76f),
                      r.danger ? red(a) : textNormal(a), 0.76f);
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

    const float av = 36.f;
    const float ax = me.x + 12.f;
    const float ay = me.y + (me.height - av) * 0.5f;
    drawAvatar(ren, u.id != 0 ? u.avatarUrl(64) : "", name, ax, ay, av, a);
    drawStatus(ren, u.id != 0 ? "online" : "offline", ax + av - 4.f, ay + av - 4.f, 5.f, bgUser(a), a);

    float chipW = 0.f;
    drawHint(ren, nxui::Button::Y, T("discord_ui.switch", "Switch"), me.right() - 12.f,
             me.y + (me.height - 26.f) * 0.5f, a, &chipW);
    const float tx = ax + av + 10.f;
    const float tw = me.right() - 24.f - chipW - tx;
    drawLabel(ren, m_font, tx, me.y + 11.f, fit(m_font, clean(name), tw, 0.76f), textNormal(a), 0.76f);
    if (m_smallFont) {
        drawLabel(ren, m_smallFont, tx, me.y + 34.f,
                  fit(m_smallFont, T("discord_ui.status_online", "Online"), tw, 0.70f), textMuted(a), 0.70f);
    }
}

void DiscordPanel::drawQrView(nxui::Renderer& ren, const nxui::Rect& p, float a) {
    const float textW = p.width - 48.f;
    drawLabel(ren, m_font, p.x + 24.f, p.y + 36.f, fit(m_font, T("discord_ui.scan_qr", "Scan QR Code"), textW),
              textNormal(a));
    // The hint is longer than the panel in several languages: wrap it (two
    // lines at most) instead of letting it run past the right edge.
    float hintBottom = p.y + 64.f;
    if (m_smallFont) {
        constexpr float kHintScale = 0.82f;
        const auto lines = wrapLines(m_smallFont,
            T("discord_ui.scan_hint", "Scan with Discord mobile app (User Settings > Scan QR Code)"),
            textW / kHintScale);
        const float lineH = m_smallFont->measure("Ag").y * kHintScale + 2.f;
        float ly = p.y + 64.f;
        for (std::size_t i = 0; i < lines.size() && i < 2; ++i) {
            const std::string line = i == 1 && lines.size() > 2 ? fit(m_smallFont, lines[1] + " ...", textW, kHintScale)
                                                               : fit(m_smallFont, lines[i], textW, kHintScale);
            drawLabel(ren, m_smallFont, p.x + 24.f, ly, line, textMuted(a), kHintScale);
            ly += lineH;
        }
        hintBottom = ly;
    }

    const auto snap = m_service ? m_service->remoteAuth().snapshot() : DiscordRemoteAuth::Snapshot{};
    if (snap.qrSize > 0 && !snap.qrModules.empty()) {
        const float qrBoxSize = 260.f;
        const float qx = p.x + (p.width - qrBoxSize) * 0.5f;
        const float qy = std::max(p.y + 104.f, hintBottom + 22.f);

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
            const float sy = qy + qrBoxSize + 26.f;
            drawLabel(ren, m_smallFont, p.x + 24.f, sy, fit(m_smallFont, status, textW), textNormal(a));
            drawLabel(ren, m_smallFont, p.x + 24.f, sy + 30.f,
                      fit(m_smallFont, T("discord_ui.press_b_cancel", "Press (B) to Cancel"), textW), textMuted(a));
        }
    } else {
        drawLabel(ren, m_font, p.x + 100.f, p.y + 240.f, T("discord_ui.generating_qr", "Generating QR Code..."), textMuted(a));
        if (m_smallFont) {
            drawLabel(ren, m_smallFont, p.x + 24.f, p.y + 430.f, T("discord_ui.press_b_cancel", "Press (B) to Cancel"), textMuted(a));
        }
    }
}

void DiscordPanel::drawChat(nxui::Renderer& ren, const nxui::Rect& p, float a) {
    // Top bar: a back chevron, "#"/name, and the reply button capsule.
    m_backHit = {p.x, p.y, 56.f, kTopH};
    {
        const float cx = p.x + 26.f, cy = p.y + kTopH * 0.5f;
        ren.drawLine({cx + 4.f, cy - 8.f}, {cx - 4.f, cy}, textMuted(a), 2.5f);
        ren.drawLine({cx - 4.f, cy}, {cx + 4.f, cy + 8.f}, textMuted(a), 2.5f);
    }
    float replyW = 0.f;
    drawHint(ren, nxui::Button::X, T("discord_ui.reply", "Reply"), p.right() - 12.f, p.y + (kTopH - 26.f) * 0.5f, a,
             &replyW);
    float tx = p.x + 46.f;
    if (m_chatGuildId != 0) { // a server channel: "# name"
        drawLabel(ren, m_font, tx + 2.f, p.y + 14.f, "#", textMuted(a), 0.86f);
        tx += 24.f;
    }
    drawLabel(ren, m_font, tx, p.y + 16.f, fit(m_font, m_chatName, p.right() - 24.f - replyW - tx, 0.80f),
              textNormal(a), 0.80f);
    ren.drawRect({p.x, p.y + kTopH - 1.f, p.width, 1.f}, {1.f, 1.f, 1.f, 0.08f * a});

    // Message list area
    const float chatY = p.y + kTopH;
    const float chatH = p.height - kTopH - kInputH;
    const nxui::Rect chatArea{p.x + 8.f, chatY, p.width - 16.f, chatH};

    const auto msgs = m_service ? m_service->messages(m_chatId) : std::vector<Message>{};
    const Snowflake meId = m_service ? m_service->currentUser().id : 0;

    if (msgs.empty()) {
        if (m_smallFont) {
            float ly = chatY + 24.f;
            for (const auto& line : wrapLines(m_smallFont, T("discord_ui.no_messages", "No messages yet."),
                                              (p.width - 48.f) / 0.8f)) {
                drawLabel(ren, m_smallFont, p.x + 24.f, ly, line, textMuted(a), 0.8f);
                ly += 22.f;
            }
        }
    } else {
        // One block per message. A call record or a system notice is a card
        // (no author header); a message from the same author within a few
        // minutes continues the previous block without repeating the header,
        // as the phone app does.
        struct MsgLayout {
            enum Kind { Normal, CallCard, System } kind = Normal;
            bool grouped = false;
            std::vector<std::string> lines;
            std::vector<std::string> images;
            std::vector<std::pair<std::string, int>> embedLines; // text, 0 body / 1 title / 2 provider
            std::vector<std::string> files;
            std::string author;
            std::string cardTitle, cardDetail;
            bool missed = false;
            float height = 0.f;
        };
        const float maxTextW = chatArea.width - 70.f;
        std::vector<MsgLayout> layouts(msgs.size());
        float totalMsgH = 16.f;
        for (std::size_t i = 0; i < msgs.size(); ++i) {
            auto& lay = layouts[i];
            const auto& m = msgs[i];
            lay.author = clean(m.author.displayName());
            if (lay.author.empty()) lay.author = "?";
            const auto mt = static_cast<MessageType>(m.type);
            if (mt == MessageType::Call || m.hasCall) {
                lay.kind = MsgLayout::CallCard;
                const bool joined = std::find(m.callParticipants.begin(), m.callParticipants.end(), meId) !=
                                    m.callParticipants.end();
                lay.missed = !joined && m.author.id != meId;
                lay.cardTitle = lay.missed ? T("discord_ui.call_missed", "Missed call")
                                           : T("discord_ui.call_ended", "Call ended");
                const long long t0 = parseIsoUtc(m.timestamp);
                const long long t1 = parseIsoUtc(m.callEndedTimestamp);
                std::string detail;
                if (m.callEndedTimestamp.empty())
                    detail = T("discord_ui.call_ongoing", "In progress");
                else if (t0 >= 0 && t1 >= t0)
                    detail = callDuration(t1 - t0);
                lay.cardDetail = detail + (detail.empty() ? "" : " - ") + formatDiscordTimestamp(m.timestamp);
                lay.height = 58.f;
            } else if (mt == MessageType::RecipientAdd || mt == MessageType::RecipientRemove ||
                       mt == MessageType::ChannelNameChange || mt == MessageType::ChannelIconChange ||
                       mt == MessageType::ChannelPinnedMessage || mt == MessageType::UserJoin) {
                lay.kind = MsgLayout::System;
                switch (mt) {
                case MessageType::RecipientAdd: lay.cardTitle = lay.author + " " + T("discord_ui.sys_added", "added someone to the group"); break;
                case MessageType::RecipientRemove: lay.cardTitle = lay.author + " " + T("discord_ui.sys_left", "left the group"); break;
                case MessageType::ChannelNameChange: lay.cardTitle = lay.author + " " + T("discord_ui.sys_renamed", "changed the name") + ": " + clean(m.content); break;
                case MessageType::ChannelIconChange: lay.cardTitle = lay.author + " " + T("discord_ui.sys_icon", "changed the icon"); break;
                case MessageType::ChannelPinnedMessage: lay.cardTitle = lay.author + " " + T("discord_ui.sys_pinned", "pinned a message"); break;
                default: lay.cardTitle = lay.author + " " + T("discord_ui.sys_joined", "joined the server"); break;
                }
                lay.cardTitle = fit(m_smallFont, lay.cardTitle, chatArea.width - 40.f, 0.78f);
                lay.height = 30.f;
            } else {
                if (i > 0 && layouts[i - 1].kind == MsgLayout::Normal && msgs[i - 1].author.id == m.author.id) {
                    const long long prev = parseIsoUtc(msgs[i - 1].timestamp), cur = parseIsoUtc(m.timestamp);
                    lay.grouped = prev >= 0 && cur >= prev && cur - prev < 7 * 60;
                }
                lay.lines = wrapLines(m_smallFont, clean(cleanText(messageText(m.content))), maxTextW);
                if (!m.sticker.empty())
                    lay.lines.push_back("[" + T("discord_ui.sticker", "Sticker") + ": " + clean(m.sticker) + "]");
                for (const auto& emb : m.embeds) {
                    if (!emb.provider.empty())
                        lay.embedLines.push_back({fit(m_smallFont, clean(emb.provider), maxTextW - 16.f, 0.8f), 2});
                    else if (!emb.author.empty())
                        lay.embedLines.push_back({fit(m_smallFont, clean(emb.author), maxTextW - 16.f, 0.8f), 2});
                    for (const auto& l : wrapLines(m_smallFont, clean(emb.title), maxTextW - 16.f))
                        lay.embedLines.push_back({l, 1});
                    auto desc = wrapLines(m_smallFont, clean(cleanText(messageText(emb.description))), maxTextW - 16.f);
                    if (desc.size() > 4) { desc.resize(4); desc.back() = fit(m_smallFont, desc.back() + " ...", maxTextW - 16.f); }
                    for (const auto& l : desc) lay.embedLines.push_back({l, 0});
                }
                for (const auto& img : m.images) lay.images.push_back(img.url);
                for (const auto& file : m.attachmentUrls) lay.files.push_back(clean(file));
                float h = lay.grouped ? 2.f : 28.f;
                h += static_cast<float>(lay.lines.size()) * 20.f;
                h += static_cast<float>(lay.embedLines.size()) * 20.f + (lay.embedLines.empty() ? 0.f : 8.f);
                h += static_cast<float>(lay.images.size()) * 156.f;
                h += static_cast<float>(lay.files.size()) * 22.f;
                lay.height = lay.grouped ? std::max(h, 22.f) : std::max(h, 46.f);
            }
            totalMsgH += lay.height + (lay.grouped ? 2.f : 8.f);
        }

        const float maxScroll = std::max(0.f, totalMsgH - chatH + 10.f);
        if (m_chatAtBottom || m_chatScroll > maxScroll) m_chatScroll = maxScroll;
        if (m_chatScroll >= maxScroll - 1.f) m_chatAtBottom = true;

        ren.pushClipRect(chatArea);

        float y = chatY + 10.f - m_chatScroll;
        for (std::size_t i = 0; i < msgs.size(); ++i) {
            const auto& m = msgs[i];
            const auto& lay = layouts[i];
            const float mh = lay.height;
            if (y + mh >= chatArea.y - 20.f && y <= chatArea.bottom() + 20.f) {
                if (lay.kind == MsgLayout::CallCard) {
                    const nxui::Rect card{chatArea.x + 8.f, y, chatArea.width - 16.f, mh - 4.f};
                    ren.drawRoundedRect(card, bgTertiary(a), 10.f);
                    // Phone handset: green for a call that happened, red for a missed one.
                    const nxui::Color ic = lay.missed ? red(a) : green(a);
                    const float cx = card.x + 24.f, cy = card.y + card.height * 0.5f;
                    ren.drawCircle({cx, cy}, 13.f, ic.withAlpha(0.22f * a), 20);
                    ren.drawRoundedRect({cx - 8.f, cy - 2.f, 16.f, 5.f}, ic, 2.5f);
                    ren.drawRoundedRect({cx - 9.f, cy - 5.f, 5.f, 8.f}, ic, 2.f);
                    ren.drawRoundedRect({cx + 4.f, cy - 5.f, 5.f, 8.f}, ic, 2.f);
                    drawLabel(ren, m_font, card.x + 48.f, card.y + 6.f,
                              fit(m_font, lay.cardTitle, card.width - 60.f, 0.74f), textNormal(a), 0.74f);
                    if (m_smallFont)
                        drawLabel(ren, m_smallFont, card.x + 48.f, card.y + 30.f,
                                  fit(m_smallFont, lay.cardDetail, card.width - 60.f, 0.70f), textMuted(a), 0.70f);
                    y += mh + 8.f;
                    continue;
                }
                if (lay.kind == MsgLayout::System) {
                    if (m_smallFont)
                        drawLabel(ren, m_smallFont, chatArea.x + 18.f, y + 6.f, lay.cardTitle, textMuted(a), 0.78f);
                    y += mh + 8.f;
                    continue;
                }

                float ly = y + 2.f;
                if (!lay.grouped) {
                    // Avatar
                    const nxui::Texture* avTex = requestImage(m.author.avatarUrl(64), 64);
                    const nxui::Rect avRect{chatArea.x + 8.f, y + 4.f, 34.f, 34.f};
                    if (avTex) {
                        ren.drawTextureRounded(avTex, avRect, 17.f, {1.f, 1.f, 1.f, a});
                    } else {
                        ren.drawCircle({avRect.x + 17.f, avRect.y + 17.f}, 17.f, blurple(a), 24);
                        std::string init = lay.author.substr(0, 1);
                        if (!init.empty() && static_cast<unsigned char>(init[0]) >= 0x80) init = "?";
                        if (m_smallFont)
                            drawLabel(ren, m_smallFont, avRect.x + 12.f, avRect.y + 10.f, init, textNormal(a));
                    }

                    // Author name & time, kept inside the column.
                    const std::string fts = formatDiscordTimestamp(m.timestamp);
                    const float tsW = (m_smallFont && !fts.empty()) ? m_smallFont->measure(fts).x * 0.72f + 10.f : 0.f;
                    const std::string author = fit(m_font, lay.author, maxTextW - tsW, 0.78f);
                    drawLabel(ren, m_font, chatArea.x + 50.f, y + 2.f, author, blurple(a), 0.78f);
                    if (m_smallFont && !fts.empty()) {
                        const float nw = m_font ? m_font->measure(author).x * 0.78f : 80.f;
                        drawLabel(ren, m_smallFont, chatArea.x + 50.f + nw + 10.f, y + 6.f, fts, textMuted(a), 0.72f);
                    }
                    ly = y + 24.f;
                }

                // Content lines
                for (const auto& line : lay.lines) {
                    if (m_smallFont) {
                        drawLabel(ren, m_smallFont, chatArea.x + 50.f, ly, line, textNormal(a));
                    }
                    ly += 20.f;
                }

                // Embeds (link previews): a card with the site, title in blue
                // and the description muted, as on the phone.
                if (!lay.embedLines.empty()) {
                    const float eh = lay.embedLines.size() * 20.f + 8.f;
                    const nxui::Rect ecard{chatArea.x + 46.f, ly, maxTextW + 4.f, eh};
                    ren.drawRoundedRect(ecard, bgTertiary(0.8f * a), 6.f);
                    ren.drawRoundedRect({ecard.x, ecard.y, 3.f, ecard.height}, blurple(a), 1.5f);
                    float ey = ly + 4.f;
                    for (const auto& [line, kind] : lay.embedLines) {
                        const nxui::Color c = kind == 1 ? rgb(0, 168, 252, a) : kind == 2 ? textNormal(a) : textMuted(a);
                        drawLabel(ren, m_smallFont, chatArea.x + 56.f, ey, line, c, kind == 2 ? 0.8f : 1.f);
                        ey += 20.f;
                    }
                    ly += eh;
                }

                // Pictures (attachments and link thumbnails)
                for (const auto& imgUrl : lay.images) {
                    const nxui::Texture* imgTex = requestImage(imgUrl, 320);
                    float bw = std::min(maxTextW, 260.f), bh = 144.f;
                    if (imgTex && imgTex->width() > 0 && imgTex->height() > 0) {
                        const float s = std::min(bw / imgTex->width(), bh / imgTex->height());
                        bw = imgTex->width() * s;
                        bh = imgTex->height() * s;
                    }
                    const nxui::Rect imgBox{chatArea.x + 50.f, ly + 4.f, bw, bh};
                    if (imgTex) {
                        ren.drawTextureRounded(imgTex, imgBox, 8.f, {1.f, 1.f, 1.f, a});
                    } else {
                        ren.drawRoundedRect(imgBox, bgTertiary(a), 8.f);
                        ren.drawRoundedRectOutline(imgBox, {1.f, 1.f, 1.f, 0.12f * a}, 8.f, 1.f);
                        if (m_smallFont) {
                            const bool failed = imageFailed(imgUrl);
                            drawLabel(ren, m_smallFont, imgBox.x + 16.f, imgBox.y + imgBox.height * 0.5f - 8.f,
                                      failed ? T("discord_ui.image_unavailable", "[Image unavailable]")
                                             : T("discord_ui.loading_image", "[Loading image...]"),
                                      textMuted(a));
                        }
                    }
                    ly += 156.f;
                }

                // Other files: their name, the menu cannot open them.
                for (const auto& file : lay.files) {
                    if (m_smallFont)
                        drawLabel(ren, m_smallFont, chatArea.x + 50.f, ly + 2.f,
                                  fit(m_smallFont, "[" + T("discord_ui.file", "File") + "] " + file, maxTextW, 0.8f),
                                  rgb(0, 168, 252, a), 0.8f);
                    ly += 22.f;
                }
            }
            y += mh + (lay.grouped ? 2.f : 8.f);
        }

        ren.popClipRect();
    }

    // Input bar at bottom
    m_replyHit = {p.x + 12.f, p.y + p.height - kInputH + 6.f, p.width - 24.f, 44.f};
    ren.drawRoundedRect(m_replyHit, bgTertiary(a), 8.f);
    ren.drawRoundedRectOutline(m_replyHit, {1.f, 1.f, 1.f, 0.12f * a}, 8.f, 1.f);
    if (m_smallFont) {
        float capW = 0.f;
        drawHint(ren, nxui::Button::A, T("discord_ui.write", "Write"), m_replyHit.right() - 9.f, m_replyHit.y + 9.f, a,
                 &capW);
        const std::string ph = T("discord_ui.message_to", "Message") + (m_chatGuildId != 0 ? " #" : " @") + m_chatName;
        drawLabel(ren, m_smallFont, m_replyHit.x + 14.f, m_replyHit.y + 12.f,
                  fit(m_smallFont, ph, m_replyHit.width - 36.f - capW, 0.78f), textMuted(a), 0.78f);
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
