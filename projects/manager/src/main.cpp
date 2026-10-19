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

extern "C" {
    void __libnx_init_time(void);
    void __nx_win_init(void);
    void __nx_win_exit(void);

    // Dynamic heap initialization with progressive ladder.
    // In Album (applet) mode, libnx's default calculation clamps or overflows
    // and selects 512 MB (0x20000000), which exceeds the 450 MB available pool,
    // immediately triggering diagAbortWithResult(0x1F59) during CRT0 (__libnx_init)
    // before __appInit, userAppInit, or main can ever run.
    void __libnx_initheap(void) {
        extern char* fake_heap_start;
        extern char* fake_heap_end;

        static constexpr size_t kManagerHeapLadder[] = {
            128u * 1024u * 1024u,
            64u  * 1024u * 1024u,
            32u  * 1024u * 1024u,
            16u  * 1024u * 1024u,
            8u   * 1024u * 1024u,
        };

        void*  addr = nullptr;
        size_t size = 0;
        for (size_t want : kManagerHeapLadder) {
            if (R_SUCCEEDED(svcSetHeapSize(&addr, want))) {
                size = want;
                break;
            }
        }

        fake_heap_start = static_cast<char*>(addr);
        fake_heap_end   = static_cast<char*>(addr) + size;
    }
}

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

// Override libnx default __appInit to prevent hidden diagAbortWithResult aborts
// before any diagnostic logging or user code runs.
extern "C" void __appInit(void) {
    svcOutputDebugString("[Manager] __appInit enter", 25);

    // 1. Service manager
    Result rc = smInitialize();
    svcOutputDebugString(R_SUCCEEDED(rc) ? "[Manager] smInit OK" : "[Manager] smInit FAIL", 19);

    // 2. Filesystem service
    rc = fsInitialize();
    svcOutputDebugString(R_SUCCEEDED(rc) ? "[Manager] fsInit OK" : "[Manager] fsInit FAIL", 19);

    // 3. Mount SD card FIRST so diagnostic files can be written immediately
    rc = fsdevMountSdmc();
    svcOutputDebugString(R_SUCCEEDED(rc) ? "[Manager] sdmc OK" : "[Manager] sdmc FAIL", 17);
    rawDiag("[__appInit] sdmc mounted rc=0x%X", rc);

    // 4. Applet
    rc = appletInitialize();
    rawDiag("[__appInit] appletInitialize rc=0x%X", rc);

    // 5. HID (do NOT abort on failure: library applets/forwarders can share sessions)
    rc = hidInitialize();
    rawDiag("[__appInit] hidInitialize rc=0x%X", rc);

    // 6. Time
    rc = timeInitialize();
    rawDiag("[__appInit] timeInitialize rc=0x%X", rc);
    __libnx_init_time();

    // 7. Settings
    rc = setInitialize();
    rawDiag("[__appInit] setInitialize rc=0x%X", rc);
    rc = setsysInitialize();
    rawDiag("[__appInit] setsysInitialize rc=0x%X", rc);

    // 8. RomFS with fallback
    rc = romfsInit();
    rawDiag("[__appInit] romfsInit rc=0x%X", rc);
    if (R_FAILED(rc)) {
        rc = romfsMountFromFsdev("sdmc:/switch/OmniLaunch-Manager/OmniLaunch-Manager.nro", 0, "romfs");
        rawDiag("[__appInit] romfsMountFromFsdev fallback rc=0x%X", rc);
    }

    // 9. Window
    __nx_win_init();
    rawDiag("[__appInit] __nx_win_init done");

    rawDiag("[__appInit] complete");
}

extern "C" void __appExit(void) {
    rawDiag("[__appExit] enter");
    __nx_win_exit();
    romfsExit();
    setsysExit();
    setExit();
    timeExit();
    hidExit();
    appletExit();
    fsdevUnmountAll();
    fsExit();
    smExit();
    rawDiag("[__appExit] exit");
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
