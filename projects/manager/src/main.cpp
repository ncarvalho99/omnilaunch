#include "ManagerActivity.hpp"

#include <nxui/Application.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <switchu/file_log.hpp>
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <memory>

extern "C" void userAppInit(void) {
    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        // Fallback when argv[0] is not supplied by the forwarder
        romfsMountFromFsdev("sdmc:/switch/OmniLaunch-Manager/OmniLaunch-Manager.nro", 0, "romfs");
    }
    timeInitialize();
    setInitialize();
    setsysInitialize();
}

extern "C" void userAppExit(void) {
    setsysExit();
    setExit();
    timeExit();
    romfsExit();
}

int main(int argc, char** argv) {
    switchu::FileLog::open("manager");
    switchu::FileLog::setImmediate(true);
    switchu::FileLog::log("[main] OmniLaunch Manager starting (argc=%d, argv0=%s)",
                          argc, (argc > 0 && argv && argv[0]) ? argv[0] : "<null>");

    SDL_Init(0);
    TTF_Init();
    nxui::Renderer::setShaderBasePath("romfs:/shaders/");
    nxui::I18n::instance().initialize("romfs:/i18n", "en-US");

    nxui::Application application;
    application.setActivity(std::make_unique<switchu::manager::ManagerActivity>());
    const bool initialized = application.initialize();
    switchu::FileLog::log("[main] application.initialize() result=%d", initialized ? 1 : 0);
    if (initialized) {
        switchu::FileLog::log("[main] entering application.run()");
        application.run();
        switchu::FileLog::log("[main] exited application.run()");
    } else {
        switchu::FileLog::log("[main] nxui initialization failed");
    }
    application.shutdown();

    TTF_Quit();
    SDL_Quit();
    switchu::FileLog::close();
    return initialized ? 0 : 1;
}
