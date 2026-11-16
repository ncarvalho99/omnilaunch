#pragma once
#include "core/GridModel.hpp"
#include "launcher/IconStreamer.hpp"
#include <functional>
namespace nxui { class ThreadPool; }
class AppListLoader { public:
std::vector<AppEntry> apps;
void setPendingTransform(std::function<void()>) {}
void setPrefetchIcons(bool) {}
void startAsync(nxui::ThreadPool&) {}
bool isReady() const { return true; }
bool finalize(GridModel& m,IconStreamer&) { for(auto a:apps) m.addEntry(a); return true; }
static std::vector<std::uint8_t> loadIconData(std::uint64_t) { return {}; }
};
