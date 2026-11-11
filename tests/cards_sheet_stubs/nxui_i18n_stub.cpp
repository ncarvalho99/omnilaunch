#include <nxui/core/I18n.hpp>

namespace nxui {
I18n& I18n::instance() {
    static I18n s_inst;
    return s_inst;
}
std::string I18n::tr(std::string_view, std::string_view fallback) const {
    return std::string(fallback);
}
}
