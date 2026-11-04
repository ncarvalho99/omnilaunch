#pragma once
// Cancellation state for themeshop::http, kept free of curl/libnx so host
// tests can exercise it.
//
// Two independent signals:
// - Frontend scope: every request belongs to the frontend scope that was open
//   when it started. Closing the scope (frontend leaving) aborts those requests
//   and refuses new ones until the next frontend opens a scope. It is not a
//   permanent latch, so a later frontend can use the network again.
// - Terminal cancel: launch handoff and process exit. Never reset; the process
//   is about to leave HOME.

#include <atomic>
#include <cstdint>

namespace themeshop::http {

class CancelScope {
public:
    struct Token {
        std::uint64_t generation = 0;
    };

    // Captured at request entry, before waiting for the shared request mutex,
    // so a request queued behind another one is still cancelled with its scope.
    // A request started while no frontend scope is open gets generation 0,
    // which never matches, so reopening the scope does not revive it.
    // Generation is read before the open flag; closeFrontend() writes them in
    // the opposite order, so a concurrent close always yields a dead token.
    Token begin() const {
        const std::uint64_t generation = m_generation.load(std::memory_order_acquire);
        if (!m_open.load(std::memory_order_acquire))
            return Token{0};
        return Token{generation};
    }

    bool cancelled(const Token& token) const {
        return m_terminal.load(std::memory_order_acquire) ||
               m_generation.load(std::memory_order_acquire) != token.generation;
    }

    void openFrontend() {
        m_open.store(true, std::memory_order_release);
    }

    // Closed first, then the generation moves: every token issued before this
    // call stops matching, and begin() hands out dead tokens until reopened.
    void closeFrontend() {
        m_open.store(false, std::memory_order_release);
        m_generation.fetch_add(1, std::memory_order_acq_rel);
    }

    void cancelTerminal() {
        m_terminal.store(true, std::memory_order_release);
    }

    bool terminal() const {
        return m_terminal.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint64_t> m_generation{1};
    std::atomic<bool> m_open{true};
    std::atomic<bool> m_terminal{false};
};

} // namespace themeshop::http
