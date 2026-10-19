#include "OverlayDialog.hpp"
#include "settings/SettingsGlassTuning.hpp"
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <sstream>
#include <cstdint>
#include <utility>
#include <cmath>
#include <cstdio>

namespace {

static nxui::Rect scaledRect(const nxui::Rect& rect, float scale) {
    nxui::Rect scaled = rect;
    if (scale >= 0.999f) {
        return scaled;
    }

    float width = scaled.width * scale;
    float height = scaled.height * scale;
    scaled.x += (scaled.width - width) * 0.5f;
    scaled.y += (scaled.height - height) * 0.5f;
    scaled.width = width;
    scaled.height = height;
    return scaled;
}

static bool dateTimeLeapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

static int dateTimeDaysInMonth(int year, int month) {
    static constexpr int days[] = {31, 28, 31, 30, 31, 30,
                                   31, 31, 30, 31, 30, 31};
    month = std::clamp(month, 1, 12);
    return days[month - 1] + ((month == 2 && dateTimeLeapYear(year)) ? 1 : 0);
}

} // namespace


OverlayDialog::OverlayDialog() {
    setFrameworkTouchEnabled(false);
    setVisible(false);
    m_cursor.setBorderWidth(2.6f);
}


std::vector<std::string> OverlayDialog::wrapMessage(nxui::Font* font, const std::string& text,
                                                    float maxWidth, float scale) {
    std::vector<std::string> lines;
    if (!font || text.empty()) return lines;
    // Explicit breaks are content here: release notes are a list, and folding
    // them into one paragraph would lose the shape the author gave them.
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t breakAt = text.find('\n', start);
        const std::string paragraph = text.substr(start, breakAt == std::string::npos
                                                         ? std::string::npos : breakAt - start);
        if (paragraph.empty()) {
            lines.push_back("");
        } else {
            std::istringstream stream(paragraph);
            std::string word, current;
            while (stream >> word) {
                if (current.empty()) { current = word; continue; }
                const std::string candidate = current + " " + word;
                if (font->measure(candidate).x * scale <= maxWidth) current = candidate;
                else { lines.push_back(current); current = word; }
            }
            if (!current.empty()) lines.push_back(current);
        }
        if (breakAt == std::string::npos) break;
        start = breakAt + 1;
    }
    return lines;
}

void OverlayDialog::scrollMessage(int delta) {
    if (!m_messageScrollable) return;
    const int maxScroll = std::max(0, (int)m_messageLines.size() - m_messageVisibleLines);
    const int next = std::clamp(m_messageScroll + delta, 0, maxScroll);
    if (next == m_messageScroll) return;
    m_messageScroll = next;
    if (m_navSfxCb) m_navSfxCb();
}

nxui::Rect OverlayDialog::panelRect() const {
    float panelX = 640.f - m_panelW * 0.5f;
    float panelY = 360.f - m_panelH * 0.5f;
    return {panelX, panelY, m_panelW, m_panelH};
}


void OverlayDialog::buildWidgetTree() {
    m_mode = DialogMode::Buttons;
    clearChildren();
    m_btnWidgets.clear();
    m_buttonFocus.clear();
    m_titleLabel.reset();
    m_messageLabel.reset();
    m_buttonRow.reset();

    nxui::Font* titleFont = m_font;
    nxui::Font* bodyFont  = m_smallFont ? m_smallFont : m_font;

    nxui::Color textPrimary   = m_theme ? m_theme->textPrimary   : nxui::Color::white();
    nxui::Color textSecondary = m_theme ? m_theme->textSecondary
                                        : nxui::Color(0.82f, 0.82f, 0.9f, 1.f);

    const int btnCount = std::max(1, (int)m_buttons.size());
    nxui::Font* labelFont = bodyFont ? bodyFont : titleFont;
    float widestButton = 140.f;
    if (labelFont) {
        for (const auto& button : m_buttons) {
            widestButton = std::max(widestButton,
                                    labelFont->measure(button.label).x * 0.94f
                                        + kButtonLabelPadding * 2.f);
        }
    }

    // A dialog used to be fixed at 560 px. Adding a fourth action to the
    // software menu left each button narrower than a translated label, so the
    // text crossed its edge. Give all actions the room required by the widest
    // label, up to the safe width of the 1280 px layout.
    const float requestedPanelW = widestButton * btnCount
                                + kButtonGap * (btnCount - 1)
                                + kPanelPadX * 2.f;
    m_panelW = std::clamp(requestedPanelW, kPanelW, 1120.f);
    float contentW = m_panelW - kPanelPadX * 2.f;

    float titleH = 0.f;
    if (titleFont && !m_title.empty()) {
        nxui::Label probe(m_title, titleFont);
        probe.setScale(0.92f);
        probe.setMultiline(true);
        titleH = probe.measureWrappedText(contentW).y;
    }
    float msgH   = 0.f;
    m_messageLines.clear();
    m_messageScroll = 0;
    m_messageScrollable = false;
    m_messageVisibleLines = 0;
    if (bodyFont && !m_message.empty()) {
        nxui::Label probe(m_message, bodyFont);
        probe.setScale(0.96f);
        probe.setMultiline(true);
        msgH = probe.measureWrappedText(contentW).y;

        // Past this the panel would run off a 720p screen. Longer text is kept
        // whole and scrolled instead of being truncated.
        constexpr float kMaxMessageH = 300.f;
        if (msgH > kMaxMessageH) {
            m_messageLines = wrapMessage(bodyFont, m_message, contentW, 0.96f);
            m_messageLineHeight = std::max(14.f, bodyFont->measure("Ag").y * 0.96f * 1.12f);
            m_messageVisibleLines = std::max(1, (int)(kMaxMessageH / m_messageLineHeight));
            m_messageScrollable = (int)m_messageLines.size() > m_messageVisibleLines;
            msgH = m_messageVisibleLines * m_messageLineHeight;
        }
    }

    m_panelH = kPanelPadY
             + (titleH > 0.f ? titleH + kTitleMsgGap : 0.f)
             + (msgH   > 0.f ? msgH   + kMsgBtnGap   : 0.f)
             + kButtonH
             + kPanelPadY;

    setAxis(nxui::Axis::COLUMN);
    setRect(panelRect());
    setCornerRadius(kPanelRadius);
    setPadding(kPanelPadY, kPanelPadX, kPanelPadY, kPanelPadX);
    setAlignItems(nxui::AlignItems::STRETCH);
    setBackingEnabled(false);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setWireframeEnabled(false);
    setPanelOpacity(0.94f);
    if (m_theme) {
        setBaseColor(m_theme->panelBase.withAlpha(std::clamp(m_theme->panelBase.a * 0.82f, 0.18f, 0.32f)));
        setBorderColor(m_theme->panelBorder.withAlpha(std::clamp(m_theme->panelBorder.a * 0.95f, 0.14f, 0.36f)));
        setHighlightColor(m_theme->panelHighlight.withAlpha(std::clamp(m_theme->panelHighlight.a * 0.70f, 0.03f, 0.10f)));
    }

    if (titleFont && !m_title.empty()) {
        m_titleLabel = std::make_shared<nxui::Label>(m_title, titleFont);
        m_titleLabel->setTextColor(textPrimary);
        m_titleLabel->setScale(0.92f);
        m_titleLabel->setMultiline(true);
        m_titleLabel->setLineSpacing(1.04f);
        m_titleLabel->setHAlign(nxui::Label::HAlign::Center);
        m_titleLabel->setVAlign(nxui::Label::VAlign::Center);
        m_titleLabel->setRect({0, 0, contentW, titleH});
        m_titleLabel->setMarginBottom(kTitleMsgGap);
        addChild(m_titleLabel);
    }

    if (bodyFont && !m_message.empty() && m_messageLines.empty()) {
        m_messageLabel = std::make_shared<nxui::Label>(m_message, bodyFont);
        m_messageLabel->setTextColor(textSecondary);
        m_messageLabel->setScale(0.96f);
        m_messageLabel->setMultiline(true);
        m_messageLabel->setLineSpacing(1.12f);
        m_messageLabel->setHAlign(nxui::Label::HAlign::Center);
        m_messageLabel->setVAlign(nxui::Label::VAlign::Top);
        m_messageLabel->setRect({0, 0, contentW, msgH});
        m_messageLabel->setMarginBottom(kMsgBtnGap);
        addChild(m_messageLabel);
    } else if (!m_messageLines.empty()) {
        // A spacer holds the reading area open; render() paints into it.
        auto reserved = std::make_shared<nxui::Box>(nxui::Axis::COLUMN);
        reserved->setRect({0, 0, contentW, msgH});
        reserved->setMarginBottom(kMsgBtnGap);
        addChild(reserved);
        m_messageTop = msgH;
    }

    float btnW    = (contentW - kButtonGap * (btnCount - 1)) / (float)btnCount;

    m_buttonRow = std::make_shared<nxui::Box>(nxui::Axis::ROW);
    m_buttonRow->setRect({0, 0, contentW, kButtonH});
    m_buttonRow->setGap(kButtonGap);
    m_buttonRow->setAlignItems(nxui::AlignItems::STRETCH);
    m_buttonRow->setWireframeEnabled(false);

    for (int i = 0; i < btnCount; ++i) {
        auto btn = std::make_shared<ActionButton>();
        btn->setTheme(m_theme);
        btn->setCornerRadius(kButtonRadius);
        btn->setRect({0, 0, btnW, kButtonH});
        btn->setVisualState(1.f, 0.f);
        btn->setWireframeEnabled(false);
        btn->setGrow(1.f);

        auto lbl = std::make_shared<nxui::Label>(m_buttons[i].label);
        lbl->setFont(labelFont);
        lbl->setTextColor(textPrimary);

        // Buttons split the row evenly, so a long label is a label wider than
        // its button -- and the text simply drew past the edge. English hides
        // this ("Cancel", "OK"); "Informações do software" does not, and every
        // translated build was one long word away from the same break.
        //
        // Shrink to fit rather than clip or ellipsize: the whole label still
        // reads, which matters most on the button that deletes something.
        float labelScale = 0.94f;
        if (labelFont && !m_buttons[i].label.empty()) {
            const float available = btnW - kButtonLabelPadding * 2.f;
            const float natural = labelFont->measure(m_buttons[i].label).x;
            if (natural > 0.f && available > 0.f && natural * labelScale > available)
                labelScale = std::max(kButtonLabelMinScale, available / natural);
        }
        lbl->setScale(labelScale);
        lbl->setHAlign(nxui::Label::HAlign::Center);
        lbl->setVAlign(nxui::Label::VAlign::Center);
        lbl->setRect({0, 0, btnW, kButtonH});
        lbl->setGrow(1.f);
        btn->addChild(lbl);

        m_buttonRow->addChild(btn);
        m_btnWidgets.push_back(btn);
    }

    m_buttonFocus.resize((size_t)btnCount);
    for (int i = 0; i < btnCount; ++i)
        m_buttonFocus[(size_t)i].setImmediate(i == m_selected ? 1.f : 0.f);

    addChild(m_buttonRow);

    layout();
}

