// CardsHomeActivity: the Settings rows that open something of their own
// (HomeNX E7, C-ST): the background picture, Bluetooth audio devices, the
// controller test, the time zone and the manual date/time. Console settings
// go through settings::system_prefs, the same calls Classic Settings uses.
// Same class and thread (main) rules as CardsHomeSheets.cpp.

#include "cards/CardsHomeActivity.hpp"
#include "bluetooth/BluetoothManager.hpp"
#include "core/DebugLog.hpp"
#include "home/HomeCore.hpp"
#include "settings/SystemPrefs.hpp"
#include "widgets/OverlayDialog.hpp"
#include <nxui/Application.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/ThreadPool.hpp>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <future>
#include <filesystem>
#include <switch.h>

namespace cards {

namespace {

constexpr const char* kWallDir = "sdmc:/config/OmniLaunch/backgrounds";
constexpr std::size_t kWallMax = 48;  // rows in the picker
constexpr int kWallMaxSide = 1280;     // drawn full screen at 720p

bool isPicture(const std::filesystem::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".jpg" || ext == ".jpeg" || ext == ".png";
}

std::vector<std::string> listWallpapers() {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(kWallDir, ec)) {
        if (e.is_regular_file(ec) && isPicture(e.path()))
            out.push_back(e.path().string());
        if (out.size() >= 512)
            break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::string fileName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

} // namespace

// ── Sub-sheets opened from Settings ─────────────────────────────────────────
void CardsHomeActivity::openSettingsAt(int section, int row) {
    m_subSheetTitle.clear();
    m_btAudioSheet = false;
    m_sheet.open(nxui::I18n::instance().tr("sidebar.settings", "Settings"), settingsSections(), section, row);
    m_sheet.onReorderSection([this](int d) { reorderSettingsSection(d); });
}

void CardsHomeActivity::openSubSheet(std::string title, std::vector<CardsSheet::Section> sections) {
    // B on the sub-sheet comes back to the Settings row it was opened from.
    m_settingsReturnSection = m_sheet.section();
    m_settingsReturnRow = m_sheet.row();
    m_subSheetTitle = title;
    m_sheet.onReorderSection({});
    m_sheet.open(std::move(title), std::move(sections));
}

// ── Background picture ──────────────────────────────────────────────────────
void CardsHomeActivity::openWallpaperSheet() {
    using Row = CardsSheet::Row;
    auto& tr = nxui::I18n::instance();
    CardsSheet::Section s{tr.tr("cards.set_wallpaper", "Background picture"), {}, "sun",
                          nxui::Color(0.66f, 0.33f, 0.97f)};
    auto choose = [this](const std::string& path) {
        auto& c = m_core.config().cards;
        c.wallpaper = path;
        saveConfig("cards wallpaper");
        startWallpaper(false);
    };
    Row none;
    none.label = tr.tr("cards.wall_game_art", "Each game's art");
    none.toggle = [this]() { return m_core.config().cards.wallpaper.empty(); };
    none.onA = [choose]() { choose(std::string()); };
    s.rows.push_back(std::move(none));
    std::error_code ec;
    std::filesystem::create_directories(kWallDir, ec); // so the folder shows up over MTP
    std::vector<std::string> files = listWallpapers();
    if (files.size() > kWallMax)
        files.resize(kWallMax);
    for (const auto& path : files) {
        Row r;
        r.label = fileName(path);
        r.toggle = [this, path]() { return m_core.config().cards.wallpaper == path; };
        r.onA = [choose, path]() { choose(path); };
        s.rows.push_back(std::move(r));
    }
    if (files.empty()) {
        Row hint;
        hint.label = tr.tr("cards.wall_none", "No pictures yet");
        hint.detail = tr.tr("cards.wall_none_desc", "Copy JPG or PNG files to config/OmniLaunch/backgrounds");
        hint.enabled = false;
        s.rows.push_back(std::move(hint));
    }
    std::vector<CardsSheet::Section> sections;
    sections.push_back(std::move(s));
    openSubSheet(tr.tr("cards.set_wallpaper", "Background picture"), std::move(sections));
}

void CardsHomeActivity::waitWallpaper() {
    if (m_wallJob.valid())
        m_wallJob.wait();
}

void CardsHomeActivity::startWallpaper(bool shuffle) {
    waitWallpaper();
    auto& c = m_core.config().cards;
    const std::string chosen = c.wallpaper;
    if (!shuffle && chosen.empty()) {
        m_wallImage.reset();
        m_wallTex.reset(); // retired by the renderer after the GPU is done
        m_wallPath.clear();
        return;
    }
    auto image = std::make_shared<nxui::DecodedImage>();
    auto path = std::make_shared<std::string>(chosen);
    const std::string previous = m_wallPath;
    m_wallImage = image;
    try {
        m_wallJob = m_core.threadPool().submit([image, path, shuffle, previous]() {
            if (shuffle) {
                // Another of the owner's pictures at every start (HomeNX
                // "Shuffle background"); the saved choice is left alone.
                std::vector<std::string> all = listWallpapers();
                if (all.size() > 1)
                    all.erase(std::remove(all.begin(), all.end(), previous), all.end());
                if (!all.empty())
                    *path = all[armGetSystemTick() % all.size()];
            }
            if (!path->empty())
                *image = nxui::Texture::decodeFile(*path, kWallMaxSide);
        });
    } catch (...) {
        m_wallImage.reset(); // pool shutting down
        return;
    }
    m_wallPending = path;
}

void CardsHomeActivity::pumpWallpaper() {
    if (!m_wallJob.valid() ||
        m_wallJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return;
    try { m_wallJob.get(); } catch (...) {}
    auto image = std::move(m_wallImage);
    auto path = std::move(m_wallPending);
    if (!image || !path)
        return;
    if (path->empty()) {
        m_wallTex.reset();
        m_wallPath.clear();
        return;
    }
    auto tex = std::make_unique<nxui::Texture>();
    if (image->valid() && tex->loadFromDecoded(app().gpu(), app().renderer(), *image)) {
        m_wallTex = std::move(tex);
        m_wallPath = *path;
        DebugLog::log("[cards] wallpaper %s", m_wallPath.c_str());
    } else {
        DebugLog::log("[cards] wallpaper unreadable: %s", path->c_str());
        showToast(nxui::I18n::instance().tr("cards.wall_failed", "That picture could not be read"));
    }
}

// ── Bluetooth audio devices (Classic BluetoothTab) ──────────────────────────
std::vector<CardsSheet::Section> CardsHomeActivity::bluetoothAudioSections() {
    using Row = CardsSheet::Row;
    auto& tr = nxui::I18n::instance();
    CardsSheet::Section paired{tr.tr("settings.bluetooth.paired_devices", "Paired Devices"), {}, "bluetooth",
                               nxui::Color(0.05f, 0.65f, 0.91f)};
    CardsSheet::Section found{tr.tr("settings.bluetooth.discover_devices", "Discover Devices"), {}, "wifi",
                              nxui::Color(0.08f, 0.72f, 0.65f)};
    if (!bluetooth::IsAvailable()) {
        Row r;
        r.label = "Bluetooth";
        r.value = []() { return std::string("..."); };
        r.enabled = false;
        paired.rows.push_back(r);
        found.rows.push_back(std::move(r));
    } else {
        const auto connected = bluetooth::GetConnectedAudioDevice();
        const auto list = bluetooth::ListPairedAudioDevices();
        if (list.empty()) {
            Row r;
            r.label = tr.tr("settings.bluetooth.no_paired_devices", "No paired devices");
            r.enabled = false;
            paired.rows.push_back(std::move(r));
        }
        for (const auto& device : list) {
            const bool isConnected = bluetooth::IsDeviceValid(connected) &&
                                     bluetooth::AddressesEqual(connected.addr, device.addr);
            Row r;
            r.label = bluetooth::DeviceName(device);
            r.value = [isConnected]() {
                auto& t = nxui::I18n::instance();
                return isConnected ? t.tr("settings.bluetooth.connected", "Connected")
                                   : t.tr("settings.bluetooth.not_connected", "Not connected");
            };
            r.onA = [this, device, isConnected]() {
                auto& t = nxui::I18n::instance();
                std::vector<OverlayDialog::ButtonDef> buttons;
                if (isConnected)
                    buttons.push_back({t.tr("settings.bluetooth.disconnect", "Disconnect"),
                                       [device]() { bluetooth::DisconnectAudioDevice(device); }, true});
                else
                    buttons.push_back({t.tr("settings.bluetooth.connect", "Connect"),
                                       [device]() { bluetooth::ConnectAudioDevice(device); }, true});
                buttons.push_back({t.tr("settings.bluetooth.unpair", "Unpair"),
                                   [device]() { bluetooth::UnpairAudioDevice(device); }, true});
                buttons.push_back({t.tr("button.cancel", "Cancel"), []() {}, true});
                m_audio.playSfx(Sfx::ModalShow);
                m_userSelect->show(t.tr("settings.bluetooth.device_actions", "Device Actions"),
                                   bluetooth::DeviceName(device), std::move(buttons), 0, {});
                focusManager().setFocus(m_userSelect.get());
            };
            paired.rows.push_back(std::move(r));
        }
        Row search;
        search.label = tr.tr("settings.bluetooth.search_inactive", "Device search");
        search.toggle = []() { return bluetooth::IsDiscovering(); };
        search.onA = []() {
            if (bluetooth::IsDiscovering())
                bluetooth::StopDiscovery();
            else
                bluetooth::StartDiscovery();
        };
        found.rows.push_back(std::move(search));
        for (const auto& device : bluetooth::ListDiscoveredAudioDevices()) {
            Row r;
            r.label = bluetooth::DeviceName(device);
            r.onA = [this, device]() {
                auto& t = nxui::I18n::instance();
                m_audio.playSfx(Sfx::ModalShow);
                m_userSelect->show(t.tr("settings.bluetooth.device_actions", "Device Actions"),
                                   bluetooth::DeviceName(device),
                                   {{t.tr("settings.bluetooth.pair_connect", "Pair & Connect"),
                                     [device]() { bluetooth::ConnectAudioDevice(device); }, true},
                                    {t.tr("button.cancel", "Cancel"), []() {}, true}},
                                   0, {});
                focusManager().setFocus(m_userSelect.get());
            };
            found.rows.push_back(std::move(r));
        }
    }
    std::vector<CardsSheet::Section> out;
    out.push_back(std::move(paired));
    out.push_back(std::move(found));
    return out;
}

void CardsHomeActivity::openBluetoothAudioSheet() {
    m_core.startBluetooth();
    // Drop stale change flags: the sheet is built from the current lists.
    bluetooth::HasPairedChanges();
    bluetooth::HasConnectedChanges();
    bluetooth::HasDiscoveredChanges();
    openSubSheet(nxui::I18n::instance().tr("cards.bt_audio", "Audio devices"), bluetoothAudioSections());
    m_btAudioSheet = true;
}

void CardsHomeActivity::pumpBluetoothAudioSheet() {
    if (!m_btAudioSheet)
        return;
    if (!m_sheet.isOpen() || m_sheet.title() != m_subSheetTitle) {
        m_btAudioSheet = false;
        if (bluetooth::IsDiscovering())
            bluetooth::StopDiscovery();
        return;
    }
    bool changed = false;
    if (bluetooth::IsAvailable()) {
        changed |= bluetooth::HasPairedChanges();
        changed |= bluetooth::HasConnectedChanges();
        changed |= bluetooth::HasDiscoveredChanges();
    }
    if (changed)
        m_sheet.open(m_sheet.title(), bluetoothAudioSections(), m_sheet.section(), m_sheet.row());
}

// ── Controller test (Classic ControllerTestScreen) ──────────────────────────
void CardsHomeActivity::openPadTest() {
    if (!m_padTest) {
        m_padTest = std::make_shared<ControllerTestScreen>();
        m_padTest->setRect({0, 0, 1280, 720});
        m_padTest->setFont(&m_font);
        m_padTest->setSmallFont(&m_fontSmall);
        m_padTest->setTheme(&m_overlayTheme);
        m_padTest->setInput(&app().input());
        m_padTest->onCloseSfx([this]() { m_audio.playSfx(Sfx::ModalHide); });
        m_padTest->onClosed([this]() { m_overlayLatch = true; });
    }
    m_audio.playSfx(Sfx::ModalShow);
    m_padTest->show();
    m_overlayLatch = true;
    focusManager().setFocus(m_padTest.get());
}

// ── Time zone and date/time (Classic SystemTab) ─────────────────────────────
void CardsHomeActivity::openTimezonePicker(const std::string& filter) {
    auto& tr = nxui::I18n::instance();
    std::vector<std::string> zones;
    for (auto& z : settings::system_prefs::timezoneNames()) {
        std::string a = z, b = filter;
        for (char& c : a) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (char& c : b) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (b.empty() || a.find(b) != std::string::npos)
            zones.push_back(std::move(z));
    }
    if (zones.empty()) {
        showToast(tr.tr("settings.system.timezone_none", "No matching timezones."));
        return;
    }
    const std::string current = settings::system_prefs::currentTimezone();
    int selected = 0;
    for (int i = 0; i < static_cast<int>(zones.size()); ++i)
        if (zones[static_cast<std::size_t>(i)] == current) {
            selected = i;
            break;
        }
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->showTimezonePicker(
        zones, selected,
        [this](const std::string& zone) {
            const Result rc = settings::system_prefs::setTimezone(zone);
            DebugLog::log("[cards] set timezone '%s' rc=0x%X", zone.c_str(), rc);
            auto& t = nxui::I18n::instance();
            showToast(R_SUCCEEDED(rc) ? t.tr("settings.system.timezone_saved", "Timezone updated.")
                                      : t.tr("settings.system.timezone_failed", "Could not change timezone."));
            m_statusTimer = 0.f;
        },
        [this]() {
            auto& t = nxui::I18n::instance();
            requestText(t.tr("settings.system.timezone_filter_title", "Filter timezones"),
                        t.tr("settings.system.timezone_filter_guide",
                             "Type to filter (e.g. Sao_Paulo). Leave empty for all."),
                        std::string(), [this](const std::string& next) { openTimezonePicker(next); });
        });
    focusManager().setFocus(m_userSelect.get());
}

void CardsHomeActivity::openDateTimeEditor() {
    auto& tr = nxui::I18n::instance();
    if (settings::system_prefs::internetTimeSync()) {
        showToast(tr.tr("settings.system.disable_internet_time_first",
                        "Disable Internet clock synchronization first."));
        return;
    }
    settings::system_prefs::DateTime now;
    if (!settings::system_prefs::currentDateTime(now)) {
        showToast(tr.tr("settings.system.time_change_failed", "The date and time setting could not be changed."));
        return;
    }
    OverlayDialog::DateTimeValue value;
    value.year = now.year;
    value.month = now.month;
    value.day = now.day;
    value.hour = now.hour;
    value.minute = now.minute;
    m_audio.playSfx(Sfx::ModalShow);
    m_userSelect->showDateTimeEditor(value, [this](const OverlayDialog::DateTimeValue& edited) {
        settings::system_prefs::DateTime v;
        v.year = edited.year;
        v.month = edited.month;
        v.day = edited.day;
        v.hour = edited.hour;
        v.minute = edited.minute;
        // Queued, not applied: the daemon sets the clocks and the status
        // pill reads the new time on its next refresh.
        const bool ok = R_SUCCEEDED(settings::system_prefs::setManualDateTime(v));
        if (!ok)
            showToast(nxui::I18n::instance().tr("settings.system.time_change_failed",
                                                "The date and time setting could not be changed."));
        m_statusTimer = 0.f;
        return ok;
    });
    focusManager().setFocus(m_userSelect.get());
}

} // namespace cards
