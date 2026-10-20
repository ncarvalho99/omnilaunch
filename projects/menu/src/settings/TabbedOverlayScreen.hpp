#pragma once
#include <nxui/core/Types.hpp>
#include <nxui/core/Font.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/Theme.hpp>
#include <nxui/core/Animation.hpp>
#include <nxui/widgets/GlassWidget.hpp>
#include "widgets/SelectionCursor.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>
#include <mutex>

class OverlayDialog;

namespace settings::tabs {
class SystemTab;
class AudioTab;
class DisplayTab;
class InternetTab;
class ControllersTab;
class BluetoothTab;
class SleepTab;
class ThemeShopInstalledTab;
class ThemeShopCommunityTab;
class AboutTab;
}

class TabbedOverlayScreen : public nxui::GlassWidget {
public:
    friend class settings::tabs::SystemTab;

    enum class ScreenMode {
        Settings,
        ThemeShop,
        GameOptions,
        FolderOptions,
        ActivityLog,
    };

    explicit TabbedOverlayScreen(ScreenMode mode = ScreenMode::Settings);
    virtual ~TabbedOverlayScreen();

    void setFont(nxui::Font* f)      { m_font = f; }
    void setSmallFont(nxui::Font* f)  { m_smallFont = f; }
    void setTheme(const nxui::Theme* t);

    void show();
    void hide();
    bool isActive() const { return m_active || m_animating; }
    bool isFullyVisible() const { return m_active && !m_animating; }
    int activeTabIndex() const { return m_tabIndex; }

    enum class FocusArea { Tabs, Content };
    FocusArea focusArea() const { return m_focusArea; }

    // The app hides the occluded home scene while this overlay is settled —
    // measured at ~16ms of GPU per frame spent on content the panel covers.
    // When set, onRender draws the frozen blurred backdrop as the base so the
    // margin around the panel shows the scene instead of stale framebuffer.
    void setSceneHidden(bool hidden) { m_sceneHidden = hidden; }
    bool sceneHidden() const         { return m_sceneHidden; }

    void setPinBottomTabEnabled(bool enabled) { m_pinBottomTab = enabled; }
    bool isPinBottomTabEnabled() const { return m_pinBottomTab; }

    void rebuildCurrentTab();
    void refreshCurrentTabWidgets();
    void handleTouch(nxui::Input& input);
    void warmup();

    struct DialogButtonDef {
        std::string label;
        std::function<void()> onPress;
    };
    using DialogRequestCb = std::function<void(const std::string& title,
                                               const std::string& msg,
                                               std::vector<DialogButtonDef> buttons)>;
    void onDialogRequest(DialogRequestCb cb) { m_dialogRequestCb = std::move(cb); }
    void requestDialog(const std::string& title, const std::string& msg,
                       std::vector<DialogButtonDef> buttons);
    struct DateTimeEditorValue {
        int year = 2000;
        int month = 1;
        int day = 1;
        int hour = 0;
        int minute = 0;
    };
    using DateTimeCommitCb = std::function<bool(const DateTimeEditorValue&)>;
    using DateTimeEditorRequestCb =
        std::function<void(const DateTimeEditorValue&, DateTimeCommitCb)>;
    void onDateTimeEditorRequest(DateTimeEditorRequestCb cb) {
        m_dateTimeEditorRequestCb = std::move(cb);
    }
    void requestDateTimeEditor(const DateTimeEditorValue& initial,
                               DateTimeCommitCb onCommit);
    void requestToast(const std::string& msg, float holdSeconds = 2.5f);

    using TimezoneSelectCb = std::function<void(const std::string& zone)>;
    using TimezoneSearchCb = std::function<void()>;
    using TimezonePickerRequestCb = std::function<void(
        const std::vector<std::string>& zones,
        int selectedIndex,
        TimezoneSelectCb onSelect,
        TimezoneSearchCb onSearch)>;
    void onTimezonePickerRequest(TimezonePickerRequestCb cb) {
        m_timezonePickerRequestCb = std::move(cb);
    }
    void requestTimezonePicker(const std::vector<std::string>& zones,
                               int selectedIndex,
                               TimezoneSelectCb onSelect,
                               TimezoneSearchCb onSearch);

