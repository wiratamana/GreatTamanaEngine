# PHASE0 — Master Strategy: Embedded HTTP Networking (`network-impl-1`)

This document is the **orchestrator**. It does not itself contain
implementation steps — it defines the goal, the current situation, the
locked design decisions, and the map of child phase documents that carry out
the actual code changes, in order. Every child phase document follows the
same three-step shape (Goal / Situation / Plan) and must be executed in
numeric order, since each phase's code depends on the previous one existing.

Read this file first. Then execute, in order:

- `PHASE1_NETWORK_MODULE_FOUNDATION_AND_ROUTE_LOGIC.md`
- `PHASE2_NETWORK_SERVER_BACKGROUND_THREAD_LIFECYCLE.md`
- `PHASE3_APPLICATION_INTEGRATION_AND_THREAD_SAFETY_DOCS.md`
- `PHASE4_AUTOMATED_TESTS_AND_REGRESSION_SAFETY.md`

Always re-read the previous phase's own completion report (each phase's
working agreement, mirrored from every other campaign in this repository —
see `task_manager/scene-serialization-1/`, `task_manager/job_system/` — is to
write a short `PHASEn_COMPLETION_REPORT.md` next to this file once that
phase's code compiles) before starting the next one — it may record a
decision or a snag that changes a later phase's exact plan.

---

## Step 1: The Goal (Where are we going?)

Give GreatTamanaEngine a first, real, working slice of **network I/O**: the
engine process opens a local HTTP endpoint while it runs, and answers a
request against it without ever blocking the main loop (`Application::Run()`
— the render/game/editor frame loop). Concretely, the acceptance behavior is:

```
GET http://127.0.0.1:8080/http_hello_world   ->  200 OK, body: "hello world"
```

while the engine's own window keeps rendering/updating at full frame rate,
completely undisturbed by the HTTP request having arrived or being served.

This is the **first** networking feature this engine has ever had — nothing
under `src/` talks to a socket today (see Step 2). The goal of this specific
campaign is deliberately narrow (one working endpoint, proven end-to-end,
non-blocking) — it exists to stand up the whole plumbing (dependency, module,
background-thread lifecycle, engine composition-root wiring, thread-safety
rules, automated tests) correctly ONCE, so a second/third endpoint later is a
five-minute addition, not a redesign.

## Step 2: The Situation (Where are we now?)

