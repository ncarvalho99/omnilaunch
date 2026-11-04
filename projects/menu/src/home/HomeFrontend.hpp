#pragma once
// Frontend factory (HomeNX C7). The one place that turns a HomeStyle into an
// Activity, so boot, the tutorial hand-off and (Part D) the live switch all
// build frontends the same way.

#include "home/HomeStyle.hpp"
#include <memory>

namespace nxui { class Activity; }

namespace home {

class HomeCore;

/// The style that will actually be built for `requested`. Cards has no
/// frontend until Part D, so it resolves to Classic.
HomeStyle effectiveHomeStyle(HomeStyle requested);

/// Builds the frontend for `style` (after effectiveHomeStyle). The Activity is
/// constructed only; onCreate runs when the Application installs it.
std::unique_ptr<nxui::Activity> makeFrontend(HomeCore& core, HomeStyle style,
                                             bool fromTutorial = false);

} // namespace home
