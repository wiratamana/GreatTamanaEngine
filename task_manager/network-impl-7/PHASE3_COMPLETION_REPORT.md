# PHASE3 Completion Report — Application Wiring + Frame-Loop Integration

**Parent:** `PHASE0_MASTER_STRATEGY.md`. Executed exactly as specified in
`PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md`, having first read
`PHASE1_COMPLETION_REPORT.md` and `PHASE2_COMPLETION_REPORT.md` (neither
recorded any deviation from their own strategy documents that this phase
needed to build against instead of the original wording — both Phase 1 and
Phase 2 landed exactly as written).

## What was done

### 1. `src/Application/Application.h`

- Added `#include "EditorUiCommandBridge.h"` alongside the existing
  `#include "EngineCommandBridge.h"` / `#include "FrameCaptureBridge.h"`.
- Added the new member, `EditorUiCommandBridge m_uiCommandBridge;`, declared
  immediately after `m_commandBridge` and BEFORE `m_networkServer` — the
  exact same "constructed first, destroyed last relative to
  `m_networkServer`, so its address stays valid for the constructor call"
  ordering already used for `m_captureBridge`/`m_commandBridge`, with a
  matching doc comment adapted from the existing two.

### 2. `src/Network/NetworkServer.h`

- Added a forward declaration, `namespace gte { class EditorUiCommandBridge; }`,
  mirroring the existing `FrameCaptureBridge`/`EngineCommandBridge` forward
  declarations.
- Extended the constructor with a THIRD, appended, defaulted
  (`= nullptr`) parameter, `EditorUiCommandBridge* uiCommandBridge`, after
  `commandBridge` — every existing call site (including every
  `tests/Network/NetworkServerTests.cpp` no-argument construction) keeps
  compiling unchanged.
- Added the matching private member, `EditorUiCommandBridge* m_uiCommandBridge = nullptr;`,
  with the same "non-owning, nullptr degrades gracefully" doc-comment style
  as `m_commandBridge`.

### 3. `src/Network/NetworkServer.cpp`

