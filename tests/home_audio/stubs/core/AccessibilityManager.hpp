#pragma once
#include <string>
inline bool mixerAlive=false;
inline int speechShutdowns=0;
inline bool speechReleasedBeforeMixer=false;
class AccessibilityManager {
 public:
 void initialize(bool, const std::string&, const std::string&) {}
 void setSpeakHints(bool) {} void setSpeakContextEveryFocus(bool) {}
 void setSpeechRate(int) {} void cancelAnnouncement() {}
 void shutdown() { ++speechShutdowns; speechReleasedBeforeMixer=mixerAlive; }
};
