#pragma once
#include "core/AccessibilityManager.hpp"
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>
enum class Sfx { Navigate, Activate, PageChange, ModalShow, ModalHide, LaunchGame,
 ThemeToggle, ToggleOff, SliderUp, SliderDown, ConfirmPositive, Volume };
struct TrackInfo { std::string path,title; };
inline std::mutex gateMutex;
inline std::condition_variable gateCv;
inline bool blockLoad=false, enteredLoad=false, releaseLoad=false;
class AudioManager {
 public:
 int opens=0, closes=0, plays=0, stops=0, clears=0;
 bool playing=false, paused=false;
 std::vector<TrackInfo> playlist;
 bool initialize() { if(!mixerAlive) { mixerAlive=true; ++opens; } return true; }
 void shutdown() { if(mixerAlive) { mixerAlive=false; ++closes; } }
 void setVolume(float) {} void setSfxVolume(float) {} void setShuffle(bool) {}
 bool isPlaying() const { return playing; }
 void stop() { ++stops; playing=false; paused=false; }
 void stopAll() { stop(); }
 void clearTracks() { ++clears; playlist.clear(); }
 void clearSfx() {}
 void play() { ++plays; playing=true; paused=false; }
 std::vector<TrackInfo> tracks() const { return playlist; }
 void loadTrack(const std::string& path, const std::string& title="") {
  auto stem=path.substr(path.find_last_of('/')+1); stem=stem.substr(0,stem.find_last_of('.'));
  playlist.push_back({path,title.empty()?stem:title});
 }
 void loadNamedSfx(const std::string&,const std::string&,float) {}
 void loadSfx(Sfx id,const std::string&) {
  if(id!=Sfx::Navigate) return;
  std::unique_lock<std::mutex> lk(gateMutex);
  if(!blockLoad) return;
  enteredLoad=true; gateCv.notify_all();
  gateCv.wait(lk,[]{return releaseLoad;}); blockLoad=false;
 }
};
