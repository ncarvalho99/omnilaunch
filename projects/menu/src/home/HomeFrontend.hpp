#pragma once
// Frontend factory (HomeNX C7). The one place that turns a HomeStyle into an
// Activity, so boot, the tutorial hand-off and the live switch (D2) all build
// frontends the same way.

#include "home/HomeStyle.hpp"
#include <memory>

namespace nxui { class Activity; class Application; class Input; }

namespace home {

class HomeCore;

/// The style that will actually be built for `requested`.
HomeStyle effectiveHomeStyle(HomeStyle requested);

/// Builds the frontend for `style` (after effectiveHomeStyle). The Activity is
/// constructed only; onCreate runs when the Application installs it.
std::unique_ptr<nxui::Activity> makeFrontend(HomeCore& core, HomeStyle style,
                                             bool fromTutorial = false);

/// Installs the run-loop recovery used when a live switch's onCreate fails:
/// homeStyle falls back to Classic (saved and committed) and Classic is built.
void installFrontendRecovery(nxui::Application& app, HomeCore& core);

/// Live switch (D2). Saves `style` as homeStyle, commits the SD card and asks
/// the Application to swap frontends at the end of this frame. Callers refuse
/// the switch during a launch or an exit first. Returns false (nothing
/// changed) when that style is already being shown or a change is pending.
bool switchFrontend(nxui::Application& app, HomeCore& core, HomeStyle style);

/// Transition input contract (D2): true while any button, either stick
/// (direction or click) or the touch screen is still held. A frontend reached
/// by a switch ignores input until this turns false.
bool anyInputHeld(const nxui::Input& input);

} // namespace home
