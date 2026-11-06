#include "cards/CardsSheet.hpp"
#include "cards/CardsChrome.hpp"
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#ifdef SWITCHU_MENU
#include <nxui/core/GpuDevice.hpp>
#endif
#include <algorithm>
#include <cmath>

namespace cards {

namespace {
constexpr float kW = 760.f;
constexpr float kH = 560.f;
constexpr float kX = (1280.f - kW) * 0.5f;
constexpr float kY = (720.f - kH) * 0.5f;
constexpr float kRowH = 54.f;
constexpr int kVisibleRows = 7;
constexpr float kRepeatDelay = 0.35f;
constexpr float kRepeatRate = 0.075f;
} // namespace

void CardsSheet::open(std::string title, std::vector<Section> sections, int section, int row) {
    m_title = std::move(title);
    m_sections = std::move(sections);
    if (m_sections.empty())
        m_sections.push_back({});
    m_section = section;
    m_row = row;
    m_scroll = 0;
    m_reorderSection = {};
    if (!m_open)
        m_in = 0.f;
    m_open = true;
    m_backdropCacheValid = false;
    m_repeatDir = 0;
    clampFocus();
}

void CardsSheet::close() {
    m_open = false;
    m_backdropCacheValid = false;
}

void CardsSheet::clampFocus() {
    m_section = std::clamp(m_section, 0, static_cast<int>(m_sections.size()) - 1);
    const int n = static_cast<int>(m_sections[static_cast<std::size_t>(m_section)].rows.size());
    m_row = n > 0 ? std::clamp(m_row, 0, n - 1) : 0;
    if (m_row < m_scroll)
        m_scroll = m_row;
    if (m_row >= m_scroll + kVisibleRows)
        m_scroll = m_row - kVisibleRows + 1;
    m_scroll = std::max(0, m_scroll);
}

void CardsSheet::moveRow(int dir) {
    const int n = static_cast<int>(m_sections[static_cast<std::size_t>(m_section)].rows.size());
    if (n == 0)
        return;
    const int next = std::clamp(m_row + dir, 0, n - 1);
    if (next != m_row) {
        m_row = next;
        clampFocus();
        if (m_nav) m_nav();
    }
}

bool CardsSheet::handleInput(nxui::Input& in, float dt) {
    if (!m_open)
        return false;
    using B = nxui::Button;
    if (in.isDown(B::B)) {
        m_open = false;
        if (m_onClosed) m_onClosed();
        return true;
    }
    if (m_sections.size() > 1) {
        if (m_reorderSection && (in.isDown(B::X) || in.isDown(B::Y))) {
            auto cb = m_reorderSection;
            cb(in.isDown(B::X) ? -1 : 1);
            return true;
        }
        const int n = static_cast<int>(m_sections.size());
        if (in.isDown(B::L) || in.isDown(B::ZL)) {
            m_section = (m_section + n - 1) % n;
            m_row = 0;
            m_scroll = 0;
            clampFocus();
            if (m_nav) m_nav();
            return true;
        }
        if (in.isDown(B::R) || in.isDown(B::ZR)) {
            m_section = (m_section + 1) % n;
            m_row = 0;
            m_scroll = 0;
            clampFocus();
            if (m_nav) m_nav();
            return true;
        }
    }
    if (in.touchDown()) {
        const float x = in.touchX(), y = in.touchY();
        const float py = kY + 18.f * (1.f-m_in)*(1.f-m_in);
        if (m_sections.size() > 1 && y >= py + 58.f && y <= py + 103.f && x >= kX && x <= kX+kW) {
            const int n = static_cast<int>(m_sections.size());
            m_section = (m_section + (x < 640.f ? n-1 : 1)) % n;
            m_row = m_scroll = 0;
            clampFocus();
            if (m_nav) m_nav();
            return true;
        }
        const float top = py + 78.f + (m_sections.size() > 1 ? 52.f : 0.f);
        if (x >= kX+24.f && x <= kX+kW-24.f && y >= top && y < top+kVisibleRows*kRowH) {
            const int i = m_scroll + static_cast<int>((y-top)/kRowH);
            const auto& list = m_sections[static_cast<std::size_t>(m_section)].rows;
            if (i < static_cast<int>(list.size())) {
                const bool activate = i == m_row;
                m_row = i;
                const Row row = list[static_cast<std::size_t>(i)];
                if (activate && row.enabled) {
                    if (row.onA) row.onA();
                    else if (row.onChange) row.onChange(1);
                } else if (m_nav) m_nav();
                return true;
            }
        }
    }
    auto& rows = m_sections[static_cast<std::size_t>(m_section)].rows;
    const bool up = in.isHeld(B::DUp) || in.isHeld(B::LStickU);
    const bool down = in.isHeld(B::DDown) || in.isHeld(B::LStickD);
    const int dir = up == down ? 0 : (up ? -1 : 1);
    if (dir != 0 && dir != m_repeatDir) {
        m_repeatDir = dir;
        m_repeatTimer = kRepeatDelay;
        moveRow(dir);
    } else if (dir != 0) {
        m_repeatTimer -= dt;
        if (m_repeatTimer <= 0.f) {
            m_repeatTimer = kRepeatRate;
            moveRow(dir);
        }
    } else {
        m_repeatDir = 0;
    }
    if (rows.empty())
        return true;
    // Copy: a handler may reopen the sheet and replace `rows`.
    const Row row = rows[static_cast<std::size_t>(m_row)];
    if (!row.enabled)
        return true;
    const bool l = in.isDown(B::DLeft) || in.isDown(B::LStickL);
    const bool r = in.isDown(B::DRight) || in.isDown(B::LStickR);
    if ((l || r) && row.onChange) {
        if (m_act) m_act();
        row.onChange(l ? -1 : 1);
        return true;
    }
    if (in.isDown(B::A)) {
        if (row.onA) {
            if (m_act) m_act();
            row.onA();
        } else if (row.onChange) {
            if (m_act) m_act();
            row.onChange(1);
        }
    }
    return true;
}

void CardsSheet::update(float dt) {
    if (m_open)
        m_in = std::min(1.f, m_in + dt / 0.18f);
}

void CardsSheet::render(nxui::Renderer& ren, CardsChrome& chrome) const {
    if (!m_open)
        return;
    const float a = 1.f - (1.f - m_in) * (1.f - m_in);
    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, {0.f, 0.f, 0.f, 0.45f * a});
    const nxui::Rect p{kX, kY + 18.f * (1.f - a), kW, kH};

