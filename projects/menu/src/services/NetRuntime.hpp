#pragma once

namespace switchu::services::net {

// Shared, reference-counted nifm + BSD socket runtime.
//
// libnx reference-counts nifmInitialize but NOT the socket layer:
// socketInitialize answers "already initialized" to a second caller and
// socketExit() removes the "soc:" device unconditionally. Two independent users
// (Theme Shop HTTP and the NTP clock sync) each initialising and exiting it on
// their own therefore let one tear the device down while the other still holds
// an open descriptor; the next close() on that descriptor dereferences a null
// devoptab entry and the menu dies with a data abort. Every user goes through
// here instead, and the socket layer is only exited when the last one leaves.
//
// acquire() returns false if the runtime could not be brought up (nothing is
// held then, so do not call release()). Each successful acquire() must be
// paired with exactly one release(); Guard does this.
bool acquire();
void release();

// The same runtime plus libcurl's global state (curl_global_init/cleanup),
// reference-counted on top of acquire()/release(). Every curl user in the menu
// (Theme Shop HTTP, NetService HTTP and WebSocket) holds one of these while it
// uses curl, so one user retrying or shutting down can no longer run
// curl_global_cleanup() under another user's transfer.
bool acquireHttp();
void releaseHttp();

class Guard {
public:
    Guard() : m_ok(acquire()) {}
    ~Guard() { if (m_ok) release(); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
    explicit operator bool() const { return m_ok; }

private:
    bool m_ok;
};

class HttpLease {
public:
    HttpLease() : m_ok(acquireHttp()) {}
    ~HttpLease() { if (m_ok) releaseHttp(); }
    HttpLease(const HttpLease&) = delete;
    HttpLease& operator=(const HttpLease&) = delete;
    explicit operator bool() const { return m_ok; }

private:
    bool m_ok;
};

} // namespace switchu::services::net
