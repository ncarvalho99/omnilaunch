// Host contract tests: production HomeAudio/HomeCore/ThreadPool, fake devices.
// These validate ownership/snapshot/order logic, not SDL/espeak or console sound.
#include "home/HomeCore.hpp"
#include <cassert>
#include <chrono>
#include <future>
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
 std::vector<int> order;
 core.addShutdownHook(ShutdownPhase::ActivityReleased,"activity",[&]{order.push_back(0);});
 core.addShutdownHook(ShutdownPhase::GpuResources,"gpu",[&]{order.push_back(1);});
 core.runShutdownPhase(ShutdownPhase::ProcessExit);
 assert((order==std::vector<int>{0,1}));
 assert(!service.loadFuture.valid() && audio.closes==1 && speechShutdowns==1);
 assert(speechReleasedBeforeMixer);
 core.runShutdownPhase(ShutdownPhase::ProcessExit);
 assert(audio.closes==1 && speechShutdowns==1);

 // C3 HomeCore launcher, pool, config, and quiesce routing contracts
 bool quiesceCalled = false;
 core.setFrontendQuiesce([&quiesceCalled] { quiesceCalled = true; });
 core.quiesceWriters();
 assert(quiesceCalled);
 core.setFrontendQuiesce(nullptr);

 assert(core.config().soundPreset == "wiiu");
 assert(!core.threadPool().acceptingTasks()); // Pool was shut down in Services phase above

 std::cout<<"PASS tutorial reuse, unchanged/changed playlist, serialized snapshots, drain/shutdown order, C3 launcher/pool contracts\n";
}
