# PHASE0 — Master Strategy: Editor-Only Engine Logging + Smart Log-Fetch Endpoint (`logger-1`)

This document is the **orchestrator**. It does not itself contain
implementation steps — it defines the goal, the current situation, the
locked design decisions, and the map of child phase documents that carry out
the actual code changes, in order. Every child phase document follows the
same three-step shape (Goal / Situation / Plan) and must be executed in
numeric order, since each phase's code depends on the previous one existing.

Read this file first. Then execute, in order:

- `PHASE1_CORE_LOGGER_MODULE.md`
- `PHASE2_ENGINE_INTEGRATION_AND_FRAME_STAMPING.md`
- `PHASE3_NETWORK_ENDPOINTS_GET_LOGS_AND_CLEAR_LOGS.md`
- `PHASE4_EDITOR_LOG_PANEL_UI.md`
- `PHASE5_SMOKE_TEST_AND_FULL_VALIDATION.md`

Always re-read the previous phase's own completion report (each phase's
working agreement, mirrored from every other campaign in this repository —
see `task_manager/network-impl-1/`, `task_manager/frame-debugger-1/` — is to
write a short `PHASEn_COMPLETION_REPORT.md` next to this file once that
phase's code compiles) before starting the next one — it may record a
decision or a snag that changes a later phase's exact plan.

**Every implementation phase, and anything it further delegates, must use the
`ask_questions` tool whenever it hits a genuine ambiguity or a design choice
this document doesn't already pin down.** This rule propagates recursively:
if an implementation phase itself delegates a sub-task, that delegation
prompt must repeat this same instruction.

---

## Step 1: The Goal (Where are we going?)

Give GreatTamanaEngine its first real, unified logging system, with three
concrete, working deliverables:

