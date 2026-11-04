#include "ProgressDialog.hpp"
#include "../settings/SettingsGlassTuning.hpp"
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Input.hpp>
#include <nxui/core/Renderer.hpp>
#include <algorithm>
#include <cmath>

namespace {
nxui::Rect scaledRect(const nxui::Rect& rect, float scale) {
    nxui::Rect out = rect;
    float w = out.width * scale;
    float h = out.height * scale;
    out.x += (out.width - w) * 0.5f;
    out.y += (out.height - h) * 0.5f;
    out.width = w;
    out.height = h;
    return out;
}

std::string ellipsize(nxui::Font* font, const std::string& text, float maxWidth, float scale) {
    if (!font || text.empty() || font->measure(text).x * scale <= maxWidth)
        return text;
    std::string out = text;
    while (!out.empty()) {
        out.pop_back();
        std::string candidate = out + "...";
        if (font->measure(candidate).x * scale <= maxWidth)
            return candidate;
    }
    return "...";
}
}

ProgressDialog::ProgressDialog() {
    setVisible(false);
    setFocusable(true);
    setCornerRadius(kPanelRadius);
    setForceLiquidGlass(true);
    setBlurEnabled(false);
    setPanelOpacity(0.90f);
}

nxui::Rect ProgressDialog::panelRect() const {
    const float h = m_onCancel ? kPanelHWithButton : kPanelH;
    return {(1280.f - kPanelW) * 0.5f, (720.f - h) * 0.5f, kPanelW, h};
}

nxui::Rect ProgressDialog::cancelButtonRect(const nxui::Rect& panel) const {
    constexpr float w = 200.f, h = 44.f;
    return {panel.x + (panel.width - w) * 0.5f, panel.bottom() - h - 22.f, w, h};
}

void ProgressDialog::setCancelAction(const std::string& label, std::function<void()> onCancel) {
    m_cancelLabel = label;
    m_onCancel = std::move(onCancel);
    m_cancelRequested = false;
    clearActions();
    if (m_onCancel) {
        addAction(static_cast<uint64_t>(nxui::Button::A), [this] { requestCancel(); });
        addAction(static_cast<uint64_t>(nxui::Button::B), [this] { requestCancel(); });
    }
}

void ProgressDialog::requestCancel() {
    if (!m_active || !m_onCancel || m_cancelRequested)
        return;
    m_cancelRequested = true;
    auto cb = m_onCancel;
    cb();
}

void ProgressDialog::markCancelling(const std::string& message) {
    m_cancelRequested = true;
    m_message = message;
    m_progress01 = -1.f;
}

void ProgressDialog::show(const std::string& title, const std::string& message, float progress01) {
    m_title = title;
    m_message = message;
    m_progress01 = progress01;
    m_onCancel = nullptr;
    m_cancelLabel.clear();
    m_cancelRequested = false;
    clearActions();
    m_active = true;
    m_animatingOut = false;
    m_overlayAlpha.setImmediate(0.f);
    m_overlayAlpha.set(1.f, 0.18f, nxui::Easing::outCubic);
    m_panelScale.setImmediate(0.92f);
    m_panelScale.set(1.f, 0.20f, nxui::Easing::outCubic);
    m_progressAnim.setImmediate(std::clamp(progress01, 0.f, 1.f));
    m_backdropCacheValid = false;
    setVisible(true);
}

void ProgressDialog::updateState(const std::string& message, float progress01) {
    if (m_cancelRequested)
        return; // keep the "cancelling" text until the owner hides the dialog
    m_message = message;
    m_progress01 = progress01;
    if (progress01 >= 0.f)
        m_progressAnim.set(std::clamp(progress01, 0.f, 1.f), 0.16f, nxui::Easing::outCubic);
}

void ProgressDialog::hide() {
    if (!m_active)
        return;
    m_active = false;
    m_animatingOut = true;
    m_overlayAlpha.set(0.f, 0.16f, nxui::Easing::outCubic);
    m_panelScale.set(0.96f, 0.16f, nxui::Easing::outCubic);
}

void ProgressDialog::update(float dt) {
    m_overlayAlpha.update(dt);
    m_panelScale.update(dt);
    m_progressAnim.update(dt);
    m_spinnerT += dt;
    if (m_animatingOut && m_overlayAlpha.value() <= 0.01f) {
        m_animatingOut = false;
        setVisible(false);
    }
}

