#pragma once
// Which HOME frontend the menu shows (HomeNX C7). Stored in config.json as a
// stable string, never an ordinal, so a config written by a newer build still
// reads on an older one. Unknown or missing values mean Classic.

#include <string>
#include <string_view>

namespace home {

enum class HomeStyle {
    Classic,
    Cards,
};

inline HomeStyle parseHomeStyle(std::string_view value) {
    if (value == "cards")
        return HomeStyle::Cards;
    return HomeStyle::Classic;
}

inline const char* homeStyleName(HomeStyle style) {
    switch (style) {
    case HomeStyle::Cards:   return "cards";
    case HomeStyle::Classic:
    default:                 return "classic";
    }
}

} // namespace home
