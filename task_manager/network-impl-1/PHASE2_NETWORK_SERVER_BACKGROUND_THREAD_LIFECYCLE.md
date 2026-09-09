# PHASE2 — `NetworkServer`: Real Socket, Background Thread, Race-Free Lifecycle

Parent: `PHASE0_MASTER_STRATEGY.md`. Prerequisite: `PHASE1_NETWORK_MODULE_FOUNDATION_AND_ROUTE_LOGIC.md`
must already be done (this phase includes `NetworkRoutes.h` and appends to
the same `add_library(gte_core ...)` source block Phase 1 edited).

## Step 1: The Goal

Implement `gte::Network::NetworkServer` — an RAII class that owns a real
`httplib::Server`, binds it to `127.0.0.1` on a caller-chosen port (or an
OS-assigned ephemeral port), and runs its blocking accept/serve loop on a
**dedicated background `std::thread`**, so `Start()` itself returns
immediately (never blocks the caller) and the server keeps answering
requests entirely independently of whatever the main thread is doing. This
is the literal "non-blocking background networking" requirement from
`PHASE0_MASTER_STRATEGY.md`'s Step 1.

`Stop()` (and the destructor, which calls it automatically) must cleanly
signal the server to stop and join the background thread — no detached
thread, no leaked socket, no hang.

## Step 2: The Situation

- `httplib::Server` (from the already-vendored `third_party/httplib/httplib.h`,
  currently staged at tag `v0.54.1` — see `third_party/httplib/.gte_fetched_ref`)
  exposes **(this was directly verified against the actually-vendored header
  during this campaign's 2nd-iteration strategy review — every method name/
  signature below is confirmed present at that exact tag; re-verify with a
  quick `search_in_dir`/`grep` over `third_party/httplib/httplib.h` for
  `bind_to_port`/`bind_to_any_port`/`listen_after_bind`/`is_running`/`stop(`
  only if `HTTPLIB_RELEASE_TAG` is ever bumped away from this pinned value,
  since it defaults to `"latest"` and can drift on a from-scratch fetch)**:
  - `Get(pattern, handler)` — registers a route handler.
  - `bind_to_port(host, port, socket_flags = 0)` → `bool` — binds the
    listening socket to a SPECIFIC port synchronously, without blocking on
    accepting connections yet. Returns `false` on failure (e.g. port already
    in use).
  - `bind_to_any_port(host, socket_flags = 0)` → `int` — binds to an
    OS-assigned ephemeral port and returns the real port number that was
    chosen (or a negative value on failure).
  - `listen_after_bind()` → `bool` — the actual blocking loop: starts
    accepting/serving connections against whatever was already bound above,
    and does not return until `stop()` is called from another thread. **This
    is the one call that must run on the background thread, never the
    calling thread.**
  - `stop()` — thread-safe; unblocks a concurrently-running
    `listen_after_bind()` call and makes it return. Safe to call from any
    thread.
  - `is_running()` — whether the server is currently inside
    `listen_after_bind()`.
- This split (`bind_to_port`/`bind_to_any_port` happens synchronously, on
  the CALLING thread, immediately; `listen_after_bind()` — the actual
  blocking part — happens on the background thread) is exactly what removes
  a race a naive "just call `listen(host, port)` on a new thread" design
  would have: with a single combined `listen()` call, the caller has no way
  to know the real bound port (needed for Phase 4's ephemeral-port test, and
  useful for logging) until AFTER the background thread has already started
  and the bind has happened at some non-deterministic point inside it. By
  binding synchronously first, `Start()` can return the real port
  immediately, deterministically, with zero polling/sleeping/retrying
  needed anywhere.
- `gte::Jobs::JobSystem` (`src/Jobs/JobSystem.h/.cpp`) is this codebase's
  existing precedent for "a class that owns a real `std::thread` and cleans
  it up correctly in its destructor" — follow its spirit (RAII, no detached
  threads, destructor always joins) but not its literal shape (it's a
  multi-worker pool built for short CPU jobs; `NetworkServer` is one
  dedicated thread running one long-lived blocking call).