bool OverlayDialog::loadUsers(nxui::GpuDevice& gpu, nxui::Renderer& ren) {
    m_users.clear();

    AccountUid uids[8] = {};
    s32 count = 0;
    Result rc = accountListAllUsers(uids, 8, &count);
    if (R_FAILED(rc) || count <= 0)
        return false;

    for (int i = 0; i < count; ++i) {
        AccountProfile profile{};
        rc = accountGetProfile(&profile, uids[i]);
        if (R_FAILED(rc))
            continue;

        AccountProfileBase base{};
        AccountUserData userData{};
        rc = accountProfileGet(&profile, &userData, &base);
        if (R_FAILED(rc)) {
            accountProfileClose(&profile);
            continue;
        }

        UserEntry entry;
        entry.uid = uids[i];
        entry.nickname = base.nickname;

        u32 imgSize = 0;
        if (R_SUCCEEDED(accountProfileGetImageSize(&profile, &imgSize)) && imgSize > 0) {
            std::vector<std::uint8_t> imgBuf(imgSize);
            u32 realSize = 0;
            if (R_SUCCEEDED(accountProfileLoadImage(&profile, imgBuf.data(), imgSize, &realSize))
                && realSize > 0) {
                entry.icon.loadFromMemory(gpu, ren, imgBuf.data(), realSize);
            }
        }

        accountProfileClose(&profile);
        m_users.push_back(std::move(entry));
    }

    return !m_users.empty();
}

void OverlayDialog::buildUserSelect() {
    m_mode = DialogMode::UserSelect;
    clearChildren();
    m_btnWidgets.clear();
    m_buttonFocus.clear();
    m_titleLabel.reset();
    m_messageLabel.reset();
    m_buttonRow.reset();

    int count = std::max(1, userSlotCount());
    float titleH = m_font ? m_font->measure("A").y * 1.1f : 28.f;
    float nameH = m_smallFont ? m_smallFont->measure("Ag").y : (m_font ? m_font->measure("Ag").y : 20.f);
    float totalUsersW = count * kUserAvatarSize + (count - 1) * kUserAvatarGap;

    m_panelW = totalUsersW + kPanelPadX * 2.f;
    m_panelH = kPanelPadY * 2.f + titleH + kUserTitleGap + kUserAvatarSize + 12.f + nameH;

    setAxis(nxui::Axis::COLUMN);
    setRect(panelRect());
    setCornerRadius(kPanelRadius);
    setPadding(0.f);
    setAlignItems(nxui::AlignItems::STRETCH);
    setBackingEnabled(false);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setWireframeEnabled(false);
    setPanelOpacity(0.94f);
    if (m_theme) {
        setBaseColor(m_theme->panelBase.withAlpha(std::clamp(m_theme->panelBase.a * 0.82f, 0.18f, 0.32f)));
        setBorderColor(m_theme->panelBorder.withAlpha(std::clamp(m_theme->panelBorder.a * 0.95f, 0.14f, 0.36f)));
        setHighlightColor(m_theme->panelHighlight.withAlpha(std::clamp(m_theme->panelHighlight.a * 0.70f, 0.03f, 0.10f)));
    }

    m_userAvatarRects.resize((size_t)userSlotCount());
}

void OverlayDialog::buildDateTimeEditor() {
    m_mode = DialogMode::DateTime;
    clearChildren();
    m_btnWidgets.clear();
    m_buttonFocus.clear();
    m_titleLabel.reset();
    m_messageLabel.reset();
    m_buttonRow.reset();
    m_panelW = 760.f;
    m_panelH = 300.f;
    setAxis(nxui::Axis::COLUMN);
    setRect(panelRect());
    setCornerRadius(kPanelRadius);
    setPadding(0.f);
    setAlignItems(nxui::AlignItems::STRETCH);
    setBackingEnabled(false);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setWireframeEnabled(false);
    setPanelOpacity(0.94f);
    if (m_theme) {
        setBaseColor(m_theme->panelBase.withAlpha(
            std::clamp(m_theme->panelBase.a * 0.82f, 0.18f, 0.32f)));
        setBorderColor(m_theme->panelBorder.withAlpha(
            std::clamp(m_theme->panelBorder.a * 0.95f, 0.14f, 0.36f)));
        setHighlightColor(m_theme->panelHighlight.withAlpha(
            std::clamp(m_theme->panelHighlight.a * 0.70f, 0.03f, 0.10f)));
    }
}

void OverlayDialog::animateButtonFocus(float duration, nxui::EasingFunc easing) {
    int n = (int)m_buttonFocus.size();
    for (int i = 0; i < n; ++i) {
        float target = (i == m_selected) ? 1.f : 0.f;
        m_buttonFocus[(size_t)i].set(target, duration, easing);
    }
}

void OverlayDialog::currentAccessibilityParts(std::string& context,
                                              std::string& position,
                                              std::string& summary,
                                              bool& forceRepeat) const {
    context.clear();
    position.clear();
    summary.clear();
    forceRepeat = false;

    if (!m_active || m_animatingOut)
        return;

    context = m_title;
    if (!m_message.empty())
        context += (context.empty() ? "" : ". ") + m_message;

    if (m_mode == DialogMode::UserSelect) {
        std::string name;
        if (m_selected >= 0 && m_selected < (int)m_users.size())
            name = m_users[(size_t)m_selected].nickname;
        if (name.empty())
            name = "Profil " + std::to_string(m_selected + 1);
        summary = name;
        if (m_accessibilitySpeakPosition && !m_users.empty())
            position = std::to_string(m_selected + 1) + " sur " + std::to_string((int)m_users.size());
        if (m_accessibilitySpeakHints)
            summary += ". Gauche et droite pour changer de profil. A pour valider. B pour annuler.";
        return;
    }

    if (m_mode == DialogMode::DateTime) {
        auto& i18n = nxui::I18n::instance();
        const std::string labels[5] = {
            i18n.tr("settings.system.year", "Year"),
            i18n.tr("settings.system.month", "Month"),
            i18n.tr("settings.system.day", "Day"),
            i18n.tr("settings.system.hour", "Hour"),
            i18n.tr("settings.system.minute", "Minute"),
        };
        const int values[5] = {m_dateTime.year, m_dateTime.month, m_dateTime.day,
                               m_dateTime.hour, m_dateTime.minute};
        summary = labels[m_dateTimeField] + " " +
                  std::to_string(values[m_dateTimeField]);
        if (m_accessibilitySpeakPosition)
            position = std::to_string(m_dateTimeField + 1) + " / 5";
        if (m_accessibilitySpeakHints)
            summary += ". " + i18n.tr(
                "settings.system.manual_time_popup_hint",
                "Left/right selects a field. Up/down changes its value.");
        forceRepeat = true;
        return;
    }

    if (m_mode == DialogMode::Timezone) {
        auto& i18n = nxui::I18n::instance();
        if (m_timezoneSearchFocused) {
            summary = i18n.tr("settings.system.timezone_filter_option", "Search");
            if (m_accessibilitySpeakHints)
                summary += ". "
                        + i18n.tr("accessibility.settings.companion_actions",
                                  "A to activate. Right for list. B to close.");
        } else if (!m_timezoneOptions.empty()) {
            const int idx = std::clamp(
                m_timezoneHover, 0, (int)m_timezoneOptions.size() - 1);
            summary = m_timezoneOptions[(size_t)idx];
            if (m_accessibilitySpeakPosition)
                position = std::to_string(idx + 1) + " "
                         + i18n.tr("accessibility.context.of", "of") + " "
                         + std::to_string((int)m_timezoneOptions.size());
            if (m_accessibilitySpeakHints)
                summary += ". "
                        + i18n.tr("settings.system.timezone_popup_hint",
                                  "Left for Search. Up/down to choose. A to confirm.");
        }
        return;
    }

    std::string choice;
    if (m_selected >= 0 && m_selected < (int)m_buttons.size())
        choice = m_buttons[(size_t)m_selected].label;

    if (!choice.empty())
        summary = choice;
    if (m_accessibilitySpeakPosition && !m_buttons.empty())
        position = std::to_string(m_selected + 1) + " sur " + std::to_string((int)m_buttons.size());
    if (m_accessibilitySpeakHints) {
        if (!summary.empty()) summary += ". ";
        summary += "Gauche et droite pour changer. A pour valider. B pour annuler.";
    }
}

