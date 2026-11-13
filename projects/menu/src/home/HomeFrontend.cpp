#include "home/HomeFrontend.hpp"
#include "cards/CardsHomeActivity.hpp"
#include "core/DebugLog.hpp"
#include "core/LeaveFrameCache.hpp"
#include "core/WiiUMenuApp.hpp"
#include "home/HomeCore.hpp"
#include <nxui/Application.hpp>
#include <nxui/core/Input.hpp>
#ifdef SWITCHU_MENU
#include <switchu/sd_commit.hpp>
#endif

namespace home {

namespace {
// The style of the frontend most recently built by makeFrontend. A switch
// compares against this, not config.homeStyle, which a recovery may have
// rewritten.
HomeStyle g_builtStyle = HomeStyle::Classic;
} // namespace

HomeStyle effectiveHomeStyle(HomeStyle requested) {
    return requested;
}

std::unique_ptr<nxui::Activity> makeFrontend(HomeCore& core, HomeStyle style,
                                             bool fromTutorial) {
    const HomeStyle effective = effectiveHomeStyle(style);
    DebugLog::log("[home] frontend '%s'", homeStyleName(effective));
    g_builtStyle = effective;
    if (effective == HomeStyle::Cards)
        return std::make_unique<cards::CardsHomeActivity>(core);

    auto activity = std::make_unique<WiiUMenuApp>(core);
    activity->setTutorialStartupFade(fromTutorial);
    return activity;
}

void installFrontendRecovery(nxui::Application& app, HomeCore& core) {
    app.setActivityRecovery([&core]() -> std::unique_ptr<nxui::Activity> {
        DebugLog::log("[home] frontend '%s' failed to start; recovering to classic",
                      homeStyleName(g_builtStyle));
        auto& config = core.config();
        if (config.homeStyle != HomeStyle::Classic) {
            config.homeStyle = HomeStyle::Classic;
            config.save();
#ifdef SWITCHU_MENU
            switchu::commitSdCard("home style recovery");
#endif
        }
        // A recovered Classic is still reached by a switch.
        core.markFrontendSwitch();
        return makeFrontend(core, HomeStyle::Classic);
    });
}

bool anyInputHeld(const nxui::Input& in) {
    using B = nxui::Button;
    static constexpr B kButtons[] = {
        B::A, B::B, B::X, B::Y, B::L, B::R, B::ZL, B::ZR, B::Plus, B::Minus,
        B::DUp, B::DDown, B::DLeft, B::DRight, B::LStick, B::RStick,
        B::LStickU, B::LStickD, B::LStickL, B::LStickR,
        B::RStickU, B::RStickD, B::RStickL, B::RStickR,
    };
    for (B b : kButtons)
        if (in.isHeld(b))
            return true;
    return in.isTouching();
}

bool switchFrontend(nxui::Application& app, HomeCore& core, HomeStyle style) {
    if (app.activityChangePending() || effectiveHomeStyle(style) == g_builtStyle)
        return false;
    auto& config = core.config();
    config.homeStyle = style;
    // When switching to Classic, retire any legacy IconGrid cards layout mode
    // so Classic always opens in its clean Classic UI.
    if (style == HomeStyle::Classic && config.appLayoutMode == AppLayoutMode::Cards) {
        config.appLayoutMode = AppLayoutMode::Grid;
    }
    if (!config.save())
        DebugLog::log("[home] homeStyle save failed");
#ifdef SWITCHU_MENU
    switchu::commitSdCard("home style switch");
#endif
    DebugLog::log("[home] switching frontend to '%s'", homeStyleName(style));
    // The leave snapshot belongs to the frontend that saved it. Left in place,
    // Classic would open its next return on a Cards frame (or the reverse) for
    // the splash hold before its own UI replaced it.
    LeaveFrameCache::clear();
    core.markFrontendSwitch();
    app.requestActivity(makeFrontend(core, style));
    return true;
}

} // namespace home
