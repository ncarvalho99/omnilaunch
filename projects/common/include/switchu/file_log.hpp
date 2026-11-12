#pragma once
#include <cstdio>
#include <cstdarg>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <switch.h>
#include <switchu/log_utils.hpp>

namespace switchu {

class FileLog {
public:
    static constexpr const char* LOG_DIR = "sdmc:/config/OmniLaunch";
    static constexpr const char* LOG_EXTENSION = ".log";
    static constexpr size_t MAX_LOG_FILES = 5;
    static constexpr size_t MAX_ARCHIVED_LOGS = MAX_LOG_FILES - 1;
    // What the menu passes to open(). Five files of this size are hours of a
    // session rather than five of its restarts, and about 2.5 MB on the card.
    static constexpr std::uintmax_t MENU_ROTATE_BYTES = 512u * 1024u;

    // A log that rotates on every open holds as much history as the process
    // has starts. That is right for the daemon, which starts once per boot,
    // and wrong for the menu, which starts again every time a game is closed:
    // pass a size here and the log is appended to until it reaches it.
    static constexpr std::uintmax_t ROTATE_ON_EVERY_OPEN = 0;

    static void open(const char* tag, std::uintmax_t rotateAboveBytes = ROTATE_ON_EVERY_OPEN) {
        auto& self = inst();
        std::lock_guard<std::mutex> lock(self.m_mutex);

        log_detail::ensure_log_dir(LOG_DIR);
        close_current_file(self);

        const bool can_truncate = log_detail::rotate_current_log(
            LOG_DIR, tag, LOG_EXTENSION, MAX_ARCHIVED_LOGS, rotateAboveBytes);

        char path[256];
        log_detail::build_current_log_path(path, sizeof(path), LOG_DIR, tag, LOG_EXTENSION);
        self.m_sink.open(path, can_truncate);
        if (self.m_sink.is_open()) {
            char timestamp[32];
            log_detail::format_line_timestamp(timestamp, sizeof(timestamp));
            self.m_sink.append(std::string("[") + timestamp + "] === " + tag + " log start ===");
            self.m_sink.flush();
            fsdevCommitDevice("sdmc");
        }
    }

    
    static void logCommit(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log("%s", buf);
        flush();
        fsdevCommitDevice("sdmc");
    }

    static void close() {
        auto& self = inst();
        std::lock_guard<std::mutex> lock(self.m_mutex);
        close_current_file(self);
        fsdevCommitDevice("sdmc");
    }

    // Forces buffered lines out to the SD card. Call before anything that may
    // terminate the process without unwinding.
    static void flush() {
        auto& self = inst();
        std::lock_guard<std::mutex> lock(self.m_mutex);
        self.m_sink.flush();
        fsdevCommitDevice("sdmc");
    }

    // Drains lines that have been buffered for a while. Intended for processes
    // that run indefinitely and never reach close().
    static void flushIfStale(int maxAgeSeconds = 2) {
        auto& self = inst();
        std::lock_guard<std::mutex> lock(self.m_mutex);
        self.m_sink.flush_if_stale(maxAgeSeconds);
    }

    // Write every line straight through instead of buffering.
    //
    // Buffering is the right default once a process is running, but it hides
    // exactly the startup traces you need when one hangs before its main loop:
    // a menu that wedged during init left behind a log holding nothing but the
    // header, with everything after it still in RAM. Startup runs immediate,
    // and the main loop turns buffering back on.
    static void setImmediate(bool immediate) {
        auto& self = inst();
        std::lock_guard<std::mutex> lock(self.m_mutex);
        self.m_immediate = immediate;
        if (immediate) {
            self.m_sink.flush();
            fsdevCommitDevice("sdmc");
        }
    }

    // Moves the periodic SD writes off the caller's thread: a worker wakes
    // every `periodMs` and drains lines buffered for a second or more, so the
    // menu's frame no longer waits on the card for its timed log flush
    // (HomeNX writes its log from a thread for the same reason). flush() and
    // logCommit() stay synchronous for crash paths.
    static void startBackgroundFlush(int periodMs = 500) {
        auto& self = inst();
        std::lock_guard<std::mutex> wl(self.m_writerMutex);
        if (self.m_writer.joinable())
            return;
        self.m_writerStop = false;
        {
            std::lock_guard<std::mutex> sinkLock(self.m_mutex);
            self.m_sink.set_deferred(true);
        }
        self.m_writer = std::thread([&self, periodMs]() {
            std::unique_lock<std::mutex> lock(self.m_writerMutex);
            while (!self.m_writerStop) {
                self.m_writerCv.wait_for(lock, std::chrono::milliseconds(periodMs));
                if (self.m_writerStop)
                    break;
                lock.unlock();
                {
                    std::lock_guard<std::mutex> sinkLock(self.m_mutex);
                    self.m_sink.flush_if_stale(1);
                }
                lock.lock();
            }
        });
    }

    static void stopBackgroundFlush() {
        auto& self = inst();
        {
            std::lock_guard<std::mutex> wl(self.m_writerMutex);
            self.m_writerStop = true;
        }
        self.m_writerCv.notify_all();
        if (self.m_writer.joinable())
            self.m_writer.join();
        std::lock_guard<std::mutex> sinkLock(self.m_mutex);
        self.m_sink.set_deferred(false);
        self.m_sink.flush();
    }

    static bool backgroundFlushActive() {
        auto& self = inst();
        std::lock_guard<std::mutex> wl(self.m_writerMutex);
        return self.m_writer.joinable() && !self.m_writerStop;
    }

    static void log(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        char timestamp[32];
        log_detail::format_line_timestamp(timestamp, sizeof(timestamp));

        auto& self = inst();
        std::lock_guard<std::mutex> lock(self.m_mutex);

        std::fprintf(stderr, "[%s] %s\n", timestamp, buf);
        self.m_sink.append(std::string("[") + timestamp + "] " + buf);
        if (self.m_immediate) {
            self.m_sink.flush();
            fsdevCommitDevice("sdmc");
        }
    }

private:
    static FileLog& inst() { static FileLog s; return s; }
    ~FileLog() {
        {
            std::lock_guard<std::mutex> wl(m_writerMutex);
            m_writerStop = true;
        }
        m_writerCv.notify_all();
        if (m_writer.joinable())
            m_writer.join();
    }

    static void close_current_file(FileLog& self) {
        if (!self.m_sink.is_open())
            return;

        char timestamp[32];
        log_detail::format_line_timestamp(timestamp, sizeof(timestamp));
        self.m_sink.append(std::string("[") + timestamp + "] === log end ===");
        self.m_sink.close();
    }

    std::mutex m_mutex;
    log_detail::buffered_sink m_sink;
    std::mutex m_writerMutex;
    std::condition_variable m_writerCv;
    std::thread m_writer;
    bool m_writerStop = false;
    // Startup is unbuffered; the main loop relaxes this once it is running.
    bool m_immediate = true;
};

}
