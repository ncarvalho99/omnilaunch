#pragma once

namespace settings::debug {

struct SettingsGlassTuning {
    float preBlurRadius = 3.0f;
    // 8, not 15. The blur runs once per open to fill the backdrop cache, and
    // since that capture moved to full resolution it processes four times the
    // pixels — measured as a single 55ms frame on the first open, which is the
    // one-frame hitch reported there. Halving the iterations halves that, and
    // at full resolution a radius of 3 already reaches half as far as it did
    // at 640x360, so the result was never going to look like the old one
    // regardless of iteration count.
    int blurIterations = 8;
    // Upper bound on the full-resolution blur passes run on the opening frame
    // (each pass is two 1280x720 draws). The radius widening in applyBlur
    // turned 8 iterations into 12-24 passes, a 30-55 ms first frame; HomeNX
    // opens its panels without this pass at all. 6 keeps the frosted look.
    int maxOpenPasses = 6;
    float shaderBlurIntensity = 0.0f;
    float refractionIntensity = 0.035f;
    float glowIntensity = 0.14f;
    float saturation = 0.94f;
    float roughness = 0.004f;
    float powerFactor = 20.0f;
    float inset = 10.0f;
    float tintAlphaDark = 0.22f;
    float tintAlphaLight = 0.18f;
    float shade = 0.12f;
};

inline SettingsGlassTuning& settingsGlassTuning() {
    static SettingsGlassTuning tuning;
    return tuning;
}

inline void resetSettingsGlassTuning() {
    settingsGlassTuning() = SettingsGlassTuning{};
}

} // namespace settings::debug