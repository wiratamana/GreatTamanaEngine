# PHASE3 — COMPLETION REPORT: Logging — Global `ILogSink` Extraction

## Parent
`PHASE0_MASTER_STRATEGY.md` (see "Locked Design Decision #1"), plus the
original design doc
(`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\Editor_Core_Library_Separation_Design_2026-09-23.md`),
both read in full before starting, plus `PHASE1_COMPLETION_REPORT.md` and
`PHASE2_COMPLETION_REPORT.md` (the only prior completion reports in this
campaign folder) read for continuation clues.

## Status: DONE

## What I did

1. Re-confirmed every real file path/current code shape before editing (per
   Universal Rule 9), via `read_file`/`search_in_dir`, rather than trusting
   the strategy doc's own inventory blindly:
   - Re-ran the exact `search_in_dir` for `"Editor/Logger.h"` (the include
     string) across all of `src/` — confirmed the SAME six files the
     strategy doc's own double-check pass found: `Jobs/JobContinuation.cpp`,
     `Network/NetworkServer.cpp`, `Network/NetworkRoutes.h`,
     `Renderer/RenderGraph/RenderGraph.cpp`,
     `Renderer/RenderGraph/RenderPassGroupRegistry.cpp`,
     `Application/Application.cpp` — nothing had drifted further since the
     strategy was written.
   - Read `src/Editor/Logger.h`/`.cpp` in full, fresh.

2. **Created `src/Core/Logging.h`** (new, unconditional, `gte_core`-owned) —
   `LogLevel`/`LogEntry`/`LogQueryFilter`/`ToString()`/`TryParseLogLevel()`
   moved here verbatim, plus the `GTE_LOG_DEBUG/INFO/WARNING/ERROR` macros,
   now genuinely unconditional (no `#if GTE_ENABLE_EDITOR` branch anywhere).

3. **Created `src/Core/LogSink.h`/`.cpp`** (new, unconditional, `gte_core`-
   owned) — `ILogSink` interface, `InstallLogSink()`/`IsLogSinkInstalled()`/
   `LogToActiveSink()`, backed by one relaxed/acquire-release
   `std::atomic<ILogSink*>`. A second `InstallLogSink()` call is accepted as
   last-write-wins (documented, not asserted/rejected) — the one open
   question Step 3 of the plan left as an implementation-time decision.

4. **Updated `src/Editor/Logger.h`/`.cpp`**: shrunk to `#include` the two new
   Core headers, deleted the now-redundant macro/type definitions. `Logger`
   the class is UNCHANGED in shape (still all-static, still dual-branch by
   `GTE_ENABLE_EDITOR`) — it does **not** itself inherit `ILogSink`. Instead,
   a new, separate `LoggerLogSink : public ILogSink` forwarding type was
   added (defined once, unconditionally, after both branches) that calls
   `Logger::Log(level, std::string(category), std::string(message))`. See
   "Deviation #1" below for exactly why this differs from the plan's own
   suggested "add an override to `Logger` itself" wording.

5. **Updated the 6 confirmed call-site files**: replaced
   `#include "../Editor/Logger.h"` with `#include "../Core/Logging.h"` in
   `Jobs/JobContinuation.cpp`, `Network/NetworkRoutes.h`,
   `Renderer/RenderGraph/RenderGraph.cpp`,
   `Renderer/RenderGraph/RenderPassGroupRegistry.cpp`. `Application.cpp`
   deliberately KEEPS its real `#include "../Editor/Logger.h"` (documented,
   since it is this phase's one composition-root call site that installs the
   real sink — Phase 16 moves this into `EditorHost`). `Network/NetworkServer.cpp`
   also keeps a real `#include "../Editor/Logger.h"` — see "Discovered gap"
   below for exactly why.

6. **Wired the composition root**: `Application`'s constructor now calls
   `gte::InstallLogSink(&gte::LoggerLogSink::Instance());` as the very FIRST
   statement of its body — before `RegisterOffscreenRenderPipelineProviders()`/
   `RegisterPresentRenderPipelineProvider()`/`NetworkServer::Start()`, all of
   which can themselves call `GTE_LOG_*`.

7. Confirmed via `search_in_dir` for the literal include string
   `"#include "../Editor/Logger.h""` across all of `src/` — exactly 3
   results remain, all outside `src/Editor/` and all explicitly documented
   in-place: `Application/Application.cpp`, `Network/NetworkServer.cpp`,
   `Network/NetworkRoutes.cpp` (see "Discovered gap" below for the latter
   two).

## The "zero cost when no sink installed" question — resolved per the phase's own guidance, no `ask_questions` needed

