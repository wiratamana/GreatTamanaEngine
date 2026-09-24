# Logging

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Editor/Logger.h/.cpp` (`gte::Logger`) is the engine's first unified
logging system (`logger-1` campaign,
`task_manager/logger-1/PHASE0_MASTER_STRATEGY.md`) - a process-global,
thread-safe, in-memory log store any subsystem can call from any thread, with
zero cost in a full release runtime game build. Follow these rules whenever
touching this module or adding a new call site:

- **`Logger.h` uses the exact same dual-branch `#if GTE_ENABLE_EDITOR` /
  `#else` shape as `Profiling/ScopeTimer.h`'s `GTE_PROFILE_SCOPE`.** A full,
  real, thread-safe implementation (declared in `Logger.h`, defined
  out-of-line in `Logger.cpp`, which is ONLY added to the build inside
  CMake's `if(GTE_ENABLE_EDITOR)` block) exists when the switch is ON; a
  second, fully inline, trivial no-op class of the IDENTICAL name and method
  signatures (including matching non-`noexcept` `Log()`/`Clear()`
  signatures) exists when it's OFF - nothing ever needs linking against
  `Logger.cpp` in an OFF build. `Logger.h` itself is a THIRD documented
  exception (alongside `EditorLayer.h`/`NullEditorLayer.cpp` and
  `EditorPanelRegistry.h`) to "everything under `src/Editor/` compiles only
  when `GTE_ENABLE_EDITOR` is ON" - it, and every method it declares, must be
  safely `#include`-able and callable with zero `#ifdef` at the call site,
  from ANY file in the engine, regardless of `GTE_ENABLE_EDITOR`'s value.
  **`LogLevel`/`LogEntry`/`LogQueryFilter`, and the free functions
  `ToString(LogLevel)`/`TryParseLogLevel(...)`, are declared AND DEFINED
  fully `inline`, unconditionally (outside any `#if`)** - this is a
  deliberate correction made in `PHASE3` after `PHASE1` originally declared
  `ToString`/`TryParseLogLevel` in the header but defined them out-of-line in
  `Logger.cpp`: since `Network/NetworkRoutes.cpp` (an ALWAYS-compiled
  translation unit, never gated by `GTE_ENABLE_EDITOR`) calls both
  UNCONDITIONALLY at the language level, and `Logger.cpp` is only compiled
  into the build when the switch is ON, a hypothetical `GTE_ENABLE_EDITOR=OFF`
  build would have failed to LINK `NetworkRoutes.cpp.obj` with an unresolved
  external symbol, even though neither function is ever actually reached at
  runtime in that configuration. Making both fully `inline` sidesteps this
  hazard entirely, for both configurations, with no `#ifdef` needed at any
  call site - a future contributor adding a new always-visible free
  function to this header that a genuinely always-compiled file (like
  `NetworkRoutes.cpp`) might call should default to the same `inline`
  treatment rather than an out-of-line `Logger.cpp` definition, unless that
  function's behavior genuinely differs by `GTE_ENABLE_EDITOR` (like
  `Logger` the class itself, which is correctly dual-defined per branch).
- **`GTE_LOG_DEBUG`/`INFO`/`WARNING`/`ERROR` are the ONE sanctioned way any
  call site anywhere in the engine talks to the Logger** - mirroring
  `GTE_PROFILE_SCOPE` being the sanctioned way to talk to `ScopeTimer`. They
  vanish to `((void)0)` (the `category`/`message` expressions are NEVER
  EVEN EVALUATED, not just discarded) when `GTE_ENABLE_EDITOR` is OFF - a
  full release runtime game build pays exactly zero cost for every
  `GTE_LOG_*` call site in the entire engine, including the cost of building
  the message string itself. Calling `gte::Logger::` methods directly
  (bypassing the macros) is reserved for this module's own tests and for
  `Network/NetworkRoutes.cpp`'s `Query()`/`Clear()` calls (which need the
  real return values, not a fire-and-forget log call).
- **No `printf`-style formatting.** A call site always passes an
  already-built `std::string` message (e.g.
  `GTE_LOG_WARNING("Renderer", "value=" + std::to_string(x))`), never a
  format string plus variadic arguments.