- **The HTTP library is already vendored, but completely unused.**
  `cmake/FetchHttplib.cmake` already fetches
  [cpp-httplib](https://github.com/yhirose/cpp-httplib) (a single-header,
  header-only HTTP/HTTPS server+client library) into
  `third_party/httplib/httplib.h` and defines an INTERFACE target `httplib`
  (linking `ws2_32`/`crypt32` on Windows). The root `CMakeLists.txt` already
  links `httplib` `PUBLIC` into `gte_core` (see its own comment: *"included
  here purely as a debugging/tooling dependency... nothing in gte_core uses
  it yet"*). **No engine source file `#include <httplib.h>`s it anywhere.**
  This campaign is what finally uses it for real.
- **No networking module exists.** There is no `src/Network/` folder, no
  socket code, nothing in `AGENTS.md` documenting thread-safety rules for a
  network thread (unlike the Job System's own worker-thread pool, which has a
  whole audited section — see AGENTS.md, "Job System", Phase 4's
  thread-safety table).
- **The engine already has ONE precedent for a real background OS thread
  living alongside the main loop**: `gte::Jobs::JobSystem`
  (`src/Jobs/JobSystem.h/.cpp`) — a `std::thread`-based worker pool, plus a
  registered-background-thread mechanism for a long-lived polling thread
  (`RegisterBackgroundThread()`/`IsShuttingDown()`). This campaign's
  `NetworkServer` follows the same house style (RAII-owned `std::thread`,
  clean `Stop()`/join in the destructor, no detached threads) but is
  deliberately its OWN, separate, much simpler class — a single dedicated
  listener thread, not a worker pool — since serving one blocking
  `accept()`/`recv()` loop is a fundamentally different shape of work than
  scheduling short CPU jobs across N workers.
- **`Application` (`src/Application/Application.h/.cpp`) is the engine's one
  composition root** — it already owns `SdlContext`, `Window`, `Renderer`,
  `rg::RenderGraph`, `IEditorLayer`, and `Game` as RAII members, constructed
  in a fixed order, and drives everything from `Run()`'s single frame loop.
  Any new always-on engine subsystem is wired in here, exactly like every
  existing one.
- **The codebase has an established, repeated convention for a toggleable
  subsystem**: `GTE_ENABLE_EDITOR` / `GTE_ENABLE_PROFILER` /
  `GTE_ENABLE_JOB_SYSTEM` (see root `CMakeLists.txt` and `AGENTS.md`) are all
  CMake `option()`s, each exposed to C++ as a `PUBLIC` `1`/`0` compile
  definition, each following the same rule: **the class/module itself always
  compiles** (so it stays available/testable in every build configuration),
  and only its actual PRODUCTION call site / internal behavior is gated by
  the switch. This campaign adds a new one, `GTE_ENABLE_NETWORK`, following
  that exact precedent.
- **User-confirmed design constraints for this campaign** (see "Locked
  Design Decisions" below): the server is **localhost-only** — it must never
  be reachable from another machine on the network.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions

These were confirmed before writing any phase document and MUST NOT be
silently changed by a later phase without updating this file first:

1. **HTTP library**: cpp-httplib, via the already-fetched `httplib` CMake
   target. No new dependency is fetched by this campaign.
2. **Bind address: loopback-only (`127.0.0.1`), always.** The server is never
   bound to `0.0.0.0`/a LAN-visible interface — this was an explicit,
   confirmed constraint from the project owner ("the network is only for
   localhost"). This is not just a default value that a caller could
   override upward — `NetworkServer::Start(int port)`'s public API in Phase
   2 has NO `host` parameter of any kind (this was corrected during this
   campaign's 2nd-iteration strategy review — an earlier draft of Phase 2
   had sketched `Start(const std::string& host, int port)`, which would have
   silently reopened exactly this loophole), so this constraint is enforced
   by the type itself, not by convention/caller discipline.
3. **Default port: `8080`.** `NetworkServer::Start(int port)` takes ONLY the
   port as a parameter (so a future embedder/test can choose a different one
   — see Phase 2's `port == 0` "ephemeral port" support, used by Phase 4's
   own integration test to avoid ever colliding with a real running instance
   of the engine on the same machine), but `Application`'s own production
   call site (Phase 3) uses `8080` as the concrete value.
4. **New compile-time toggle: `GTE_ENABLE_NETWORK` (CMake `option()`,
   default `ON`)** — added to the root `CMakeLists.txt` immediately after
   `GTE_ENABLE_JOB_SYSTEM`, following the exact same "class always compiles,
   `PUBLIC` `1`/`0` macro, only the production call site is `#if`-gated"
   precedent as the three existing toggles. A `GTE_ENABLE_NETWORK=OFF` build
   never opens a socket at all, at zero cost, while `src/Network/`'s own
   classes/tests still compile and pass either way (mirroring
   `JobSystemTests.cpp`'s own "passes identically whether
   `GTE_ENABLE_JOB_SYSTEM` is ON or OFF" property).
5. **Auto-started, not opt-in via a CLI flag.** `Application`'s constructor
   starts the server automatically (gated by `#if GTE_ENABLE_NETWORK`) every
   time the engine runs, the same way `SdlMemoryTracker::Install()` is
   unconditional-once-the-switch-is-on rather than needing a runtime flag.
   Given the loopback-only bind (point 2), this is a low-risk default — see
   Phase 3 for the exact wiring and its own doc-comment reasoning.
6. **Module location: `src/Network/`**, an always-compiled, engine-level
   module with **zero** dependency on ECS/Renderer/Editor/Game — the same
   "own layer, no upward dependency" shape as `src/Physics/`, `src/Jobs/`,
   and `src/Scene/`. This is what keeps `NetworkRoutes.h`'s pure handler
   logic genuinely Tier-1-testable (see `TESTING.md`'s taxonomy) and keeps
   the door open for `src/Network/` to be reused from a future headless/
   server build with no window/renderer at all.
7. **Endpoint contract for this campaign's one deliverable**: `GET
   /http_hello_world` → HTTP 200, `Content-Type: text/plain; charset=utf-8`,
   body exactly `hello world` (no trailing newline). Everything else (any
   other path/method) falls through to cpp-httplib's own default 404 — no
   custom catch-all handler is added in this campaign.

### Non-Goals (explicitly out of scope for `network-impl-1`)

Stated here so no phase document below "helpfully" scope-creeps into these —
each would need its own dedicated, reviewed design pass first:

- **No engine-state-touching endpoint.** Every route handler added in this
  campaign is a pure function of its own request data, with **zero** access
  to `Registry`/`Renderer`/`Game`/`AssetDatabase`/anything else engine-side —
  see Phase 3's new "Networking" section in `AGENTS.md` for the full
  thread-safety reasoning (this mirrors the Job System's own Phase 4
  thread-safety audit table almost exactly: everything not specifically
  reviewed and marked safe is NEVER-safe for this new background thread to
  touch). A future endpoint that needs to read/write engine state (e.g. an
  `/scene_info` endpoint, or a remote "spawn a primitive" command) needs a
  dedicated, reviewed thread-safe bridge (e.g. a fixed-size, mutex-guarded
  command queue the main thread drains once per frame, mirroring the shape
  of `Jobs::detail::JobQueue`) — that bridge is real, follow-on work, **not**
  built here.
- **No HTTPS/TLS.** cpp-httplib supports it (compiled in only when
  `CPPHTTPLIB_OPENSSL_SUPPORT` + linked OpenSSL are present), but this
  campaign never defines that macro or links OpenSSL — plain HTTP only,
  consistent with "localhost-only debugging aid", not a production remote
  API surface.
- **No authentication/authorization.** Loopback-only bind is this
  campaign's entire security model — no API key, no token, nothing.
- **No JSON (de)serialization / request-body parsing.** The one endpoint in
  this campaign is a bodyless `GET`. No JSON library is vendored or needed.
- **No Editor UI panel for the network server** (e.g. a "Network" panel
  showing request count/last request/start-stop toggle). A natural future
  addition once the engine has more than one endpoint worth observing —
  explicitly deferred.
- **No dynamic route registration from outside `src/Network/`.** The route
  table is a fixed, hand-written list wired once in `NetworkServer.cpp` — no
  plugin/registry pattern, mirroring the Editor's own deliberate "no
  `IEditorPanel` abstraction preemptively" precedent (see `AGENTS.md`,
  "Editor Module Structure").

### Phase Map

| Phase | Deliverable |
|---|---|
| **1** | `src/Network/NetworkRoutes.h/.cpp` — pure, httplib-free handler logic (`HandleHelloWorld()`), CMake wiring (`GTE_ENABLE_NETWORK` option + compile definition + `gte_core` sources). |
| **2** | `src/Network/NetworkServer.h/.cpp` — the RAII class owning a real `httplib::Server` + a dedicated background `std::thread`, with race-free `Start()`/`Stop()`/`BoundPort()`, wired to the Phase 1 route table. |
| **3** | `Application` integration (construct + `#if GTE_ENABLE_NETWORK`-gated `Start()`/automatic `Stop()` via RAII), plus a new "Networking" section in `AGENTS.md` documenting the thread-safety rule every future route handler must follow, plus a `README.md` "Status" entry. |
| **4** | Automated tests: `tests/Network/NetworkRoutesTests.cpp` (Tier 1, pure logic) and `tests/Network/NetworkServerTests.cpp` (a real, ephemeral-port, real-socket, real-background-thread end-to-end test using `httplib::Client`), wired into `tests/CMakeLists.txt`. |

### Definition of Done for the whole campaign

- `cmake --build build` succeeds with `GTE_ENABLE_NETWORK` at its default
  (`ON`) AND with `-DGTE_ENABLE_NETWORK=OFF` (both must compile — the class
  always compiles, only the call site is gated).
- Running the built `GreatTamanaEngine.exe` and issuing
  `GET http://127.0.0.1:8080/http_hello_world` (from the same machine)
  returns `hello world` while the engine window keeps rendering.
- `ctest` (see the regression command below) passes, including the two new
  `Network/` test files.
- `AGENTS.md` has a new "Networking" section; `README.md`'s "Status" section
  has a new bullet describing this feature, matching every other feature's
  own documentation precedent in that file.

### Regression / build commands (reference — see each phase for exact use)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```
