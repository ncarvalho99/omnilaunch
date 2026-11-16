#include "ThemeShopTabBuilders.hpp"
#include <nxui/core/I18n.hpp>
#include <algorithm>
#include <cmath>
#include <string>

// The OmniLaunch tab holds only what changes how the theme looks: the colour of
// the windows, the roundness of every corner and the colour of the text. Every
// row applies live; the choice is saved with the rest of the config when the
// shop closes. The other rows that used to live here are in the Options tab.

namespace themeshop::tabs {

namespace {

using SettingItem = ThemeShopScreen::SettingItem;
using ItemType = ThemeShopScreen::ItemType;

int pct(float v) { return static_cast<int>(std::lround(std::clamp(v, 0.f, 1.f) * 100.f)); }

} // namespace

ThemeShopScreen::Tab OmniLaunchTab::build(ThemeShopScreen& screen) {
    auto& i18n = nxui::I18n::instance();
    ThemeShopScreen::Tab t;
    t.name = "OmniLaunch";

    // Four sliders that edit one colour: hue, saturation, brightness, opacity.
    // `hsva` is the screen's copy, so the hue is not lost while the colour is
    // grey; `color` is what reaches the renderer.
    auto colourRows = [&](colorhex::Hsva* hsva, nxui::Color* color, bool withAlpha,
                          const char* hexKey) {
        struct Channel { const char* key; const char* fallback; float* value; int steps; bool degrees; };
        std::vector<Channel> channels = {
            {"themeshop.look.hue", "Hue", &hsva->h, 72, true},
            {"themeshop.look.saturation", "Saturation", &hsva->s, 20, false},
            {"themeshop.look.brightness", "Brightness", &hsva->v, 20, false},
        };
        if (withAlpha)
            channels.push_back({"themeshop.look.opacity", "Opacity", &hsva->a, 20, false});

        for (const auto& ch : channels) {
            SettingItem it;
            it.label = i18n.tr(ch.key, ch.fallback);
            it.type = ItemType::Slider;
            it.sliderSteps = ch.steps;
            it.floatVal = std::clamp(*ch.value, 0.f, 1.f);
            it.anim01 = it.floatVal;
            const bool degrees = ch.degrees;
            auto info = [degrees](float v) {
                return degrees ? std::to_string(static_cast<int>(std::lround(v * 360.f)) % 360) + "\xC2\xB0"
                               : std::to_string(pct(v)) + "%";
            };
            it.infoText = info(it.floatVal);
            float* value = ch.value;
            it.onChange = [&screen, hsva, color, value, info](SettingItem& self) {
                self.anim01 = self.floatVal;
                self.infoText = info(self.floatVal);
                *value = std::clamp(self.floatVal, 0.f, 1.f);
                *color = colorhex::toColor(*hsva);
                screen.refreshAppearanceHexRows();
                screen.publishAppearance();
            };
            t.items.push_back(std::move(it));
        }

        // The exact value, typed: any colour, not only what the sliders reach.
        SettingItem it;
        it.key = hexKey;
        it.label = i18n.tr("themeshop.look.hex", "Colour code");
        it.description = withAlpha
            ? i18n.tr("themeshop.look.hex_desc_alpha", "Type a code such as #000000 or #1A1A2ECC (the last two digits are the opacity).")
            : i18n.tr("themeshop.look.hex_desc", "Type a code such as #FFFFFF or #FFD54F.");
        it.type = ItemType::Action;
        it.buttonLabel = colorhex::format(*color, withAlpha);
        it.onChange = [&screen, hsva, color, withAlpha](SettingItem&) {
            if (!screen.m_requestTextEntryCb)
                return;
            auto& tr = nxui::I18n::instance();
            screen.m_requestTextEntryCb(
                tr.tr("themeshop.look.hex", "Colour code"),
                withAlpha ? "#RRGGBBAA" : "#RRGGBB",
                colorhex::format(*color, withAlpha),
                [&screen, hsva, color, withAlpha](std::string text) {
                    auto parsed = colorhex::parse(text);
                    if (!parsed) {
                        screen.requestToast(nxui::I18n::instance().tr("themeshop.look.hex_invalid",
                            "That is not a colour code. Use 6 or 8 digits, 0-9 and A-F."));
                        return;
                    }
                    if (!withAlpha)
                        parsed->a = 1.f;
                    *color = *parsed;
                    *hsva = colorhex::fromColor(*parsed, hsva->h);
                    screen.publishAppearance();
                    // The sliders show the new colour; rebuilt next frame.
                    screen.m_omniTabRebuildPending = true;
                });
        };
        t.items.push_back(std::move(it));
    };

    auto& look = screen.m_appearance;

    // --- Windows -----------------------------------------------------------
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.look.windows", "Windows");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.look.solid", "Solid window colour");
        it.description = i18n.tr("themeshop.look.solid_desc",
            "Off: the theme's glass. On: windows, side buttons and panels take one flat colour. "
            "A solid colour skips the blur, so it is lighter on the console; opaque black suits OLED screens.");
        it.type = ItemType::Toggle;
        it.boolVal = look.solidPanels;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_appearance.solidPanels = self.boolVal;
            screen.publishAppearance();
            screen.m_omniTabRebuildPending = true;   // show or hide the colour rows
        };
        t.items.push_back(std::move(it));
    }
    if (look.solidPanels)
        colourRows(&screen.m_panelHsva, &look.panelColor, true, "appearance.panel_hex");
    {
        // How much the screen behind a menu is blurred. It applies to the
        // theme's glass and to a solid colour below 100 % opacity; behind an
        // opaque colour nothing shows through. Config key glassSharpness.
        SettingItem it;
        it.label = i18n.tr("settings.display.glass_sharpness", "Glass sharpness");
        it.description = i18n.tr("settings.display.glass_sharpness_desc",
                                 "How clearly the screen behind menus shows through them.");
        it.type = ItemType::Slider;
        it.floatVal = std::clamp(screen.m_glassSharpness, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            screen.m_glassSharpness = std::clamp(self.floatVal, 0.f, 1.f);
            if (screen.m_glassSharpnessCb) screen.m_glassSharpnessCb(screen.m_glassSharpness);
        };
        t.items.push_back(std::move(it));
    }

    // --- Corners -----------------------------------------------------------
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.look.corners", "Corners");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.look.roundness", "Corner roundness");
        it.description = i18n.tr("themeshop.look.roundness_desc",
            "Windows, buttons, icons and the selection frame together. 0% makes every corner square.");
        it.type = ItemType::Slider;
        it.sliderSteps = 10;
        it.floatVal = std::clamp(look.cornerRoundness, 0.f, 1.f);
        it.anim01 = it.floatVal;
        it.infoText = std::to_string(pct(it.floatVal)) + "%";
        it.onChange = [&screen](SettingItem& self) {
            self.anim01 = self.floatVal;
            self.infoText = std::to_string(pct(self.floatVal)) + "%";
            screen.m_appearance.cornerRoundness = std::clamp(self.floatVal, 0.f, 1.f);
            screen.publishAppearance();
        };
        t.items.push_back(std::move(it));
    }

    // --- Text --------------------------------------------------------------
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.look.text", "Text");
        it.type = ItemType::Section;
        t.items.push_back(std::move(it));
    }
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.look.text_custom", "Custom text colour");
        it.description = i18n.tr("themeshop.look.text_custom_desc",
            "Off: the theme's colours. On: titles, labels and hints use the colour below, for text that is hard to read over a theme.");
        it.type = ItemType::Toggle;
        it.boolVal = look.customTextColor;
        it.anim01 = it.boolVal ? 1.f : 0.f;
        it.onChange = [&screen](SettingItem& self) {
            screen.m_appearance.customTextColor = self.boolVal;
            screen.publishAppearance();
            screen.m_omniTabRebuildPending = true;
        };
        t.items.push_back(std::move(it));
    }
    if (look.customTextColor)
        colourRows(&screen.m_textHsva, &look.textColor, false, "appearance.text_hex");

    // --- Reset -------------------------------------------------------------
    {
        SettingItem it;
        it.label = i18n.tr("themeshop.look.reset", "Use the theme's look");
        it.description = i18n.tr("themeshop.look.reset_desc",
            "Turns the three options above back to the theme's own glass, corners and text.");
        it.type = ItemType::Action;
        it.buttonLabel = i18n.tr("themeshop.look.reset_button", "Reset");
        it.onChange = [&screen](SettingItem&) {
            // The chosen colours are kept for the next time a switch goes on.
            screen.m_appearance.solidPanels = false;
            screen.m_appearance.customTextColor = false;
            screen.m_appearance.cornerRoundness = 1.f;
            screen.publishAppearance();
            screen.m_omniTabRebuildPending = true;
        };
        t.items.push_back(std::move(it));
    }

    return t;
}

} // namespace themeshop::tabs
