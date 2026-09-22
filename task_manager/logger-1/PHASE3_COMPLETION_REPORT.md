# PHASE3 — Network Endpoints: `GET /get_logs` and `POST /clear_logs` — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. Phase document:
`PHASE3_NETWORK_ENDPOINTS_GET_LOGS_AND_CLEAR_LOGS.md`.

## Summary

Landed `GET /get_logs`/`POST /clear_logs` exactly per this phase's Step 3
plan, including the pre-delegation correction called out for this run:
`ParsedGetLogsQuery::valid` defaults to `false` (matching this file's own
`ParsedActivateTabQuery` convention), and `ParseGetLogsQuery()`'s
implementation explicitly sets `result.valid = true;` as its very last
statement, only once every one of the six parameters has been checked and
none of them failed - read directly from the phase document's own corrected
Step 3.1/3.2 text, not any cached understanding.

### 3.1 `src/Network/NetworkRoutes.h`

- Added `#include "../Editor/Logger.h"` alongside the existing
  `#include "../Editor/EditorPanelCatalog.h"`.
- Added, appended as a new `--- task_manager/logger-1 campaign, PHASE3`
  section right before the closing `} // namespace gte::Network` (following
  this file's own established "most recent campaign appended at the end"
  convention):
  - `ParsedGetLogsQuery` (`valid = false` default, `errorMessage`, `filter`)
  - `ParseGetLogsQuery(sinceIdParam, minLevelParam, categoryParam,
    keywordParam, frameMinParam, frameMaxParam, limitParam)`
  - `BuildGetLogsResponseJson(entries, loggingEnabled, latestId)`
  - `BuildClearLogsResponseJson(clearedCount)`
  - A doc-comment block explicitly documenting the "no bridge needed"
    exception (Locked Design Decision #14) right where the new declarations
    start, cross-referencing `AGENTS.md` ("Logging") and
    `docs/conventions/logging.md`/`docs/conventions/networking.md`.

### 3.2 `src/Network/NetworkRoutes.cpp`

- Added a new, dedicated `TryParseNonNegativeUInt64()` helper (local
  anonymous namespace) rather than reusing the existing `TryParseWholeInt()`
  helper verbatim: that existing helper deliberately accepts a leading `-`
  (needed for `/frame_debugger/select_event`'s own "-1 means deselect"
  semantics), which is wrong for `since_id`/`frame_min`/`frame_max`/`limit` -
  every one of those four must reject a negative value outright, per this
  phase document's own Step 3.2 validation rules. This is a genuine, small,
  deliberate divergence from "reuse verbatim if one already exists" - the
  existing helper's own *behavior* (not just its name) doesn't match what
  this phase needs, so a new, correctly-scoped helper was written instead of
  silently accepting `"-5"` as a valid `since_id`.
- `ParseGetLogsQuery(...)`: parses all six fields exactly per the phase
  document's validation table (empty -> default/unset; malformed -> `valid
  = false` with a per-field, specific `errorMessage`; `limit` empty ->
  `200`, non-empty and too large -> silently clamped to `Logger::kCapacity`
  rather than rejected). `category`/`keyword` are copied through verbatim
  (always valid). `result.valid = true;` is set explicitly as the very last
  statement, per this phase's corrected Step 3.1/3.2.
- `BuildGetLogsResponseJson(...)`/`BuildClearLogsResponseJson(...)`: built
  via `nlohmann::json`, matching every other `Build*ResponseJson()` in this
  file exactly (see `BuildListTabsResponseJson()`'s own style).

### 3.3 `src/Network/NetworkServer.cpp` wiring

Added `server.Get("/get_logs", ...)` and `server.Post("/clear_logs", ...)`
inside the existing `RegisterRoutes(...)` function, mirroring `GET
/list_tabs`'s own "needs no bridge at all" shape exactly - no new parameter
on `RegisterRoutes()`/`NetworkServer`'s constructor, no new bridge class.
`/get_logs` parses the request, returns `400`
(`BuildGenericErrorResponseJson`) on a bad parameter, otherwise calls
`Logger::Query(parsed.filter)` directly and responds `200` with
`BuildGetLogsResponseJson(entries, Logger::IsEnabled(),
Logger::LatestEntryId())`. `/clear_logs` reads `Logger::EntryCount()` first
(the "before" count), calls `Logger::Clear()`, and responds `200` with
`BuildClearLogsResponseJson(clearedCount)`. Neither route has an
`alreadyPending`/`timedOut`/`503`/`504` code path at all - there is no
bridge to be pending/time out on, per Locked Design Decision #14. (Note:
`NetworkServer.cpp` already had `#include "../Editor/Logger.h"` and two
`GTE_LOG_*` call sites from `PHASE2` - no new include was needed here.)

