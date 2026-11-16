// Production catalogue/art state machines; only GPU, loader and file sources fake.
#include "cards/CardsCatalog.hpp"
#include "cards/CardsArt.hpp"
#include "cards/CardsPages.hpp"
#include "launcher/AppListLoader.hpp"
#include <nxui/core/ThreadPool.hpp>
#include <nxui/core/GpuDevice.hpp>
#include <nxui/core/Renderer.hpp>
#include <cassert>
#include <iostream>
#include <atomic>
#include <future>

namespace iconpack { std::string resolve(const std::string&, const std::string& fallback) { return fallback; } }
namespace home { std::string installedBrowserPath() { return {}; } }
std::atomic<int> scans{0};
std::promise<void> scanEntered, scanRelease;
auto scanGate = scanRelease.get_future().share();
std::vector<HomebrewApp> scanHomebrew(const std::string&,bool) {
    if (++scans == 1) { scanEntered.set_value(); scanGate.wait(); }
    HomebrewApp app; app.path="sdmc:/switch/test.nro"; app.name="Test"; app.version="1";
    return {app};
}
std::vector<std::uint8_t> readHomebrewIcon(const std::string&) { return {}; }

int main() {
    AppConfig cfg;
    nxui::ThreadPool pool(1);
    nxui::GpuDevice gpu;
    nxui::Renderer ren;
    cards::CardsCatalog catalog;
    AppListLoader loader;
    for (unsigned i = 1; i <= 60; ++i) {
        AppEntry e; e.titleId = 0x0100000000010000ULL+i;
        e.title = "game " + std::to_string(i); e.kind = GridEntryKind::Application;
        loader.apps.push_back(e);
    }
    catalog.startLoad(loader,pool);
    assert(catalog.poll(loader,cfg));
#ifdef SWITCHU_MENU
    scanEntered.get_future().wait();
    auto navigation=std::async(std::launch::async,[&] {
        catalog.setPage(AppConfig::CardsPrefs::kHomebrew,cfg);
        catalog.setPage(AppConfig::CardsPrefs::kGames,cfg);
        assert(!catalog.pollPageFiles(pool,cfg));
    });
    const bool responsive=navigation.wait_for(std::chrono::milliseconds(200))==std::future_status::ready;
    scanRelease.set_value();
    navigation.get();
    assert(responsive); // a scan blocked on SD must never block page navigation
#endif
    pool.waitForIdle();
    catalog.pollPageFiles(pool,cfg);
#ifdef SWITCHU_MENU
    for(int i=0;i<5;++i) {
        catalog.setPage(AppConfig::CardsPrefs::kHomebrew,cfg);
        assert(catalog.count()==1);
        catalog.setPage(AppConfig::CardsPrefs::kGames,cfg);
    }
    assert(scans==1); // page changes never reopen NRO metadata
#endif
    auto pixels = std::make_shared<const std::vector<std::uint8_t>>(16,255);
    cards::SnapItem item; item.titleId=catalog.at(0).app.titleId;
    item.rgba=pixels; item.w=item.h=2;
    assert(catalog.adoptSnapshotIcon(gpu,ren,item)==1);
    auto* original=catalog.at(0).icon.get();
    catalog.setPage(AppConfig::CardsPrefs::kHomebrew,cfg);
    catalog.setPage(AppConfig::CardsPrefs::kNintendo,cfg);
    catalog.setPage(AppConfig::CardsPrefs::kMedia,cfg);
    catalog.setPage(AppConfig::CardsPrefs::kGames,cfg);
    assert(catalog.at(catalog.indexOf(item.titleId)).icon.get()==original);
    catalog.releaseAll();
    catalog.setPage(AppConfig::CardsPrefs::kGames,cfg);
    assert(!catalog.at(catalog.indexOf(item.titleId)).icon);

    cards::CardsArt art;
    art.setConfig(&cfg);
    item.kind=1;
    assert(art.adoptSnapshotCover(gpu,ren,item,10.f));
    auto* cover=art.cover(item.titleId);
    // Empty other-page keep list must not evict a warm game cover.
    art.pump(gpu,ren,pool,{}, {},0,11.f,1);
    assert(art.cover(item.titleId)==cover && art.coverAge(item.titleId,11.f)>1.f);
    // Global 48-slot limit, protected focus survives pressure.
    for(unsigned i=1;i<=60;++i) {
        auto extra=item; extra.titleId=100+i;
        art.adoptSnapshotCover(gpu,ren,extra,12.f);
        art.pump(gpu,ren,pool,{}, {item.titleId},0,12.f+i,1);
    }
    int retained=art.cover(item.titleId)?1:0;
    for(unsigned i=1;i<=60;++i) retained+=art.cover(100+i)?1:0;
    assert(retained<=48 && art.cover(item.titleId)==cover);
    // Missing-cover records must not consume the 48-texture budget.
    for(unsigned i=0;i<140;++i) {
        art.pump(gpu,ren,pool,{1000+i},{item.titleId},0,100.f+i,1);
        pool.waitForIdle();
        art.pump(gpu,ren,pool,{}, {item.titleId},0,100.f+i,1);
    }
    assert(art.cover(item.titleId)==cover);
    art.releaseAll();
    assert(!art.cover(item.titleId));
    pool.shutdown();
    std::cout << "production Cards page retention, budget, invalidation PASS\n";
}
