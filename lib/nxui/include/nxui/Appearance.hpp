#pragma once
#include <nxui/core/Types.hpp>

namespace nxui {

// Process-wide look overrides chosen by the player, read by the low-level draw
// paths so every window, selector and focus ring follows them without each
// widget carrying its own copy.
//
// solidPanels replaces every glass surface (liquid glass shader, blurred
// backdrop, frosted capture) with one flat fill of panelColor. That fill reads
// no texture and needs no scene capture, so it is the cheapest way a panel can
// be drawn: an opaque black panel costs less than the default glass, never more.
//
// radiusScale multiplies every rounded corner (1 = the design radius, 0 = square).
// Circles are drawn through their own path and keep their shape; the animated
// background opts out with RadiusExemptScope so a theme's shapes stay as made.
struct Appearance {
    bool  solidPanels = false;
    Color panelColor{0.f, 0.f, 0.f, 1.f};
    float radiusScale = 1.f;
    int   radiusExempt = 0;

    static Appearance& get() {
        static Appearance instance;
        return instance;
    }

    float radius(float r) const {
        if (radiusExempt > 0 || radiusScale >= 0.999f || r <= 0.f)
            return r;
        const float s = radiusScale < 0.f ? 0.f : radiusScale;
        return r * s;
    }

    void reset() {
        solidPanels = false;
        panelColor = Color{0.f, 0.f, 0.f, 1.f};
        radiusScale = 1.f;
        radiusExempt = 0;
    }

    struct RadiusExemptScope {
        RadiusExemptScope()  { ++Appearance::get().radiusExempt; }
        ~RadiusExemptScope() { --Appearance::get().radiusExempt; }
        RadiusExemptScope(const RadiusExemptScope&) = delete;
        RadiusExemptScope& operator=(const RadiusExemptScope&) = delete;
    };
};

} // namespace nxui