    using TextEntryAcceptCb = std::function<void(const std::string&)>;
    using TextEntryRequestCb = std::function<void(const std::string& title,
                                                  const std::string& guide,
                                                  const std::string& initial,
                                                  int maxLength,
                                                  TextEntryAcceptCb onAccept)>;
    void onTextEntryRequest(TextEntryRequestCb cb) { m_textEntryRequestCb = std::move(cb); }
    void requestTextEntry(const std::string& title, const std::string& guide,
                          const std::string& initial, int maxLength,
                          TextEntryAcceptCb onAccept);

    using BoolCb = std::function<void(bool)>;
    using FloatCb = std::function<void(float)>;
    using IntCb = std::function<void(int)>;
    using VoidCb = std::function<void()>;
    using StringCb = std::function<void(const std::string&)>;
    using AccessibilityStructuredCb = std::function<void(const std::string& context,
                                                         const std::string& position,
                                                         const std::string& summary,
                                                         bool forceRepeat,
                                                         bool forceContext)>;
    void onNavigateSfx(VoidCb cb)        { m_navSfxCb = std::move(cb); }
    void onActivateSfx(VoidCb cb)        { m_activateSfxCb = std::move(cb); }
    void onCloseSfx(VoidCb cb)           { m_closeSfxCb = std::move(cb); }
    void onToggleSfx(BoolCb cb)          { m_toggleSfxCb = std::move(cb); }
    void onSliderSfx(BoolCb cb)          { m_sliderSfxCb = std::move(cb); }
    void onClosed(VoidCb cb)             { m_closedCb = std::move(cb); }
    void onAccessibilityAnnouncement(StringCb cb) { m_accessibilityCb = std::move(cb); }
    void onAccessibilityStructuredAnnouncement(AccessibilityStructuredCb cb) {
        m_accessibilityStructuredCb = std::move(cb);
    }
    void setAccessibilitySpeechPreferences(bool speakHints, bool speakPosition) {
        m_accessibilitySpeakHints = speakHints;
        m_accessibilitySpeakPosition = speakPosition;
    }
    void setAccessibilityVoiceEnabled(bool enabled) {
        m_accessibilityVoiceEnabled = enabled;
    }
    void refreshTranslations();
    ScreenMode screenMode() const { return m_mode; }

    enum class ItemType { Info, Toggle, Slider, Progress, Selector, Action, Section };

    struct SettingItem {
        std::string label;
        std::string buttonLabel;
        ItemType    type = ItemType::Info;
        std::string description;

        bool                     boolVal   = false;
        bool                     suppressToggleSfx = false;
        float                    floatVal  = 0.f;
        int                      intVal    = 0;
        int                      sliderSteps = 20;
        std::vector<std::string> options;
        std::string              infoText;
        float                    anim01 = 0.f;

        // Draw the label as a wrapped paragraph and give the row whatever
        // height that needs. Only the value column was ever fitted to width,
        // so a long label ran straight off the panel.
        bool                     wrapLabel = false;

        std::function<void(SettingItem&)> onChange;

        const std::string& effectiveButtonLabel() const {
            return buttonLabel.empty() ? label : buttonLabel;
        }

        bool focusable() const {
            return type == ItemType::Toggle || type == ItemType::Slider
                || type == ItemType::Selector || type == ItemType::Action;
        }
    };

