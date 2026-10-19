#include "TabBuilders.hpp"
#include "core/DebugLog.hpp"
#include <nxui/core/I18n.hpp>
#include <switch.h>
#include <algorithm>

namespace {

// setsys writes what the next boot reads; audctl changes what is playing now.
// The menu holds the audctl session only for the one call.
struct ScopedAudctl {
    bool ok = false;
    ScopedAudctl() { ok = R_SUCCEEDED(audctlInitialize()); }
    ~ScopedAudctl() { if (ok) audctlExit(); }
};

// libnx has no wrapper for these (audctl commands 30/31, FW 13.0.0+,
// switchbrew Audio services). A bool goes over the wire as one byte.
Result audctlSetSpeakerAutoMuteEnabledRaw(bool enable) {
    const u8 in = enable ? 1 : 0;
    return serviceDispatchIn(audctlGetServiceSession(), 30, in);
}

} // namespace

SettingsScreen::Tab settings::tabs::AudioTab::build(SettingsScreen& screen) {
    (void)screen;
    using Tab = SettingsScreen::Tab;
    using SettingItem = SettingsScreen::SettingItem;
    using ItemType = SettingsScreen::ItemType;
    auto& i18n = nxui::I18n::instance();
    Tab t;
    t.name = i18n.tr("settings.tabs.audio", "Audio");

    {
        SettingItem it;
        it.label = i18n.tr("settings.audio.speaker_auto_mute", "Speaker Auto-Mute");
        it.type = ItemType::Toggle;
        it.description = i18n.tr("settings.audio.speaker_auto_mute_desc", "Mute speakers automatically when headphones are connected.");
        bool val = false;
        setsysGetSpeakerAutoMuteFlag(&val);
        it.boolVal = val;
        it.anim01 = val ? 1.f : 0.f;
        it.onChange = [](SettingItem& self) {
            setsysSetSpeakerAutoMuteFlag(self.boolVal);
            // The saved flag is read at boot; FW 13+ can also flip it live
            // (issue #1).
            if (hosversionAtLeast(13, 0, 0)) {
                ScopedAudctl audctl;
                if (audctl.ok) {
                    const Result rc = audctlSetSpeakerAutoMuteEnabledRaw(self.boolVal);
                    DebugLog::log("[settings-audio] speaker auto-mute live=%d rc=0x%X",
                                  self.boolVal ? 1 : 0, rc);
                }
            }
        };
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.audio.headphone_warning", "Headphone Volume Warning");
        it.type = ItemType::Info;
        u32 count = 0;
        setsysGetHeadphoneVolumeWarningCount(&count);
        it.infoText = count > 0 ? i18n.tr("settings.audio.acknowledged", "Acknowledged")
                                : i18n.tr("common.active", "Active");
        t.items.push_back(std::move(it));
    }

    {
        SettingItem it;
        it.label = i18n.tr("settings.audio.tv_audio_output", "TV Audio Output");
        it.type = ItemType::Selector;
        it.description = i18n.tr("settings.audio.tv_audio_output_desc", "Select audio format sent to TV over HDMI.");
        it.options = {
            i18n.tr("settings.audio.mono", "Mono"),
            i18n.tr("settings.audio.stereo", "Stereo"),
            i18n.tr("settings.audio.surround", "Surround")
        };

        SetSysAudioOutputMode mode = (SetSysAudioOutputMode)1;
        setsysGetAudioOutputMode(SetSysAudioOutputModeTarget_Unknown0, &mode);
        it.intVal = std::clamp((int)mode, 0, 2);
        it.onChange = [](SettingItem& self) {
            setsysSetAudioOutputMode(SetSysAudioOutputModeTarget_Unknown0,
                                     (SetSysAudioOutputMode)self.intVal);
            // Apply to the TV right away too. audctl counts from 1
            // (Pcm1ch/Pcm2ch/Pcm6ch); setsys counts from 0 (issue #1).
            ScopedAudctl audctl;
            if (audctl.ok) {
                const Result rc = audctlSetAudioOutputMode(
                    AudioTarget_Tv, (AudioOutputMode)(self.intVal + 1));
                DebugLog::log("[settings-audio] tv output mode=%d live rc=0x%X",
                              self.intVal, rc);
            }
        };
        t.items.push_back(std::move(it));
    }

    return t;
}
