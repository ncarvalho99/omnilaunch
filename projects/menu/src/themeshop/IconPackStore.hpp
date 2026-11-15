#pragma once

#include "ThemeCatalogClient.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

// The icon store: its catalogue and the install/remove of a pack on the SD
// card. Every function here blocks (network or SD card) and runs on a worker.
//
// The catalogue (icons.json) sits beside the theme catalogue on the project's
// server. Its entries are mapped onto ThemeCatalogClient::Entry so the shop
// grid, previews and detail sheet draw them exactly like themes.
namespace iconstore {

constexpr const char* kCatalogUrl = "https://themes.nclabs.dev/icons.json";

struct Catalog {
    bool ok = false;
    std::string error;
    std::vector<ThemeCatalogClient::Entry> entries;
};

Catalog fetchCatalog();

struct InstallResult {
    bool ok = false;
    bool cancelled = false;
    std::string message;
};

using Progress = std::function<void(const std::string& label, float progress01)>;

// Downloads the pack's archive, unpacks it beside the destination and swaps it
// in, so a failed install never leaves a half pack where the old one was.
InstallResult install(const ThemeCatalogClient::Entry& entry, const Progress& progress,
                      const std::atomic<bool>* cancel = nullptr);

bool remove(const std::string& id, std::string& error);

} // namespace iconstore
