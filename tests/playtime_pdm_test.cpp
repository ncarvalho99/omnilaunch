#include "core/PlayTime.hpp"
#include "core/Config.hpp"
#include <switch.h>
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
using namespace switchu::menu::playtime;
namespace {
std::vector<PdmPlayEvent> events;
s32 first=10;
PdmPlayStatistics stats;
int reads=0;
bool failed=false;
u64 tick=0;
Profile selected{0x1122334455667788ULL,0x99aabbccddeeff00ULL};
PdmPlayEvent app(int type,u64 time,u64 id=123) {
 PdmPlayEvent e{}; e.play_event_type=PdmPlayEventType_Applet;
 e.event_data.applet.program_id[0]=id>>32; e.event_data.applet.program_id[1]=id;
 e.event_data.applet.applet_id=AppletId_application; e.event_data.applet.event_type=type;
 e.timestamp_steady=time; return e;
}
void session(u64 at) {
 events.push_back(app(PdmAppletEventType_Launch,at));
 PdmPlayEvent a{}; a.play_event_type=PdmPlayEventType_Account;
 a.event_data.account.uid[0]=selected[0]>>32; a.event_data.account.uid[1]=selected[0];
 a.event_data.account.uid[2]=selected[1]>>32; a.event_data.account.uid[3]=selected[1];
 events.push_back(a);
 events.push_back(app(PdmAppletEventType_InFocus,at+10));
 events.push_back(app(PdmAppletEventType_OutOfFocus,at+40));
 events.push_back(app(PdmAppletEventType_Exit,at+41));
}
u64 value() { auto out=queryAll({123},nullptr,selected); assert(out.size()==1); return out[0].second; }
}
bool accountUidIsValid(const AccountUid* uid){return uid->uid[0]||uid->uid[1];}
Result accountGetProfile(AccountProfile*,AccountUid){return 0;}
void accountProfileClose(AccountProfile*){}
Result accountGetLastOpenedUser(AccountUid*){return 1;}
Result accountListAllUsers(AccountUid*,s32,s32* count){*count=0;return 0;}
u64 armGetSystemTick(){return tick++;} u64 armTicksToNs(u64 ticks){return ticks;}
void svcSleepThread(std::int64_t){}
Result pdmqryInitialize(){return 0;} void pdmqryExit(){}
Result pdmqryQueryPlayStatisticsByApplicationId(u64,bool,PdmPlayStatistics* out){*out=stats;return 0;}
Result pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(u64,AccountUid user,bool,PdmPlayStatistics* out){assert(user.uid[0]==selected[0] && user.uid[1]==selected[1]);*out=stats;return 0;}
Result pdmqryGetAvailablePlayEventRange(s32* count,s32* start,s32* end){*count=events.size();*start=first;*end=first+events.size()-1;return 0;}
Result pdmqryQueryPlayEvent(s32 at,PdmPlayEvent* out,s32 max,s32* n){
 ++reads; if(failed)return 1; assert(at>=first); auto offset=at-first;
 *n=std::min<s32>(max,events.size()-offset); std::copy_n(events.begin()+offset,*n,out);return 0;
}
int main(){
 session(100); stats.first_entry_index=first; stats.last_entry_index=first+4;
 stats.playtime=999'000'000'000ULL;stats.total_launches=1;
 assert(value()==30'000'000'000ULL); // nonzero range + packed user ID + seconds->ns
 const int before=reads;assert(value()==30'000'000'000ULL && reads==before);
 // Completed game within the old TTL: new range must invalidate immediately.
 session(200);stats.total_launches=2;stats.last_entry_index=first+9;
 assert(value()==60'000'000'000ULL && reads>before);
 // Missing leading history: a readable log cannot replace lifetime stats.
 first=100;stats.first_entry_index=1;stats.last_entry_index=109;
 assert(value()==999'000'000'000ULL);
 // Same launch count but missing focus interval: preserve PDM statistics.
 events.clear();session(300);events.erase(events.begin()+2,events.begin()+4);
 first=200;stats.first_entry_index=200;stats.last_entry_index=202;stats.total_launches=1;
 assert(value()==999'000'000'000ULL);
 // Failed scan must publish no partial sums and is not retried for same range.
 events.clear();session(400);first=300;stats.first_entry_index=300;stats.last_entry_index=304;
 failed=true;assert(value()==999'000'000'000ULL);const int rejected=reads;
 assert(value()==999'000'000'000ULL && reads==rejected);
 failed=false;session(500);stats.total_launches=2;stats.last_entry_index=309;
 assert(value()==60'000'000'000ULL);
 // A console-wide request retains its original statistics, not profile events.
 assert(query(123).value()==999'000'000'000ULL);
 std::cout<<"raw PDM range/cache/coverage tests PASS\n";
}
