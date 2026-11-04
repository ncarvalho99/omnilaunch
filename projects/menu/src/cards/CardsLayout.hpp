#pragma once
// Geometry of the Cards HOME (requirements C-SK-1, C-SK-3, C-SK-5).
// Figures adapted from upstream/homenx @ 0448cd4,
// projects/cards/src/CardsActivity.cpp:51-62 (CardsNX build 185), with
// attribution recorded in integration/provenance/homenx-intake.md.

#include <nxui/core/Types.hpp>

namespace cards::layout {

inline constexpr float kTopY = 47.f;
inline constexpr float kCircle = 77.f;

inline constexpr float kCardW = 240.f;
inline constexpr float kCardH = 340.f;
inline constexpr float kCardGrowW = 10.f;
inline constexpr float kCardGrowH = 14.f;
inline constexpr float kCardMidY = 291.f;
inline constexpr float kCardPitch = 250.f;
inline constexpr float kCardFirstX = 136.f;
inline constexpr float kCardRadius = 26.f;
inline constexpr float kCardLift = 7.f;

inline const nxui::Rect kPanel{186.f, 503.f, 907.f, 207.f};
inline constexpr float kPanelRadius = 35.f;

/// Pure placement of card `index` for a row scrolled to `scroll` (in cards).
/// Returns false when the card is off screen. `chosen` is 1 for the focused
/// card and falls to 0 one card away.
bool cardRect(int index, float scroll, nxui::Rect& out, float& chosen);

} // namespace cards::layout
