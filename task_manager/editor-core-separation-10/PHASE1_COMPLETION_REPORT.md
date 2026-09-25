# PHASE1 — ID Conflict Detection Foundation — COMPLETION REPORT

**Campaign:** `editor-core-separation-10` — "ImGui Widget ID Uniqueness"
**Phase:** PHASE1 of 4
**Status:** DONE

---

## What was done

Built the two new, inert (not yet called by any panel) scaffolding pieces
exactly as specified by `PHASE1_ID_CONFLICT_DETECTION_FOUNDATION.md`, with no
deviation from the phase's own code listings.

### New files

- `src/Editor/ImGuiIdConflictTracker.h` / `.cpp` — pure, Tier-1-testable
  id-collision detector. Zero dependency on `<imgui.h>` or `Logger`. Public
  API: `Reset()`, `RegisterAndCheckConflict(std::uint32_t)`,
  `DistinctIdCount()`.
- `src/Editor/ImGuiIdConflictGuard.h` / `.cpp` — Editor-owned, process-global,
  main-thread-only Meyers singleton (`Instance()`), mirroring
  `LoggerLogSink::Instance()`'s exact shape. Wraps
  `ImGuiIdConflictTracker` and resolves the real `ImGui::GetID("")` value,
  logging via `GTE_LOG_ERROR("ImGuiIdConflict", ...)` exactly once per NEW
  conflict incident (persistent `m_ongoingConflicts` set survives across
  frames; only cleared/re-armed when a conflict actually stops happening for
  at least one frame — Locked Design Decision #2 from
  `PHASE0_MASTER_STRATEGY.md`).
- `tests/Editor/ImGuiIdConflictTrackerTests.cpp` — 6 GoogleTest cases covering
  every scenario the phase doc required: fresh-id registration, 2nd/3rd
  repeat conflict, `Reset()` re-arming, two distinct ids never conflicting,
  and `DistinctIdCount()` with a repeat/fresh mix.

### Edited files

- `src/Editor/ImGuiEditorLayer.cpp` — added
  `#include "ImGuiIdConflictGuard.h"` (alphabetically between
  `GpuDrivenBatchTestSpawner.h` and `ImGuiMemoryTracker.h`), and added
  `ImGuiIdConflictGuard::Instance().BeginFrame();` (plus its own explanatory
  comment) inside `NewFrame()`, immediately after `ImGui::NewFrame();` and
  before the pre-existing `BeginGizmoFrame()` call/comment. `NewFrame()` was
  found at line 350 (as of this phase's start) — matched the phase doc's
  expected shape exactly (line numbers had shifted slightly from the doc's
  own "around line 349-365" estimate, purely due to normal file drift, not a
  structural surprise). Exact final `NewFrame()` body:

  ```cpp
      void NewFrame() override
      {
          // See RenderPlatformWindows()/m_frameRendered below for why this is
          // reset here, at the very start of every frame.
          m_frameRendered = false;

          ImGui::SetCurrentContext(m_context);
          ImGui_ImplVulkan_NewFrame();
          ImGui_ImplSDL3_NewFrame();
          ImGui::NewFrame();

          // task_manager/editor-core-separation-10 campaign, PHASE1 - resets
          // the ID-conflict-detection tracker for this fresh frame. Must run
          // AFTER ImGui::NewFrame() (so ImGui::GetID() calls later this frame
          // are meaningful) and BEFORE any panel builds a single widget.
          ImGuiIdConflictGuard::Instance().BeginFrame();

          // Required by ImGuizmo before any Manipulate() call this frame
          // (Panels/ScenePanel.cpp, via TransformGizmo.h) - see
          // BeginGizmoFrame()'s own comment for why this is called right
          // here rather than from BuildUI() below.
          BeginGizmoFrame();
      }
  ```

- `CMakeLists.txt` (root) — added the 4 new `gte_editor` source files right
  after `src/Editor/Logger.cpp` (found at line 989 as of this phase's start,
  matching the phase doc's own "around line 988-989" estimate).
- `tests/CMakeLists.txt` — added `Editor/ImGuiIdConflictTrackerTests.cpp` to
  `GTE_TEST_SOURCES` right after `Editor/LoggerTests.cpp` (found at line 2309
  as of this phase's start, matching the phase doc's own "around line 2309"
  estimate).

## Verification performed

1. **Configure**: re-ran `cmake -S . -B build` (both root `CMakeLists.txt`
   and `tests/CMakeLists.txt` file lists changed) — succeeded, only
   pre-existing/unrelated warnings (MinGW static-CRT plugin-linkage no-ops,
   KTX git-describe fallback), nothing related to this phase's own files.
2. **Incremental build**: `cmake --build build --target GreatTamanaEngineTests`
   — recompiled exactly the expected 4 translation units
   (`ImGuiIdConflictTracker.cpp`, `ImGuiIdConflictGuard.cpp`,
   `ImGuiEditorLayer.cpp`, `ImGuiIdConflictTrackerTests.cpp`), relinked
   `gte_editor.a` and `GreatTamanaEngineTests.exe`. Zero warnings, zero
   errors.
3. **New test run only** (not the full suite — Locked Design Decision #8):
   `tests\GreatTamanaEngineTests.exe --gtest_filter=*ImGuiIdConflictTracker*`
   → `[==========] 6 tests from 1 test suite ran. [ PASSED ] 6 tests.`
   All 6 cases (`FirstRegistrationOfAFreshIdIsNotAConflict`,
   `SecondRegistrationOfTheSameIdIsAConflict`,
   `ThirdRegistrationOfTheSameIdIsAlsoAConflict`,
   `ResetClearsSeenIdsSoAPreviouslyConflictingIdIsFreshAgain`,
   `TwoDifferentIdsNeverConflictWithEachOther`,
   `DistinctIdCountReflectsAMixOfRepeatsAndFreshIds`) pass.
4. **`search_in_dir` sweep**: confirmed `ImGuiIdConflictGuard`/
   `ImGuiIdConflictTracker` are referenced NOWHERE else yet except the two
   places this phase added — `ImGuiEditorLayer.cpp`'s `NewFrame()` (the
   `BeginFrame()` call) and the test file. No panel calls
   `CheckCurrentIdScope()` yet, as expected — that starts in PHASE2.

No full build / no full `ctest` regression was run this phase, per this
campaign's own Locked Design Decision #8 (reserved for PHASE4).

## Nothing visible changed

As designed: this phase is pure, safe, additive scaffolding. No panel's
on-screen behavior changed. The `ImGuiIdConflictGuard::BeginFrame()` call
added to `NewFrame()` only resets an inert tracker nobody feeds yet — it has
zero observable effect until PHASE2 wires `ScopedUniqueId`/
`CheckCurrentIdScope()` into real panel code.

## Notes / deviations for PHASE2

- None. Every code listing in `PHASE1_ID_CONFLICT_DETECTION_FOUNDATION.md`
  was used verbatim (only the surrounding file's line numbers had the
  expected minor drift already flagged as "re-verify, do not trust
  blindly" by the phase doc itself — the actual code shape at each insertion
  point matched the doc's expectations exactly, no signature mismatch, no
  structural surprise).
- `ImGuiIdConflictGuard::CheckCurrentIdScope(const char* debugContext, const
  char* debugKey)` is ready and untouched by any call site — PHASE2 can build
  `gte::ScopedUniqueId` directly on top of it exactly as the master strategy
  describes, then retrofit `RenderGraphPanel.cpp`'s three loops.