- **Four log levels, in ordinal order: `Debug`, `Info`, `Warning`, `Error`**
  (`LogLevel`, `Logger.h`) - used both for a "minimum level" HTTP filter
  (`min_level`) and the Editor Log panel's own per-level toggle checkboxes.
  `ToString(LogLevel)`/`TryParseLogLevel(text, LogLevel*)` are the
  canonical string round-trip (case-insensitive on parse; an unrecognized
  string leaves the output pointee untouched and returns `false`).
- **A fixed, `constexpr` 2000-entry ring buffer (`Logger::kCapacity`), FIFO
  eviction.** Once full, recording one more entry evicts the single oldest
  entry first. Not runtime-configurable in this campaign.
- **Every entry gets a monotonically increasing 64-bit `id`, assigned once
  at record time, under the SAME mutex that guards the ring buffer itself**
  (so "ascending id order" and "append order" can never disagree under
  concurrent callers) **and NEVER reset or reused by `Clear()`.** This is
  what makes a caller's `since_id` cursor meaningful even across a clear: a
  caller polling `GET /get_logs?since_id=<last seen id>` never has to worry
  about a stale cursor silently becoming ambiguous after someone else calls
  `POST /clear_logs`.
- **Every entry carries BOTH a frame number (`Logger::SetCurrentFrame()`'s
  most recent value) AND a wall-clock-style `timestampSeconds`** (seconds
  since THIS PROCESS's first ever `Logger::Log()` call - deliberately
  independent of `gte::Time`/`gte::EngineContext`, so a log call made before
  the very first rendered frame, e.g. during engine boot, still has a
  meaningful "when"). `Application::Run()` calls
  `Logger::SetCurrentFrame(m_engineContext.time.FrameCount())` once per real
  frame, unconditionally (no `#if GTE_ENABLE_EDITOR` guard needed at that
  call site either).
- **Every entry carries a free-text `category` string** (e.g. `"Renderer"`,
  `"Jobs"`, `"Network"`, `"Application"`), set by the caller at the
  `GTE_LOG_*` call site, filterable via `GET /get_logs?category=...` with an
  exact, case-sensitive match (the simplest possible semantics for v1 - no
  hierarchical/wildcard category matching).
- **`Logger::Query(const LogQueryFilter&)` applies every filter field as a
  logical AND**, returns matches in ASCENDING id order (oldest first) -
  EXCEPT that when `filter.limit > 0` and more than `limit` entries match,
  only the NEWEST `limit` of them are kept (still returned in ascending id
  order among themselves). This "keep the newest N, but return them
  oldest-first" shape bounds response size while still reading top-to-bottom
  in a Console-style UI/log the same way a human expects.
- **No persistent/on-disk log file, no WebSocket/streaming/"tail -f"-style
  live push, no structured/multi-field message payloads, no stack traces.**
  A log is lost the moment the process exits (or the buffer wraps past 2000
  entries); a caller wanting near-real-time updates polls `GET /get_logs`
  repeatedly using `since_id`; a log entry's `message` is one plain string,
  full stop. None of the existing, permanent, always-on
  `std::fprintf(stderr, ...)` call sites scattered through the engine
  (`Application.cpp`, `Jobs/JobContinuation.cpp`, `main.cpp`,
  `Network/NetworkServer.cpp`, `Renderer/RenderGraph/RenderGraphCompiler.cpp`,
  `Renderer/RenderGraph/RenderPipeline.h`,
  `Renderer/Vulkan/VulkanInstance.cpp`) were migrated to the Logger by this
  campaign, and none ever will be, by explicit project-owner decision - they
  keep working exactly as they do today, console-only, forever. This module
  is a NEW, parallel mechanism only new call sites use going forward.

## Editor "Log" panel