std::string OverlayDialog::currentAccessibilitySummary() const {
    std::string context;
    std::string position;
    std::string summary;
    bool forceRepeat = false;
    currentAccessibilityParts(context, position, summary, forceRepeat);

    std::string out = context;
    if (!summary.empty())
        out += (out.empty() ? "" : ". ") + summary;
    if (!position.empty())
        out += (out.empty() ? "" : ". ") + position;
    return out;
}

void OverlayDialog::announceCurrentSelection(bool forceRepeatOverride, bool forceContextOverride) {
    if (m_accessibilityStructuredCb) {
        std::string context;
        std::string position;
        std::string summary;
        bool forceRepeat = false;
        currentAccessibilityParts(context, position, summary, forceRepeat);
        forceRepeat = forceRepeat || forceRepeatOverride;
        m_accessibilityStructuredCb(context, position, summary, forceRepeat, forceContextOverride);
        return;
    }
    if (m_accessibilityCb)
        m_accessibilityCb(currentAccessibilitySummary());
}


void OverlayDialog::show(const std::string& title,
                         const std::string& message,
                         std::vector<ButtonDef> buttons,
                         int initialSelected,
                         CancelCallback onCancel) {
    m_title   = title;
    m_message = message;
    m_buttons = std::move(buttons);
    if (m_buttons.empty())
        m_buttons.push_back({"OK", {}, true});

    m_selected = std::clamp(initialSelected, 0, (int)m_buttons.size() - 1);
    m_onCancel = std::move(onCancel);

    m_active       = true;
    m_animatingOut = false;
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;

    buildWidgetTree();
    layout(panelRect());

    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.92f);
    m_contentReveal.setImmediate(0.f);

    m_overlayAlpha.set(1.f,  0.24f, nxui::Easing::outCubic);
    m_panelScale.set(1.f,    0.28f, nxui::Easing::outCubic);
    m_contentReveal.set(1.f, 0.34f, nxui::Easing::outCubic);

    if (m_selected < (int)m_btnWidgets.size()) {
        nxui::Rect br = scaledRect(m_btnWidgets[m_selected]->rect(), m_panelScale.value());
        m_cursor.setCornerRadius(kButtonRadius);
        m_cursor.moveTo(br.expanded(3.f), kButtonRadius, 0.001f);
    }

    animateButtonFocus(0.001f, nxui::Easing::linear);

    setFocusable(true);
    setVisible(true);
    setupActions();

    m_touchHitButton  = -1;
    m_touchHitUser = -1;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
    m_pendingInitialAccessibilityFrames = 2;
}

void OverlayDialog::showUserSwitcher(UserSelectCallback onSelect,
                                     std::function<void()> onCreate,
                                     std::string createLabel,
                                     CancelCallback onCancel) {
    m_allowCreateUser = static_cast<bool>(onCreate);
    m_onCreateUser = std::move(onCreate);
    m_createUserLabel = std::move(createLabel);
    // Unlike the launch flow, this one is worth opening with no accounts at
    // all: creating the first one is exactly what it is for.
    m_title = nxui::I18n::instance().tr("userselect.switch_title", "Accounts");
    m_message.clear();
    m_buttons.clear();
    m_selected = std::clamp(m_selected, 0, std::max(0, userSlotCount() - 1));
    m_onUserSelect = std::move(onSelect);
    m_onCancel = std::move(onCancel);

    m_active = true;
    m_animatingOut = false;
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;

    buildUserSelect();

    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.92f);
    m_contentReveal.setImmediate(0.f);
    m_overlayAlpha.set(1.f, 0.18f, nxui::Easing::outCubic);
    m_panelScale.set(1.f, 0.22f, nxui::Easing::outBack);
    m_contentReveal.set(1.f, 0.22f, nxui::Easing::outCubic);
    m_cursor.setCornerRadius(kUserAvatarSize * 0.5f);
    m_cursor.moveTo(userAvatarRect(m_selected).expanded(4.f), kUserAvatarSize * 0.5f, 0.001f);

    // Missed when this was split off showUserSelect, and the dialog came up
    // reachable by touch alone: the direction actions live in setupUserActions,
    // and without focus nothing would route to them anyway.
    setFocusable(true);
    setVisible(true);
    setupUserActions();

    m_touchHitButton = -1;
    m_touchHitUser = -1;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
    m_pendingInitialAccessibilityFrames = 2;
    announceCurrentSelection();
}

void OverlayDialog::showUserSelect(UserSelectCallback onSelect, CancelCallback onCancel) {
    m_allowCreateUser = false;
    m_onCreateUser = nullptr;
    if (m_users.empty()) {
        if (onSelect) {
            AccountUid uid{};
            onSelect(uid);
        }
        return;
    }

    m_title = nxui::I18n::instance().tr("userselect.title", "Who's playing?");
    m_message.clear();
    m_buttons.clear();
    m_selected = std::clamp(m_selected, 0, std::max(0, userSlotCount() - 1));
    m_onUserSelect = std::move(onSelect);
    m_onCancel = std::move(onCancel);

    m_active = true;
    m_animatingOut = false;
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;

    buildUserSelect();

    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.92f);
    m_contentReveal.setImmediate(0.f);

    m_overlayAlpha.set(1.f, 0.24f, nxui::Easing::outCubic);
    m_panelScale.set(1.f, 0.28f, nxui::Easing::outCubic);
    m_contentReveal.set(1.f, 0.34f, nxui::Easing::outCubic);

    m_cursor.setCornerRadius(kUserAvatarSize * 0.5f);
    m_cursor.moveTo(userAvatarRect(m_selected).expanded(4.f), kUserAvatarSize * 0.5f, 0.001f);

    setFocusable(true);
    setVisible(true);
    setupUserActions();

    m_touchHitButton = -1;
    m_touchHitUser = -1;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
    announceCurrentSelection();
}

void OverlayDialog::showDateTimeEditor(const DateTimeValue& initial,
                                       DateTimeSaveCallback onSave,
                                       CancelCallback onCancel) {
    auto& i18n = nxui::I18n::instance();
    m_title = i18n.tr("settings.system.manual_time", "Set Date and Time");
    m_message = i18n.tr(
        "settings.system.manual_time_popup_hint",
        "Left/right selects a field. Up/down changes its value.");
    m_buttons.clear();
    m_dateTime = initial;
    m_dateTime.year = std::clamp(m_dateTime.year, 2000, 2099);
    m_dateTime.month = std::clamp(m_dateTime.month, 1, 12);
    m_dateTime.day = std::clamp(
        m_dateTime.day, 1, dateTimeDaysInMonth(m_dateTime.year, m_dateTime.month));
    m_dateTime.hour = std::clamp(m_dateTime.hour, 0, 23);
    m_dateTime.minute = std::clamp(m_dateTime.minute, 0, 59);
    m_dateTimeField = 0;
    m_onDateTimeSave = std::move(onSave);
    m_onCancel = std::move(onCancel);
    m_active = true;
    m_animatingOut = false;
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;
    buildDateTimeEditor();
    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.92f);
    m_contentReveal.setImmediate(0.f);
    m_overlayAlpha.set(1.f, 0.24f, nxui::Easing::outCubic);
    m_panelScale.set(1.f, 0.28f, nxui::Easing::outCubic);
    m_contentReveal.set(1.f, 0.34f, nxui::Easing::outCubic);
    setFocusable(true);
    setVisible(true);
    setupDateTimeActions();
    m_touchHitButton = -1;
    m_touchHitUser = -1;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
    m_pendingInitialAccessibilityFrames = 2;
    syncDateTimeCursor();
}

void OverlayDialog::buildTimezonePicker() {
    m_mode = DialogMode::Timezone;
    clearChildren();
    m_btnWidgets.clear();
    m_buttonFocus.clear();
    m_titleLabel.reset();
    m_messageLabel.reset();
    m_buttonRow.reset();

    m_panelW = 760.f;
    // Title + list (7 rows) + padding. No footer strip.
    m_panelH = 78.f + 6.f + kTimezoneVisibleRows * kTimezoneRowH + 6.f + 28.f;
    setAxis(nxui::Axis::COLUMN);
    setRect(panelRect());
    setCornerRadius(kPanelRadius);
    setPadding(0.f);
    setBackingEnabled(false);
    setLiquidGlassEnabled(false);
    setBlurEnabled(false);
    setWireframeEnabled(false);
    setPanelOpacity(0.94f);
    if (m_theme) {
        setBaseColor(m_theme->panelBase.withAlpha(
            m_theme->mode == nxui::ThemeMode::Dark ? 0.95f : 0.96f));
        setBorderColor(m_theme->panelBorder.withAlpha(0.42f));
        setHighlightColor(m_theme->panelHighlight.withAlpha(0.10f));
    }
}

