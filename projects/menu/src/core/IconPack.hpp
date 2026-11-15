#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nxui {
class Texture;
class Renderer;
}

// Icon packs: sets of the menu's own PNG icons (sidebar, launcher tiles,
// battery widgets, page arrows...) downloaded from the icon store and switched
// live, without a restart.
//
// A pack is a folder under kRoot named by its catalogue id, holding pack.json
// and PNG files named exactly like the files under romfs:/icons/. A file the
// pack does not carry falls back to the built-in one, so a partial pack still
// works.
//
// Every place that loads one of those icons asks resolve() first and reloads
// when generation() changes. The active pack is chosen by the player
// (AppConfig::iconPack); "" is the built-in set.
namespace iconpack {

constexpr const char* kRoot = "sdmc:/config/OmniLaunch/icon-packs";
// Written by the installer: SHA-256 of the archive the pack came from.
constexpr const char* kPackageMarker = "package.sha256";

struct Installed {
    std::string id;
    std::string name;
    std::string author;
    std::string version;
    std::string path;      // folder on the SD card
    std::uint64_t bytes = 0;
    int files = 0;
    std::string packageSha256; // empty for packs installed before CL 345
};

// Catalogue ids become folder names: lowercase letters, digits, '-' and '_'.
bool safeId(const std::string& id);

// Selects the active pack. An id that is unsafe or has no installed folder
// selects the built-in icons. Bumps generation() when the choice changes.
void setActive(const std::string& id);
// Bumps generation() without changing the pack: its files were replaced (an
// update of the active pack) and every icon has to be read again.
void refresh();
std::string activeId();
// Folder of the active pack, "" for the built-in icons.
std::string activeDir();
// Increments whenever the active pack changes; loaders compare it with the
// value they loaded at.
std::uint32_t generation();

// The pack's copy of fileName (e.g. "settings.png") when the active pack has
// it, otherwise fallbackPath.
std::string resolve(const std::string& fileName, const std::string& fallbackPath);
// Same, with the usual fallback chain for widgets: the pack, then
// sdmc:/switch/OmniLaunch/icons/<file>, then romfs:/icons/<file>. Returns the
// candidates in order; the caller loads the first that succeeds.
std::vector<std::string> candidates(const std::string& fileName);

// Loads the first of candidates(fileName) that decodes. Render thread only.
bool loadInto(nxui::Texture& tex, nxui::Renderer& ren, const std::string& fileName, int maxSide = 128);

// Packs on the SD card (reads every pack folder; call off the render thread
// when it matters, it walks ~35 files per pack).
std::vector<Installed> scanInstalled();
std::string installPath(const std::string& id);

} // namespace iconpack
