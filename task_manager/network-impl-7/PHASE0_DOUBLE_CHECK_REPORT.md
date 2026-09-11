# PHASE0 Double-Check Report — `network-impl-7` (second iteration)

This is a full quality/consistency review pass across all six strategy
documents for the `network-impl-7` campaign (`GET /activate_tab` /
`GET /list_tabs`), performed BEFORE any implementation exists. Every concrete
claim about the real repository (file paths, function signatures, CMake
structure, vendored third-party source line numbers) was independently
verified by opening the real files under `src/`, `CMakeLists.txt`,
`tests/CMakeLists.txt`, and `third_party/imgui/` — not just trusted from the
`.md` prose.

## Summary

Four of the six files needed **no changes at all** — every concrete claim in
them held up under direct verification. Two files had real, confirmed bugs
fixed:

- **`PHASE0_MASTER_STRATEGY.md`** — fixed (one incorrectness bug, a real
  cross-file inconsistency with Phase 3/5's own actual design).
- **`PHASE1_EDITOR_UI_COMMAND_BRIDGE.md`** — fixed (two confirmed
  incorrectness/gap bugs, both in CMake/test-registration instructions).
- `PHASE2_IMGUI_TAB_ACTIVATION_ENGINE.md` — **no changes.** Every ImGui
  line-number/behavior claim in this file (an earlier task's own focused
  correctness pass) was re-verified directly against the vendored
  `third_party/imgui/imgui.h`/`imgui.cpp`/`imgui_internal.h` sources and found
  byte-for-byte accurate, including the subtle `FocusWindow()`/
  `DockNodeUpdateTabBar()`/`g.NavWindow` mechanism explanation and every cited
  line number (508, 13744, 13901, 13962, 17998, 19731, 19856-19858, 3611).
  Nothing needed touching.
- `PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md` — **no changes.**
  `Application.h`'s member ordering (`m_commandBridge` immediately before
  `m_networkServer`), the constructor's `m_networkServer(&m_captureBridge,
  &m_commandBridge)` call, and `Application::Run()`'s real frame-loop order
  (`EngineCommandBridge` drain → `NewFrame()` → `BeginFrame()`) were all
  independently confirmed against the real `Application.h`/`Application.cpp` —
  this phase's planned insertion point and reasoning are correct as written.
- `PHASE4_HTTP_ENDPOINTS_ACTIVATE_TAB_AND_LIST_TABS.md` — **no changes.**
  `NetworkServer.h/.cpp`'s existing two-bridge-pointer constructor shape,
  `NetworkRoutes.h/.cpp`'s existing `nlohmann::json`-based builder
  conventions (`BuildGenericErrorResponseJson`, the `!success` early-return
  pattern, exact-lowercase query-parameter matching), and the
  `/get_texture`-style "parse → notFound → nullptr-bridge → submit" ordering
  were all confirmed to match this phase's own plan precisely, including the
  already-consistent `success ? 200 : 409` status mapping (see the PHASE0 fix
  below — this file was already internally consistent with the corrected
  contract).
- `PHASE5_TESTING_DOCS_AND_REGRESSION_SAFETY.md` — **no changes.** Its own
  Section 3.5 explicitly (and correctly) documents that a
  `GTE_ENABLE_EDITOR=OFF` build makes `GET /activate_tab` for a known name
  "always get a `409`-shaped outcome... never a crash" — this turned out to
  be the CORRECT half of a contradiction found in `PHASE0_MASTER_STRATEGY.md`
  (see below); Phase 5 needed no fix, `PHASE0` did. `tests/Network/
  NetworkTestHelpers.h` and `tests/Network/EngineCommandEndpointsEndToEndTests.cpp`
  (the explicit template this phase's own new end-to-end test file is meant
  to mirror) were both confirmed to exist with the exact shape described.

## Findings and fixes, per file

### `PHASE0_MASTER_STRATEGY.md` — FIXED

**Incorrectness (cross-file consistency bug):** the locked "Endpoint
contract" section originally stated that `503` covers BOTH "a `nullptr`
bridge pointer in tests" AND "`GTE_ENABLE_EDITOR=OFF`/`NullEditorLayer`".
This is factually wrong given how the rest of the campaign's OWN documents
design the feature:

- Per `PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md`, `Application`
  always constructs a real, non-null `EditorUiCommandBridge` member
  UNCONDITIONALLY (it lives under `src/Application/`, which compiles
  regardless of `GTE_ENABLE_EDITOR` — confirmed directly against
  `Application.h`'s own real member list, which already declares
  `EngineCommandBridge`/`FrameCaptureBridge` the same unconditional way).
  So in a `GTE_ENABLE_EDITOR=OFF` build, the bridge is still perfectly
  reachable and non-null.
- What actually differs in that build is `NullEditorLayer::ActivateTab()`
  (Phase 2), which always returns `tabExists == false` — which Phase 3's own
  planned wiring maps to `success = false`, and Phase 4's own planned route
  handler maps `success == false` to **409**, never 503.
- `PHASE5_TESTING_DOCS_AND_REGRESSION_SAFETY.md`'s own Section 3.5 already
  had this exactly right ("`GET /activate_tab` for a known name now always
  gets a `409`-shaped outcome from `NullEditorLayer`'s `tabExists = false`,
  never a crash") — so PHASE0's own contract directly contradicted PHASE5's
  own verification step for the exact same scenario. This is precisely the
  class of cross-file inconsistency this review pass exists to catch.

**Fix:** rewrote the `409`/`503` bullets in the "Endpoint contract" section
to correctly document TWO distinct causes of `409` (the narrow
Editor-just-started race in an ON build, and the PERMANENT case in an OFF
build), and narrowed `503` to ONLY the genuinely-nullptr-bridge-pointer case
(reachable in a test, or a hypothetical bridge-less build), explicitly
calling out that the two must never be conflated.

### `PHASE1_EDITOR_UI_COMMAND_BRIDGE.md` — FIXED (two bugs)

**Bug 1 — Incorrectness, Section 3.4 (CMakeLists.txt registration):** the
original text claimed `EditorLayer.h` "mirrors this precedent... search
`CMakeLists.txt` for `src/Editor/EditorLayer.h` to see it is listed
UNCONDITIONALLY". This is false: a direct search of the real, on-disk
`CMakeLists.txt` shows `src/Editor/EditorLayer.h` appears ONLY inside two
prose comments (around lines 22 and 552) — it is never actually listed in
any `target_sources()`/`add_library()` call at all. The same paragraph was
also internally self-contradictory: it first said to add the new header to
"the SAME `if(GTE_ENABLE_EDITOR)` `target_sources()` block", then two
sentences later said "to the unconditional list, NOT inside the
`if(GTE_ENABLE_EDITOR)` block" — two mutually exclusive instructions in the
same paragraph, which would have left an implementer to guess. **Fixed** by
removing the false precedent claim, resolving the internal contradiction,
and giving one single, unambiguous, verified instruction: add
`EditorPanelCatalog.h` to `gte_core`'s MAIN, unconditional
`add_library(gte_core STATIC ...)` list (confirmed to start at line 235 with
no enclosing `if()`), never inside the `if(GTE_ENABLE_EDITOR)` block.

**Bug 2 — Gap/Missing piece, Section 3.5 (test registration):** the original
text recommended placing the new panel-catalog test at
`tests/Editor/EditorPanelCatalogTests.cpp`, explicitly citing
`tests/Editor/EditorCameraTests.cpp` as a precedent for it belonging to "the
SAME always-built bucket as `EngineCommandBridgeTests.cpp`/
`EditorCameraTests.cpp` already are." This is false and would have produced
a real, working-as-documented-but-wrong test placement: a direct check of
the real `tests/CMakeLists.txt` shows `EditorCameraTests.cpp`, and every
other `tests/Editor/*.cpp` entry, is registered EXCLUSIVELY inside
`tests/CMakeLists.txt`'s `if(GTE_ENABLE_EDITOR)` block (confirmed around
line 1861) — meaning none of them build or run at all in a
`GTE_ENABLE_EDITOR=OFF` configure. Since `EditorPanelCatalog.h` must compile
and be tested in EVERY configuration (it is consumed by
`src/Network/NetworkRoutes.cpp`, which always compiles), following that
"precedent" verbatim would have silently produced ZERO test coverage for
`IsKnownEditorPanelName()` in a `GTE_ENABLE_EDITOR=OFF` build — directly
contradicting this SAME section's own explicit requirement two sentences
later ("Neither new test file needs `GTE_ENABLE_EDITOR`/
`GTE_ENABLE_PROJECT_PANEL` gating"). **Fixed** by correcting the claim and
giving an explicit, unambiguous instruction: register the new test file in
`tests/CMakeLists.txt`'s MAIN, unconditional `GTE_TEST_SOURCES` list
(alongside `Application/EngineCommandBridgeTests.cpp`), regardless of which
folder the `.cpp` file itself physically lives in, and explicitly NOT inside
the `if(GTE_ENABLE_EDITOR)` block every pre-existing `Editor/*Tests.cpp`
entry sits in.

## Cross-file consistency check (struct/type/field names)

Verified that every struct/enum/field name one phase document defines is
referenced identically by every later phase document that uses it:

- `ActivateTabOutcome{ success, tabExists }` (Phase 1) → referenced with
  identical field names by Phase 3 (`activation.tabExists`,
  `uiResult.activateTab.success/tabExists`) and Phase 4
  (`submit.result->activateTab`, `outcome.success`/`outcome.tabExists`). ✅
- `EditorUiCommandRequest{ kind, activateTab }` / `EditorUiCommandResult{
  kind, activateTab }` (Phase 1) → referenced identically in Phase 3's
  `Application::Run()` snippet and Phase 4's route-handler snippet. ✅
- `TabActivationResult{ tabExists }` (Phase 2, `EditorLayer.h`) → referenced
  identically by Phase 3 (`const TabActivationResult activation = ...`). ✅
- `IsKnownEditorPanelName()` / `kKnownEditorPanelNames` (Phase 1,
  `EditorPanelCatalog.h`) → referenced identically by Phase 2
  (`DockLayout.cpp`'s planned rename) and Phase 4
  (`ParseActivateTabQuery()`/`BuildListTabsResponseJson()`). ✅
- `FindAndFocusEditorWindow()` (Phase 2, `DockLayout.h`) → referenced
  identically by Phase 2's own `ImGuiEditorLayer::ActivateTab()` snippet (the
  only consumer). ✅
- JSON response shapes/status codes match verbatim between PHASE0's own
  locked "Endpoint contract" (as corrected above) and every later phase's own
  implementation snippet (Phase 4's `BuildActivateTabResponseJson()`/
  `BuildUnknownTabNameResponseJson()`/route-handler status mapping, Phase 5's
  own end-to-end test expectations).

No other mismatches were found.

## Verified-accurate claims worth calling out (spot-checked, found correct)

- `EngineCommandBridge.h/.cpp`'s exact single-global-slot shape (mutex +
  `condition_variable`, `SubmitResult{ result, alreadyPending, timedOut }`,
  the "collapse check-then-use into one locked operation" fix) — confirmed
  byte-for-byte against the real files; Phase 1's new bridge is specified as
  a faithful structural copy.
- `DockLayout.cpp`'s real `kAllPanelNames` array — confirmed to contain
  exactly the 9 unconditional names plus the conditional `"Project"` entry
  Phase 0/1 describe, in the same order.
- `EditorLayer.h`'s real include list — confirmed it does NOT `#include
  <string>` directly. (One caveat, not worth a fix: one of its transitive
  includes, `Renderer/Memory/GpuMemoryTracker.h`, DOES conditionally
  `#include <string>` under `#if GTE_ENABLE_EDITOR` — meaning `<string>` is
  already present transitively in an ON build specifically, but not in an
  OFF build. This makes Phase 2's instruction to add an explicit `#include
  <string>` to `EditorLayer.h` even MORE necessary than its own reasoning
  states, not less — since relying on the transitive OFF-config absence
  would otherwise silently compile in ON but fail in OFF. No fix needed;
  flagging only as a nuance for the implementer.)
- The exact vendored ImGui line numbers/mechanism Phase 2 cites (`imgui.h`
  line 508, `imgui.cpp` lines 13744/13901/13962/17998/19731/19856-19858,
  `imgui_internal.h` line 3611) — all confirmed character-for-character
  against `third_party/imgui/`.
- `NetworkServer.h/.cpp`'s real two-pointer constructor shape and
  `NetworkRoutes.h/.cpp`'s real `nlohmann::json` usage conventions — both
  confirmed to match every later phase's own planned extension precisely.
- `tests/Network/NetworkTestHelpers.h` and
  `tests/Network/EngineCommandEndpointsEndToEndTests.cpp` — both confirmed
  to exist with the shape Phase 5 describes as its own template.

## Scope note

Per the task's own instructions, this was a documentation/strategy review
only — no production C++ code was written, and no build/test/git command
other than the final commit was run.