    struct Tab {
        std::string              name;
        std::vector<SettingItem> items;
        std::function<void(Tab&, TabbedOverlayScreen&)> onUpdate;
    };

protected:
    virtual void buildTabs() = 0;
    virtual bool usesCustomContentLayout() const { return false; }
    // Full-screen custom views such as a game dossier can own the tab rail as
    // well as the normal content column.  Other overlays keep the stock rail.
    virtual bool customContentUsesPanel() const { return false; }
    // Custom screens may draw their own content cards.  Rendering the stock
    // content panel underneath those cards leaves its divider and rounded
    // border visible through translucent artwork.
    virtual bool drawsCustomContentPanel() const { return true; }
    virtual bool hidesTabRail() const { return false; }
    // A derived screen may arm a short render trace for a focused performance
    // investigation. The base consumes it once per rendered frame so a custom
    // layout cannot accidentally leave the trace armed when its tab changes.
    virtual bool consumeRenderDiagnosticsFrame() { return false; }
    // End of the previous traced render, for the frame trace's outside_us.
    std::uint64_t m_traceLastRenderEndTick = 0;
    virtual void drawCustomContent(nxui::Renderer&, const nxui::Rect&, const nxui::Rect&, float) {}
    virtual void updateCustomContent(float) {}
    virtual bool handleCustomPressA() { return false; }
    virtual bool handleCustomPressB() { return false; }
    virtual bool handleCustomPressX() { return false; }
    virtual bool handleCustomNavUp() { return false; }
    virtual bool handleCustomNavDown() { return false; }
    virtual bool handleCustomNavLeft() { return false; }
    virtual bool handleCustomNavRight() { return false; }
    virtual bool handleCustomTouch(nxui::Input&, const nxui::Rect&, const nxui::Rect&, const nxui::Rect&) { return false; }
    virtual float overlayHeaderHeight() const { return 0.f; }
    virtual float overlayTabWidth() const { return kTabWidth; }
    virtual void drawOverlayHeader(nxui::Renderer&, const nxui::Rect&, float) {}

    void onRender(nxui::Renderer& ren) override;
    void onContentRender(nxui::Renderer& ren) override;
    void onContentUpdate(float dt) override;

protected:
    void setupActions();
    void onPressB();
    void onPressA();
    void onPressX();
    void onNavUp();
    void onNavDown();
    void onNavLeft();
    void onNavRight();
    void scrollToFocused();
    void announceCurrentFocus();
    void announceCurrentValue();
    virtual void currentAccessibilityParts(std::string& context,
                                           std::string& position,
                                           std::string& summary,
                                           bool& forceRepeat) const;
    virtual std::string currentAccessibilitySummary() const;

    std::shared_ptr<nxui::Box> m_tabBar;
    std::shared_ptr<nxui::Box> m_tabContent;
    std::vector<std::vector<std::shared_ptr<nxui::Box>>> m_cachedTabContentWidgets;

    void rebuildTabBar();
    void rebuildContentItems();
    std::shared_ptr<nxui::Box> makeItemWidget(SettingItem& item);

    bool  m_active    = false;
    bool  m_animating = false;
    bool  m_sceneHidden = false;
    bool  m_showing   = false;
    float m_animT     = 0.f;

    static constexpr float kAnimDuration = 0.22f;

    FocusArea m_focusArea   = FocusArea::Tabs;
    int       m_tabIndex    = 0;
    int       m_contentIdx  = 0;
    float     m_scrollY     = 0.f;
    float     m_scrollTarget = 0.f;
    bool      m_backdropCacheValid = false;
    float     m_cachedPreBlurRadius = -1.f;
    int       m_cachedBlurIterations = -1;

    int rawIndexFromFocusable(int focIdx) const;
    int focusableCount() const;
    bool itemFocusable(const SettingItem& item) const;
    bool tabIsTextOnly() const;
    void clampContentIdx();
    float visibilityProgress() const;
    void syncPanelState(float eased);
    void invalidateBackdropCache();

    static constexpr float kPanelMargin   = 32.f;
    static constexpr float kTabWidth      = 260.f;
    // Height of a row, which wrapped labels exceed — see itemHeight.
    float itemHeight(const SettingItem& it, float contentWidth) const;