- **`PHASE0_MASTER_STRATEGY.md`'s locked decision #2 requires the bind
  address to be enforced BY THE TYPE ITSELF, not by convention/caller
  discipline** — i.e. `NetworkServer`'s public API must have no way to
  express any host other than loopback at all. Concretely, this means
  `Start()` takes ONLY a `port` parameter — no `host` string parameter of
  any kind — with `"127.0.0.1"` baked in as a private, internal constant
  used for both `bind_to_port()`/`bind_to_any_port()` calls. (An earlier
  draft of this phase document sketched `Start(const std::string& host, int
  port)`, which would have silently reopened exactly the loophole decision
  #2 was written to close — a caller could have passed `"0.0.0.0"` and
  nothing in the type system would have stopped it. Fixed here, before any
  code was written against it, during this campaign's 2nd-iteration
  strategy review.)

## Step 3: The Plan

### 3.1 — Create `src/Network/NetworkServer.h`

```cpp
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
// type by value) - see the "Pimpl" note below for why this header still
// works.
namespace httplib { class Server; }

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
    NetworkServer();
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
};

} // namespace gte::Network
```

(The exact Pimpl shape is an implementation detail left to whoever writes
the `.cpp` — a direct `httplib::Server m_server;` member with a plain
`#include <httplib.h>` at the top of `NetworkServer.h` is ALSO an acceptable,
simpler alternative if the Pimpl indirection is judged not worth its
complexity; either way, `NetworkServer.h`'s PUBLIC method signatures above
must stay exactly as specified, since Phase 3/4 code is written against
them — in particular, **`Start(int port)` must NOT grow a `host` parameter**,
per this phase's own Step 2 above. If the simpler direct-member approach is
chosen, remove the forward-declaration/Pimpl comments above accordingly and
note the resulting `NetworkServer.h -> httplib.h` include as an intentional,
Tier-2-only header dependency in this class's own file comment. Note the
explicit `#include <memory>` above — required for `std::unique_ptr<Impl>`;
do not drop it and rely on some other header transitively providing it.)

### 3.2 — Create `src/Network/NetworkServer.cpp`

Key structure (adapt to whichever Pimpl-vs-direct-member shape 3.1 settled
on):

```cpp
#include "NetworkServer.h"

#include "NetworkRoutes.h"

#include <httplib.h>

#include <cstdio>

namespace gte::Network {

namespace {

// Loopback-only, always - see PHASE0_MASTER_STRATEGY.md's locked "bind
// address" decision and this file's own class doc comment. The ONLY place
// this literal appears - Start()'s signature has no host parameter to
// smuggle a different value in through.
constexpr const char* kBindHost = "127.0.0.1";

// The one, hand-written route table for this campaign - see
// PHASE0_MASTER_STRATEGY.md's locked "Endpoint contract". A future endpoint
// is added here as one more server.Get(...)/Post(...) line, forwarding to
// its own NetworkRoutes.h function - never composing response text inline
// in this lambda.
void RegisterRoutes(httplib::Server& server)
{
    server.Get("/http_hello_world", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(HandleHelloWorld(), "text/plain; charset=utf-8");
    });
}

} // namespace

struct NetworkServer::Impl {
    httplib::Server server;
};

NetworkServer::NetworkServer() : m_impl(std::make_unique<Impl>())
{
    // Registered exactly ONCE per NetworkServer instance, here in the
    // constructor - never inside Start() - so a Start()/Stop()/Start()
    // restart cycle (or a Start() that overlaps a failed bind retry) can
    // NEVER re-register the same route handler onto the same
    // httplib::Server a second time. This closes, by construction, a
    // defensive concern an earlier draft of this phase document had left
    // as a "verify against the vendored header, guard with a bool flag if
    // needed" caveat - registering once, up front, needs no such guard at
    // all.
    RegisterRoutes(m_impl->server);
}

NetworkServer::~NetworkServer()
{
    Stop();
}

void NetworkServer::Start(int port)
{
    if (m_running.load()) {
        return; // Already running - safe no-op, see header comment.
    }

    const int resolvedPort = (port == 0)
        ? m_impl->server.bind_to_any_port(kBindHost)
        : (m_impl->server.bind_to_port(kBindHost, port) ? port : -1);

    if (resolvedPort < 0) {
        std::fprintf(stderr, "NetworkServer: failed to bind %s:%d - network endpoint disabled this run.\n",
            kBindHost, port);
        return; // Non-fatal - see header comment.
    }

    m_boundPort.store(resolvedPort);
    m_running.store(true);

    m_thread = std::thread([this]() {
        m_impl->server.listen_after_bind();
        m_running.store(false);
    });

    std::fprintf(stdout, "NetworkServer: listening on %s:%d\n", kBindHost, resolvedPort);
}

void NetworkServer::Stop()
{
    if (m_impl) {
        m_impl->server.stop();
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_running.store(false);
    m_boundPort.store(0);
}

bool NetworkServer::IsRunning() const noexcept { return m_running.load(); }
int NetworkServer::BoundPort() const noexcept { return m_boundPort.load(); }

} // namespace gte::Network
```