1. **A native engine Logger** any subsystem (Renderer, Jobs, Network, ECS,
   Application, ...) can call from any thread, that only ever records
   anything in an **Editor build** (`GTE_ENABLE_EDITOR` ON) — a full release
   runtime game build carries **zero** logging cost (not even evaluating a
   log message's own string-building expression), matching this project's
   existing "true empty no-op when compiled out" precedent
   (`Profiling/ScopeTimer.h`'s `GTE_PROFILE_SCOPE` macro).
2. **A "Log" Editor panel** (Unity Console-style): a scrollable, colored,
   filterable list of everything logged this session, with a Clear button.
3. **A smart HTTP fetch endpoint**, `GET /get_logs`, that an external
   caller (a human, a script, or an LLM agent driving the engine over
   HTTP — see every existing `GET /get_texture`/`GET /list_tabs`-style
   route) can use to pull log entries filtered by keyword, category, level,
   frame-number range, and an incremental cursor (`since_id`) so repeated
   polling never has to re-download the whole buffer — plus a companion
   `POST /clear_logs` to reset it.

The final phase (`PHASE5`) must produce a real, automated, end-to-end smoke
test that logs real entries, fetches them back over a real HTTP round trip
against every filter this campaign adds, and asserts the JSON is correct.

## Step 2: The Situation (Where are we now?)

- **There is no logging module of any kind today.** A repo-wide search turns
  up only scattered, permanent, always-on `std::fprintf(stderr, ...)` calls
  in a handful of files (`Application.cpp` twice, `Jobs/JobContinuation.cpp`,
  `main.cpp` twice, `Network/NetworkServer.cpp` twice,
  `Renderer/RenderGraph/RenderGraphCompiler.cpp`,
  `Renderer/RenderGraph/RenderPipeline.h`,
  `Renderer/Vulkan/VulkanInstance.cpp` three times) — no ring buffer, no
  levels, no filtering, no HTTP exposure, nothing shared. **By explicit
  project-owner decision, none of these existing `fprintf` call sites are
  touched by this campaign** — they keep working exactly as they do today,
  console-only, forever. This campaign only adds a **new**, parallel
  mechanism that **new** call sites (and, in `PHASE2`, a small number of
  freshly-added demonstration call sites) use going forward.
- **This project has three directly-reusable precedents for "a class that
  always compiles, but only actually does something when a specific switch
  is ON", which this campaign's Logger deliberately copies the shape of:**
  - `Profiling/ScopeTimer.h`'s `GTE_PROFILE_SCOPE` macro + `ScopeTimer` class:
    a **full, working class** is declared inside `#if GTE_ENABLE_PROFILER`,
    and a **second, completely empty, fully inline class of the exact same
    name** is declared inside the matching `#else` — so a caller's code
    never needs its own `#ifdef`, and the compiled-out branch has a trivial
    constructor doing nothing at all, "the compiler has nothing left to even
    inline away" (see that file's own comment).
  - `Memory/SdlMemoryTracker.h`: a **static-method, process-global class with
    no instance/DI needed** (its own comment: "the counters here are
    necessarily static/process-global... there is nowhere to stash a `this`
    pointer") — exactly the shape a cross-cutting, called-from-everywhere
    Logger needs, since threading a `Logger&` through every constructor in
    the engine the way `gte::Time`/`gte::EngineContext` are deliberately,
    explicitly *never* done (see `Core/EngineContext.h`'s own doc comment:
    "Deliberately NOT a singleton... exactly one instance... threaded down
    explicitly") would be a completely disproportionate, invasive change for
    a logging facility, unlike simulation `Time` (which genuinely needs to be
    swappable/mockable per-test). **`Renderer/Memory/GpuMemoryTracker.h` is
    NOT a second example of this same shape**, despite sometimes being
    grouped with `SdlMemoryTracker` loosely in passing conversation — checked
    directly against the real file, `GpuMemoryTracker` is actually an
    ordinary, non-static, deliberately non-copyable/non-movable INSTANCE
    class (its own class comment: "always create exactly one and own it via
    `std::shared_ptr`, handing copies of that shared_ptr to every Buffer/
    RenderTexture it creates") — the OPPOSITE shape from what this Logger
    needs. `PHASE1_CORE_LOGGER_MODULE.md`'s own Step 2 documents this
    correction in detail and is the authoritative source; `Logger` must copy
    `SdlMemoryTracker`'s all-static shape only, never `GpuMemoryTracker`'s.
  - `Editor/EditorPanelCatalog.h`: a header that **physically lives under
    `src/Editor/` yet is `#include`d directly from `src/Network/`**
    (`Network/NetworkRoutes.h` line 6), documented there as "a SECOND
    explicit, documented exception" to the rule that everything under
    `src/Editor/` compiles only when `GTE_ENABLE_EDITOR` is ON (the first
    exception being `EditorLayer.h`/`NullEditorLayer.cpp`) — this works
    because a **header with no matching gated `.cpp`** simply compiles
    wherever it's included, in any configuration.
- **`Core/Time.h`'s `Time::FrameCount()`** (`std::uint64_t`, incremented once
  per real `Application::Run()` loop iteration by `Time::Advance()`) is the
  engine's one existing "current frame number" concept — but `Time`/
  `EngineContext` are explicitly non-singleton, DI-only objects owned by
  `Application`, never globally reachable. A Logger call site deep inside
  `Renderer`/`Jobs`/`Network` has no way to reach it directly. This campaign
  needs its own small, independent, globally-readable "what frame is it right
  now" value, updated once per frame by `Application::Run()` (mirroring how
  `SdlMemoryTracker`/`GpuMemoryTracker` each already maintain their own
  independent piece of global state, for their own independent reason) —
  **not** a second copy of `Time` itself, and not a change to `Time`/
  `EngineContext`'s own non-singleton design.
- **`src/Network/`'s established route-handler convention**
  (`docs/conventions/networking.md`): every route handler must be "a PURE
  function of its own request data only" and must never touch
  `Registry`/`Renderer`/`Game`/`AssetDatabase`/`IEditorLayer` directly —
  reaching engine state only through one of several small, reviewed,
  narrow cross-thread bridges (`FrameCaptureBridge`, `EngineCommandBridge`,
  `EditorUiCommandBridge`, `FrameDebuggerCommandBridge`,
  `AssetImportCommandBridge`). **This rule exists because none of those
  subsystems (ECS/Renderer/Game/ImGui) were ever built with concurrent
  access in mind.** The new Logger is a **deliberate, different case**: it
  is designed, from `PHASE1` onward, to be safely called concurrently from
  any thread (main thread, Job worker threads, the Network background
  thread) — it is a purpose-built, self-contained, thread-safe store, not
  "engine state" in the ECS/Renderer/Game sense. `PHASE3` therefore lets
  `GET /get_logs`/`POST /clear_logs` call the Logger **directly**, with
  **no new bridge**, and must document this explicitly as its own narrow
  exception category, distinct from (and never to be read as loosening) the
  existing ECS/Renderer/Game bridge rule.
- **`GET /list_tabs`** (`Network/NetworkServer.cpp`) is the existing
  "needs no bridge at all" precedent this campaign's read-only endpoint
  shape most resembles: "a pure function of build configuration, zero
  runtime/thread/bridge dependency" — `GET /get_logs` is the same shape,
  just a pure function of the Logger's own already-thread-safe state instead
  of compile-time config.
- **The Editor panel wiring convention is fixed and well-precedented**:
  `EditorPanelCatalog.h`'s `kKnownEditorPanelNames` (the one shared list
  `DockLayout.cpp`'s default layout AND `GET /activate_tab`/`GET /list_tabs`
  both read from), `DockLayout.cpp`'s `BuildDefaultDockLayout()`
  (`ImGui::DockBuilderDockWindow(...)` calls), and
  `Editor/ImGuiEditorLayer.cpp`'s fixed, hand-written per-frame sequence of
  `m_xxxPanel.Build(m_ctx, ...)` calls (see `Editor/Panels/JobsPanel.h` for
  the closest analog: a small, stateful, non-polymorphic class, called by
  name — this project deliberately has **no** `IEditorPanel` interface/
  registry).
- **JSON responses already use vendored `nlohmann::json`**
  (`cmake/FetchJson.cmake`), used throughout `Network/NetworkRoutes.cpp`.
- **Tests are already split, in `tests/CMakeLists.txt`, into a block that
  compiles unconditionally and a block guarded by
  `if(GTE_ENABLE_EDITOR) ... endif()`** (see e.g. `Editor/JobsPanelDataTests.cpp`
  inside that guard) — this campaign's Logger tests follow the SAME guard,
  since the Logger's real implementation only exists in that configuration.

## Step 3: The Plan (How do we get there?)

### Locked Design Decisions

These were confirmed with the project owner before writing any phase
document (via `ask_questions`) and MUST NOT be silently changed by a later
phase without updating this file first:

1. **Gating switch: reuse the existing `GTE_ENABLE_EDITOR` switch directly.
   No new CMake `option()` is introduced.** The Logger's actual recording
   behavior is gated by this exact switch — "full release runtime game"
   means a `GTE_ENABLE_EDITOR=OFF` build.
2. **No migration of any existing `std::fprintf(stderr, ...)` call site.**
   They all stay byte-for-byte unchanged, forever, as part of this campaign.
   Only brand-new call sites use the new Logger (`PHASE2` adds a small,
   deliberately limited number of these, purely to prove the mechanism
   integrates cleanly across more than one real engine subsystem — the
   actual, rigorous proof that the Logger is safe under real concurrent
   multi-thread load is `PHASE1`'s own dedicated `LoggerTests.cpp`
   concurrency test, not `PHASE2`'s call sites, some of which cannot be
   proven to run on any particular thread — see `PHASE2`'s own Step 2 for
   the fact-checked detail).
3. **Four log levels: `Debug`, `Info`, `Warning`, `Error`** (in that ordinal
   order — used both for a "minimum level" HTTP filter and for the Editor
   panel's per-level toggle checkboxes).
4. **Bounded ring buffer, capacity 2000 entries.** Once full, the oldest
   entry is evicted (FIFO) each time a new one is recorded. This is a fixed
   `constexpr` capacity, not a runtime-configurable value, for this
   campaign.
5. **Every entry gets a monotonically increasing 64-bit ID**, assigned once
   at record time and never reused — **not reset by `Clear()`** — so a
   caller's previously-seen `since_id` cursor is never ambiguous even across
   a clear.
6. **`GET /get_logs` supports a `since_id` cursor filter** (strictly greater
   than), in addition to `min_level`, `category`, `keyword` (case-insensitive
   substring match on the message), `frame_min`/`frame_max` (inclusive), and
   `limit` (default 200, hard-clamped to the 2000-entry capacity even if a
   caller asks for more).
7. **Every entry carries a free-text `category` string** (e.g. `"Renderer"`,
   `"Jobs"`, `"Network"`, `"Application"`), settable by the caller at the
   `GTE_LOG_*` call site and filterable via `GET /get_logs?category=...`
   (exact, case-sensitive match — simplest possible semantics for v1).
8. **No `printf`-style formatting in the logging macros.** A call site
   always passes an already-built `std::string` message
   (`GTE_LOG_WARNING("Renderer", "value=" + std::to_string(x))`), never a
   format string + variadic args.
9. **Every entry carries BOTH a frame number (`std::uint64_t`) AND a real
   wall-clock-style timestamp (`double`, seconds since the Logger's own
   first use this process — independent of `gte::Time`/`EngineContext`, see
   Step 2)**, so a log call made before the very first rendered frame (e.g.
   during engine boot) still has a meaningful "when".
10. **Both a Clear button in the Log panel AND a `POST /clear_logs` network
    endpoint** are in scope for this campaign.
11. **Endpoint path: `GET /get_logs`.** The companion clear endpoint is
    `POST /clear_logs` (chosen for symmetry with this project's existing
    `POST /save_scene`/`POST /load_scene`/`POST /instantiate_primitive`
    naming, and because it mutates state, unlike every existing bare `GET`
    route).
12. **Module location: `src/Editor/Logger.h` + `src/Editor/Logger.cpp`.**
    This is a deliberate, THIRD documented exception to "everything under
    `src/Editor/` compiles only when `GTE_ENABLE_EDITOR` is ON", alongside
    `EditorLayer.h`/`NullEditorLayer.cpp` and `EditorPanelCatalog.h` (see
    Step 2) — `Logger.h` itself must be safely `#include`-able, and every
    one of its declared static methods safely callable with zero `#ifdef`
    at the call site, from **any** file in the engine (`Renderer/`,
    `Jobs/`, `Network/`, `ECS/`, `Application/`, ...) **regardless** of
    `GTE_ENABLE_EDITOR`'s value — by giving `Logger.h` the exact same
    dual-class-declaration shape as `ScopeTimer.h` (Step 2): a full
    implementation (declared here, defined out-of-line in `Logger.cpp`,
    which is ONLY added to the build inside CMake's `if(GTE_ENABLE_EDITOR)`
    block) inside `#if GTE_ENABLE_EDITOR`, and a second, fully inline,
    trivial no-op class of the identical name and method signatures inside
    `#else` (so nothing ever needs linking when the switch is OFF). The
    `GTE_LOG_DEBUG`/`INFO`/`WARNING`/`ERROR` macros themselves ALSO fully
    vanish to `((void)0)` when the switch is OFF (see Locked Design
    Decision 13) — this is a deliberate belt-and-suspenders design: even
    though calling the OFF-branch's inline no-op `Logger::Log(...)` would
    already be nearly free, it would still evaluate the `message`
    expression as a function argument first; the vanishing macro is what
    guarantees that expression is **never evaluated at all**.
13. **The `GTE_LOG_*` macros are the ONE sanctioned way any call site
    anywhere in the engine ever talks to the Logger** (mirroring
    `GTE_PROFILE_SCOPE` being the sanctioned way to talk to `ScopeTimer`) —
    they, not `gte::Logger::Log(...)` directly, are what fully vanishes to
    `((void)0)` when `GTE_ENABLE_EDITOR` is OFF. Calling `gte::Logger::`
    methods directly (bypassing the macros) is reserved for this campaign's
    own tests and for `Network/NetworkRoutes.cpp`'s `Query()`/`Clear()`
    calls (which need the real return values, not a fire-and-forget log
    call) — see Locked Design Decision 14.
14. **`GET /get_logs`/`POST /clear_logs` call `gte::Logger::Query(...)`/
    `gte::Logger::Clear()` DIRECTLY, with NO new cross-thread bridge.** This
    is an explicit, narrow exception to the Networking convention's
    "route handler reaches engine state only through a reviewed bridge"
    rule (Step 2) — it must be documented as such in
    `docs/conventions/networking.md`/`docs/conventions/logging.md` in
    `PHASE3`, precisely so a future contributor never reads it as
    permission to bypass that rule for actual ECS/Renderer/Game state.

### Non-Goals (explicitly out of scope for `logger-1`)

Stated here so no phase document below "helpfully" scope-creeps into these —
each would need its own dedicated, reviewed design pass first:

- **No migration of existing `fprintf(stderr, ...)` call sites** (Locked
  Design Decision 2) — not even a "quick pass" while a phase happens to be
  editing a nearby line.
- **No persistent/on-disk log file.** The Logger is a pure in-memory ring
  buffer; a log is lost the moment the process exits (or the buffer wraps
  past 2000 entries). Adding file persistence is real, separate,
  follow-on work.
- **No WebSocket/streaming/"tail -f"-style live push.** `GET /get_logs` is a
  plain, one-shot HTTP request/response — a caller wanting near-real-time
  updates polls it repeatedly using `since_id`.
- **No structured/multi-field message payloads, and no stack traces.** A
  log entry's `message` is one plain string, full stop.
- **No new `GTE_ENABLE_*` CMake switch of any kind** (Locked Design
  Decision 1).
- **No authentication/authorization on the new endpoints** — they inherit
  the exact same loopback-only bind security model every other endpoint in
  this engine already has (`NetworkServer` still binds only to
  `127.0.0.1`, unconditionally, by construction).
- **No `printf`-style formatting macro** (Locked Design Decision 8).
- **No runtime-configurable ring-buffer capacity.** 2000 is a fixed
  `constexpr` for this campaign; making it configurable is real, separate,
  follow-on work if ever needed.
- **No Frame-Debugger integration** (e.g. clicking a log line to jump the
  Frame Debugger to that frame) — a natural future addition, explicitly
  deferred.

### Phase Map

| Phase | Deliverable |
|---|---|
| **1** | `src/Editor/Logger.h`/`.cpp` — `LogLevel`, `LogEntry`, `LogQueryFilter`, the thread-safe `Logger` static class (dual `#if`/`#else` shape), the `GTE_LOG_*` macros. CMake wiring. Tier-1 tests (`tests/Editor/LoggerTests.cpp`). No engine call sites, no network, no UI yet. |
| **2** | `Application::Run()` calls `Logger::SetCurrentFrame(...)` once per frame; a small, deliberately limited number of brand-new `GTE_LOG_*` call sites across `Application`/`Jobs`/`Network`, added ALONGSIDE (never replacing) existing `fprintf` calls, proving zero-`#ifdef` reachability from anywhere in the engine (the rigorous multi-thread safety proof itself already lives in `PHASE1`'s own concurrency test). |
| **3** | `Network/NetworkRoutes.h`/`.cpp` gain `ParseGetLogsQuery()`/`BuildGetLogsResponseJson()`/`BuildClearLogsResponseJson()`; `NetworkServer.cpp` registers `GET /get_logs`/`POST /clear_logs` (no new bridge — see Locked Design Decision 14); `AGENTS.md` + `docs/conventions/logging.md` (new) + `docs/conventions/networking.md` updated; Tier-1 tests. |
| **4** | `src/Editor/LogPanelData.h`/`.cpp` (pure formatting/filtering) + `src/Editor/Panels/LogPanel.h`/`.cpp` (the "Log" Editor panel) + `EditorPanelCatalog.h`/`DockLayout.cpp`/`ImGuiEditorLayer.h`/`.cpp` wiring. Tier-1 tests. |
| **5** | Real, ephemeral-port, real-socket, real-background-thread end-to-end smoke test of `GET /get_logs`/`POST /clear_logs` against every filter this campaign adds (mirrors `tests/Network/NetworkServerTests.cpp`'s existing pattern); full build; full `ctest` run; `README.md` "Status" bullet; final documentation cross-check. |

### Definition of Done for the whole campaign

- `cmake --build build` succeeds (default configuration, `GTE_ENABLE_EDITOR`
  ON — the project's default).
- `ctest` (see the regression command below) passes, including every new
  test file this campaign adds.
- Running the built `GreatTamanaEngine.exe` and issuing
  `GET http://127.0.0.1:8080/get_logs` (optionally with `since_id`/
  `min_level`/`category`/`keyword`/`frame_min`/`frame_max`/`limit` query
  parameters) returns a well-formed JSON body reflecting real, currently
  recorded log entries, while the engine window keeps rendering. Issuing
  `POST http://127.0.0.1:8080/clear_logs` empties the buffer.
- A live Editor session shows a "Log" tab, docked alongside "Memory"/
  "Profiler"/"Jobs"/"Atmosphere", displaying real log entries, filterable by
  level/category/keyword, with a working Clear button.
- `AGENTS.md` has a new "Logging" section; `README.md`'s "Status" section has
  a new bullet describing this feature, matching every other feature's own
  documentation precedent in that file.

### Regression / build commands (reference — see each phase for exact use)

```
cmake --build build
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure
```

Per this campaign's own working agreement: **only `PHASE5` runs a full
build + full `ctest` pass.** Phases 1-4 do a fast, targeted incremental
compile check only (e.g. building just the `gte_core`/`GreatTamanaEngineTests`
targets, or even just re-compiling the handful of translation units touched)
— never a full regression run, to keep iteration fast on this machine.