    static constexpr float kRowHeight     = 68.f;
    static constexpr float kSectionHeight = 48.f;
    static constexpr float kTabRowHeight  = 58.f;
    static constexpr float kPanelRadius   = 26.f;
    static constexpr float kInnerPad      = 30.f;

    nxui::Rect panelRect() const;
    nxui::Rect panelRect(float scale) const;
    nxui::Rect tabsRect() const;
    nxui::Rect tabsRect(const nxui::Rect& panel) const;
    // Height of one tab card. The rail does not scroll, so a tab count that
    // does not fit at the natural height is shrunk to fit instead of being
    // clipped off the bottom of the panel.
    float      tabCardHeight(int tabCount, const nxui::Rect& rail) const;
    nxui::Rect contentRect() const;
    nxui::Rect contentRect(const nxui::Rect& panel) const;
    float      contentTotalHeight() const;

    void drawBackground(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawTabs(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawContent(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawDropdown(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void drawTrackChangedToast(nxui::Renderer& ren, const nxui::Rect& panel, float opacity);
    void syncDebugWireframeRects(const nxui::Rect& panel);
    void openDropdown(int rawIdx);
    void closeDropdown(bool animated);

    std::vector<Tab> m_tabs;
    ScreenMode m_mode = ScreenMode::Settings;
    bool              m_pinBottomTab = true;
    nxui::Font*       m_font      = nullptr;
    nxui::Font*       m_smallFont = nullptr;
    const nxui::Theme* m_theme    = nullptr;

    SelectionCursor m_focusCursor;
    nxui::AnimatedFloat m_tabReveal;
    nxui::AnimatedFloat m_dropdownAnim;
    nxui::AnimatedFloat m_trackToastAnim;
    float m_trackToastHold = 0.f;
    bool  m_trackToastFading = false;
    mutable std::mutex m_toastMutex;
    std::string m_toastText;
    float m_uiTime = 0.f;

    int   m_tabSwitchDir    = 0;
    nxui::AnimatedFloat m_contentSlideAnim;
    nxui::AnimatedFloat m_tabAccentW;

    bool m_dropdownOpen = false;
    bool m_dropdownClosing = false;
    int  m_dropdownRawIdx = -1;
    int  m_dropdownHover = 0;
    float m_dropdownVisualStart = 0.f;
    float m_touchStartDropdownVisualStart = 0.f;

    VoidCb  m_navSfxCb;
    VoidCb  m_activateSfxCb;
    VoidCb  m_closeSfxCb;
    VoidCb  m_closedCb;
    DialogRequestCb m_dialogRequestCb;
    DateTimeEditorRequestCb m_dateTimeEditorRequestCb;
    TimezonePickerRequestCb m_timezonePickerRequestCb;
    TextEntryRequestCb m_textEntryRequestCb;
    BoolCb  m_toggleSfxCb;
    BoolCb  m_sliderSfxCb;
    StringCb m_accessibilityCb;
    AccessibilityStructuredCb m_accessibilityStructuredCb;
    bool m_accessibilityVoiceEnabled = true;
    bool m_accessibilitySpeakHints = true;
    bool m_accessibilitySpeakPosition = true;
    int m_i18nListenerId = -1;
    bool m_deferredRefresh = false;

    // Touch tracking
    enum class TouchTarget { None, Tab, Content, Dropdown };
    TouchTarget m_touchTarget = TouchTarget::None;
    int   m_touchHitIndex = -1;
    bool  m_touchOnSelected = false;
    bool  m_touchDirectControl = false;
    float m_touchStartX = 0.f;
    float m_touchStartY = 0.f;
    float m_touchStartScroll = 0.f;
    bool  m_touchScrolling = false;
    bool  m_touchDraggingSlider = false;
    bool  m_ignoreInitialTouchRelease = false;
};