`src/Editor/LogPanelData.h/.cpp` (pure, ImGui-free: `LogPanelFilterState`,
`BuildLogQueryFilter()`, `FilterByEnabledLevels()`, `ColorForLevel()`,
`FormatLogEntryLine()`) plus `src/Editor/Panels/LogPanel.h/.cpp` (the actual
ImGui window) give the Editor a real, working, Unity-Console-style panel
docked alongside "Memory"/"Profiler"/"Render Graph"/"Atmosphere"/"Jobs"/
"Project" - registered in `EditorPanelRegistry`/`DockLayout.cpp` under the
exact panel name `"Log"` (so `GET /activate_tab?name=Log`/`GET /list_tabs`
already work with zero further Network-layer code). Four always-on-by-default
level checkboxes filter by an ARBITRARY subset (not a single min-level
threshold - deliberately different from `GET /get_logs`' own `min_level`
filter, since a plain ordinal threshold cannot express "Debug + Error but not
Info/Warning"), plus free-text category/keyword filters and an "Auto-scroll"
checkbox. Re-queries `Logger::Query()` fresh every frame (no incremental
`since_id` cursor needed at only up to 2000 entries). The "Clear" button calls
`Logger::Clear()` directly, no bridge - same justification as `POST
/clear_logs` below (this runs on the main thread anyway, exactly like every
other Editor panel's own direct engine calls).

## `GET /get_logs` / `POST /clear_logs`

`Network/NetworkRoutes.h`'s `ParseGetLogsQuery()`/`BuildGetLogsResponseJson()`/
`BuildClearLogsResponseJson()`, wired by `Network/NetworkServer.cpp`'s
`RegisterRoutes()`, expose the Logger over HTTP:

- **`GET /get_logs`** accepts `since_id` (strictly greater than, base-10
  non-negative integer), `min_level` (`"debug"`/`"info"`/`"warning"`/
  `"error"`, case-insensitive), `category` (exact, case-sensitive match),
  `keyword` (case-insensitive substring match on `message`), `frame_min`/
  `frame_max` (inclusive, base-10 non-negative integers), and `limit`
  (base-10 non-negative integer; defaults to `200` when omitted; SILENTLY
  CLAMPED to `Logger::kCapacity` - 2000 - when larger, rather than treated
  as an error, since asking for "too many" is harmless, unlike a genuinely
  malformed value). Every parameter is optional; an empty/absent value means
  "don't filter on this field" (mirroring `LogQueryFilter`'s own
  "empty/default means match everything" semantics). Responds `400`
  (`BuildGenericErrorResponseJson()`) for a malformed `since_id`/`min_level`/
  `category`/`keyword`/`frame_min`/`frame_max`/`limit` value, or `200` with
  `{"logging_enabled": true|false, "count": N, "latest_id": M,
  "entries": [{"id":..., "frame":..., "timestamp_seconds":..., "level":
  "Info", "category":"...", "message":"..."}, ...]}` otherwise -
  `logging_enabled` is `Logger::IsEnabled()` (always `false` in a
  `GTE_ENABLE_EDITOR=OFF` build, where `entries` is therefore always empty
  regardless of any filter) and `latest_id` is `Logger::LatestEntryId()`,
  useful for a caller establishing an initial `since_id` cursor ("start
  watching from now onward").
- **`POST /clear_logs`** empties the ring buffer and responds `200` with
  `{"success": true, "cleared_count": N}` (`N` is however many entries were
  in the buffer immediately before this call cleared it). Takes no request
  body/query parameters at all.
- **Both routes call `gte::Logger::Query(...)`/`gte::Logger::Clear()`
  DIRECTLY, with NO new cross-thread bridge - this is a DELIBERATE, NARROW
  EXCEPTION to `docs/conventions/networking.md`'s "a route handler reaches
  engine state only through a reviewed bridge" rule**, specific to Logger's
  own from-day-one thread-safe design (every public `Logger::` method is
  safe to call concurrently from ANY thread by construction - see this
  file's own section above). This must NEVER be read as permission for any
  OTHER future route to bypass the bridge rule for actual ECS/Renderer/Game
  state - those subsystems were never built with concurrent access in mind,
  unlike Logger, which was designed for it from `PHASE1` onward. A future
  contributor reaching for this precedent to justify a direct, bridge-free
  call into `Registry`/`Renderer`/`Game`/ImGui from a route handler is
  misapplying it.

Full campaign writeup: `task_manager/logger-1/PHASE0_MASTER_STRATEGY.md` and
each `PHASEn_COMPLETION_REPORT.md` in that same folder.
