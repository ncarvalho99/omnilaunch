#include "cards/CardsPages.hpp"
#include <algorithm>

namespace cards::pages {

std::vector<int> shown(const Prefs& prefs) {
    std::vector<int> out;
    for (int p : prefs.pageOrder)
        if (!prefs.pageHidden(p))
            out.push_back(p);
    if (out.empty() && !prefs.pageOrder.empty())
        out.push_back(prefs.pageOrder.front()); // one always shows
    if (out.empty())
        out.push_back(Prefs::kGames);
    return out;
}

int first(const Prefs& prefs) {
    return shown(prefs).front();
}

int step(const Prefs& prefs, int current, int dir) {
    const auto list = shown(prefs);
    const int n = static_cast<int>(list.size());
    auto it = std::find(list.begin(), list.end(), current);
    if (it == list.end())
        return list.front();
    const int pos = static_cast<int>(it - list.begin());
    return list[static_cast<std::size_t>((pos + (dir > 0 ? 1 : -1) + n) % n)];
}

bool movePage(Prefs& prefs, int page, int dir) {
    auto& order = prefs.pageOrder;
    auto it = std::find(order.begin(), order.end(), page);
    if (it == order.end())
        return false;
    const auto pos = static_cast<std::size_t>(it - order.begin());
    if (dir < 0 && pos > 0) {
        std::swap(order[pos], order[pos - 1]);
        return true;
    }
    if (dir > 0 && pos + 1 < order.size()) {
        std::swap(order[pos], order[pos + 1]);
        return true;
    }
    return false;
}

bool togglePage(Prefs& prefs, int page) {
    if (std::find(prefs.pageOrder.begin(), prefs.pageOrder.end(), page) == prefs.pageOrder.end())
        return false;
    auto& hidden = prefs.hiddenPages;
    auto it = std::find(hidden.begin(), hidden.end(), page);
    if (it != hidden.end()) {
        hidden.erase(it);
        return true;
    }
    if (shown(prefs).size() <= 1)
        return false;
    hidden.push_back(page);
    return true;
}

std::size_t rank(const std::vector<std::uint64_t>& order, std::uint64_t id) {
    auto it = std::find(order.begin(), order.end(), id);
    return static_cast<std::size_t>(it - order.begin());
}

std::vector<std::uint64_t> ordered(const std::vector<std::uint64_t>& ids,
                                   const std::vector<std::uint64_t>& order) {
    std::vector<std::uint64_t> out(ids);
    std::stable_sort(out.begin(), out.end(), [&order](std::uint64_t a, std::uint64_t b) {
        return rank(order, a) < rank(order, b);
    });
    return out;
}

bool setMedia(Prefs& prefs, std::uint64_t id, bool media) {
    if (!id || isSystemId(id)) return false;
    auto& list = prefs.mediaTitles;
    auto it = std::find(list.begin(), list.end(), id);
    if (media == (it != list.end()))
        return false;
    if (media) {
        list.push_back(id);
    } else {
        list.erase(it);
        prefs.mediaOrder.erase(std::remove(prefs.mediaOrder.begin(), prefs.mediaOrder.end(), id),
                               prefs.mediaOrder.end());
    }
    return true;
}

} // namespace cards::pages