    // Cached backdrop for sheets (HomeNX E8, C-PF-3).
    // Once the sheet has settled (m_in >= 0.99f), capture and blur the backdrop
    // once into OFF_DIALOG instead of recalculating every frame.
    bool drawnGlass = false;
#ifdef SWITCHU_MENU
    if (m_in >= 0.99f && ren.gpu().offscreenReady()) {
        if (!m_backdropCacheValid) {
            ren.captureToOffscreenSharp();
            ren.applyBlur(2.5f, 2);
            ren.copyOffscreen(nxui::GpuDevice::OFF_SHARP_A, nxui::GpuDevice::OFF_DIALOG);
            m_backdropCacheValid = true;
        }
        ren.drawLiquidGlass(nxui::GpuDevice::OFF_DIALOG, p, 30.f, CardsChrome::glassTint(1.f), a, 0.35f);
        drawnGlass = true;
    }
#endif
    if (!drawnGlass) {
        ren.drawRoundedRect(p, {0.08f, 0.09f, 0.13f, 0.92f * a}, 30.f);
    }
    ren.drawRoundedRectOutline(p, {1.f, 1.f, 1.f, 0.16f * a}, 30.f, 1.5f);
    const nxui::Color white = CardsChrome::kWhite.withAlpha(a);
    const nxui::Color dim{0.78f, 0.80f, 0.88f, a};

    auto& tf = chrome.titleFont();
    auto& lf = chrome.labelFont();
    auto& sf = chrome.smallFont();
    chrome.textMid(ren, tf, chrome.fit(tf, m_title, kW - 64.f), p.x + 32.f, p.y + 40.f, white);

