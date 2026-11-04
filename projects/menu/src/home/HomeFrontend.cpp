#include "home/HomeFrontend.hpp"
#include "cards/CardsHomeActivity.hpp"
#include "core/DebugLog.hpp"
#include "core/WiiUMenuApp.hpp"

namespace home {

HomeStyle effectiveHomeStyle(HomeStyle requested) {
    // D1a: the Cards frontend exists (games page only). There is no switch
    // entry yet (D2); it is reached by "homeStyle": "cards" in config.json.
    return requested;
}

std::unique_ptr<nxui::Activity> makeFrontend(HomeCore& core, HomeStyle style,
                                             bool fromTutorial) {
    const HomeStyle effective = effectiveHomeStyle(style);
    DebugLog::log("[home] frontend '%s'", homeStyleName(effective));
    if (effective == HomeStyle::Cards)
        return std::make_unique<cards::CardsHomeActivity>(core);

    auto activity = std::make_unique<WiiUMenuApp>(core);
    activity->setTutorialStartupFade(fromTutorial);
    return activity;
}

} // namespace home
