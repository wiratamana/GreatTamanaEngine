# PHASE3 — Logging: Global `ILogSink` Extraction (New Gap Fix)

## Parent
`PHASE0_MASTER_STRATEGY.md` — see "Locked Design Decision #1" there.

## Step 1: The Goal

Today, `src/Jobs/JobContinuation.cpp`, `src/Network/NetworkServer.cpp`,
`src/Network/NetworkRoutes.h`, `src/Renderer/RenderGraph/RenderGraph.cpp`, and
`src/Renderer/RenderGraph/RenderPassGroupRegistry.cpp` — all files destined
for `gte_core` — directly `#include "../../Editor/Logger.h"` (or the
equivalent relative path) and either call `GTE_LOG_*` or use types that used
to live only in that header. `GTE_LOG_*` hard-resolves to the Editor-only
`::gte::Logger` class today. This must become zero direct dependency on
anything under `src/Editor/`. `GTE_LOG_*` must keep working, unconditionally,
from any thread, from any file, exactly as it does today — just routed
through a `gte_core`-owned indirection instead of a hard Editor include.

## Step 2: The Situation / The Problem

Read `src/Editor/Logger.h` in full first (already read once during strategy
planning — re-read it fresh, it may have changed). Key facts:

- `GTE_LOG_DEBUG/INFO/WARNING/ERROR` macros are defined in `Logger.h`,
  compiling to `((void)0)` when `GTE_ENABLE_EDITOR` is OFF, or to
  `::gte::Logger::Log(...)` when ON.