    float y = p.y + 78.f;
    if (m_sections.size() > 1) {
        // Section tabs: the current one centred with its neighbours.
        const auto& cur = m_sections[static_cast<std::size_t>(m_section)].title;
        const std::string tab = "L  \u2039  " + cur + "  \u203A  R";
        const float w = lf.measure(tab).x;
        ren.drawRoundedRect({p.x + (kW - w) * 0.5f - 18.f, y - 20.f, w + 36.f, 40.f},
                            CardsChrome::accent(0.28f * a), 20.f);
        chrome.textMid(ren, lf, tab, p.x + (kW - w) * 0.5f, y, white);
        // Position dots.
        const float n = static_cast<float>(m_sections.size());
        const float dx = 14.f;
        const float sx = p.x + kW * 0.5f - (n - 1.f) * dx * 0.5f;
        for (std::size_t i = 0; i < m_sections.size(); ++i)
            ren.drawCircle({sx + static_cast<float>(i) * dx, y + 32.f}, 3.5f,
                           {1.f, 1.f, 1.f, (static_cast<int>(i) == m_section ? 0.9f : 0.3f) * a}, 12);
        y += 52.f;
    }

    const auto& rows = m_sections[static_cast<std::size_t>(m_section)].rows;
    if (rows.empty())
        return;
    const int last = std::min(static_cast<int>(rows.size()), m_scroll + kVisibleRows);
    const float rowsTop = y;
    for (int i = m_scroll; i < last; ++i) {
        const auto& row = rows[static_cast<std::size_t>(i)];
        const nxui::Rect r{p.x + 24.f, y, kW - 48.f, kRowH - 6.f};
        const bool focus = i == m_row;
        if (focus) {
            ren.drawRoundedRect(r, {1.f, 1.f, 1.f, 0.14f * a}, 16.f);
            ren.drawRoundedRectOutline(r, CardsChrome::accent(0.9f * a), 16.f, 2.f);
        }
        const nxui::Color ink = row.enabled ? white : dim.withAlpha(0.5f * a);
        const bool twoLines = !row.detail.empty() || static_cast<bool>(row.progress);
        const float labelY = twoLines ? r.y + 15.f : r.y + r.height * 0.5f;
        std::string value = row.value ? row.value() : std::string();
        if (row.onChange && !value.empty())
            value = "\u2039  " + value + "  \u203A";
        const float vw = value.empty() ? 0.f : sf.measure(value).x;
        chrome.textMid(ren, lf, chrome.fit(lf, row.label, r.width - vw - 48.f), r.x + 16.f, labelY, ink);
        if (!value.empty())
            chrome.textMid(ren, sf, value, r.right() - 16.f - vw, r.y + r.height * 0.5f, ink);
        if (row.progress) {
            const float f = std::clamp(row.progress(), 0.f, 1.f);
            const nxui::Rect bar{r.x + 16.f, r.y + 32.f, std::min(300.f, r.width * 0.5f), 6.f};
            ren.drawRoundedRect(bar, {1.f, 1.f, 1.f, 0.18f * a}, 3.f);
            if (f > 0.f)
                ren.drawRoundedRect({bar.x, bar.y, bar.width * f, bar.height},
                                    (f >= 1.f ? CardsChrome::kGreen : CardsChrome::accent(1.f)).withAlpha(a), 3.f);
        } else if (!row.detail.empty()) {
            chrome.textMid(ren, sf, chrome.fit(sf, row.detail, r.width - vw - 48.f), r.x + 16.f, r.y + 35.f, dim);
        }
        y += kRowH;
    }
    if (m_reorderSection)
        chrome.textMid(ren, sf, "X / Y  < >", p.right()-148.f, p.y+40.f, dim);
    // Scroll hints.
    if (m_scroll > 0)
        ren.drawTriangle({p.x + kW * 0.5f - 8.f, rowsTop - 4.f}, {p.x + kW * 0.5f + 8.f, rowsTop - 4.f},
                         {p.x + kW * 0.5f, rowsTop - 12.f}, dim);
    if (last < static_cast<int>(rows.size()))
        ren.drawTriangle({p.x + kW * 0.5f - 8.f, y + 2.f}, {p.x + kW * 0.5f + 8.f, y + 2.f},
                         {p.x + kW * 0.5f, y + 10.f}, dim);
}

} // namespace cards