### 3.4 A real, confirmed link hazard fixed (not part of the original plan text, but explicitly flagged by `PHASE1` for this phase to check)

`PHASE1_COMPLETION_REPORT.md`'s own "worth flagging" note called out that
`ToString(LogLevel)`/`TryParseLogLevel(...)` were DECLARED unconditionally in
`Logger.h` but DEFINED out-of-line in `Logger.cpp` - a file only added to the
build inside CMake's `if(GTE_ENABLE_EDITOR)` block. Since this phase's own
`Network/NetworkRoutes.cpp` (an ALWAYS-compiled translation unit) now calls
both functions UNCONDITIONALLY (`ParseGetLogsQuery()` calls
`TryParseLogLevel()`; `BuildGetLogsResponseJson()` calls `ToString()`), a
hypothetical `GTE_ENABLE_EDITOR=OFF` build would have failed to LINK
`NetworkRoutes.cpp.obj` with an unresolved external symbol - exactly the
hazard PHASE1 asked this phase to check for. Confirmed real (by inspection of
`CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` gate around `Logger.cpp`) and
fixed at the root: both functions are now fully `inline`, defined directly in
`Logger.h`, unconditionally (outside any `#if`) - `Logger.cpp` no longer
defines either, with a comment pointing back at `Logger.h` for the full
rationale. This is the one deviation from `PHASE1`'s original shipped code in
this phase, made because `PHASE1` itself asked for exactly this check. No
`ask_questions` call was needed - the fix is a straightforward, unambiguous
correction of a self-flagged defect, not a new design choice. (A full
`GTE_ENABLE_EDITOR=OFF` build was NOT run to physically reproduce the
original link failure, per this phase's own "no full build" working
agreement - the fix was derived from directly reading `Logger.cpp`/
`CMakeLists.txt`'s real gating, which unambiguously confirms the hazard
without needing to reproduce it.)

### 3.5 Documentation

- `AGENTS.md`: added a new `## Logging` section immediately AFTER the
  existing `## Networking` section (the first of the two options the phase
  document offered), and extended `## Networking`'s own summary sentence to
  mention `GET /get_logs`/`POST /clear_logs` and the bridge-exception
  cross-reference.