void OverlayDialog::showTimezonePicker(const std::vector<std::string>& zones,
                                       int selectedIndex,
                                       TimezoneSelectCallback onSelect,
                                       TimezoneSearchCallback onSearch,
                                       CancelCallback onCancel) {
    auto& i18n = nxui::I18n::instance();
    m_title = i18n.tr("settings.system.timezone", "Timezone");
    m_message = i18n.tr(
        "settings.system.timezone_popup_hint",
        "Left for Search. Up/down to choose. A to confirm.");
    m_buttons.clear();
    m_timezoneOptions = zones;
    m_timezoneHover = std::clamp(
        selectedIndex, 0, std::max(0, (int)m_timezoneOptions.size() - 1));
    m_timezoneScroll = 0;
    m_timezoneSearchFocused = false;
    ensureTimezoneHoverVisible();
    m_onTimezoneSelect = std::move(onSelect);
    m_onTimezoneSearch = std::move(onSearch);
    m_onCancel = std::move(onCancel);

    m_active = true;
    m_animatingOut = false;
    m_backdropCacheValid = false;
    m_cachedPreBlurRadius = -1.f;
    m_cachedBlurIterations = -1;
    buildTimezonePicker();

    m_overlayAlpha.setImmediate(0.f);
    m_panelScale.setImmediate(0.92f);
    m_contentReveal.setImmediate(0.f);
    m_overlayAlpha.set(1.f, 0.24f, nxui::Easing::outCubic);
    m_panelScale.set(1.f, 0.28f, nxui::Easing::outCubic);
    m_contentReveal.set(1.f, 0.34f, nxui::Easing::outCubic);

    setFocusable(true);
    setVisible(true);
    setupTimezoneActions();
    m_touchHitButton = -1;
    m_touchHitUser = -1;
    m_touchHitTimezone = -1;
    m_touchOnTimezoneSearch = false;
    m_touchOnSelected = false;
    m_ignoreInitialTouchRelease = true;
    m_pendingInitialAccessibilityFrames = 2;
    syncTimezoneCursor();
}

void OverlayDialog::hide() {
    if (!m_active || m_animatingOut) return;

    if (m_closeSfxCb) m_closeSfxCb();

    m_animatingOut = true;
    m_overlayAlpha.set(0.f,   0.20f, nxui::Easing::outCubic);
    m_panelScale.set(0.96f,   0.20f, nxui::Easing::outCubic);
    m_contentReveal.set(0.f,  0.16f, nxui::Easing::outCubic);

    setFocusable(false);
    clearActions();
}


void OverlayDialog::setupActions() {
    clearActions();

    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (!m_active || m_animatingOut || m_buttons.empty()) return;
        int n = (int)m_buttons.size();
        m_selected = (m_selected + n - 1) % n;
        animateButtonFocus(0.16f, nxui::Easing::outCubic);
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection();
    });

    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (!m_active || m_animatingOut || m_buttons.empty()) return;
        int n = (int)m_buttons.size();
        m_selected = (m_selected + 1) % n;
        animateButtonFocus(0.16f, nxui::Easing::outCubic);
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection();
    });

    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (!m_active || m_animatingOut) return;
        scrollMessage(-1);
    });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (!m_active || m_animatingOut) return;
        scrollMessage(1);
    });

    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (!m_active || m_animatingOut) return;
        activateSelected();
    });

    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (!m_active || m_animatingOut) return;
        cancel();
    });
}

void OverlayDialog::setupUserActions() {
    clearActions();

    auto selectPrevious = [this]() {
        if (!m_active || m_animatingOut || userSlotCount() <= 0) return;
        int n = userSlotCount();
        m_selected = (m_selected + n - 1) % n;
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection();
    };

    auto selectNext = [this]() {
        if (!m_active || m_animatingOut || userSlotCount() <= 0) return;
        int n = userSlotCount();
        m_selected = (m_selected + 1) % n;
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection();
    };

    addDirectionAction(nxui::FocusDirection::LEFT, selectPrevious);
    addDirectionAction(nxui::FocusDirection::UP, selectPrevious);
    addDirectionAction(nxui::FocusDirection::RIGHT, selectNext);
    addDirectionAction(nxui::FocusDirection::DOWN, selectNext);

    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (!m_active || m_animatingOut) return;
        activateSelectedUser();
    });

    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (!m_active || m_animatingOut) return;
        cancel();
    });
}

void OverlayDialog::setupDateTimeActions() {
    clearActions();
    addDirectionAction(nxui::FocusDirection::LEFT,
                       [this]() { moveDateTimeField(-1); });
    addDirectionAction(nxui::FocusDirection::RIGHT,
                       [this]() { moveDateTimeField(1); });
    addDirectionAction(nxui::FocusDirection::UP,
                       [this]() { adjustDateTimeField(1); });
    addDirectionAction(nxui::FocusDirection::DOWN,
                       [this]() { adjustDateTimeField(-1); });
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_active && !m_animatingOut) saveDateTime();
    });
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (m_active && !m_animatingOut) cancel();
    });
}

void OverlayDialog::setupTimezoneActions() {
    clearActions();
    addDirectionAction(nxui::FocusDirection::LEFT, [this]() {
        if (!m_active || m_animatingOut || m_timezoneSearchFocused) return;
        m_timezoneSearchFocused = true;
        syncTimezoneCursor();
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection(true, false);
    });
    addDirectionAction(nxui::FocusDirection::RIGHT, [this]() {
        if (!m_active || m_animatingOut || !m_timezoneSearchFocused) return;
        m_timezoneSearchFocused = false;
        syncTimezoneCursor();
        if (m_navSfxCb) m_navSfxCb();
        announceCurrentSelection(true, false);
    });
    addDirectionAction(nxui::FocusDirection::UP, [this]() {
        if (!m_timezoneSearchFocused) moveTimezoneHover(-1);
    });
    addDirectionAction(nxui::FocusDirection::DOWN, [this]() {
        if (!m_timezoneSearchFocused) moveTimezoneHover(1);
    });
    addAction(static_cast<uint64_t>(nxui::Button::A), [this]() {
        if (m_active && !m_animatingOut) activateTimezoneSelection();
    });
    addAction(static_cast<uint64_t>(nxui::Button::B), [this]() {
        if (m_active && !m_animatingOut) cancel();
    });
}

void OverlayDialog::ensureTimezoneHoverVisible() {
    const int total = (int)m_timezoneOptions.size();
    if (total <= 0) {
        m_timezoneHover = 0;
        m_timezoneScroll = 0;
        return;
    }
    m_timezoneHover = std::clamp(m_timezoneHover, 0, total - 1);
    const int visible = std::min(total, kTimezoneVisibleRows);
    if (m_timezoneHover < m_timezoneScroll)
        m_timezoneScroll = m_timezoneHover;
    else if (m_timezoneHover >= m_timezoneScroll + visible)
        m_timezoneScroll = m_timezoneHover - visible + 1;
    m_timezoneScroll = std::clamp(m_timezoneScroll, 0, std::max(0, total - visible));
}

void OverlayDialog::moveTimezoneHover(int direction) {
    if (!m_active || m_animatingOut || m_timezoneOptions.empty() || direction == 0)
        return;
    const int total = (int)m_timezoneOptions.size();
    m_timezoneHover = (m_timezoneHover + direction + total) % total;
    ensureTimezoneHoverVisible();
    syncTimezoneCursor();
    if (m_navSfxCb) m_navSfxCb();
    announceCurrentSelection(true, false);
}

void OverlayDialog::activateTimezoneSelection() {
    if (!m_active || m_animatingOut) return;
    if (m_timezoneSearchFocused) {
        auto cb = std::move(m_onTimezoneSearch);
        if (m_activateSfxCb) m_activateSfxCb();
        hide();
        if (cb) cb();
        return;
    }
    if (m_timezoneOptions.empty()) return;
    const int idx = std::clamp(m_timezoneHover, 0, (int)m_timezoneOptions.size() - 1);
    const std::string zone = m_timezoneOptions[(size_t)idx];
    auto cb = std::move(m_onTimezoneSelect);
    if (m_activateSfxCb) m_activateSfxCb();
    hide();
    if (cb) cb(zone);
}

void OverlayDialog::moveDateTimeField(int direction) {
    if (!m_active || m_animatingOut || direction == 0) return;
    m_dateTimeField = (m_dateTimeField + (direction > 0 ? 1 : 4)) % 5;
    syncDateTimeCursor();
    if (m_navSfxCb) m_navSfxCb();
    announceCurrentSelection();
}

void OverlayDialog::adjustDateTimeField(int direction) {
    if (!m_active || m_animatingOut || direction == 0) return;
    auto wrap = [direction](int value, int lo, int hi) {
        value += direction > 0 ? 1 : -1;
        if (value > hi) value = lo;
        if (value < lo) value = hi;
        return value;
    };
    switch (m_dateTimeField) {
        case 0: m_dateTime.year = wrap(m_dateTime.year, 2000, 2099); break;
        case 1: m_dateTime.month = wrap(m_dateTime.month, 1, 12); break;
        case 2:
            m_dateTime.day = wrap(
                m_dateTime.day, 1,
                dateTimeDaysInMonth(m_dateTime.year, m_dateTime.month));
            break;
        case 3: m_dateTime.hour = wrap(m_dateTime.hour, 0, 23); break;
        case 4: m_dateTime.minute = wrap(m_dateTime.minute, 0, 59); break;
    }
    m_dateTime.day = std::min(
        m_dateTime.day, dateTimeDaysInMonth(m_dateTime.year, m_dateTime.month));
    if (m_navSfxCb) m_navSfxCb();
    announceCurrentSelection(true);
}

void OverlayDialog::saveDateTime() {
    const bool saved = !m_onDateTimeSave || m_onDateTimeSave(m_dateTime);
    if (!saved) return;
    if (m_activateSfxCb) m_activateSfxCb();
    hide();
}

void OverlayDialog::activateSelected() {
    if (m_selected < 0 || m_selected >= (int)m_buttons.size()) return;

    auto btn = m_buttons[m_selected];
    if (!btn.closeOnPress && m_activateSfxCb) m_activateSfxCb();
    if (btn.closeOnPress) hide();
    if (btn.onPress) btn.onPress();
}

