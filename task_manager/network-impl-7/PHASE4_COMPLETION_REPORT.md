# PHASE4 Completion Report — HTTP Endpoints: `GET /activate_tab` and `GET /list_tabs`

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Executed exactly as specified in
`PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md`, having first read
`PHASE1_COMPLETION_REPORT.md`, `PHASE2_COMPLETION_REPORT.md`, and
`PHASE3_COMPLETION_REPORT.md` (none recorded any deviation from their own
strategy documents that this phase needed to build against instead of the
original wording — Phases 1-3 all landed exactly as written, and Phase 3 in
particular confirmed `Application` always owns a real, non-null
`EditorUiCommandBridge` and that `NetworkServer`'s constructor/`RegisterRoutes()`
already accept and forward the `EditorUiCommandBridge*` pointer, with no route
using it yet — this phase's own starting point).

## What was done

### 1. `src/Network/NetworkRoutes.h`

- Added `#include "../Editor/EditorPanelCatalog.h"` to this file's includes
  (safe in every `GTE_ENABLE_EDITOR` configuration, per Phase 1's own
  Section 3.1 reasoning — a header with no matching `.cpp` is never gated by
  CMake).
- Added, verbatim per Section 3.1 of the strategy document: `ParsedActivateTabQuery`
  (`valid`/`errorMessage`/`notFound`/`tabName`), `ParseActivateTabQuery()`,
  `BuildActivateTabResponseJson()`, `BuildUnknownTabNameResponseJson()`, and
  `BuildListTabsResponseJson()` — all placed at the end of the file, before the
  closing `} // namespace gte::Network`, exactly as instructed.

### 2. `src/Network/NetworkRoutes.cpp`

Implemented all five new declarations exactly per Section 3.2, using
`nlohmann::json` for correct string escaping (a caller-supplied tab name can
contain characters needing JSON escaping, e.g. via the 404/409 error
messages) — matching this file's existing `BuildGenericErrorResponseJson()`/
`BuildInstantiatePrimitiveResponseJson()` conventions exactly. One small,
deliberate addition over the strategy document's own snippet:
`BuildActivateTabResponseJson()` explicitly marks its `tabExists` parameter
`(void)`-cast with a one-line comment, since the function's own body (as
specified) only ever branches on `success` — this avoids an "unused
parameter" question for a future reader without changing the function's
documented signature/behavior in any way (see "Deviations" below).

### 3. `src/Network/NetworkServer.cpp`

- `RegisterRoutes()` gained the two new registrations, placed directly after
  `RegisterListTexturesRoute(server, captureBridge);` per Section 3.3:
  - `GET /list_tabs` — a direct `server.Get(...)` lambda needing no bridge at
    all, calling `BuildListTabsResponseJson()`.
  - `GET /activate_tab?name=<PanelName>` — parses the query via
    `ParseActivateTabQuery()`, checks `!parsed.valid` (400), THEN
    `parsed.notFound` (404) — **checked before** the `uiCommandBridge ==
    nullptr` check (503), exactly matching the strategy document's own
    "IMPORTANT ordering detail" instruction — then submits to
    `EditorUiCommandBridge::SubmitAndWait()` and maps `alreadyPending` (503),
    `timedOut` (504), and the final `ActivateTabOutcome` (`success ? 200 :
    409`) to their documented status codes/bodies.
- `#include "../Application/EditorUiCommandBridge.h"` was **already present**
  in this file from Phase 3 (confirmed by inspection before editing) — no new
  include was needed here.

### 4. Tests (Tier 1, added in this same phase)

- `tests/Network/NetworkRoutesTests.cpp` — added, mirroring the existing
  `ParseGetTextureQuery`/`BuildListTexturesResponseJson` test style/naming
  exactly, every case Section 3.4 listed:
  `ParseActivateTabQueryTests.RejectsEmptyName`,
  `.AcceptsKnownName`, `.FlagsUnknownNameAsNotFoundNotInvalid`,
  `.IsCaseSensitive`; `BuildActivateTabResponseJsonTests.SuccessShape`/
  `.FailureShape`; `BuildUnknownTabNameResponseJsonTests.Shape`;
  `BuildListTabsResponseJsonTests.ContainsEveryKnownPanelName` (loop-driven
  against `gte::kKnownEditorPanelNames`, so it never needs updating when a
  panel is added/removed). 9 new tests total in this file.
- `tests/Network/NetworkServerTests.cpp` — added the two ordering-regression
  tests Section 3.4 required: `ActivateTabWithNullBridgeRespondsServiceUnavailableForAKnownName`
  (a bare, no-argument `NetworkServer` — `uiCommandBridge == nullptr` — real
  HTTP round-trip for `name=Profiler` asserts `503`) and
  `ActivateTabWithNullBridgeStillRespondsNotFoundForAnUnknownName` (same
  nullptr-bridge server, `name=NotARealTab` asserts `404`, proving the
  Section 3.3 ordering rule is actually implemented, not just documented). 2
  new tests total in this file.
- No new test *file* was created — both additions landed in pre-existing,
  already-registered files (`tests/CMakeLists.txt` already lists both), so no
  CMake registration change was needed this phase.
