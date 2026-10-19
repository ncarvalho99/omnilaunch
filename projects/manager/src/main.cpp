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
#include <memory>

// Standard libnx user initialization hooks.
// CRT0 (init.o) handles envSetup, heap setup via envHasHeapOverride() (which nx-hbloader
// supplies in both Album applet mode and Title Override mode), thread init, argument
// parsing, standard services (sm, applet, hid, time, fs, fsdev, vi/nwindow), and then
// calls userAppInit().
extern "C" void userAppInit(void) {
    Result rc = romfsInit();
    if (R_FAILED(rc)) {
        svcOutputDebugString("[Manager] romfsInit FAILED", 26);
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

static void logMilestone(const char* fmt, ...) {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    svcOutputDebugString(buf, std::strlen(buf));
    std::fprintf(stderr, "%s\n", buf);
    switchu::FileLog::log("%s", buf);

    // Also write directly to the manager directory as an unbuffered secondary trail
    if (FILE* f = std::fopen("sdmc:/switch/OmniLaunch-Manager/manager.log", "a")) {
        std::fprintf(f, "%s\n", buf);
        std::fflush(f);
        std::fclose(f);
        fsdevCommitDevice("sdmc");
    }
}

int main(int argc, char** argv) {
    // Open primary file log with immediate flushing
    switchu::FileLog::open("manager");
    switchu::FileLog::setImmediate(true);

    logMilestone("[main] OmniLaunch Manager starting (argc=%d, argv0=%s)",
                 argc, (argc > 0 && argv && argv[0]) ? argv[0] : "<null>");

    // Hook up GpuDevice debug sink to our diagnostic logger
    nxui::GpuDevice::setDebugSink([](const char* msg) {
        logMilestone("[gpu-sink] %s", msg);
    });

    try {
        // Inspect runtime heap facts
        extern char* fake_heap_start;
        extern char* fake_heap_end;
        const bool hasOverride = envHasHeapOverride();
        const size_t heapBytes = (fake_heap_end >= fake_heap_start)
            ? static_cast<size_t>(fake_heap_end - fake_heap_start) : 0;
        logMilestone("[main] heap: envHasOverride=%d, start=%p, end=%p, size=%zu MB",
                     hasOverride ? 1 : 0, fake_heap_start, fake_heap_end, heapBytes / (1024 * 1024));

        // Test RomFS assets
        FILE* fontTest = std::fopen("romfs:/fonts/DejaVuSans.ttf", "rb");
        if (fontTest) {
            std::fseek(fontTest, 0, SEEK_END);
            long fsz = std::ftell(fontTest);
            std::fclose(fontTest);
            logMilestone("[main] romfs:/fonts/DejaVuSans.ttf size=%ld bytes", fsz);
        } else {
            logMilestone("[main] WARNING: romfs:/fonts/DejaVuSans.ttf failed to open (errno=%d)", errno);
        }

        FILE* shaderTest = std::fopen("romfs:/shaders/basic_vsh.dksh", "rb");
        if (shaderTest) {
            std::fclose(shaderTest);
            logMilestone("[main] romfs:/shaders/basic_vsh.dksh verified present");
        } else {
            logMilestone("[main] WARNING: romfs:/shaders/basic_vsh.dksh failed to open (errno=%d)", errno);
        }

        logMilestone("[main] SDL_Init(0)...");
        int sdlRc = SDL_Init(0);
        logMilestone("[main] SDL_Init rc=%d", sdlRc);

        logMilestone("[main] TTF_Init()...");
        int ttfRc = TTF_Init();
        logMilestone("[main] TTF_Init rc=%d", ttfRc);

        nxui::Renderer::setShaderBasePath("romfs:/shaders/");
        logMilestone("[main] I18n::initialize...");
        nxui::I18n::instance().initialize("romfs:/i18n", "en-US");

        logMilestone("[main] creating Application...");
        nxui::Application application;

        logMilestone("[main] setActivity(ManagerActivity)...");
        application.setActivity(std::make_unique<switchu::manager::ManagerActivity>());

        logMilestone("[main] calling application.initialize()...");
        const bool initialized = application.initialize();
        logMilestone("[main] application.initialize() returned %d", initialized ? 1 : 0);

        if (initialized) {
            logMilestone("[main] entering application.run()");
            application.run();
            logMilestone("[main] exited application.run()");
        } else {
            logMilestone("[main] ERROR: nxui initialization failed!");
        }

        logMilestone("[main] application.shutdown()...");
        application.shutdown();
        logMilestone("[main] application.shutdown() complete");

        TTF_Quit();
        SDL_Quit();
        switchu::FileLog::close();
        logMilestone("[main] returning %d", initialized ? 0 : 1);
        return initialized ? 0 : 1;
    } catch (const std::exception& ex) {
        logMilestone("[main] FATAL EXCEPTION: %s", ex.what());
        switchu::FileLog::close();
        return 1;
    } catch (...) {
        logMilestone("[main] UNKNOWN FATAL EXCEPTION");
        switchu::FileLog::close();
        return 1;
    }
}
