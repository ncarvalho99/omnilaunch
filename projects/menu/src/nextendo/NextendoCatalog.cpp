#include "nextendo/NextendoCatalog.hpp"

#include <nlohmann/json.hpp>
#include <cstdlib>

namespace nextendo {

bool parseOnlineCounts(const std::string& json, std::map<std::uint64_t, int>& outCounts, int& outTotal) {
    const auto j = nlohmann::json::parse(json, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return false;

    auto itCounts = j.find("counts");
    if (itCounts != j.end() && itCounts->is_object()) {
        for (auto it = itCounts->begin(); it != itCounts->end(); ++it) {
            if (!it.value().is_number_integer()) continue;
            const std::uint64_t id = std::strtoull(it.key().c_str(), nullptr, 16);
            if (id != 0) {
                const int cnt = it.value().get<int>();
                if (cnt >= 0) outCounts[id] = cnt;
            }
        }
    }

    auto itJeux = j.find("jeux");
    if (itJeux != j.end() && itJeux->is_array()) {
        for (const auto& game : *itJeux) {
            if (!game.is_object()) continue;
            const int count = game.value("joueurs", 0);
            auto itTitres = game.find("titres");
            if (itTitres != game.end() && itTitres->is_array()) {
                for (const auto& t : *itTitres) {
                    if (!t.is_string()) continue;
                    const std::uint64_t id = std::strtoull(t.get<std::string>().c_str(), nullptr, 16);
                    if (id != 0 && outCounts.find(id) == outCounts.end()) {
                        outCounts[id] = count;
                    }
                }
            }
        }
    }

    outTotal = 0;
    for (const auto& kv : outCounts) {
        outTotal += kv.second;
    }
    return !outCounts.empty();
}

} // namespace nextendo