- Full end-to-end tests with a real, background-thread-driven
  `EditorUiCommandBridge` round-trip (a fake "main thread" fulfilling a
  request submitted by a fake "network thread") remain Phase 5's own job, per
  the strategy document's own Section 3.4 closing note — this phase's tests
  only prove the pure parsing/response-building functions and the
  nullptr-bridge degrade-gracefully paths.

## Deviations from the strategy document

None of substance. The only difference from the document's own illustrative
code snippet is the `(void)tabExists;` comment inside
`BuildActivateTabResponseJson()` (a defensive-clarity addition, not a
behavior change — the function's documented success/failure JSON shapes are
byte-for-byte what the strategy document specifies, confirmed by the
`SuccessShape`/`FailureShape` tests above matching it exactly). Every file,
function/struct name, response-JSON shape, and status-code mapping otherwise
matches `PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md` exactly,
including the locked "Endpoint contract" from `PHASE0_MASTER_STRATEGY.md`
(as corrected by `PHASE0_DOUBLE_CHECK_REPORT.md`).

## Verification performed

- `cmake --build build` (fast compile check, per this campaign's own workflow
  rule) — succeeded cleanly: `gte_core`, `NetworkRoutesTests.cpp`/
  `NetworkServerTests.cpp` object files, `GreatTamanaEngine.exe`, and
  `GreatTamanaEngineTests.exe` all rebuilt with zero warnings/errors
  attributable to this change.
- Ran the full `GreatTamanaEngineTests.exe` binary filtered to
  `ParseActivateTabQueryTests.*:BuildActivateTabResponseJsonTests.*:BuildUnknownTabNameResponseJsonTests.*:BuildListTabsResponseJsonTests.*:NetworkServerTests.ActivateTab*`
  — all 10 new tests passed. Also ran the full `*Network*`-filtered subset
  (22 tests, including every pre-existing `NetworkServerTests`/
  `NetworkRoutesTests` test) with zero regressions.
- Manual smoke test (Section "Step 4" of the strategy document):
  `run_app_background`'d the built `GreatTamanaEngine.exe` (PID 17924), then:
  - `GET /list_tabs` → `200`,
    `{"tabs":["Hierarchy","Inspector","Scene","Game","Memory","Profiler","Render Graph","Jobs","Atmosphere","Project"]}`
    (the default `GTE_ENABLE_PROJECT_PANEL=ON` build, so `"Project"` is
    present at the end, as documented).
  - `GET /activate_tab?name=Profiler` → `200`,
    `{"activated_tab":"Profiler","success":true}`.
  - `GET /activate_tab?name=NotARealTab` → `404`,
    `{"error":"unknown tab name 'NotARealTab' - see GET /list_tabs for the currently known names","success":false}`.
  - Immediately captured `GET /get_swapchain` and visually confirmed the
    "Profiler" tab is genuinely the frontmost/active tab among the
    bottom-docked group (Memory/Profiler/Render Graph/Jobs/Atmosphere/
    Project), proving the full chain (HTTP → `EditorUiCommandBridge` →
    `Application::Run()`'s drain step → `IEditorLayer::ActivateTab()` →
    real ImGui focus change) works end-to-end through a real running engine,
    not just through the nullptr-bridge unit tests above.
  - `stop_app_background`'d the process (PID 17924) afterward.
- No full `ctest` regression run performed this phase, per this campaign's
  own workflow rule (only required starting Phase 5).

## Exact state left in

- Modified files: `src/Network/NetworkRoutes.h`, `src/Network/NetworkRoutes.cpp`,
  `src/Network/NetworkServer.cpp`, `tests/Network/NetworkRoutesTests.cpp`,
  `tests/Network/NetworkServerTests.cpp`. No other file was touched, and no
  new file was created (no CMake registration change was needed).
- `GET /activate_tab` and `GET /list_tabs` are now fully live, reachable
  endpoints on a running engine, verified both by automated Tier-1 tests and
  by a real, running-engine manual smoke test that visually confirmed the
  tab-activation side effect.
- Every status code/JSON body documented in `PHASE0_MASTER_STRATEGY.md`'s
  locked "Endpoint contract" was exercised at least once this phase EXCEPT
  `504` (bridge timeout) and the `409` "known panel, but no live window yet"
  case with a REAL (non-nullptr) bridge — both remain Phase 5's job via a
  full end-to-end test with a fake "main thread" fulfillment loop (mirroring
  `EngineCommandEndpointsEndToEndTests.cpp`), since reproducing them requires
  controlling a real `EditorUiCommandBridge`'s fulfillment timing directly,
  not just a nullptr-bridge/real-Editor-window smoke test.
- Build directory `build/` is left in a successfully-built state (Debug build
  via Ninja/MinGW, `GTE_ENABLE_EDITOR=ON`/`GTE_ENABLE_NETWORK=ON`/
  `GTE_ENABLE_PROJECT_PANEL=ON` — every default — matching the configuration
  already present before this phase started). No other build configuration
  (`GTE_ENABLE_EDITOR=OFF`, `GTE_ENABLE_PROJECT_PANEL=OFF`) was
  configured/built this phase — that remains Phase 5's job, per the
  campaign's own Definition of Done.
- On branch `feature/network-impl` throughout (never switched).

Ready for Phase 5 (`PHASE5_TESTING_DOCS_AND_REGRESSION_SAFETY.md`).