void ProgressDialog::render(nxui::Renderer& ren) {
    if (!isActive() || !m_theme)
        return;

    float alpha = m_overlayAlpha.value();
    if (alpha <= 0.01f)
        return;

    ren.drawRect({0.f, 0.f, 1280.f, 720.f}, nxui::Color(0.f, 0.f, 0.f, 0.50f * alpha));

    nxui::Rect panel = scaledRect(panelRect(), m_panelScale.value());

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

    nxui::Color glassTint = m_theme->panelBase.withAlpha(m_theme->mode == nxui::ThemeMode::Dark
        ? std::clamp(tuning.tintAlphaDark, 0.0f, 1.0f)
        : std::clamp(tuning.tintAlphaLight, 0.0f, 1.0f));
    nxui::Rect glassRect = panel.shrunk(std::max(0.0f, tuning.inset));
    float glassRadius = std::max(12.0f, kPanelRadius - std::max(0.0f, tuning.inset) * 0.5f);

    ren.drawLiquidGlass(kBackdropCacheTarget,
                        glassRect,
                        glassRadius,
                        glassTint,
                        alpha,
                        std::clamp(tuning.shade, 0.0f, 1.0f));
    ren.drawRoundedRectOutline(glassRect,
                               m_theme->panelBorder.withAlpha(std::clamp(m_theme->panelBorder.a * 0.90f, 0.14f, 0.34f) * alpha),
                               glassRadius,
                               1.2f);
    ren.drawRoundedRectOutline(glassRect.shrunk(1.5f),
                               m_theme->panelHighlight.withAlpha(std::clamp(m_theme->panelHighlight.a * 0.90f, 0.04f, 0.10f) * alpha),
                               std::max(0.0f, glassRadius - 1.5f),
                               1.0f);
    ren.liquidGlassSettings() = savedGlass;

    float pad = 38.f;
    if (m_font) {
        ren.drawText(ellipsize(m_font, m_title, panel.width - pad * 2.f, 1.f),
                     {panel.x + pad, panel.y + 34.f}, m_font,
                     m_theme->textPrimary.withAlpha(alpha), 1.f);
    }
    if (m_smallFont) {
        ren.drawText(ellipsize(m_smallFont, m_message, panel.width - pad * 2.f, 0.80f),
                     {panel.x + pad, panel.y + 86.f}, m_smallFont,
                     m_theme->textSecondary.withAlpha(0.94f * alpha), 0.80f);
    }

    nxui::Rect track = {panel.x + pad, panel.y + 148.f, panel.width - pad * 2.f, 18.f};
    nxui::Color trackBg = m_theme->panelBase.withAlpha(0.12f * alpha);
    nxui::Color trackBorder = m_theme->panelBorder.withAlpha(0.32f * alpha);
    nxui::Color trackHighlight = m_theme->panelHighlight.withAlpha(0.18f * alpha);
    ren.drawRoundedRect(track, trackBg, 9.f);
    ren.drawRoundedRectOutline(track, trackBorder, 9.f, 1.2f);
    ren.drawRoundedRectOutline(track.shrunk(1.f), trackHighlight, 8.f, 1.f);

    if (m_progress01 >= 0.f) {
        float p = std::clamp(m_progressAnim.value(), 0.f, 1.f);
        nxui::Rect fill = track;
        fill.width = std::max(18.f, track.width * p);
        ren.drawRoundedRect(fill, m_theme->cursorNormal.withAlpha(0.88f * alpha), 9.f);
        if (m_smallFont) {
            std::string pct = std::to_string((int)std::round(p * 100.f)) + "%";
            nxui::Vec2 pctSize = m_smallFont->measure(pct);
            ren.drawText(pct, {track.right() - pctSize.x * 0.72f, track.bottom() + 14.f},
                         m_smallFont, m_theme->textPrimary.withAlpha(alpha), 0.72f);
        }
    } else {
        float segmentW = track.width * 0.28f;
        float travel = track.width - segmentW;
        float phase = std::fmod(m_spinnerT * 0.65f, 1.f);
        nxui::Rect fill = {track.x + travel * phase, track.y, segmentW, track.height};
        ren.drawRoundedRect(fill, m_theme->cursorNormal.withAlpha(0.82f * alpha), 9.f);
    }

    if (m_onCancel) {
        const nxui::Rect btn = cancelButtonRect(panel);
        const float a = (m_cancelRequested ? 0.45f : 1.f) * alpha;
        ren.drawRoundedRect(btn, m_theme->cursorNormal.withAlpha(0.30f * a), 22.f);
        ren.drawRoundedRectOutline(btn, m_theme->cursorNormal.withAlpha(0.90f * a), 22.f, 2.f);
        if (m_smallFont) {
            const nxui::Vec2 size = m_smallFont->measure(m_cancelLabel);
            const float scale = 0.85f;
            ren.drawText(m_cancelLabel,
                         {btn.x + (btn.width - size.x * scale) * 0.5f,
                          btn.y + (btn.height - size.y * scale) * 0.5f},
                         m_smallFont, m_theme->textPrimary.withAlpha(a), scale);
        }
    }
}