void OverlayDialog::activateSelectedUser() {
    if (m_selected < 0 || m_selected >= userSlotCount())
        return;
    if (isCreateSlot(m_selected)) {
        auto cb = m_onCreateUser;
        if (m_activateSfxCb) m_activateSfxCb();
        hide();
        if (cb) cb();
        return;
    }
    AccountUid uid = m_users[(size_t)m_selected].uid;
    auto cb = std::move(m_onUserSelect);
    if (m_activateSfxCb) m_activateSfxCb();
    hide();
    if (cb)
        cb(uid);
}

void OverlayDialog::cancel() {
    auto cb = std::move(m_onCancel);
    hide();
    if (cb) cb();
}


void OverlayDialog::handleTouch(nxui::Input& input) {
    if (!m_active || m_animatingOut) return;

    if (m_mode == DialogMode::Timezone) {
        if (input.touchDown()) {
            if (m_ignoreInitialTouchRelease)
                m_ignoreInitialTouchRelease = false;
            m_touchHitTimezone = -1;
            m_touchOnTimezoneSearch = false;
            m_touchOnSelected = false;
            const float tx = input.touchX();
            const float ty = input.touchY();
            if (timezoneSearchRect().expanded(8.f).contains(tx, ty)) {
                m_touchOnTimezoneSearch = true;
                m_touchOnSelected = m_timezoneSearchFocused;
                return;
            }
            const nxui::Rect list = timezoneListRect();
            if (list.contains(tx, ty)) {
                const float localY = ty - list.y - 6.f;
                const int visual = (int)std::floor(localY / (kTimezoneRowH * m_panelScale.value()));
                const int idx = m_timezoneScroll + visual;
                if (idx >= 0 && idx < (int)m_timezoneOptions.size()) {
                    m_touchHitTimezone = idx;
                    m_touchOnSelected = (!m_timezoneSearchFocused && idx == m_timezoneHover);
                }
            }
        }
        if (input.touchUp()) {
            if (m_ignoreInitialTouchRelease) {
                m_ignoreInitialTouchRelease = false;
                m_touchHitTimezone = -1;
                m_touchOnTimezoneSearch = false;
                return;
            }
            const float dx = std::abs(input.touchDeltaX());
            const float dy = std::abs(input.touchDeltaY());
            if (dx < 20.f && dy < 20.f) {
                if (m_touchOnTimezoneSearch) {
                    if (m_touchOnSelected) {
                        activateTimezoneSelection();
                    } else {
                        m_timezoneSearchFocused = true;
                        syncTimezoneCursor();
                        if (m_navSfxCb) m_navSfxCb();
                        announceCurrentSelection(true, false);
                    }
                } else if (m_touchHitTimezone >= 0) {
                    if (m_touchOnSelected) {
                        m_timezoneSearchFocused = false;
                        m_timezoneHover = m_touchHitTimezone;
                        activateTimezoneSelection();
                    } else {
                        m_timezoneSearchFocused = false;
                        m_timezoneHover = m_touchHitTimezone;
                        ensureTimezoneHoverVisible();
                        syncTimezoneCursor();
                        if (m_navSfxCb) m_navSfxCb();
                        announceCurrentSelection(true, false);
                    }
                } else if (!scaledRect(rect(), m_panelScale.value()).contains(
                               input.touchX(), input.touchY())) {
                    cancel();
                }
            }
            m_touchHitTimezone = -1;
            m_touchOnTimezoneSearch = false;
            m_touchOnSelected = false;
        }
        return;
    }

    if (m_mode == DialogMode::DateTime) {
        if (input.touchDown()) {
            if (m_ignoreInitialTouchRelease)
                m_ignoreInitialTouchRelease = false;
            m_touchHitButton = -1;
            for (int i = 0; i < 5; ++i) {
                if (dateTimeFieldRect(i).expanded(10.f).contains(
                        input.touchX(), input.touchY())) {
                    m_touchHitButton = i;
                    break;
                }
            }
        }
        if (input.touchUp()) {
            if (m_ignoreInitialTouchRelease) {
                m_ignoreInitialTouchRelease = false;
                m_touchHitButton = -1;
                return;
            }
            const float dx = std::abs(input.touchDeltaX());
            const float dy = std::abs(input.touchDeltaY());
            if (dx < 20.f && dy < 20.f) {
                if (m_touchHitButton >= 0) {
                    m_dateTimeField = m_touchHitButton;
                    syncDateTimeCursor();
                    if (m_navSfxCb) m_navSfxCb();
                    announceCurrentSelection(true, false);
                } else if (!scaledRect(rect(), m_panelScale.value()).contains(
                               input.touchX(), input.touchY())) {
                    cancel();
                }
            }
            m_touchHitButton = -1;
        }
        return;
    }

    if (m_mode == DialogMode::UserSelect) {
        if (input.touchDown()) {
            if (m_ignoreInitialTouchRelease)
                m_ignoreInitialTouchRelease = false;

            float tx = input.touchX();
            float ty = input.touchY();
            m_touchHitUser = -1;
            m_touchOnSelected = false;
            for (int i = 0; i < userSlotCount(); ++i) {
                if (userAvatarRect(i).expanded(12.f).contains(tx, ty)) {
                    m_touchHitUser = i;
                    break;
                }
            }
            if (m_touchHitUser >= 0 && m_touchHitUser == m_selected)
                m_touchOnSelected = true;
        }

        if (input.touchUp()) {
            if (m_ignoreInitialTouchRelease) {
                m_ignoreInitialTouchRelease = false;
                m_touchHitUser = -1;
                m_touchOnSelected = false;
                return;
            }

            float dx = std::abs(input.touchDeltaX());
            float dy = std::abs(input.touchDeltaY());
            if (dx < 20.f && dy < 20.f) {
                if (m_touchHitUser >= 0 && m_touchHitUser < userSlotCount()) {
                    if (m_touchOnSelected || m_allowCreateUser) {
                        m_selected = m_touchHitUser;
                        activateSelectedUser();
                    } else {
                        m_selected = m_touchHitUser;
                        if (m_navSfxCb) m_navSfxCb();
                        announceCurrentSelection();
                    }
                } else {
                    float px = input.touchX();
                    float py = input.touchY();
                    if (!scaledRect(rect(), m_panelScale.value()).contains(px, py))
                        cancel();
                }
            }
            m_touchHitUser = -1;
            m_touchOnSelected = false;
        }
        return;
    }

    if (input.touchDown()) {
        if (m_ignoreInitialTouchRelease)
            m_ignoreInitialTouchRelease = false;

        float tx = input.touchX();
        float ty = input.touchY();
        m_touchHitButton  = -1;
        m_touchOnSelected = false;
        for (int i = 0; i < (int)m_btnWidgets.size(); ++i) {
            nxui::Rect buttonRect = scaledRect(m_btnWidgets[i]->rect(), m_panelScale.value()).expanded(10.f);
            if (buttonRect.contains(tx, ty)) {
                m_touchHitButton = i;
                break;
            }
        }
        if (m_touchHitButton >= 0 && m_touchHitButton == m_selected)
            m_touchOnSelected = true;
    }

    if (input.touchUp()) {
        if (m_ignoreInitialTouchRelease) {
            m_ignoreInitialTouchRelease = false;
            m_touchHitButton  = -1;
            m_touchOnSelected = false;
            return;
        }

        float dx = std::abs(input.touchDeltaX());
        float dy = std::abs(input.touchDeltaY());
        if (dx < 20.f && dy < 20.f) {
            if (m_touchHitButton >= 0 && m_touchHitButton < (int)m_buttons.size()) {
                if (m_touchOnSelected) {
                    activateSelected();
                } else {
                    m_selected = m_touchHitButton;
                    animateButtonFocus(0.16f, nxui::Easing::outCubic);
                    if (m_navSfxCb) m_navSfxCb();
                    announceCurrentSelection();
                }
            } else {
                float px = input.touchX();
                float py = input.touchY();
                if (!scaledRect(rect(), m_panelScale.value()).contains(px, py))
                    cancel();
            }
        }
        m_touchHitButton  = -1;
        m_touchOnSelected = false;
    }
}


void OverlayDialog::syncCursor() {
    if (m_mode == DialogMode::DateTime) {
        syncDateTimeCursor();
        return;
    }
    if (m_mode == DialogMode::Timezone) {
        syncTimezoneCursor();
        return;
    }
    if (m_mode == DialogMode::UserSelect) {
        syncUserCursor();
        return;
    }

    if (m_selected >= 0 && m_selected < (int)m_btnWidgets.size()) {
        nxui::Rect br = scaledRect(m_btnWidgets[m_selected]->rect(), m_panelScale.value());
        m_cursor.moveTo(br.expanded(3.f), kButtonRadius, 0.16f);
    }
    if (m_theme)
        m_cursor.setColor(m_theme->cursorNormal);
    m_cursor.setOpacity(m_overlayAlpha.value());
}

void OverlayDialog::syncDateTimeCursor() {
    const nxui::Rect field = dateTimeFieldRect(m_dateTimeField);
    m_cursor.setCornerRadius(12.f * m_panelScale.value());
    m_cursor.moveTo(field.expanded(4.f), 12.f * m_panelScale.value(), 0.14f);
    if (m_theme)
        m_cursor.setColor(m_theme->cursorNormal);
    m_cursor.setOpacity(m_overlayAlpha.value());
}