- Added `#include "../Application/EditorUiCommandBridge.h"`.
- `RegisterRoutes(...)`'s signature gained the new `EditorUiCommandBridge*
  uiCommandBridge` parameter (appended, matching the header), and the
  constructor now forwards `m_uiCommandBridge` into both its own
  member-initializer list and the `RegisterRoutes(...)` call.
- **No new route was registered.** `RegisterRoutes()`'s body gained zero new
  `server.Get(...)`/`server.Post(...)` calls — the new parameter is plumbed
  through but otherwise unused in this phase's own function body, exactly as
  Section 3.2 of the strategy document requires ("keep this phase's diff
  scoped to plumbing only"). This is expected to produce an "unused
  parameter" compiler note at most (verified: the actual build produced zero
  warnings of any kind, gte_core's own CMake target does not enable
  `-Wall`/`-Wextra`/`-Werror` for this translation unit — see
  `third_party/ktx/CMakeLists.txt` for where those flags actually live in
  this repository, confirmed to not apply here).

### 4. `src/Application/Application.cpp`

- Updated the constructor's member-initializer list: `m_networkServer(&m_captureBridge,
  &m_commandBridge)` became `m_networkServer(&m_captureBridge, &m_commandBridge,
  &m_uiCommandBridge)`, with the existing comment block extended to describe
  the third bridge the same way it already described the first two.
- Inserted the new per-frame drain block **immediately after
  `m_editorLayer->NewFrame();` and before `m_renderer.BeginFrame();`** —
  the very next statement after `NewFrame()`, exactly matching Section 3.4's
  placement instruction and its own "why this exact frame position"
  reasoning (Dear ImGui's window/dock state is only valid to touch AFTER
  `NewFrame()` has run for the current frame, and a change made here is
  still visible in THIS SAME frame's own tab rendering since `BuildUI()`
  has not run yet). The block:
  - Peeks at most one pending request via
    `m_uiCommandBridge.TryPeekPendingCommandRequest()`.
  - Wraps the work in `GTE_PROFILE_SCOPE("Application::ExecuteEditorUiCommand")`,
    mirroring `EngineCommandBridge`'s own drain block one level below.
  - Calls `m_editorLayer->ActivateTab(uiRequest->activateTab.tabName)` (the
    Phase 2 virtual method) and maps its `TabActivationResult::tabExists`
    into both `ActivateTabOutcome::tabExists` and `ActivateTabOutcome::success`
    (they are identical today — the outcome type keeps them as two separate
    fields purely so a future distinct failure mode could diverge them
    without an API change, per `EditorUiCommandBridge.h`'s own doc comment).
  - Calls `m_uiCommandBridge.FulfillCommand(uiResult)` to hand the result
    back to whichever network thread (if any) is waiting.
- No new `#include` was needed for `std::optional` — confirmed by successful
  compilation; `Application.h` already transitively includes it via
  `EngineCommandBridge.h`/`EditorUiCommandBridge.h`, exactly as the strategy
  document predicted (Section 3.4's own note to "verify this explicitly
  rather than assume").

## Deviations from the strategy document

None. Every file, member name, constructor-argument order, and frame-loop
insertion point matches `PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md`
exactly. No production code outside the four files listed above
(`Application.h`, `Application.cpp`, `NetworkServer.h`, `NetworkServer.cpp`)
was touched. No test file was added, per the strategy document's own Section
3.5 ("Do not add any test in this phase" — this wiring is not independently
Tier-1-testable in isolation from a live `Application`/window/Vulkan device,
the same "Tier 2, no automated coverage yet" bucket `Application::Run()`
itself already falls into).

## Verification performed

- `cmake --build build` (fast compile check, per this campaign's own
  workflow rule) — succeeded cleanly. All 9 build steps completed
  (`gte_core`, `NetworkServerTests.cpp`/`CaptureEndpointsEndToEndTests.cpp`/
  `EngineCommandEndpointsEndToEndTests.cpp` object files, `GreatTamanaEngine.exe`,
  and `GreatTamanaEngineTests.exe`), with zero warnings or errors
  attributable to this change.
- Manual smoke test (per Section 3.5's own suggested substitute — a live
  request end-to-end through the bridge isn't possible without Phase 4's
  HTTP route yet, so this instead confirms the wiring doesn't break anything
  that already works): launched the freshly built `GreatTamanaEngine.exe` in
  the background (`run_app_background`, PID 16624) and confirmed
  `GET http://127.0.0.1:8080/http_hello_world` still returns `200 hello
  world` — i.e. `NetworkServer`'s three-pointer constructor and the new
  per-frame drain block (which is never actually "pending" today, since
  nothing can submit to `EditorUiCommandBridge` until Phase 4 exists) run
  every frame with no crash, no hang, and no regression to an existing,
  already-working endpoint. Stopped the process (`stop_app_background`)
  afterward.
- Correctness of the exact frame-loop insertion point (immediately after
  `NewFrame()`, before `BeginFrame()`/`BuildUI()`) was additionally confirmed
  by direct code inspection against Section 2's own "why this exact frame
  position" reasoning, since — as the strategy document itself notes — there
  is no way to exercise this bridge end-to-end (i.e. observe a real tab
  actually change) until Phase 4's HTTP route exists to submit a request
  through it.
- No behavior change to any EXISTING endpoint, panel, or frame-loop step —
  this phase is strictly additive (confirmed both by the passing manual
  smoke test above and by `git status`/diff showing changes confined to
  exactly the four files this phase's own plan named).
- No full `ctest` regression run performed this phase, per this campaign's
  own workflow rule (only required starting Phase 5).

## Exact state left in

- Modified files: `src/Application/Application.h`, `src/Application/Application.cpp`,
  `src/Network/NetworkServer.h`, `src/Network/NetworkServer.cpp`. No other
  file was touched.
- `Application` now owns a real, non-null `EditorUiCommandBridge` member
  (`m_uiCommandBridge`) unconditionally, regardless of `GTE_ENABLE_EDITOR` —
  matching `PHASE0_DOUBLE_CHECK_REPORT.md`'s corrected "Endpoint contract"
  (the bridge pointer handed to `NetworkServer` is only ever `nullptr` in a
  test that constructs `NetworkServer` directly with no arguments, never in
  production).
- `Application::Run()`'s frame loop now drains at most one pending
  `EditorUiCommandRequest` per frame, immediately after
  `m_editorLayer->NewFrame()`, calling into the real, working
  `IEditorLayer::ActivateTab()` mechanism Phase 2 already manually verified.
  This drain step is currently **unreachable in practice** — nothing can
  ever place a request into `m_uiCommandBridge` yet, since no HTTP route
  calls `EditorUiCommandBridge::SubmitAndWait()` — so this new code runs
  every frame, finds nothing pending, and is a no-op, exactly as expected
  for this phase.
- `NetworkServer`'s constructor/`RegisterRoutes()` now accept and forward a
  third `EditorUiCommandBridge*` pointer, but no HTTP route yet reads it —
  this is Phase 4's job (`PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md`).
- Build directory `build/` is left in a successfully-built state (Debug
  build via Ninja/MinGW, `GTE_ENABLE_EDITOR=ON`/`GTE_ENABLE_NETWORK=ON` —
  both defaults — matching the configuration already present before this
  phase started). No other build configuration (`GTE_ENABLE_EDITOR=OFF`,
  `GTE_ENABLE_NETWORK=OFF`) was configured/built this phase — that remains
  Phase 5's job, per the campaign's own Definition of Done.
- On branch `feature/network-impl` throughout (never switched), 21 commits
  ahead of `origin/feature/network-impl` before this phase's own commit.

Ready for Phase 4 (`PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md`).
