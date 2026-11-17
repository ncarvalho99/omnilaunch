#pragma once
// Small menu PNGs (page arrows, battery widgets, game card badge, XMB and
// launcher tiles), kept decoded for the life of the process.
//
// Classic is destroyed and created again on every Cards <-> Classic switch.
// Reading and decoding those files off the card each time held the switch
// frame for hundreds of milliseconds; with the decoded pixels kept here a new
// Classic only uploads them. Entries are keyed by the resolved file path and
// maxSide; a change of icon pack generation drops them all.
#include <nxui/core/Texture.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <string>
#include <vector>

namespace decoded_assets {

// Render thread. Uploads the cached pixels of `path` into `tex`, decoding and
// caching them first when they are not there yet (the old synchronous cost,
// paid once per process instead of once per switch).
bool load(nxui::Texture& tex, nxui::GpuDevice& gpu, nxui::Renderer& ren,
          const std::string& path, int maxSide = 128);

// Decodes `paths` on `pool` into the cache. A path already cached or already
// being decoded is skipped. Safe to call from the render thread at start-up.
void prewarm(nxui::ThreadPool& pool, const std::vector<std::string>& paths, int maxSide = 128);

// True when no prewarm is still decoding any of `paths`, so load() on them
// only uploads. A path never asked for counts as ready.
bool ready(const std::vector<std::string>& paths, int maxSide = 128);

} // namespace decoded_assets