void OverlayDialog::syncTimezoneCursor() {
    const float sc = m_panelScale.value();
    nxui::Rect target = m_timezoneSearchFocused
        ? timezoneSearchRect()
        : timezoneRowRect(m_timezoneHover - m_timezoneScroll);
    // Keep the focus ring inside the list frame at the bottom edge.
    if (!m_timezoneSearchFocused) {
        const nxui::Rect list = timezoneListRect().shrunk(2.f * sc);
        target = target.expanded(2.f);
        if (target.bottom() > list.bottom())
            target.y -= (target.bottom() - list.bottom());
        if (target.y < list.y)
            target.y = list.y;
        if (target.height > list.height)
            target.height = list.height;
        m_cursor.setCornerRadius(12.f * sc);
        m_cursor.moveTo(target, 12.f * sc, 0.14f);
    } else {
        m_cursor.setCornerRadius(14.f * sc);
        m_cursor.moveTo(target.expanded(3.f), 14.f * sc, 0.14f);
    }
    if (m_theme) m_cursor.setColor(m_theme->cursorNormal);
    m_cursor.setOpacity(m_overlayAlpha.value());
}

nxui::Rect OverlayDialog::timezoneSearchRect() const {
    const nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    const float sc = m_panelScale.value();
    const nxui::Rect list = timezoneListRect();
    return {
        panel.x + kPanelPadX * sc,
        list.y,
        kTimezoneSearchW * sc,
        kButtonH * sc
    };
}

nxui::Rect OverlayDialog::timezoneListRect() const {
    const nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    const float sc = m_panelScale.value();
    const float left = panel.x + (kPanelPadX + kTimezoneSearchW + 14.f) * sc;
    const float top = panel.y + 78.f * sc;
    const float rightPad = kPanelPadX * sc;
    // Exact fit for visible rows + inner padding so the bottom highlight
    // never clips through the list frame.
    const float listH =
        (6.f + kTimezoneVisibleRows * kTimezoneRowH + 6.f) * sc;
    return {
        left,
        top,
        std::max(0.f, panel.right() - rightPad - left),
        listH
    };
}

nxui::Rect OverlayDialog::timezoneRowRect(int visualRow) const {
    const nxui::Rect list = timezoneListRect();
    const float sc = m_panelScale.value();
    return {
        list.x + 8.f * sc,
        list.y + 6.f * sc + visualRow * kTimezoneRowH * sc,
        std::max(0.f, list.width - 16.f * sc),
        (kTimezoneRowH - 4.f) * sc
    };
}

void OverlayDialog::renderTimezoneContent(nxui::Renderer& ren, float alpha) {
    if (alpha <= 0.01f) return;
    const nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    const float sc = m_panelScale.value();
    const float contentAlpha = alpha * m_contentReveal.value();
    nxui::Font* bodyFont = m_smallFont ? m_smallFont : m_font;
    const nxui::Color primary = m_theme ? m_theme->textPrimary : nxui::Color::white();
    const nxui::Color secondary = m_theme ? m_theme->textSecondary
        : nxui::Color(0.82f, 0.82f, 0.9f, 1.f);
    const nxui::Color accent = m_theme ? m_theme->cursorNormal : nxui::Color::white();
    auto& i18n = nxui::I18n::instance();

    if (m_font) {
        ren.drawText(m_title,
            {panel.x + kPanelPadX * sc, panel.y + 26.f * sc},
            m_font, primary.withAlpha(contentAlpha), 0.92f * sc);
    }

    const nxui::Rect search = timezoneSearchRect();
    const bool searchHot = m_timezoneSearchFocused;
    nxui::Color searchFill = m_theme
        ? m_theme->panelBase.withAlpha(searchHot ? 0.98f : 0.90f)
        : nxui::Color(0.15f, 0.17f, 0.23f, searchHot ? 0.98f : 0.90f);
    const nxui::Color searchBorder = searchHot ? accent :
        (m_theme ? m_theme->panelBorder : nxui::Color::white());
    ren.drawFrostedInset(search, searchFill,
                         searchBorder.withAlpha(searchHot ? 0.75f : 0.30f),
                         primary.withAlpha(searchHot ? 0.14f : 0.06f),
                         14.f * sc, contentAlpha);
    if (bodyFont) {
        const std::string searchLabel = i18n.tr(
            "settings.system.timezone_filter_option", "Search");
        const nxui::Vec2 tsz = bodyFont->measure(searchLabel);
        ren.drawText(
            searchLabel,
            {search.x + (search.width - tsz.x * 0.84f * sc) * 0.5f,
             search.y + (search.height - tsz.y * 0.84f * sc) * 0.5f},
            bodyFont, primary.withAlpha(contentAlpha), 0.84f * sc);
    }

    const nxui::Rect list = timezoneListRect();
    nxui::Color listFill = m_theme
        ? m_theme->panelBase.withAlpha(0.55f)
        : nxui::Color(0.10f, 0.12f, 0.16f, 0.55f);
    ren.drawRoundedRect(list, listFill.withAlpha(listFill.a * contentAlpha), 16.f * sc);
    ren.drawRoundedRectOutline(
        list,
        (m_theme ? m_theme->panelBorder : nxui::Color::white()).withAlpha(0.35f * contentAlpha),
        16.f * sc, 1.2f);

    const int total = (int)m_timezoneOptions.size();
    const int visible = std::min(total, kTimezoneVisibleRows);
    ren.pushClipRect(list.shrunk(2.f * sc));
    for (int i = 0; i < visible; ++i) {
        const int idx = m_timezoneScroll + i;
        if (idx < 0 || idx >= total) continue;
        const nxui::Rect row = timezoneRowRect(i);
        const bool hovered = !m_timezoneSearchFocused && idx == m_timezoneHover;
        if (hovered) {
            ren.drawRoundedRect(
                row,
                accent.withAlpha(0.18f * contentAlpha),
                12.f * sc);
            ren.drawRoundedRectOutline(
                row, accent.withAlpha(0.45f * contentAlpha), 12.f * sc, 1.2f);
        }
        if (bodyFont) {
            std::string text = m_timezoneOptions[(size_t)idx];
            const float maxW = std::max(0.f, row.width - 20.f * sc);
            while (!text.empty() && bodyFont->measure(text).x * 0.82f * sc > maxW)
                text.pop_back();
            if (text.size() < m_timezoneOptions[(size_t)idx].size() && text.size() > 3)
                text = text.substr(0, text.size() - 3) + "...";
            const nxui::Vec2 tsz = bodyFont->measure(text);
            ren.drawText(
                text,
                {row.x + 12.f * sc,
                 row.y + (row.height - tsz.y * 0.82f * sc) * 0.5f},
                bodyFont,
                (hovered ? primary : secondary).withAlpha(contentAlpha),
                0.82f * sc);
        }
    }
    ren.popClipRect();

    if (total > visible) {
        const float railH = std::max(1.f, list.height - 20.f * sc);
        const float thumbH = std::max(24.f * sc, railH * ((float)visible / (float)total));
        const float maxStart = (float)std::max(1, total - visible);
        const float thumbY = list.y + 10.f * sc
            + (railH - thumbH) * ((float)m_timezoneScroll / maxStart);
        ren.drawRoundedRect(
            {list.right() - 10.f * sc, list.y + 10.f * sc, 3.f * sc, railH},
            (m_theme ? m_theme->panelBorder : nxui::Color::white()).withAlpha(0.22f * contentAlpha),
            1.5f * sc);
        ren.drawRoundedRect(
            {list.right() - 10.5f * sc, thumbY, 4.f * sc, thumbH},
            accent.withAlpha(0.55f * contentAlpha),
            2.f * sc);
    }
}

void OverlayDialog::syncUserCursor() {
    if (m_selected >= 0 && m_selected < userSlotCount()) {
        nxui::Rect r = userAvatarRect(m_selected);
        m_cursor.moveTo(r.expanded(4.f), r.width * 0.5f, 0.16f);
    }
    if (m_theme)
        m_cursor.setColor(m_theme->cursorNormal);
    m_cursor.setOpacity(m_overlayAlpha.value());
}

void OverlayDialog::syncChildOpacities() {
    if (m_mode == DialogMode::DateTime) {
        const float alpha = m_overlayAlpha.value();
        setScale(m_panelScale.value());
        setPanelOpacity(alpha);
        setRect(panelRect());
        return;
    }
    if (m_mode == DialogMode::UserSelect) {
        syncUserOpacities();
        return;
    }

    float alpha  = m_overlayAlpha.value();
    float reveal = m_contentReveal.value();
    float sc     = m_panelScale.value();

    setScale(sc);
    setPanelOpacity(alpha);
    setRect(panelRect());

    int idx = 0;
    auto applyStagger = [&](nxui::Widget* w) {
        if (!w) return;
        float delay = std::min(0.5f, idx * 0.08f);
        float local = std::clamp((reveal - delay) / 0.25f, 0.f, 1.f);
        w->setOpacity(alpha * local);
        ++idx;
    };

    if (m_titleLabel)   applyStagger(m_titleLabel.get());
    if (m_messageLabel) applyStagger(m_messageLabel.get());
    for (auto& btn : m_btnWidgets) {
        applyStagger(btn.get());
        for (auto& c : btn->children())
            c->setOpacity(btn->opacity());
    }

    for (int i = 0; i < (int)m_btnWidgets.size(); ++i) {
        float focus = (i < (int)m_buttonFocus.size()) ? m_buttonFocus[(size_t)i].value() : 0.f;
        focus = std::clamp(focus, 0.f, 1.f);
        m_btnWidgets[i]->setTheme(m_theme);
        m_btnWidgets[i]->setVisualState(alpha, focus);
    }
}

void OverlayDialog::syncUserOpacities() {
    float alpha = m_overlayAlpha.value();
    float sc = m_panelScale.value();
    setScale(sc);
    setPanelOpacity(alpha);
    setRect(panelRect());
}