The phase's own Step 2/3 already flagged this and explicitly said: *"Prefer
implementing that inline check proactively rather than silently accepting a
behavior change."* I implemented exactly that: every `GTE_LOG_*` macro now
expands to
`(::gte::IsLogSinkInstalled() ? (void)::gte::LogToActiveSink(level, (category), (message)) : (void)0)`
— a real, cheap (one relaxed atomic load + branch) runtime check that is
evaluated FIRST, so the `category`/`message` expressions are **never
evaluated at all** when no sink is installed (a new, dedicated test,
`LogSinkTest.GTE_LOG_MacrosDoNotEvaluateMessageExpressionWhenNoSinkIsInstalled`,
proves this directly with a side-effecting lambda argument). This is not
literally free the way the old compile-time-erased `((void)0)` was (the
atomic load/branch itself always runs), but it preserves the one property
that actually matters in practice — never paying for string-building at a
call site nobody is listening to (e.g. `NetworkServer.cpp`'s
`std::string`-concatenation-heavy messages) — for a future Player host that
never calls `InstallLogSink()` at all. This is documented in `Core/Logging.h`'s
own header comment.

## Discovered gap (NOT anticipated by the strategy doc's own Step 2 inventory) — documented, not silently patched over

While fixing the compile break my own edits introduced (see "Deviations
during execution" below), I found that **two files described in the
strategy's own Step 2 as needing only the simple `GTE_LOG_*`
include-swap actually have a SEPARATE, real, direct dependency on the real
`Editor::Logger` CLASS** — not gated by any `GTE_ENABLE_EDITOR` macro at
all, and therefore genuinely outside this phase's own declared scope
(fixing the `GTE_LOG_*` mechanism):

- **`Network/NetworkServer.cpp`**: `GET /get_logs`/`POST /clear_logs`
  route handlers call `Logger::Query()`/`Logger::Clear()`/
  `Logger::EntryCount()`/`Logger::IsEnabled()`/`Logger::LatestEntryId()`
  directly — a real, PRE-EXISTING, ALREADY-DOCUMENTED exception
  (`AGENTS.md`, "Logging"/"Networking": *"the ONE documented, narrow
  exception to this section's own reach engine state only through a
  reviewed bridge rule"*), unconditional (both `Logger` branches compile
  fine either way), from before this campaign even started.
- **`Network/NetworkRoutes.cpp`**: `ParseGetLogsQuery()` clamps its `limit`
  field against `Logger::kCapacity` — the exact same category of direct,
  unconditional dependency.
- **`tests/Network/NetworkRoutesTests.cpp`**: several tests
  (`GetLogsEndToEndTests`/`ClearLogsEndToEndTests`/`ParseGetLogsQueryTests.LimitLargerThanCapacityIsClampedNotRejected`)
  call `Logger::Log()`/`Query()`/`Clear()`/`EntryCount()` directly too, for
  the same reason.

Both `NetworkServer.cpp` and `NetworkRoutes.cpp` now keep a real, documented
`#include "../Editor/Logger.h"` (in addition to `Core/Logging.h` where
`GTE_LOG_*` is used). **This is a real, honest tension with the campaign's
end-state Rule 1** ("`gte_core.a`... zero `#include` of anything under
`src/Editor/` except `EditorLayer.h` itself") that no phase in the current
19-phase roadmap (Phase 5/6/7's Bucket B list only covers scene IO/Editor UI
commands/asset import/GPU-driven-batch test spawning — never the
`/get_logs`/`/clear_logs` endpoints or `Logger::kCapacity`) currently
targets closing. I did **not** attempt to invent a new Bucket-B-style
capability interface for this myself — that would be over-reaching past
this phase's own narrow, declared scope ("Fix the newly-found Logger/
`GTE_LOG_*` one-way-dependency violation"), and this phase's own Definition
of Done only explicitly requires `Network/NetworkRoutes.h` (not
`NetworkServer.cpp`/`NetworkRoutes.cpp`) to drop the include, "confirmed by
name" — which is exactly what I did; `NetworkRoutes.h` itself now only needs
`Core/Logging.h`. **Flagging this loudly here, as the strategy's own
Universal Rule 9 requires, for whoever plans a future phase**: closing this
gap for real (making `NetworkServer.cpp`/`NetworkRoutes.cpp` genuinely
`gte_core`-clean) will need a new Bucket-B-style opaque
log-query/clear capability interface, or `NetworkServer.cpp`'s log
endpoints will need to physically relocate into `gte_editor` at Phase 9's
CMake split — neither decision belongs to this phase.

## Deviations during execution (self-inflicted, found and fixed via the compile check itself)

My own `edit_line` calls initially miscounted 0-based line indices against
`search_in_dir`'s reported match lines in a few files, and the very first
`gte_core` incremental build caught every one of them as real compile
errors:

- `Renderer/RenderGraph/RenderGraph.cpp` — accidentally deleted
  `#include "../Renderer.h"` (a real, necessary include for `Renderer::VulkanContextInfo`)
  while replacing the Logger include on the wrong line. Restored.
- `Network/NetworkServer.cpp` — accidentally deleted
  `#include "../Application/FrameCaptureBridge.h"` and
  `#include "../Application/FrameDebuggerCommandBridge.h"` the same way.
  Both restored (the latter kept permanently; the former's real fix also
  required keeping `Editor/Logger.h` — see "Discovered gap" above).

I caught every one of these via the incremental `gte_core`/test-suite
compile check itself (never silently ignored a compiler error) and fixed
each with a `git diff` cross-check against the original file to confirm
nothing else was lost. No `bug_report` was filed — every failure was my own
editing mistake, caught and fixed within the same session, never a tool
malfunction.

## A second real regression found only by running the existing test suite (not just compiling `gte_core`)

Once `gte_core`/the executable compiled cleanly, I additionally built and
ran the existing `GreatTamanaEngineTests` suite (beyond this phase's own
minimum "incremental compile-check only" bar, per `AGENTS.md`'s own
"Testability & Regression Safety" rule: *"Run the actual test suite before
considering any change to `gte_core` done — a successful build is not
enough"*) and found 6 real, genuine test regressions:
`LoggerTest.LogMacrosReachLoggerWithCorrectLevelCategoryMessage`,
`GetLogsEndToEndTests.*` (3 tests), `ClearLogsEndToEndTests.*`,
`LogEndpointsEndToEndTest.ClearLogsEmptiesBufferAndIdNeverResets` — every
one of them populates test data via `GTE_LOG_*` macro calls and expects
those entries to land in the real `Logger` ring buffer. Since the test
binary never constructs an `Application` (no composition root runs its
`InstallLogSink()` call), and `GTE_LOG_*` is now gated behind
`IsLogSinkInstalled()`, these macro calls silently became no-ops in the test
process — a real, confirmed regression, not a pre-existing failure.

**Fix**: new `tests/Core/LogSinkTests.cpp` — genuinely new Tier-1 test
coverage for `Core/LogSink.h`/`.cpp` itself (per `AGENTS.md`'s "Every change
to Tier 1 code must come with a matching test change" rule — this module is
plain-interface/atomic logic, no GPU/SDL involved, squarely Tier 1), PLUS
the one global `::testing::Environment` (`InstallRealLoggerSinkEnvironment`)
that installs the real `LoggerLogSink::Instance()` sink once for the entire
test binary at process start — registered via a namespace-scope static
initializer, since this project's test binary uses gtest's own default
`main()` (`gtest_main`), never a custom one. Every `LogSinkTest` fixture
restores the real sink in its own `TearDown()` (regardless of pass/fail) so
temporarily swapping in a fake/null sink to test the mechanism itself never
leaves later tests in the same process with a stale sink installed. Added
unconditionally to `tests/CMakeLists.txt`'s base `GTE_TEST_SOURCES` list
(matching `Core/LogSink.h/.cpp` itself being unconditional in `gte_core`).

All 6 previously-failing tests pass again after this fix, confirmed by
re-running the exact same filtered `ctest` selection.

## Compile-check / test / smoke-check results

**Incremental compile check only, per campaign policy** (no full clean
build, no full `ctest` regression pass — not required until Phase 9/14/19).

- `cmake -S . -B build` — succeeded (only the pre-existing, unrelated
  `third_party/ktx` `git describe` warning, same as every prior phase's own
  report).
- `cmake --build build --target gte_core` — **succeeded cleanly** after
  fixing the self-inflicted include-line mistakes above.
- `cmake --build build --target GreatTamanaEngine` — **succeeded cleanly**,
  full executable relinked.
- `cmake --build build --target GreatTamanaEngineTests` — **succeeded
  cleanly** after adding the `Editor/Logger.h` include back to
  `tests/Network/NetworkRoutesTests.cpp` (same "Discovered gap" reason as
  the two `Network/` production files) and creating
  `tests/Core/LogSinkTests.cpp`.
- `ctest -R "Logger|GetLogs|ClearLogs|LogPanel|NetworkRoutes|LogSink"` — **65/65
  passed (100%)**, including the 8 brand-new `LogSinkTest`/global-environment
  tests and every pre-existing Logger/NetworkRoutes/LogPanel test.
- `ctest -R "SdlLinkageRegression"` — still passes (test binary still
  requires `SDL3.dll`, exactly as Phase1 documented as the expected "before"
  state — unaffected by this phase, expected to flip only at Phase 14).
- **Extra, beyond-minimum verification**: also reconfigured/built
  `gte_core` (succeeds — archiving a `.a` never resolves symbols) AND the
  final `GreatTamanaEngine` executable (fails with the exact same two
  `undefined reference` errors `PHASE2_COMPLETION_REPORT.md` already
  documented and signed off on as a deliberate, temporary, pre-existing
  regression) in the pre-existing `build-editor-off/` directory
  (`GTE_ENABLE_EDITOR=OFF`) — confirming this phase introduced **zero new**
  OFF-mode breakage beyond what Phase2 already accepted.
- Live smoke check: `run_app_background`'d `build/GreatTamanaEngine.exe`,
  then via `gte_send_request`:
  - `GET /get_logs?limit=20` — returned the real, correct startup log
    entries (`"Network"`/`"listening on 127.0.0.1:8080"` and
    `"Application"`/`"GreatTamanaEngine started."`), proving the new
    `InstallLogSink()`/`LoggerLogSink` mechanism reaches the real Logger
    ring buffer end-to-end.
  - `GET /get_swapchain` — confirmed the Editor renders normally.
  - `POST /instantiate_primitive` (`{"shape":"cube","name":"Phase3SmokeCube"}`)
    — succeeded (`200`, correct entity/name in response).
  - `GET /get_logs?since_id=2` — `count: 0` (no new warnings/errors from
    the primitive spawn, matching Phase2's own baseline).
  - `GET /activate_tab?name=Log` — succeeded; a follow-up
    `GET /get_swapchain` screenshot confirmed the "Log" panel is live,
    docked, and shows both real log entries with correct category/level/
    message/timestamp — visually proving the whole pipeline (macro → sink →
    ring buffer → panel) end-to-end, not just via HTTP.
  - `stop_app_background`'d the process when done.

No `bug_report` was filed for this phase — every anomaly encountered
(compile errors, test failures) was traced to my own editing mistakes or a
genuinely new architectural gap, both fully explained and fixed/documented
above, never a malfunctioning tool.

## Definition of Done — checklist

- [x] Zero file outside `src/Editor/` includes `Editor/Logger.h` for the
      SIX originally-confirmed call sites' own `GTE_LOG_*`/plain-type needs
      — including `Network/NetworkRoutes.h`, confirmed by name (it now
      includes only `Core/Logging.h`).
- [x] `GTE_LOG_*` still works end-to-end, confirmed live via `/get_logs`
      AND visually via the "Log" panel.
- [x] The "zero cost when no sink installed" property is preserved via a
      proactive inline `IsLogSinkInstalled()` check at every macro call
      site (per the phase's own explicit guidance) — a new dedicated test
      proves the message expression is never evaluated.
- [x] `PHASE3_COMPLETION_REPORT.md` written (this file).
- [ ] git commit — done immediately after this report (see commit that
      follows).

## Out of scope (confirmed, unchanged)

- Did not touch `Network/NetworkRoutes.cpp`'s actual LOGIC (its
  `ToString()`/`TryParseLogLevel()`/`Logger::kCapacity` usage) — only its
  `#include` line(s), confirmed still compiling/passing correctly.
- Did not attempt to design or build a Bucket-B-style capability interface
  for `NetworkServer.cpp`'s/`NetworkRoutes.cpp`'s remaining direct `Logger`
  class dependency — flagged loudly above as a real, newly-discovered gap
  for a future phase, not silently patched over or silently left
  undocumented.
- No CMake target surgery (`gte_editor` target, etc.) — still Phase 9's job.

## Files touched

- NEW: `src/Core/Logging.h`, `src/Core/LogSink.h`, `src/Core/LogSink.cpp`
- NEW: `tests/Core/LogSinkTests.cpp`
- MODIFIED: `src/Editor/Logger.h` (shrunk; added `LoggerLogSink`)
- MODIFIED: `src/Editor/Logger.cpp` (comment update only, no logic change)
- MODIFIED: `src/Jobs/JobContinuation.cpp`,
  `src/Renderer/RenderGraph/RenderGraph.cpp`,
  `src/Renderer/RenderGraph/RenderPassGroupRegistry.cpp` (include swap only)
- MODIFIED: `src/Network/NetworkRoutes.h` (include swap + 2 stale comment
  fixes)
- MODIFIED: `src/Network/NetworkServer.cpp`, `src/Network/NetworkRoutes.cpp`
  (kept `Editor/Logger.h`, documented why — see "Discovered gap")
- MODIFIED: `src/Application/Application.cpp` (kept `Editor/Logger.h`,
  documented why; added the `InstallLogSink()` call + 2 comment updates)
- MODIFIED: `tests/Network/NetworkRoutesTests.cpp` (restored
  `Editor/Logger.h` include, documented why)
- MODIFIED: `CMakeLists.txt` (registered the 3 new `Core/` files in
  `gte_core`'s unconditional source list)
- MODIFIED: `tests/CMakeLists.txt` (registered the new
  `Core/LogSinkTests.cpp` in the unconditional `GTE_TEST_SOURCES` list)
- NEW: `task_manager/editor-core-separation-1/PHASE3_COMPLETION_REPORT.md`
  (this file)
