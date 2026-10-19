#include "ManagerActivity.hpp"

#include <nxui/Application.hpp>
#include <nxui/core/I18n.hpp>
#include <nxui/core/Renderer.hpp>
#include <switchu/file_log.hpp>
#include <switch.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>

static void rawDiag(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    svcOutputDebugString(buf, std::strlen(buf));

    if (FILE* f = std::fopen("sdmc:/manager_diag.txt", "a")) {
        std::fprintf(f, "%s\n", buf);
        std::fflush(f);
        std::fclose(f);
        fsdevCommitDevice("sdmc");
    }
    if (FILE* f2 = std::fopen("sdmc:/switch/OmniLaunch-Manager/diag.txt", "a")) {
        std::fprintf(f2, "%s\n", buf);
        std::fflush(f2);
        std::fclose(f2);
        fsdevCommitDevice("sdmc");
    }
}

extern "C" void userAppInit(void) {
    rawDiag("[diag] userAppInit enter");
    Result rc = romfsInit();
    rawDiag("[diag] romfsInit rc=0x%X", rc);
    if (R_FAILED(rc)) {
        rc = romfsMountFromFsdev("sdmc:/switch/OmniLaunch-Manager/OmniLaunch-Manager.nro", 0, "romfs");
        rawDiag("[diag] romfsMountFromFsdev fallback rc=0x%X", rc);
    }
    rc = timeInitialize();
    rawDiag("[diag] timeInitialize rc=0x%X", rc);
    rc = setInitialize();
    rawDiag("[diag] setInitialize rc=0x%X", rc);
    rc = setsysInitialize();
    rawDiag("[diag] setsysInitialize rc=0x%X", rc);
    rawDiag("[diag] userAppInit exit");
}

extern "C" void userAppExit(void) {
    rawDiag("[diag] userAppExit enter");
    setsysExit();
    setExit();
    timeExit();
    romfsExit();
    rawDiag("[diag] userAppExit exit");
}

int main(int argc, char** argv) {
    rawDiag("[diag] main enter: argc=%d, argv0=%s", argc, (argc > 0 && argv && argv[0]) ? argv[0] : "<null>");
    try {
        rawDiag("[diag] testing sdmc:/config/OmniLaunch directory...");
        DIR* d = opendir("sdmc:/config/OmniLaunch");
        if (d) {
            rawDiag("[diag] opendir sdmc:/config/OmniLaunch OK");
            closedir(d);
        } else {
            rawDiag("[diag] opendir sdmc:/config/OmniLaunch FAIL: errno=%d", errno);
        }

        rawDiag("[diag] calling FileLog::open('manager')...");
        switchu::FileLog::open("manager");
        switchu::FileLog::setImmediate(true);
        switchu::FileLog::log("[main] OmniLaunch Manager starting (argc=%d, argv0=%s)",
                              argc, (argc > 0 && argv && argv[0]) ? argv[0] : "<null>");
        rawDiag("[diag] FileLog::open complete");

        rawDiag("[diag] SDL_Init(0)...");
        int sdlRc = SDL_Init(0);
        rawDiag("[diag] SDL_Init rc=%d", sdlRc);

        rawDiag("[diag] TTF_Init()...");
        int ttfRc = TTF_Init();
        rawDiag("[diag] TTF_Init rc=%d", ttfRc);

        rawDiag("[diag] testing font file in romfs...");
        FILE* fontTest = std::fopen("romfs:/fonts/DejaVuSans.ttf", "rb");
        if (fontTest) {
            std::fseek(fontTest, 0, SEEK_END);
            long fsz = std::ftell(fontTest);
            std::fclose(fontTest);
            rawDiag("[diag] romfs:/fonts/DejaVuSans.ttf size=%ld bytes", fsz);
        } else {
            rawDiag("[diag] romfs:/fonts/DejaVuSans.ttf FAILED to open: errno=%d", errno);
        }

        nxui::Renderer::setShaderBasePath("romfs:/shaders/");
        rawDiag("[diag] I18n::initialize...");
        nxui::I18n::instance().initialize("romfs:/i18n", "en-US");
        rawDiag("[diag] I18n done");

        rawDiag("[diag] creating Application...");
        nxui::Application application;
        rawDiag("[diag] setActivity...");
        application.setActivity(std::make_unique<switchu::manager::ManagerActivity>());
        rawDiag("[diag] calling application.initialize()...");
        const bool initialized = application.initialize();
        rawDiag("[diag] application.initialize() returned %d", initialized ? 1 : 0);
        switchu::FileLog::log("[main] application.initialize() result=%d", initialized ? 1 : 0);

        if (initialized) {
            rawDiag("[diag] entering application.run()...");
            switchu::FileLog::log("[main] entering application.run()");
            application.run();
            rawDiag("[diag] application.run() exited");
            switchu::FileLog::log("[main] exited application.run()");
        } else {
            rawDiag("[diag] application.initialize() was FALSE");
            switchu::FileLog::log("[main] nxui initialization failed");
        }

        rawDiag("[diag] application.shutdown()...");
        application.shutdown();
        rawDiag("[diag] application.shutdown() done");

        TTF_Quit();
        SDL_Quit();
        switchu::FileLog::close();
        rawDiag("[diag] main returning %d", initialized ? 0 : 1);
        return initialized ? 0 : 1;
    } catch (const std::exception& ex) {
        rawDiag("[diag] FATAL EXCEPTION in main: %s", ex.what());
        return 1;
    } catch (...) {
        rawDiag("[diag] UNKNOWN FATAL EXCEPTION in main");
        return 1;
    }
}
