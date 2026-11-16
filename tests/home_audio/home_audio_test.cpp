// Host contract tests: production HomeAudio/HomeCore/ThreadPool, fake devices.
// These validate ownership/snapshot/order logic, not SDL/espeak or console sound.
#include "home/HomeCore.hpp"
#include "home/HomeStyle.hpp"
#include "themeshop/ThemeHttp.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include <cassert>
#include <chrono>
#include <future>
#include <thread>
#include <iostream>

int main() {
 using namespace home;
 HomeCore core;
 auto& service=core.audioServices();
 auto& audio=service.audio;
 service.startTutorial();
 assert(audio.opens==1 && audio.playlist.size()==1);
 service.endTutorial();
 assert(audio.closes==0 && audio.playlist.empty());
 nxui::ThreadPool pool(2);
 AppConfig config;
 service.themeMusicTracks={"theme/a.mp3","theme/b.mp3"};
 service.startClassic(pool,config,{});
 service.waitForLoad();
 service.started=true; // frontend publication, as finishAudioLoad does.
 assert(audio.opens==1 && audio.playlist.size()==2);
 audio.play(); audio.paused=true;
 const int plays=audio.plays, stops=audio.stops, clears=audio.clears;
 service.startClassic(pool,config,{});
 assert(!service.loadFuture.valid());
 assert(!service.reloadMusicTracks(config,{}));
 assert(audio.plays==plays && audio.stops==stops && audio.clears==clears && audio.paused);
 service.themeMusicTracks={"theme/new.mp3"};
 assert(service.reloadMusicTracks(config,{}));
 assert(audio.playlist.size()==1 && audio.playlist[0].path=="theme/new.mp3");
 // Hold A in a real two-worker pool, mutate caller config, then request B.
 // B must wait before replacing A's future; the A snapshot stays immutable.
 config.audioSourcePreference="theme_only";
 { std::lock_guard<std::mutex> lk(gateMutex); blockLoad=true; enteredLoad=false; releaseLoad=false; }
 service.queuePreset(pool,"wiiu",config,{});
 { std::unique_lock<std::mutex> lk(gateMutex);
   assert(gateCv.wait_for(lk,std::chrono::seconds(5),[]{return enteredLoad;})); }
 config.audioSourcePreference="custom_only";
 { std::lock_guard<std::mutex> lk(gateMutex); releaseLoad=true; } gateCv.notify_all();
 service.waitForLoad();
 assert(audio.playlist.size()==1); // A kept its theme_only configuration snapshot.
 { std::lock_guard<std::mutex> lk(gateMutex); blockLoad=true; enteredLoad=false; releaseLoad=false; }
 service.queuePreset(pool,"wiiu",config,{});
 { std::unique_lock<std::mutex> lk(gateMutex);
   assert(gateCv.wait_for(lk,std::chrono::seconds(5),[]{return enteredLoad;})); }
 auto next=std::async(std::launch::async,[&]{service.queuePreset(pool,"wiiu",config,{});});
 assert(next.wait_for(std::chrono::milliseconds(40))==std::future_status::timeout);
 { std::lock_guard<std::mutex> lk(gateMutex); releaseLoad=true; } gateCv.notify_all();
 next.get(); service.waitForLoad();
 assert(audio.playlist.empty()); // B's custom_only snapshot supersedes A.
 assert(!service.loadFuture.valid());
 // Finalization after detach consumes an outstanding load before speech/mixer.
 service.queuePreset(pool,"wiiu",config,{});
 // C4 HTTP cancel scope: a frontend switch cancels only the leaving frontend's
 // requests; the next frontend can use the network. The terminal cancel stays.
 {
  using themeshop::http::scope;
  auto oldReq = scope.begin();            // Classic request in flight
  assert(!scope.cancelled(oldReq));
  core.frontendStopping();                // Classic leaving
  assert(scope.cancelled(oldReq));
  auto during = scope.begin();            // issued between the two frontends
  assert(scope.cancelled(during));
  core.frontendStarted();                 // Cards starting
  assert(scope.cancelled(oldReq) && scope.cancelled(during)); // not revived
  auto newReq = scope.begin();
  assert(!scope.cancelled(newReq));       // network usable after the switch
  core.frontendStopping(); core.frontendStarted();
  assert(scope.cancelled(newReq) && !scope.cancelled(scope.begin()));
 }
 core.catalog().setPendingTransform([](std::vector<PendingApp>&){});
 core.startBluetooth();
 assert(bluetooth::initializeCalls==1);
 std::vector<int> order;
 core.addShutdownHook(ShutdownPhase::ActivityReleased,"activity",[&]{order.push_back(0);});
 core.addShutdownHook(ShutdownPhase::GpuResources,"gpu",[&]{order.push_back(1);});
 core.runShutdownPhase(ShutdownPhase::ProcessExit);
 assert((order==std::vector<int>{0,1}));
 assert(!service.loadFuture.valid() && audio.closes==1 && speechShutdowns==1);
 assert(speechReleasedBeforeMixer);
 core.runShutdownPhase(ShutdownPhase::ProcessExit);
 assert(audio.closes==1 && speechShutdowns==1);
 // C4/C5b Services phase: pool joined, SteamGridDB scrape stopped, then HTTP runtime, then Bluetooth; once each.
 assert(themeshop::http::shutdownCalls==1 && bluetooth::finalizeCalls==1 && !core.catalog().transform);
 assert((fake::serviceOrder==std::vector<std::string>{"catalogue","steamgriddb","net","http","bluetooth"}));
 // Process shutdown itself is terminal (no explicit cancel, scope was open):
 // reopening a frontend scope afterwards does not clear it.
 core.frontendStarted();
 assert(themeshop::http::scope.cancelled(themeshop::http::scope.begin()));
 // C5a play-time service: host build has no pdm, so a batch yields no figures.
 {
  HomeCore pcore;
  AppConfig pcfg;
  assert(pcore.playtime().poll(pcfg)==-1 && !pcore.playtime().running());
  assert(!pcore.playtime().start(pcore.threadPool(), {}, 5));
  assert(!pcore.playtime().running());          // empty list: nothing queued
  assert(pcore.playtime().start(pcore.threadPool(), {1,2}, 7));
  assert(pcore.playtime().running());
  assert(!pcore.playtime().start(pcore.threadPool(), {4}, 9)); // one batch at a time
  int r=-1; for(int i=0;i<200 && r<0;++i){ r=pcore.playtime().poll(pcfg); if(r<0) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
  assert(r==0 && !pcore.playtime().running());
  // No pdm on host, so no title answers: the launch sequence stays unconfirmed.
  assert(pcfg.playtimeCheckedSequence==0);
  pcore.playtime().start(pcore.threadPool(), {3}, 8);
  pcore.runShutdownPhase(ShutdownPhase::ProcessExit); // collects it before the pool joins
  assert(!pcore.playtime().running());
 } // C7 home style: stable strings, unknown/missing means Classic.
 {
  using home::HomeStyle;
  assert(home::parseHomeStyle("cards")==HomeStyle::Cards);
  assert(home::parseHomeStyle("classic")==HomeStyle::Classic);
  assert(home::parseHomeStyle("")==HomeStyle::Classic && home::parseHomeStyle("Cards")==HomeStyle::Classic);
  assert(std::string(home::homeStyleName(HomeStyle::Cards))=="cards" && std::string(home::homeStyleName(HomeStyle::Classic))=="classic");
 } // C3 HomeCore launcher, pool, config, and quiesce routing contracts
 bool quiesceCalled = false;
 core.setFrontendQuiesce([&quiesceCalled] { quiesceCalled = true; });
 core.setRequestExit([] {});
 core.enterSleep(); // real Core -> launcher callback -> writer drain
 assert(core.discord().asleep && core.discord().terminalStops == 0);
 core.discord().wake();
 core.quiesceWriters();
 assert(quiesceCalled);
 assert(core.discord().terminalStops == 1);
 core.setFrontendQuiesce(nullptr);

 assert(core.config().soundPreset == "wiiu");
 assert(!core.threadPool().acceptingTasks()); // Pool was shut down in Services phase above

 std::cout<<"PASS tutorial reuse, unchanged/changed playlist, serialized snapshots, drain/shutdown order, C3 launcher/pool contracts, C4 http scope/service order\n";
}
