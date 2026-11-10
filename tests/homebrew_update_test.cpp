// Host tests for the homebrew update catalogue (HomeNX G5).
#include "homebrew/HomebrewUpdateCatalog.hpp"
#include <cassert>
#include <iostream>

using namespace hbupdate;

int main() {
    // Version comparison.
    assert(isNewer("1.2.0", "1.1.9"));
    assert(isNewer("v1.2.0", "1.1.9"));
    assert(!isNewer("1.1.9", "v1.1.9"));
    assert(!isNewer("1.0", "1.0.0"));
    assert(isNewer("1.0.1", "1.0"));
    assert(!isNewer("", "1.0"));
    assert(isNewer("2.0", ""));
    assert(!isNewer("1.0.8", "1.0.10"));
    // JKSV's store version is a date (12/02/2025); the NACP one may be another date.
    assert(!isNewer("12/02/2025", "12/02/2025"));
    // Huge digit runs saturate, no overflow.
    assert(!isNewer("99999999999999999999999", "99999999999999999999999"));

    assert(squash("Hekate Toolbox") == "hekatetoolbox");
    assert(knownRepo("JKSV") == "J-D-K/JKSV");
    assert(knownRepo("Nope").empty());

    // Store parsing keeps only safe packages with an .nro binary.
    const std::string repo = R"({"packages":[
        {"name":"sphaira","title":"sphaira","version":"1.0.8","binary":"/switch/sphaira/sphaira.nro","description":"x","screens":3},
        {"name":"evil/../x","title":"Evil","version":"9","binary":"/switch/evil/evil.nro"},
        {"name":"theme","title":"A theme","version":"1","binary":"none"},
        {"name":"ftpd","title":"ftpd pro","version":"3.2.1","binary":"/switch/ftpd/ftpd.nro"}
    ]})";
    auto store = parseStore(repo);
    assert(store.size() == 2);
    assert(store[0].name == "sphaira" && store[0].version == "1.0.8");
    assert(parseStore("not json").empty());

    Installed sph{"sdmc:/switch/sphaira/sphaira.nro", "sphaira", "1.0.6"};
    const StorePackage* p = matchStore(store, sph);
    assert(p && p->name == "sphaira");
    Offer o = storeOffer(*p);
    assert(o.url == "https://switch.cdn.fortheusers.org/zips/sphaira.zip");
    assert(o.zipEntry == "switch/sphaira/sphaira.nro");
    // By title when the path differs.
    Installed ftp{"sdmc:/switch/other/ftp.nro", "ftpd pro", "3.0"};
    assert(matchStore(store, ftp) && matchStore(store, ftp)->name == "ftpd");
    Installed none{"sdmc:/switch/x/x.nro", "Unknown", "1"};
    assert(!matchStore(store, none));

    // GitHub release: the asset named like the installed file wins.
    const std::string rel = R"({"tag_name":"v2.1.0","assets":[
        {"name":"other.nro","browser_download_url":"https://github.com/a/b/releases/download/v2.1.0/other.nro","size":10},
        {"name":"JKSV.nro","browser_download_url":"https://github.com/a/b/releases/download/v2.1.0/JKSV.nro","size":20},
        {"name":"bad.nro","browser_download_url":"http://insecure/bad.nro","size":5}]})";
    Installed jksv{"sdmc:/switch/JKSV/JKSV.nro", "JKSV", "2.0"};
    Offer g;
    assert(githubOffer(rel, "J-D-K/JKSV", jksv, 15, g));
    assert(g.latest == "2.1.0" && g.zipEntry.empty());
    assert(g.url.find("/JKSV.nro") != std::string::npos);
    assert(g.source == "GitHub J-D-K/JKSV");
    // Zip-only release: not offered.
    assert(!githubOffer(R"({"tag_name":"1","assets":[{"name":"a.zip","browser_download_url":"https://x/a.zip"}]})",
                        "a/b", jksv, 1, g));
    // Tag without digits: only when the size differs.
    const std::string latest = R"({"tag_name":"latest","assets":[{"name":"JKSV.nro","browser_download_url":"https://x/JKSV.nro","size":20}]})";
    assert(!githubOffer(latest, "a/b", jksv, 20, g));
    assert(githubOffer(latest, "a/b", jksv, 19, g) && g.latest == "latest");

    // State round trip and pending rules.
    State s;
    s.checkedAt = 1794308100;
    s.offers[sph.path] = o;
    s.offers["sdmc:/switch/bad/bad.nro"] = Offer{"2", "x", "http://insecure", ""};
    s.installed["sdmc:/switch/old/old.nro"] = "1.0";
    State r = parseState(dumpState(s));
    assert(r.checkedAt == s.checkedAt);
    assert(r.offers.size() == 1); // the non-HTTPS offer is dropped on load
    assert(r.offers.at(sph.path).zipEntry == o.zipEntry);
    assert(r.installed.at("sdmc:/switch/old/old.nro") == "1.0");
    assert(pendingOffer(r, sph));
    Installed sphNew{sph.path, sph.name, "1.0.8"};
    assert(!pendingOffer(r, sphNew));
    // Already installed by OmniLaunch at that version: not offered again even
    // when the NACP inside reports an older number.
    r.installed[sph.path] = "1.0.8";
    assert(!pendingOffer(r, sph));
    assert(parseState("{broken").offers.empty());

    assert(managedPath("sdmc:/switch/OmniLaunch/OmniLaunch.nro"));
    assert(managedPath("sdmc:/hbmenu.nro"));
    assert(!managedPath("sdmc:/switch/JKSV/JKSV.nro"));

    std::cout << "homebrew_update_test: all passed" << std::endl;
    return 0;
}
