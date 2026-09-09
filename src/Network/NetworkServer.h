#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

// Forward-declared rather than #including <httplib.h> here - keeps this
// header cheap for any future consumer that only needs to hold/pass around
// a NetworkServer&/pointer without itself needing httplib's own types. The
// real httplib::Server member still has to be defined somewhere non-
// forward-declarable though (a class member can't be a forward-declared
// type by value) - see the Pimpl (Impl) member below for why this header
// still works.
namespace httplib { class Server; }

// Forward-declared rather than #including "../Application/FrameCaptureBridge.h"
// here, for the same cheap-header reason as httplib::Server above -
// network-impl-2 campaign, Phase 3
// (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md).
namespace gte { class FrameCaptureBridge; }

namespace gte::Network {

// Owns a real, embedded HTTP server (cpp-httplib) bound to loopback
// (127.0.0.1) ONLY - this class has NO PARAMETER anywhere in its public API
// that could express any other host (see PHASE0_MASTER_STRATEGY.md's locked
// "bind address" decision - this is deliberately enforced by the shape of
// Start() itself, not merely by what Application happens to pass it).
// Start() returns immediately; the actual blocking accept/serve loop
// (httplib::Server::listen_after_bind()) runs on a dedicated background
// std::thread this class owns - so calling Start() never blocks the caller
// (Application's main frame loop), and every registered route handler (see
// NetworkRoutes.h) runs on that ONE background thread, never the main
// thread. See AGENTS.md, "Networking", for the thread-safety rules this
// implies for any route handler.
//
// RAII: the destructor calls Stop() automatically, so a NetworkServer that
// goes out of scope always cleanly stops the socket and joins its thread -
// never a leaked thread/socket, mirroring every other RAII-owned resource
// in this engine (see AGENTS.md, "Coding Guidelines").
class NetworkServer {
public:
    // `captureBridge` (network-impl-2 campaign, Phase 3 -
    // PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) is a
    // DEFAULTED, non-owning pointer - kept default (nullptr) by every
    // existing tests/Network/NetworkServerTests.cpp call site (seven
    // no-argument `NetworkServer server;` constructions), so this remains a
    // fully backward-compatible signature change. Non-null in production
    // (Application owns the real FrameCaptureBridge and passes its address -
    // see Application.cpp) - `nullptr` means "no engine-state-touching
    // routes are wired up" (a route needing it responds 503 rather than
    // crashing - see NetworkServer.cpp's own RegisterRoutes()).
    explicit NetworkServer(FrameCaptureBridge* captureBridge = nullptr);
    ~NetworkServer();

    NetworkServer(const NetworkServer&) = delete;
    NetworkServer& operator=(const NetworkServer&) = delete;
    NetworkServer(NetworkServer&&) = delete;
    NetworkServer& operator=(NetworkServer&&) = delete;

    // Binds to 127.0.0.1:port (synchronously - see this class's own .cpp
    // comment for why bind and "start serving" are deliberately two
    // separate steps) and spawns the background thread that actually serves
    // requests. There is deliberately NO host parameter - loopback is the
    // only address this class can ever bind to, enforced by this exact
    // signature (see PHASE0_MASTER_STRATEGY.md's locked decision #2). port
    // == 0 means "let the OS assign a free ephemeral port" - BoundPort()
    // reports which one was actually chosen; used by
    // tests/Network/NetworkServerTests.cpp (Phase 4) so the test never
    // collides with a real running instance of the engine on the same
    // machine. A second call while already running (IsRunning() == true) is
    // a safe no-op - Start() is NOT re-entrant/thread-safe against itself or
    // Stop() (see this class's own file comment in NetworkServer.cpp for
    // why that's an acceptable, documented constraint rather than a bug:
    // both are only ever called from the main thread, by Application).
    //
    // If the bind itself fails (e.g. the port is already in use), this logs
    // a message to stderr and returns with IsRunning() staying false - this
    // is NON-FATAL. A failed bind must never crash/throw and must never
    // prevent the rest of the engine (window/renderer/game) from starting -
    // see AGENTS.md, "Networking", for why this class treats every one of
    // its own failure modes as "log and continue", never fatal.
    void Start(int port);

    // Stops the server (httplib::Server::stop(), thread-safe) and joins the
    // background thread. Safe to call even if Start() was never called, or
    // was already stopped (a no-op in both cases). Called automatically by
    // the destructor - production code (Application) does not need to call
    // this explicitly, but may (e.g. a future explicit "Network" Editor
    // panel toggle) since it's part of the public, documented lifecycle.
    void Stop();

    // Whether the background thread is currently up and serving. False
    // before the first Start() call, false again after Stop() (or a bind
    // failure inside Start()).
    bool IsRunning() const noexcept;

    // The actual bound TCP port - valid once IsRunning() is true. 0 before
    // the first successful Start(), and after a failed bind. When Start()
    // was called with an explicit non-zero port, this simply echoes it back
    // (once bound); when called with port == 0, this is the OS-assigned
    // ephemeral port.
    int BoundPort() const noexcept;

private:
    // Pimpl (std::unique_ptr<httplib::Server>, defined in the .cpp) rather
    // than a direct httplib::Server member - this is what lets this header
    // forward-declare httplib::Server above instead of #include <httplib.h>
    // here, so any future consumer of NetworkServer.h that never touches
    // httplib types directly doesn't transitively pull in httplib.h. See
    // NetworkServer.cpp for the actual member.
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<int> m_boundPort{ 0 };

    // Non-owning - see this class's own constructor doc comment above.
    // Application owns the real instance and must outlive this NetworkServer.
    FrameCaptureBridge* m_captureBridge = nullptr;
};

} // namespace gte::Network