- `docs/conventions/logging.md` (new file): full write-up covering the
  dual-branch header shape (and the `inline` fix from 3.4 above), the four
  log levels, the ring buffer/FIFO eviction, the monotonic never-reset `id`
  and `since_id` polling model, the full `GET /get_logs`/`POST /clear_logs`
  contract, and a clearly labeled paragraph on the bridge-exception (Locked
  Design Decision #14) with an explicit warning against misreading it as a
  precedent for any other route.
- `docs/conventions/networking.md`: added a new bullet (chronologically
  after the `scene-serialization-2` bullet, before the "Named Texture
  Capture" `##` subsection) describing the two new routes and cross-linking
  to `logging.md` rather than duplicating its detail.
- `docs/README.md`: added a `- **[Logging](conventions/logging.md)** — ...`
  bullet to the `## Conventions` list, immediately after the existing
  `Networking` bullet.

### 3.6 Tests: `tests/Network/NetworkRoutesTests.cpp`

Added as new `TEST(...)` blocks in the existing file (no new file), split
exactly per this phase's Step 3.5:

- **Always-compiled** (10 `ParseGetLogsQueryTests` cases: all-empty
  defaults, every field populated, case-insensitive `min_level`, each of
  `since_id`/`min_level`/`frame_min`/`frame_max`/`limit`'s own malformed-input
  rejection, limit-clamped-not-rejected, category/keyword always valid; 2
  `BuildGetLogsResponseJsonTests` cases; 1 `BuildClearLogsResponseJsonTests`
  case) - none of these touch live `Logger::` state, all given hand-built
  `std::vector<LogEntry>` values, so they produce identical, checkable output
  in EITHER `GTE_ENABLE_EDITOR` configuration.
- **`#if GTE_ENABLE_EDITOR`-guarded** (3 `GetLogsEndToEndTests` + 1
  `ClearLogsEndToEndTests` case): real `GTE_LOG_*`/`Logger::Query()`/
  `Logger::Clear()`/`Logger::EntryCount()` calls exercising
  `ParseGetLogsQuery()` + `Logger::Query()` + `BuildGetLogsResponseJson()`
  together end-to-end (basic round-trip, category filter, `since_id`
  cursor) and a `POST /clear_logs`-shaped clear-and-report-previous-count
  case. Every case calls `Logger::Clear()` first (test isolation, same as
  `PHASE1`'s own tests) and again at the end where a later test in the same
  binary run could otherwise see leftover entries.

## Deviations from the plan

**One, and it was explicitly anticipated by `PHASE1`'s own completion
report, not a new design decision**: `ToString(LogLevel)`/
`TryParseLogLevel(...)` moved from "declared in `Logger.h`, defined in
`Logger.cpp`" to "declared AND defined, `inline`, directly in `Logger.h`,
unconditionally" - see Section 3.4 above for the full rationale. This was a
correctness fix for a real, confirmed latent link hazard in an
untested-this-phase (`GTE_ENABLE_EDITOR=OFF`) configuration, not a scope
change to this phase's own endpoint contract - `GET /get_logs`/
`POST /clear_logs`'s request/response shape, status codes, and validation
rules are exactly what the phase document specifies, with no deviation.

No `ask_questions` call was needed: every genuine ambiguity this phase might
have hit was already resolved by `PHASE0`'s Locked Design Decisions and this
phase document's own corrected Step 3.1/3.2 text (the `valid` default
change called out explicitly in this delegation's own instructions was
followed as given).

## Verification

- Fast, targeted incremental compile check (default `GTE_ENABLE_EDITOR=ON`
  configuration):
  - `cmake --build build --target gte_core` — succeeded cleanly (6/6 steps:
    `Logger.cpp.obj`, `NetworkRoutes.cpp.obj`, `NetworkServer.cpp.obj`,
    `Application.cpp.obj`, `JobContinuation.cpp.obj` all recompiled;
    `libgte_core.a` relinked).
  - `cmake --build build --target GreatTamanaEngineTests` — succeeded
    (`Editor/LoggerTests.cpp.obj`, `Network/NetworkRoutesTests.cpp.obj`, and
    the other two `Network/NetworkRoutes*Tests.cpp.obj` files all recompiled
    cleanly; executable relinked).
- `GreatTamanaEngineTests.exe --gtest_filter=*GetLogs*:*ClearLogs*` —
  **17/17 new tests passed** (0 failures, ~2ms total).
- `GreatTamanaEngineTests.exe --gtest_filter=*Logger*:*NetworkRoutes*` —
  **33/33 tests passed** (all 12 pre-existing `PHASE1` `LoggerTest` cases
  plus every pre-existing `NetworkRoutesTests`/parameterized suite still
  green after the `Logger.h`/`.cpp` `inline` refactor - confirms no
  regression from Section 3.4's fix).
- No full build and no full `ctest` regression run were performed, per this
  campaign's own working agreement (only `PHASE5` runs those).

## Next phase

`PHASE4_EDITOR_LOG_PANEL_UI.md` can proceed: `GET /get_logs`/
`POST /clear_logs` are live, tested, and documented; `Logger::Query()`/
`Clear()`/`EntryCount()`/`LatestEntryId()`/`IsEnabled()` are all exercised
end-to-end already, so `PHASE4`'s `LogPanelData.h`/`.cpp` can lean on the
exact same `Logger::` API surface with no further changes expected to its
shape. One thing worth carrying forward: `ToString(LogLevel)` is now safely
callable from an Editor panel file (`GTE_ENABLE_EDITOR`-only code anyway)
exactly as before - the `inline` move only widened where it's SAFE to call
from, it did not change its signature, behavior, or namespace.
