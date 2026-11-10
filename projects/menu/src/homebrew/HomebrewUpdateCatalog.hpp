#pragma once
// Homebrew update catalogue (HomeNX G5, requirements C-NET-6, C-UI-13).
// Pure logic, no network or SD access, so the host tests cover it:
// where a newer .nro comes from (Homebrew App Store, or GitHub releases for a
// list of well-known projects), version comparison, and the saved state.
// Adapted from upstream/homenx @ 0448cd4, projects/updater/src/Catalog.{hpp,cpp}
// (GPL-3.0).

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace hbupdate {

/// One installed .nro and where its newer version is.
struct Offer {
    std::string latest;     ///< newest known version, as the source writes it
    std::string source;     ///< "GitHub owner/repo" or "Homebrew App Store"
    std::string url;        ///< HTTPS download (.nro, or the store's .zip)
    std::string zipEntry;   ///< path of the .nro inside the zip ("" = not a zip)
};

struct Installed {
    std::string path;       ///< sdmc:/switch/.../x.nro
    std::string name;       ///< NACP name
    std::string version;    ///< NACP display version
};

/// Lower-case letters and digits only ("Hekate Toolbox" -> "hekatetoolbox").
std::string squash(const std::string& s);

/// True when `latest` names a newer version than `installed` ("v1.2.0" > "1.1.9").
/// Numbers are compared in order; versions without digits compare as text.
bool isNewer(const std::string& latest, const std::string& installed);

/// GitHub repository for a well-known homebrew, by squashed NACP name; "" if none.
std::string knownRepo(const std::string& name);

/// The Homebrew App Store's repo.json, reduced to what matching needs.
struct StorePackage {
    std::string name;     ///< package id, also the zip name
    std::string title;
    std::string version;
    std::string binary;   ///< "/switch/x/x.nro"
};
/// Parses repo.json; drops the large text fields while parsing.
std::vector<StorePackage> parseStore(const std::string& json);

/// Matches by .nro path first, then by squashed title.
const StorePackage* matchStore(const std::vector<StorePackage>& store, const Installed& app);
Offer storeOffer(const StorePackage& pkg);

/// A GitHub "releases/latest" answer: an Offer for the .nro asset (the one
/// named like the installed file first). Zip-only releases are not offered:
/// the .nro inside cannot be identified safely. A release tag without digits
/// ("latest") is offered only when the asset size differs from the file's.
bool githubOffer(const std::string& json, const std::string& repo, const Installed& app,
                 std::int64_t installedSize, Offer& out);

/// What the updates file keeps between runs.
struct State {
    std::int64_t checkedAt = 0;                       ///< POSIX seconds
    std::map<std::string, Offer> offers;              ///< by .nro path
    std::map<std::string, std::string> installed;     ///< path -> version this app put there
};
State parseState(const std::string& json);
std::string dumpState(const State& state);

/// The offer for `app` if it is still newer than what is on the card and not
/// the very version OmniLaunch already installed there (a store can number a
/// release differently from the NACP inside it).
const Offer* pendingOffer(const State& state, const Installed& app);

/// Paths never updated here: OmniLaunch itself and its loader, hbmenu.
bool managedPath(const std::string& path);

} // namespace hbupdate
