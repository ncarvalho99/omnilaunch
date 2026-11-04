#include "home/HomeFrontend.hpp"
#include "core/DebugLog.hpp"
#include "core/WiiUMenuApp.hpp"

namespace home {

HomeStyle effectiveHomeStyle(HomeStyle requested) {
    // Part D adds the Cards frontend; until then the setting is kept but the
    // Classic menu is shown.
    (void)requested;
    return HomeStyle::Classic;
}

std::unique_ptr<nxui::Activity> makeFrontend(HomeCore& core, HomeStyle style,
                                             bool fromTutorial) {
    const HomeStyle effective = effectiveHomeStyle(style);
    if (effective != style)
        DebugLog::log("[home] style '%s' not available yet; using '%s'",
                      homeStyleName(style), homeStyleName(effective));
    else
        DebugLog::log("[home] frontend '%s'", homeStyleName(effective));

    auto activity = std::make_unique<WiiUMenuApp>(core);
    activity->setTutorialStartupFade(fromTutorial);
    return activity;
}

} // namespace home