Notes a reviewer/implementer must keep in mind:

- Routes are registered exactly ONCE, in the constructor (see above) — this
  fully closes the "does registering handlers twice on the same
  `httplib::Server` duplicate them" question the earlier draft deferred;
  there is now no code path that can call `RegisterRoutes()` more than once
  against the same `Impl::server`, regardless of how many times
  `Start()`/`Stop()` cycle.
- `Stop()` must be safe to call even when `Start()` was never called (e.g.
  a bind failure left `m_thread` never-started, or `Start()` was simply
  never invoked) — `std::thread::joinable()` correctly returns `false` for a
  default-constructed/never-started thread, so the existing `if
  (m_thread.joinable())` guard already covers this; do not remove it.
- `Start()`/`Stop()` are explicitly documented as NOT thread-safe against
  each other (see the header comment) — this is acceptable because
  `Application` (Phase 3) is the only caller, and it only ever calls both
  from the main thread. Do not add a mutex here purely for
  theoretical/unused generality — this mirrors this codebase's own
  documented "don't over-engineer beyond a component's real, current
  caller" judgment calls elsewhere (e.g. `AGENTS.md`'s own "Jobs::Dispatch()"
  design notes).
- **`Start()` takes ONLY `port` — do not add a `host` parameter back in.**
  This is not a style preference; it's the literal mechanism that makes
  `PHASE0_MASTER_STRATEGY.md`'s locked decision #2 ("enforced by the type
  itself, not by convention/caller discipline") actually true. If a future,
  separately-designed requirement genuinely needs a configurable bind
  address, that needs its own fresh design/security review first (see
  `AGENTS.md`'s new "Networking" section, Phase 3) — never a quiet parameter
  addition here.

### 3.3 — Append to `add_library(gte_core ...)`'s source list

Immediately after the `src/Network/NetworkRoutes.cpp` line Phase 1 added:

```cmake
    src/Network/NetworkServer.h
    src/Network/NetworkServer.cpp
```

### 3.4 — Verify

- `cmake --build build` compiles cleanly. Nothing calls `NetworkServer` yet
  (that's Phase 3), so this is again purely a "compiles as its own
  translation unit, includes httplib.h correctly, links against the
  `httplib`/`ws2_32`/`crypt32` targets already propagated PUBLIC from
  `gte_core`" check.
- Optional, but recommended before moving to Phase 3: a throwaway, NOT
  committed scratch `main()` (or a quick addition to the `--reimport`-style
  CLI branch already in `src/main.cpp`, temporarily) that constructs a
  `NetworkServer`, calls `Start(8080)`, prints `BoundPort()`, sleeps a few
  seconds, and calls `Stop()` — confirming manually (e.g. via
  `curl.exe http://127.0.0.1:8080/http_hello_world`, or PowerShell's
  `Invoke-WebRequest http://127.0.0.1:8080/http_hello_world`) that the whole
  bind → background-thread → route → response path works before Phase 3
  wires it permanently into `Application`. Revert this scratch code before
  committing — Phase 4's real, permanent, automated test is what actually
  proves this ongoing, not a manual throwaway check.

### 3.5 — What this phase deliberately does NOT do

- Does not touch `Application`/`main.cpp` at all (Phase 3).
- Does not add the `GTE_ENABLE_NETWORK` `#if` guard anywhere yet — that
  guard only matters at the PRODUCTION CALL SITE (`Application`'s
  constructor, Phase 3); `NetworkServer` the CLASS always compiles
  regardless of the switch, per `PHASE0_MASTER_STRATEGY.md`'s locked decision
  #6/#4.
- Does not add any test file yet (Phase 4).
- Does not add a `host` parameter to `Start()` under any circumstance — see
  Step 2 and 3.2's own notes above for why this specific temptation must be
  resisted even though it looks like harmless flexibility.
