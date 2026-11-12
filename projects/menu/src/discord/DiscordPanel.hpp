#pragma once
// Discord slide-out panel for Cards and Classic (HomeNX H2, H4, C-UI-12).
// Slides in from the left (460x688) with dark Discord styling.
// Supports QR login view, direct messages, servers/channels list, chat view,
// and account switcher.
// New OmniLaunch implementation written from scratch.

#include "discord/DiscordService.hpp"
#include "discord/DiscordTypes.hpp"

#include <nxui/core/Font.hpp>
#include <nxui/core/Renderer.hpp>
#include <nxui/core/Texture.hpp>
#include <nxui/widgets/Widget.hpp>

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nxui/core/Input.hpp>

namespace nxui { class GpuDevice; class Input; }
namespace home { class HomeCore; }

namespace discord {

class DiscordPanel : public nxui::Widget {
public:
    struct Callbacks {
        std::function<void()> onClosed;
        std::function<void(Snowflake channelId, const std::string& name)> onReply;
        std::function<void()> onNavigate;
        std::function<void()> onActivate;
        std::function<void()> onUserTokenLogin;
    };

    DiscordPanel();
    ~DiscordPanel() override;

    void setFont(nxui::Font* f) { m_font = f; }
    void setSmallFont(nxui::Font* f) { m_smallFont = f; }
    void setIconFont(nxui::Font* f) { m_iconFont = f; } // button glyphs for the hints
    void setService(DiscordService* s, home::HomeCore* core) { m_service = s; m_core = core; }
    void setCallbacks(Callbacks cb) { m_cb = std::move(cb); }

    void show();
    void hide();
    bool isActive() const { return m_active || m_anim > 0.001f; }
    bool isOpen() const { return m_active; }

    void openChat(Snowflake channelId, Snowflake guildId, const std::string& name);
    void handleTouch(const nxui::Input& in);

    nxui::Rect focusRect() const override { return {0.f, 0.f, 0.f, 0.f}; }

protected:
    void onUpdate(float dt) override;
    void onRender(nxui::Renderer& ren) override;

private:
    enum class View { Home, Chat, Accounts };

    struct Row {
        enum Kind { Dm, Channel, Category, Account, Button, Info, Error } kind = Info;
        std::string text;
        std::string detail;
        Snowflake id = 0;
        int action = 0;
        bool selected = false;
        bool unread = false;
        bool primary = false;
        bool danger = false;
        std::string avatarUrl;
        std::string status;
        int mentions = 0;
        bool voice = false;
        bool current = false;
    };

    void bindInput();
    void rebuildRows();
    void move(int dir);
    void moveRail(int dir);
    void activateRow();
    void back();
    void openAccounts();

    bool railShown() const;
    bool listFocused() const { return !railShown() || m_column == 1; }
    nxui::Rect panelRect() const;

    void drawRail(nxui::Renderer& ren, const nxui::Rect& area, float a);
    void drawRows(nxui::Renderer& ren, const nxui::Rect& area, float a);
    void drawChat(nxui::Renderer& ren, const nxui::Rect& area, float a);
    void drawMe(nxui::Renderer& ren, const nxui::Rect& area, float a);
    void drawQrView(nxui::Renderer& ren, const nxui::Rect& area, float a);
    void drawAvatar(nxui::Renderer& ren, const std::string& url, const std::string& name,
                    float x, float y, float size, float a);
    void drawStatus(nxui::Renderer& ren, const std::string& s, float cx, float cy, float r,
                    const nxui::Color& ring, float a);

    DiscordService* m_service = nullptr;
    home::HomeCore* m_core = nullptr;
    nxui::Font* m_font = nullptr;
    nxui::Font* m_smallFont = nullptr;
    nxui::Font* m_iconFont = nullptr;
    Callbacks m_cb;

    std::string clean(const std::string& text) const;       // drop letters the font lacks
    mutable std::unordered_map<std::uint32_t, bool> m_glyphs; // codepoint -> font has it
    void drawHint(nxui::Renderer& ren, nxui::Button btn, const std::string& label,
                  float rightX, float y, float a, float* outW = nullptr);

    bool m_active = false;
    float m_anim = 0.f;
    View m_view = View::Home;

    std::vector<Row> m_rows;
    int m_sel = -1;
    float m_scroll = 0.f;

    // Rail (left server column): 0 = DMs, i = guilds[i-1]
    int m_railSel = 0;
    int m_column = 1; // 0 = rail, 1 = list
    float m_railScroll = 0.f;
    std::vector<std::pair<int, nxui::Rect>> m_railHits;

    // Chat view
    Snowflake m_chatId = 0;
    Snowflake m_chatGuildId = 0;
    std::string m_chatName;
    float m_chatScroll = 0.f;

    std::vector<std::pair<int, nxui::Rect>> m_hits;
    nxui::Rect m_backHit{};
    nxui::Rect m_replyHit{};

    DiscordService::State m_lastState = DiscordService::State::SignedOut;
    std::uint64_t m_lastVersion = 0;
    View m_lastView = View::Home;
    int m_lastRailSel = 0;

    struct ImageSlot {
        nxui::Texture texture;
        std::shared_ptr<nxui::DecodedImage> decoded;
        std::future<void> future;
        bool requested = false;
        bool ready = false;
        bool failed = false;
    };
    mutable std::map<std::string, ImageSlot> m_images;
    const nxui::Texture* requestImage(const std::string& url, int maxSide = 256) const;
    void pumpImages(nxui::GpuDevice& gpu, nxui::Renderer& ren) const;
};

} // namespace discord
