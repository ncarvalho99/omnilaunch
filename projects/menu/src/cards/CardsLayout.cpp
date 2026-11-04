#include "cards/CardsLayout.hpp"
#include <algorithm>
#include <cmath>

namespace cards::layout {

namespace {
float easeOutBack(float t) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.f;
    const float u = t - 1.f;
    return 1.f + c3 * u * u * u + c1 * u * u;
}
} // namespace

bool cardRect(int index, float scroll, nxui::Rect& out, float& chosen) {
    const float rel = static_cast<float>(index) - scroll;
    const float x0 = kCardFirstX + rel * kCardPitch;
    if (x0 > 1290.f || x0 + kCardPitch < -10.f)
        return false;
    chosen = std::clamp(1.f - std::abs(rel), 0.f, 1.f);
    const float grow = easeOutBack(chosen);
    const float w = kCardW + kCardGrowW * grow;
    const float h = kCardH + kCardGrowH * grow;
    const float lift = kCardLift * chosen;
    out = {x0 + kCardW * 0.5f - w * 0.5f, kCardMidY - h * 0.5f - lift, w, h};
    return true;
}

} // namespace cards::layout