- `LogLevel`, `LogEntry`, `LogQueryFilter`, `ToString()`, `TryParseLogLevel()`
  are ALREADY unconditional/inline (a prior campaign, `logger-1`, already
  fixed a similar link hazard for `Network/NetworkRoutes.cpp` — read that
  file's own comment for the precedent of "make it inline, unconditional,
  no macro" reasoning).
- `Logger` the CLASS itself is genuinely dual-defined (`#if`/`#else`, real
  ring-buffer vs fully-inlined no-op) — its BEHAVIOR differs by
  configuration, unlike the plain data types above.
- Confirmed call sites needing this fix (re-confirmed directly against the
  real, current source during this strategy's own double-check pass, via
  `search_in_dir` for `Editor/Logger.h` across all of `src/` EXCLUDING
  `src/Editor/` itself — six files, not five; this strategy's own earlier
  inventory missed one):
  - `Jobs/JobContinuation.cpp` — `#include "../Editor/Logger.h"`, calls
    `GTE_LOG_*`.
  - `Network/NetworkServer.cpp` — `#include "../Editor/Logger.h"`, calls
    `GTE_LOG_*`.
  - `Network/NetworkRoutes.h` — **CONFIRMED, line 7 of the real file today**:
    `#include "../Editor/Logger.h"`. This file lives under `src/Network/`
    (destined for `gte_core`) and pulls in the ENTIRE `Editor/Logger.h`
    header (the `Logger` class, the macros, everything) purely to reach
    `ToString()`/`TryParseLogLevel()`, which are ALREADY unconditional/
    inline in that same header today. This file's own USE of those two
    functions needs zero reimplementation — only its `#include` line needs
    to change, to point at the new `Core/Logging.h` instead. Do not skip
    this file just because its function calls already "work" — the
    `#include` itself is the violation this phase exists to remove.
  - `Renderer/RenderGraph/RenderGraph.cpp` — `#include
    "../../Editor/Logger.h"`, calls `GTE_LOG_WARNING`.
  - `Renderer/RenderGraph/RenderPassGroupRegistry.cpp` — `#include
    "../../Editor/Logger.h"`, calls `GTE_LOG_WARNING`.
  - `Application/Application.cpp` — `#include "../Editor/Logger.h"` (this
    one moves to `EditorHost` later, Phase 16 — for now it still compiles as
    part of `gte_core`'s old, unconditional list, so it needs this fix too,
    today).
  Re-run this exact `search_in_dir` yourself at the start of this phase —
  code may have drifted since this strategy was written; treat the list
  above as a confirmed starting point, not an exhaustive guarantee.

## Step 3: The Plan

1. Create `src/Core/Logging.h` (new file, next to the already-existing
   `src/Core/Time.h`/`EngineContext.h`) containing, UNCONDITIONALLY (no
   `#if` at all):
   - `enum class LogLevel : std::uint8_t { Debug, Info, Warning, Error };`
   - `struct LogEntry { ... };` and `struct LogQueryFilter { ... };` (moved
     verbatim from `Logger.h`)
   - `inline const char* ToString(LogLevel) noexcept { ... }` and
     `inline bool TryParseLogLevel(...)` (moved verbatim)
   - The `GTE_LOG_DEBUG/INFO/WARNING/ERROR` macro DEFINITIONS — now
     UNCONDITIONAL, always expanding to a call into a new free function,
     e.g. `::gte::LogToActiveSink(LogLevel::Debug, category, message)`
     (never directly to `Logger::Log`).
2. Create `src/Core/LogSink.h` (new file) declaring:
   ```cpp
   namespace gte {
   class ILogSink {
   public:
       virtual ~ILogSink() = default;
       virtual void Log(LogLevel level, std::string_view category, std::string_view message) = 0;
   };

   // Install-once, idempotent — mirrors SdlMemoryTracker's own precedent
   // (AGENTS.md, "CPU Dependency Memory Tracking"). Safe to call from any
   // thread exactly once before the sink is first used; a second Install()
   // call with a different sink is a real, reachable misuse — decide
   // during implementation whether to assert or silently ignore, document
   // whichever is chosen.
   void InstallLogSink(ILogSink* sink);
   void LogToActiveSink(LogLevel level, std::string_view category, std::string_view message);
   } // namespace gte
   ```
   `LogToActiveSink()`'s body (in a new `src/Core/LogSink.cpp`) checks a
   global `ILogSink*` (relaxed atomic, matching `Logger`'s own existing
   concurrency approach) — if null, it is a true no-op (message/category
   NEVER evaluated beyond what the macro itself already guarantees at the
   call site — actually re-check: since this is now a real function call,
   not a compile-time-erased macro, the `category`/`message` expressions
   ARE evaluated even when no sink is installed. This is a real, accepted
   BEHAVIOR CHANGE from today's "release build, arguments not even
   evaluated" property. Flag this explicitly in the completion report and,
   if it feels risky, ask the user via `ask_questions` whether an
   always-cheap sink-installed check should be inlined at the macro
   call-site itself (e.g. `if (::gte::IsLogSinkInstalled()) { ... }` inside
   the macro expansion) to preserve the original zero-cost-when-disabled
   property. Prefer implementing that inline check proactively rather than
   silently accepting a behavior change, since `AGENTS.md`'s own Logging
   section explicitly documents "a full release runtime game build pays
   EXACTLY zero cost... including the cost of building the message string
   itself" as a load-bearing property.
3. Update `src/Editor/Logger.h`/`.cpp`: `Logger` class now IMPLEMENTS
   `ILogSink` (add `void Log(LogLevel, std::string_view, std::string_view) override`
   forwarding to its existing static `Log()` logic, or refactor `Logger`
   into a real singleton instance implementing `ILogSink` — pick whichever
   is the smaller diff against the existing ring-buffer implementation).
   Delete the now-redundant macro definitions and the now-redundant
   `LogLevel`/`LogEntry`/`LogQueryFilter`/`ToString`/`TryParseLogLevel`
   definitions from `Logger.h` — `#include "../Core/Logging.h"` instead.
4. Update all 6 call-site files listed in Step 2 above: replace
   `#include "../../Editor/Logger.h"` (or equivalent relative path) with
   `#include "../Core/Logging.h"` (adjust relative path per file's real
   location — confirm via `read_file` on each, do not guess the exact
   `../` depth). This explicitly includes `Network/NetworkRoutes.h` — its
   `ToString()`/`TryParseLogLevel()` call sites need no logic change at
   all, only the `#include` line itself.
5. Somewhere in `gte_editor`'s startup path (for now, since `EditorHost`
   doesn't exist until Phase 15, add this call directly in
   `Application`'s constructor or early `Run()` body — Phase 16 will move
   it into `EditorHost` alongside the other composition-root wiring): call
   `gte::InstallLogSink(&Logger::Instance())` (or equivalent) once, exactly
   the same "before first use" timing rule `SdlMemoryTracker`'s own
   `Install()` already documents.
6. Confirm via `search_in_dir` for `"Editor/Logger.h"` (the include string)
   across all of `src/` EXCLUDING `src/Editor/` itself — zero results
   expected after this phase.
7. Compile-check: incremental build. Live smoke check: `run_app_background`,
   trigger a few log-producing actions (e.g. `POST /instantiate_primitive`,
   which should produce Network/Engine log lines), pull `GET /get_logs` and
   confirm entries still show up in the Editor's Log panel data exactly as
   before, with correct category/level/message.

## Files Touched

- NEW `src/Core/Logging.h`, NEW `src/Core/LogSink.h`, NEW `src/Core/LogSink.cpp`
- `src/Editor/Logger.h`/`.cpp` (shrink, implement `ILogSink`)
- `src/Jobs/JobContinuation.cpp`, `src/Network/NetworkServer.cpp`,
  `src/Network/NetworkRoutes.h`,
  `src/Renderer/RenderGraph/RenderGraph.cpp`,
  `src/Renderer/RenderGraph/RenderPassGroupRegistry.cpp`,
  `src/Application/Application.cpp` (include path fix only)
- `CMakeLists.txt` (add the 3 new `src/Core/` files to `gte_core`'s
  unconditional source list)

## Definition of Done

- Zero file outside `src/Editor/` includes `Editor/Logger.h` — including
  `Network/NetworkRoutes.h`, confirmed by name.
- `GTE_LOG_*` still works end-to-end, confirmed live via `/get_logs`.
- The "zero cost when no sink installed" property is preserved or the
  deviation is explicitly flagged and resolved via `ask_questions`.
- `PHASE3_COMPLETION_REPORT.md` written + git commit.

## Out of Scope

Do not touch `Network/NetworkRoutes.cpp`'s actual LOGIC — its
`ToString()`/`TryParseLogLevel()` usage already works today via the
already-inline functions; only `NetworkRoutes.H`'s `#include` line needs to
change (Step 4 above), never its behavior. Confirm (do not reimplement) that
`NetworkRoutes.cpp` still compiles once those two functions physically move
to `Core/Logging.h`.