nxui::Rect OverlayDialog::userAvatarRect(int index) const {
    int n = userSlotCount();
    if (n <= 0 || index < 0 || index >= n)
        return {};

    nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    float sc = m_panelScale.value();
    float titleH = m_font ? m_font->measure("A").y * 1.1f : 28.f;
    float totalUsersW = n * kUserAvatarSize + (n - 1) * kUserAvatarGap;
    float startX = panel.x + (panel.width - totalUsersW * sc) * 0.5f;
    float startY = panel.y + (kPanelPadY + titleH + kUserTitleGap) * sc;
    return {
        startX + index * (kUserAvatarSize + kUserAvatarGap) * sc,
        startY,
        kUserAvatarSize * sc,
        kUserAvatarSize * sc,
    };
}

nxui::Rect OverlayDialog::dateTimeFieldRect(int index) const {
    if (index < 0 || index >= 5) return {};
    const nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    const float sc = m_panelScale.value();
    static constexpr float widths[5] = {132.f, 86.f, 86.f, 86.f, 86.f};
    constexpr float gap = 20.f;
    float totalWidth = gap * 4.f;
    for (float width : widths) totalWidth += width;
    float x = panel.x + (panel.width - totalWidth * sc) * 0.5f;
    for (int i = 0; i < index; ++i)
        x += (widths[i] + gap) * sc;
    return {x, panel.y + 112.f * sc, widths[index] * sc, 72.f * sc};
}

void OverlayDialog::renderDateTimeContent(nxui::Renderer& ren, float alpha) {
    if (alpha <= 0.01f) return;
    const nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    const float sc = m_panelScale.value();
    const float contentAlpha = alpha * m_contentReveal.value();
    nxui::Font* bodyFont = m_smallFont ? m_smallFont : m_font;
    const nxui::Color primary = m_theme ? m_theme->textPrimary : nxui::Color::white();
    const nxui::Color secondary = m_theme ? m_theme->textSecondary
        : nxui::Color(0.82f, 0.82f, 0.9f, 1.f);
    const nxui::Color accent = m_theme ? m_theme->cursorNormal : nxui::Color::white();
    auto& i18n = nxui::I18n::instance();

    if (m_font) {
        const nxui::Vec2 titleSize = m_font->measure(m_title);
        ren.drawText(m_title,
            {panel.x + (panel.width - titleSize.x * 0.92f * sc) * 0.5f,
             panel.y + 28.f * sc},
            m_font, primary.withAlpha(contentAlpha), 0.92f * sc);
    }
    if (bodyFont) {
        const nxui::Vec2 hintSize = bodyFont->measure(m_message);
        const float hintScale = std::min(0.72f * sc,
            hintSize.x > 0.f ? (panel.width - 80.f * sc) / hintSize.x : 0.72f * sc);
        ren.drawText(m_message,
            {panel.x + (panel.width - hintSize.x * hintScale) * 0.5f,
             panel.y + 70.f * sc},
            bodyFont, secondary.withAlpha(contentAlpha), hintScale);
    }

    const std::string labels[5] = {
        i18n.tr("settings.system.year", "Year"),
        i18n.tr("settings.system.month", "Month"),
        i18n.tr("settings.system.day", "Day"),
        i18n.tr("settings.system.hour", "Hour"),
        i18n.tr("settings.system.minute", "Minute"),
    };
    char values[5][8]{};
    std::snprintf(values[0], sizeof(values[0]), "%04d", m_dateTime.year);
    std::snprintf(values[1], sizeof(values[1]), "%02d", m_dateTime.month);
    std::snprintf(values[2], sizeof(values[2]), "%02d", m_dateTime.day);
    std::snprintf(values[3], sizeof(values[3]), "%02d", m_dateTime.hour);
    std::snprintf(values[4], sizeof(values[4]), "%02d", m_dateTime.minute);

    for (int i = 0; i < 5; ++i) {
        const nxui::Rect field = dateTimeFieldRect(i);
        const bool selected = i == m_dateTimeField;
        const nxui::Color base = m_theme
            ? m_theme->panelBase.withAlpha(selected ? 0.98f : 0.88f)
            : nxui::Color(0.15f, 0.17f, 0.23f, selected ? 0.98f : 0.88f);
        const nxui::Color border = selected ? accent :
            (m_theme ? m_theme->panelBorder : nxui::Color::white());
        ren.drawFrostedInset(field, base, border.withAlpha(selected ? 0.72f : 0.28f),
                            primary.withAlpha(selected ? 0.14f : 0.06f),
                            17.f * sc, contentAlpha);
        if (bodyFont) {
            const nxui::Vec2 labelSize = bodyFont->measure(labels[i]);
            const float labelScale = 0.60f * sc;
            ren.drawText(labels[i],
                {field.x + (field.width - labelSize.x * labelScale) * 0.5f,
                 field.y - 23.f * sc}, bodyFont,
                secondary.withAlpha(contentAlpha), labelScale);
        }
        if (m_font) {
            const nxui::Vec2 valueSize = m_font->measure(values[i]);
            const float valueScale = 1.05f * sc;
            ren.drawText(values[i],
                {field.x + (field.width - valueSize.x * valueScale) * 0.5f,
                 field.y + (field.height - valueSize.y * valueScale) * 0.5f},
                m_font, primary.withAlpha(contentAlpha), valueScale);
        }
        if (selected) {
            const nxui::Vec2 center{field.x + field.width * 0.5f, field.y};
            ren.drawTriangle({center.x, center.y - 15.f * sc},
                             {center.x - 6.f * sc, center.y - 7.f * sc},
                             {center.x + 6.f * sc, center.y - 7.f * sc},
                             accent.withAlpha(contentAlpha));
            const float bottom = field.bottom();
            ren.drawTriangle({center.x, bottom + 15.f * sc},
                             {center.x - 6.f * sc, bottom + 7.f * sc},
                             {center.x + 6.f * sc, bottom + 7.f * sc},
                             accent.withAlpha(contentAlpha));
        }
    }

    if (bodyFont) {
        const std::string footer = i18n.tr(
            "settings.system.manual_time_footer", "A Confirm   ·   B Cancel");
        const nxui::Vec2 footerSize = bodyFont->measure(footer);
        const float footerScale = 0.68f * sc;
        ren.drawText(footer,
            {panel.x + (panel.width - footerSize.x * footerScale) * 0.5f,
             panel.bottom() - 42.f * sc}, bodyFont,
            secondary.withAlpha(contentAlpha), footerScale);
    }
}

void OverlayDialog::renderUserContent(nxui::Renderer& ren, float alpha) {
    if (alpha <= 0.01f)
        return;

    nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());
    float sc = m_panelScale.value();
    float reveal = m_contentReveal.value();
    float contentAlpha = alpha * reveal;

    nxui::Color textPrimary = m_theme ? m_theme->textPrimary : nxui::Color::white();
    nxui::Color textSecondary = m_theme ? m_theme->textSecondary : nxui::Color(0.82f, 0.82f, 0.9f, 1.f);

    if (m_font && !m_title.empty()) {
        nxui::Vec2 titleSz = m_font->measure(m_title);
        float tx = panel.x + (panel.width - titleSz.x * sc) * 0.5f;
        float ty = panel.y + kPanelPadY * sc;
        ren.drawText(m_title, {tx, ty}, m_font, textPrimary.withAlpha(contentAlpha), sc);
    }

    nxui::Font* nameFont = m_smallFont ? m_smallFont : m_font;
    m_userAvatarRects.resize((size_t)userSlotCount());

    if (m_allowCreateUser) {
        // The slot past the last account: a ring with a plus through it, the
        // same shape the system's own selector uses so it reads as the same
        // affordance rather than another account.
        const int idx = (int)m_users.size();
        nxui::Rect avatar = userAvatarRect(idx);
        m_userAvatarRects[(size_t)idx] = avatar;

        const float selected = (idx == m_selected) ? 1.f : 0.f;
        nxui::Rect drawRect = scaledRect(avatar, 1.f + 0.08f * selected);
        const float r = drawRect.width * 0.5f;
        const nxui::Vec2 c{drawRect.x + r, drawRect.y + r};

        // Drawn rather than an image. The artwork version was tried and read
        // worse at every size: a photographic plus among photographic faces
        // competes with them, where a ring and a cross plainly is not a face.
        ren.drawCircle(c, r, textSecondary.withAlpha(0.16f * contentAlpha), 40);
        const float ring = std::max(1.5f, r * 0.055f);
        ren.drawRoundedRectOutline(drawRect, textSecondary.withAlpha(0.55f * contentAlpha), r, ring);

        const float arm = r * 0.42f;
        const float bar = std::max(2.f, r * 0.10f);
        nxui::Color plus = textPrimary.withAlpha(0.90f * contentAlpha);
        ren.drawRoundedRect({c.x - arm, c.y - bar * 0.5f, arm * 2.f, bar}, plus, bar * 0.5f);
        ren.drawRoundedRect({c.x - bar * 0.5f, c.y - arm, bar, arm * 2.f}, plus, bar * 0.5f);

        if (nameFont && !m_createUserLabel.empty()) {
            const float nameScale = 0.85f * sc;
            nxui::Vec2 sz = nameFont->measure(m_createUserLabel);
            ren.drawText(m_createUserLabel,
                         {avatar.x + (avatar.width - sz.x * nameScale) * 0.5f,
                          avatar.bottom() + 12.f * sc},
                         nameFont, textSecondary.withAlpha(contentAlpha), nameScale);
        }
    }
    for (int i = 0; i < (int)m_users.size(); ++i) {
        nxui::Rect avatar = userAvatarRect(i);
        m_userAvatarRects[(size_t)i] = avatar;

        float selected = (i == m_selected) ? 1.f : 0.f;
        float visualScale = 1.f + 0.08f * selected;
        nxui::Rect drawRect = scaledRect(avatar, visualScale);
        if (m_users[(size_t)i].icon.valid()) {
            ren.drawTextureRounded(&m_users[(size_t)i].icon,
                                   drawRect,
                                   drawRect.width * 0.5f,
                                   nxui::Color::white().withAlpha(contentAlpha));
        } else {
            float r = drawRect.width * 0.5f;
            ren.drawCircle({drawRect.x + r, drawRect.y + r}, r,
                           textSecondary.withAlpha(0.35f * contentAlpha), 32);
        }

        if (nameFont && !m_users[(size_t)i].nickname.empty()) {
            float nameScale = 0.85f * sc;
            nxui::Vec2 nameSz = nameFont->measure(m_users[(size_t)i].nickname);
            float maxW = avatar.width;
            float tx = avatar.x + (maxW - std::min(nameSz.x * nameScale, maxW)) * 0.5f;
            float ty = avatar.bottom() + 8.f * sc;
            ren.pushClipRect({avatar.x, ty - 2.f * sc, maxW, nameSz.y * nameScale + 4.f * sc});
            ren.drawText(m_users[(size_t)i].nickname,
                         {tx, ty},
                         nameFont,
                         (i == m_selected ? textPrimary : textSecondary).withAlpha(contentAlpha),
                         nameScale);
            ren.popClipRect();
        }
    }
}


