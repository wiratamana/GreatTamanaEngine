# PHASE3 — Application Integration + Thread-Safety Documentation

Parent: `PHASE0_MASTER_STRATEGY.md`. Prerequisite: Phases 1 and 2 done
(`src/Network/NetworkRoutes.*`, `src/Network/NetworkServer.*` exist and
compile as part of `gte_core`, and `NetworkServer::Start()` takes ONLY an
`int port` parameter — no `host` — per Phase 2's own locked signature).

## Step 1: The Goal

Wire `gte::Network::NetworkServer` into `Application` (the engine's
composition root) so it actually starts, automatically, the moment the
engine runs — gated behind `GTE_ENABLE_NETWORK` at the call site, per
`PHASE0_MASTER_STRATEGY.md`'s locked decisions #4/#5 — and stops cleanly via
RAII when the engine exits. Then write down, in `AGENTS.md`, the
thread-safety rule this whole campaign depends on (no route handler may
touch engine state), so a future contributor adding a second endpoint reads
the rule before writing unsafe code, the same way the Job System's own
Phase 4 thread-safety audit table already protects every other engine
subsystem from an unsafe job body.

## Step 2: The Situation

- `Application` (`src/Application/Application.h`) declares its RAII-owned
  members in a fixed order (`SdlContext`, `Window`, `Renderer`,
  `rg::RenderGraph`, `IEditorLayer`, `Game`, plus two plain `int` window-size
  fields) and constructs them in that exact order in
  `Application.cpp`'s constructor initializer list — C++ requires member
  destruction to happen in the REVERSE of declaration order, which this
  codebase already relies on deliberately (see `Application.h`'s own
  comments, e.g. "*Declared after Renderer... so it is destroyed before
  Renderer's Vulkan device/instance go away*"). Confirmed against the
  actual current file during this campaign's 2nd-iteration strategy review
  — the member list and the constructor's initializer list (currently an
  empty `{ }` body) both still match exactly what this phase's plan below
  assumes; no drift.
- `Application::Run()`'s frame loop (`src/Application/Application.cpp`) has
  no networking-related code anywhere in it today.
- `AGENTS.md` already has a "Job System" section with a full thread-safety
  classification table (NEVER / READ-SAFE / JOB-SAFE) for every existing
  engine subsystem, written specifically so a future job body doesn't have
  to independently re-derive whether touching e.g. `Registry` or `Renderer`
  from a worker thread is safe. This campaign needs the equivalent
  reasoning for the brand-new network background thread, but does NOT need
  to reproduce that entire table — it can reference it directly, since the
  conclusion is nearly identical (nothing under `src/ECS`, `src/Renderer`,
  `src/Editor`, `src/Assets` is safe to touch from any thread other than the
  main thread, full stop, for this campaign's purposes). This new section
  is inserted immediately after the existing "## Job System" section and
  before "## Render Target Format Matching" — confirmed those two headings
  are still directly adjacent in the current `AGENTS.md`.
- `README.md`'s "Status" section is an append-only, reverse-chronological
  list of shipped features (see its own many bullet points, each describing
  one shipped campaign) — this campaign's own entry belongs at the end of
  that list, immediately before the "## Roadmap" heading, following the
  exact same one-paragraph-summary-plus-cross-reference style every other
  entry already uses.

## Step 3: The Plan

### 3.1 — `Application.h` changes

Add the include and the new member. In the `#include` block at the top:

```cpp
#include "../Network/NetworkServer.h"
```

Add the new member to the `private:` section, declared **last** (after
`Game m_game;`, before the two `int` window-size fields — or after them,
either is fine since those two are plain ints with no destructor-ordering
concern; what matters is that `m_networkServer` is declared AFTER every
other RAII member):

```cpp
    // Networking campaign (task_manager/network-impl-1/) - an embedded,
    // loopback-only HTTP server (see AGENTS.md, "Networking"). Declared
    // LAST (after Game) so it is DESTROYED FIRST, before Game/the Editor/
    // Renderer/Window/SDL start tearing down - a defensive ordering choice,
    // not a strictly necessary one today (no route handler touches any
    // engine state at all yet - see NetworkRoutes.h), but it's what a
    // FUTURE endpoint that DOES need to bridge into engine state would
    // already want: the background thread is guaranteed fully stopped
    // before anything it might eventually reference starts being torn down.
    Network::NetworkServer m_networkServer;
```

### 3.2 — `Application.cpp` changes

In the constructor body (NOT the initializer list — `NetworkServer` default-
constructs trivially via its own default constructor already covering the
initializer list implicitly; only the `Start()` call needs to happen
explicitly, after every other member is fully constructed). The current
constructor body is empty (`{ }`) — confirmed against the real file during
this review — so this is a pure addition, not a merge with existing logic:

```cpp
Application::Application(const std::string& title, int width, int height)
    : m_sdlContext()
    , m_window(title, width, height)
    , m_renderer(m_window)
    , m_renderGraph(m_renderer)
    , m_editorLayer(CreateEditorLayer(m_window, m_renderer))
    , m_game()
    , m_windowWidth(width)
    , m_windowHeight(height)
{
#if GTE_ENABLE_NETWORK
    // Loopback-only (127.0.0.1 is baked into NetworkServer itself - Start()
    // deliberately has no host parameter, see Phase 2), port 8080 - see
    // AGENTS.md, "Networking", and
    // task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md's locked
    // design decisions for why this is hardcoded rather than configurable
    // yet, and why this is safe to auto-start unconditionally. A bind
    // failure (e.g. another instance of the engine already running and
    // holding port 8080) is logged by NetworkServer itself and is
    // NON-FATAL - the rest of Application still starts normally either way.
    m_networkServer.Start(8080);
#endif
}
```

Do **not** add anything to `Run()`'s frame loop — the whole point of running
the server on its own background thread (Phase 2) is that the main loop
needs zero per-frame awareness of it. `Application`'s destructor needs no
explicit code either — `m_networkServer`'s own destructor (RAII, Phase 2)
calls `Stop()` automatically when `Application` itself is destroyed, exactly
like every other member.

### 3.3 — New `AGENTS.md` section: "Networking"

Insert as a new top-level `##` section — appended after the existing "Job
System" section (before "Render Target Format Matching"), since it builds
directly on that section's own thread-safety-table precedent and is easiest
to read right after it. Content:

```markdown
## Networking

`src/Network/` (`NetworkRoutes.h/.cpp`, `NetworkServer.h/.cpp`) is the
engine's first real network I/O - a single embedded, loopback-only HTTP
server (`gte::Network::NetworkServer`, built on the already-vendored
cpp-httplib - see `cmake/FetchHttplib.cmake`), auto-started by
`Application`'s constructor (gated by the `GTE_ENABLE_NETWORK` CMake
option, default ON - see `task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md`
for the full campaign writeup). Follow these rules whenever touching this
module or adding a new endpoint:

- **The server only ever binds to `127.0.0.1` (loopback) - never a
  LAN-visible interface.** This is enforced by `NetworkServer::Start(int
  port)`'s own signature - there is no `host` parameter anywhere in its
  public API for a caller to override upward, so the constraint holds by
  construction, not by convention (see PHASE0's own "Locked Design
  Decisions"). A future requirement to expose this remotely needs a fresh,
  explicit design/security review first, not a one-line signature change.
- **Every registered route handler runs on NetworkServer's own dedicated
  background `std::thread` (via `httplib::Server::listen_after_bind()`),
  NEVER the main thread - and, symmetrically, nothing outside
  `NetworkServer.cpp` may call into `httplib::Server` directly.** This is
  a NEW background thread, structurally similar to (but independent from)
  the Job System's own worker-thread pool (see "Job System" above) - the
  exact same category of problem applies: this thread runs completely
  unsynchronized with the main thread's own frame loop, so it must never
  read OR write any engine-owned mutable state without a dedicated,
  reviewed thread-safe bridge.
- **A route handler must be a PURE function of its own request data only -
  it must NEVER touch `Registry`/`Renderer`/`Game`/`AssetDatabase`/
  `IEditorLayer`/ImGui/any other engine subsystem, directly or indirectly,
  full stop.** Every single row of the Job System's own Phase 4
  thread-safety classification table (see "Job System" above) that says
  **NEVER** for a job body applies at least as strongly here - none of
  those subsystems were built with ANY concurrent access in mind, and this
  network thread has no more special standing than an arbitrary job body
  would. `NetworkRoutes.h`'s own convention (every handler is a small, pure
  function with no engine-side parameter at all, e.g. `HandleHelloWorld()`)
  is what makes this rule trivially satisfiable today - a future endpoint
  that genuinely needs engine data (e.g. "how many entities are in the
  scene") needs a dedicated, reviewed, thread-safe bridge built first (e.g.
  a fixed-size, mutex-guarded command/snapshot queue the main thread drains
  once per frame, mirroring `Jobs::detail::JobQueue`'s own fixed-capacity,
  mutex-guarded shape) - never a raw pointer/reference into live engine
  state handed to a handler lambda.
- **Every one of `NetworkServer`'s own failure modes (a bind failure, e.g.
  the port already being in use) is NON-FATAL - log to stderr and continue,
  never throw/crash/abort engine startup.** This is a debugging/tooling
  aid layered on top of the engine, not a required subsystem the engine
  cannot run without (the same spirit `cmake/FetchHttplib.cmake`'s own
  header comment already states) - a developer running two instances of
  the engine at once, or a port already held by something else, must never
  be the reason the engine window itself fails to open. This exact path
  (a bind collision leaving `IsRunning() == false`) has an automated
  regression test - see Phase 4's `NetworkServerTests.cpp`.
- **`NetworkServer::Start()`/`Stop()` are NOT thread-safe against each
  other or against themselves (no internal mutex) - by design, since
  `Application` is their only caller, and it only ever calls both from the
  main thread.** A future caller from a different thread (e.g. a future
  Editor "Network" panel's Start/Stop button, if that panel itself isn't
  already guaranteed to run on the main thread the way every other Editor
  panel does - see "Editor Module Structure") must not assume this is safe
  without adding real synchronization first.
- **`GTE_ENABLE_NETWORK` follows the exact same "class always compiles,
  only the production call site is gated" precedent as
  `GTE_ENABLE_JOB_SYSTEM`/`GTE_ENABLE_PROFILER` (see those sections above).**
  `NetworkServer`/`NetworkRoutes` compile and their tests pass identically
  whether the switch is ON or OFF - turning it OFF only skips
  `Application`'s own `m_networkServer.Start(...)` call, so a build with it
  OFF never opens a socket at all, at zero runtime cost.
```

### 3.4 — `README.md` "Status" section addition

Append (as the very last bullet, matching the file's existing
reverse-chronological/append-at-end convention, immediately before the
"## Roadmap" heading) a short entry, e.g.:

```markdown
- **The engine now has its first real networking feature: an embedded,
  loopback-only HTTP server.** `src/Network/` (`NetworkServer.h/.cpp`,
  built on the already-vendored cpp-httplib - see `cmake/FetchHttplib.cmake`)
  is auto-started by `Application`'s constructor (gated by a new
  `GTE_ENABLE_NETWORK` CMake option, default ON) on a dedicated background
  thread, so it never blocks the main frame loop. Binds to `127.0.0.1` only
  - `NetworkServer::Start(int port)` has no `host` parameter at all, so this
  is enforced by the type itself, never reachable from another machine. One
  endpoint exists today: `GET http://127.0.0.1:8080/http_hello_world`
  returns `hello world`. See `AGENTS.md`'s new "Networking" section for the
  thread-safety rule every future endpoint must follow (a route handler
  must be a pure function of its own request data - it must never touch
  ECS/Renderer/Game/AssetDatabase directly), and
  `task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md` for the full
  campaign writeup.
```

### 3.5 — Verify

- `cmake --build build` (default `GTE_ENABLE_NETWORK=ON`) succeeds.
- `cmake --build build` with a fresh configure at `-DGTE_ENABLE_NETWORK=OFF`
  also succeeds, and (manually) running that build's `.exe` never opens
  port 8080 (confirm via `netstat -ano | findstr 8080` from a `cmd.exe`
  prompt showing nothing, or simply that a request against it fails to
  connect at all).
- (Manual smoke check, not part of the automated suite - Phase 4 owns the
  real automated proof) run the default-config `.exe` and, from the same
  machine, issue a request against
  `http://127.0.0.1:8080/http_hello_world` — e.g.
  `curl.exe http://127.0.0.1:8080/http_hello_world` (curl.exe ships with
  Windows 10/11 by default) or PowerShell's
  `(Invoke-WebRequest http://127.0.0.1:8080/http_hello_world).Content` —
  confirm a `200` response with body `hello world`, and confirm the engine
  window keeps rendering/responding throughout. Since the bind is strictly
  loopback-only, this should never trigger a Windows Defender Firewall
  "allow this app" prompt (those are for LAN-visible listeners) — if one
  does appear, that itself is a signal worth investigating before moving
  on, since it would suggest the bind address isn't as loopback-only as
  intended.

### 3.6 — What this phase deliberately does NOT do

- Does not add any Editor UI for this feature (no goal per PHASE0's
  Non-Goals).
- Does not add the automated test files yet (Phase 4) — this phase's own
  verification (3.5) is manual/build-only, consistent with this campaign's
  overall "no phase should end without a compiling result, but automated
  proof is Phase 4's dedicated job" structure.
- Does not pass a `host` argument to `Start()` — there is no such parameter
  (see Phase 2's locked `Start(int port)` signature); do not reintroduce one
  here even for "clarity" at the call site.
