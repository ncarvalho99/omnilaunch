#pragma once
#include <cstdint>
using u64=std::uint64_t; using s32=std::int32_t; using Result=unsigned;
#define R_FAILED(rc) ((rc)!=0)
#define R_SUCCEEDED(rc) ((rc)==0)
struct AccountUid { u64 uid[2]; };
struct AccountProfile {};
bool accountUidIsValid(const AccountUid*);
Result accountGetProfile(AccountProfile*,AccountUid);
void accountProfileClose(AccountProfile*);
Result accountGetLastOpenedUser(AccountUid*);
Result accountListAllUsers(AccountUid*,s32,s32*);
u64 armGetSystemTick();
u64 armTicksToNs(u64);
void svcSleepThread(std::int64_t);
enum {AppletId_application=0x1, PdmPlayLogPolicy_All=0};
enum {PdmPlayEventType_Applet, PdmPlayEventType_Account, PdmPlayEventType_PowerStateChange,
      PdmPlayEventType_OperationModeChange, PdmPlayEventType_Initialize};
enum {PdmAppletEventType_Launch, PdmAppletEventType_Exit, PdmAppletEventType_InFocus,
      PdmAppletEventType_OutOfFocus,PdmAppletEventType_OutOfFocus4,PdmAppletEventType_Exit5,PdmAppletEventType_Exit6};
struct PdmPlayEvent {
 union {
  struct {std::uint32_t program_id[2]; std::uint32_t version; std::uint8_t applet_id,storage_id,log_policy,event_type; std::uint8_t unused[12];} applet;
  struct {std::uint32_t uid[4];std::uint32_t application_id[2];std::uint8_t type;} account;
 } event_data{};
 std::uint8_t play_event_type=0;
 u64 timestamp_user=0,timestamp_network=0,timestamp_steady=0;
};
struct PdmPlayStatistics {u64 program_id=0;std::uint32_t first_entry_index=0;u64 first_timestamp_user=0;std::uint32_t last_entry_index=0;u64 last_timestamp_user=0;u64 playtime=0;std::uint32_t total_launches=0;};
Result pdmqryInitialize(); void pdmqryExit();
Result pdmqryQueryPlayStatisticsByApplicationId(u64,bool,PdmPlayStatistics*);
Result pdmqryQueryPlayStatisticsByApplicationIdAndUserAccountId(u64,AccountUid,bool,PdmPlayStatistics*);
Result pdmqryGetAvailablePlayEventRange(s32*,s32*,s32*);
Result pdmqryQueryPlayEvent(s32,PdmPlayEvent*,s32,s32*);