void OverlayDialog::update(float dt) {
    if (!m_active && !m_animatingOut) return;

    if (m_animatingOut && m_overlayAlpha.value() < 0.01f) {
        m_active       = false;
        m_animatingOut = false;
        setVisible(false);
        return;
    }

    syncChildOpacities();
    syncCursor();

    if (m_pendingInitialAccessibilityFrames > 0) {
        --m_pendingInitialAccessibilityFrames;
        if (m_pendingInitialAccessibilityFrames == 0)
            announceCurrentSelection(true, true);
    }

    if (m_mode == DialogMode::Buttons) {
        for (auto& c : children())
            c->update(dt);
    }
    m_cursor.update(dt);
}

void OverlayDialog::renderGlassPanel(nxui::Renderer& ren,
                                     const nxui::Theme* theme,
                                     const nxui::Rect& panel,
                                     float radius,
                                     const nxui::Color& base,
                                     const nxui::Color& border,
                                     const nxui::Color& highlight,
                                     float alpha,
                                     int backdropTarget) {
    if (alpha <= 0.01f)
        return;

    const auto& tuning = settings::debug::settingsGlassTuning();
    nxui::LiquidGlassSettings savedGlass = ren.liquidGlassSettings();
    auto& glass = ren.liquidGlassSettings();
    glass.refractionIntensity = std::clamp(tuning.refractionIntensity, 0.0f, 1.5f);
    glass.blurIntensity = std::max(0.0f, tuning.shaderBlurIntensity);
    glass.noiseIntensity = 0.0f;
    glass.glowIntensity = std::max(0.0f, tuning.glowIntensity);
    glass.saturation = std::max(0.0f, tuning.saturation);
    glass.opacityMultiplier = 1.0f;
    glass.roughness = std::max(0.0f, tuning.roughness);
    glass.powerFactor = std::max(1.001f, tuning.powerFactor);

    nxui::Color glassTint = theme
        ? theme->panelBase.withAlpha(theme->mode == nxui::ThemeMode::Dark
            ? std::clamp(tuning.tintAlphaDark, 0.0f, 1.0f)
            : std::clamp(tuning.tintAlphaLight, 0.0f, 1.0f))
        : base.withAlpha(0.14f);
    nxui::Rect glassRect = panel.shrunk(std::max(0.0f, tuning.inset));
    float glassRadius = std::max(12.0f, radius - std::max(0.0f, tuning.inset) * 0.5f);

    ren.drawLiquidGlass(backdropTarget,
                        glassRect,
                        glassRadius,
                        glassTint,
                        alpha,
                        std::clamp(tuning.shade, 0.0f, 1.0f));
    ren.drawRoundedRectOutline(glassRect,
                               border.withAlpha(std::clamp(border.a * 0.90f, 0.14f, 0.34f) * alpha),
                               glassRadius,
                               1.2f);
    ren.drawRoundedRectOutline(glassRect.shrunk(1.5f),
                               highlight.withAlpha(std::clamp(highlight.a * 0.90f, 0.04f, 0.10f) * alpha),
                               std::max(0.0f, glassRadius - 1.5f),
                               1.0f);
    ren.liquidGlassSettings() = savedGlass;
}

void OverlayDialog::renderScrollingMessage(nxui::Renderer& ren, const nxui::Rect& panel, float alpha) {
    if (!m_smallFont && !m_font) return;
    nxui::Font* bodyFont = m_smallFont ? m_smallFont : m_font;
    const float scaleNow = scale();
    const float padX = kPanelPadX * scaleNow;
    const float lineH = m_messageLineHeight * scaleNow;
    const float areaH = m_messageVisibleLines * lineH;
    // Sits under the title, above the button row, matching the space the spacer
    // child reserved in the column.
    const float areaY = panel.bottom() - (kPanelPadY + kButtonH + kMsgBtnGap) * scaleNow - areaH;
    const nxui::Rect area = {panel.x + padX, areaY, panel.width - padX * 2.f, areaH};

    const nxui::Color text = m_theme ? m_theme->textSecondary : nxui::Color::white();
    ren.pushClipRect(area);
    for (int i = 0; i < m_messageVisibleLines; ++i) {
        const int index = m_messageScroll + i;
        if (index >= (int)m_messageLines.size()) break;
        ren.drawText(m_messageLines[(size_t)index], {area.x, area.y + i * lineH},
                     bodyFont, text.withAlpha(text.a * alpha), 0.96f * scaleNow);
    }
    ren.popClipRect();

    if (!m_messageScrollable) return;
    const int maxScroll = std::max(1, (int)m_messageLines.size() - m_messageVisibleLines);
    const float trackH = std::max(18.f, area.height - 8.f);
    const float thumbH = std::max(14.f, trackH * m_messageVisibleLines / (float)m_messageLines.size());
    const float progress = m_messageScroll / (float)maxScroll;
    const nxui::Color accent = m_theme ? m_theme->cursorNormal : nxui::Color::white();
    ren.drawRoundedRect({area.right() - 3.f, area.y + 4.f + (trackH - thumbH) * progress, 2.f, thumbH},
                        accent.withAlpha(0.72f * alpha), 1.f);
}

void OverlayDialog::render(nxui::Renderer& ren) {
    if (!m_active && !m_animatingOut) return;

    float alpha = m_overlayAlpha.value();
    if (alpha < 0.01f) return;

    nxui::Rect panel = scaledRect(rect(), scale());
    const auto& tuning = settings::debug::settingsGlassTuning();
    bool needsBackdropRefresh = !m_backdropCacheValid
        || std::abs(m_cachedPreBlurRadius - tuning.preBlurRadius) > 0.001f
        || m_cachedBlurIterations != tuning.blurIterations;

    if (needsBackdropRefresh) {
        ren.captureToOffscreenSharp();
        if (tuning.blurIterations > 0 && tuning.preBlurRadius > 0.001f) {
            ren.applyBlur(tuning.preBlurRadius, tuning.blurIterations);
        }
        ren.copyOffscreen(nxui::GpuDevice::OFF_SHARP_A, kBackdropCacheTarget);
        m_backdropCacheValid = true;
        m_cachedPreBlurRadius = tuning.preBlurRadius;
        m_cachedBlurIterations = tuning.blurIterations;
    }

    // Translucent backdrop scrim matching default window style (Settings / Quick Settings)
    if (m_theme) {
        nxui::Rect screen = {0.f, 0.f, (float)ren.width(), (float)ren.height()};
        nxui::Color scrim = nxui::Color::lerp(m_theme->background, nxui::Color::black(),
                                              m_theme->mode == nxui::ThemeMode::Dark ? 0.72f : 0.28f)
            .withAlpha((m_theme->mode == nxui::ThemeMode::Dark ? 0.25f : 0.14f) * alpha);
        ren.drawRect(screen, scrim);
    }

    renderGlassPanel(ren, m_theme, panel, kPanelRadius, m_base, m_border, m_highlight,
                     alpha, kBackdropCacheTarget);

    if (m_mode == DialogMode::UserSelect) {
        renderUserContent(ren, alpha);
    } else if (m_mode == DialogMode::DateTime) {
        renderDateTimeContent(ren, alpha);
    } else if (m_mode == DialogMode::Timezone) {
        renderTimezoneContent(ren, alpha);
    } else {
        for (auto& c : children())
            c->render(ren);
        if (!m_messageLines.empty())
            renderScrollingMessage(ren, panel, alpha);
    }

    m_cursor.render(ren);
}

nxui::Rect OverlayDialog::focusRect() const {
    if (m_mode == DialogMode::Buttons && m_selected >= 0 &&
        m_selected < (int)m_btnWidgets.size() && m_btnWidgets[m_selected]) {
        return scaledRect(m_btnWidgets[m_selected]->rect(), m_panelScale.value());
    }
    if (m_mode == DialogMode::DateTime) {
        return dateTimeFieldRect(m_dateTimeField);
    }
    if (m_mode == DialogMode::UserSelect && m_selected >= 0 && m_selected < userSlotCount()) {
        return userAvatarRect(m_selected);
    }
    return panelRect();
}
