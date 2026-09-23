# PHASE9 — The CMake Target Split (Checkpoint: FULL ctest required)

## Parent
`PHASE0_MASTER_STRATEGY.md`. Depends on Phase 8 (zero macro branching must
remain before this surgery starts). **This is one of the design doc's own
two flagged highest-risk steps — do this as its own isolated, carefully
reviewed step.** See also Locked Design Decision #9 (the `CreateEditorLayer`
ODR/link-order fix this phase must apply) — read it before starting.

## Step 1: The Goal

Two real CMake targets exist: `gte_core` (engine only) and `gte_editor`
(depends on `gte_core`, owns everything that used to be behind the old
`GTE_ENABLE_EDITOR` block). The executable always links `gte_editor`. This
phase is PURE MECHANICAL FILE MOVEMENT, plus ONE deliberate, required rename
(the `CreateEditorLayer`/`CreateNullEditorLayer` split below) — no other
code changes beyond what Phase 8 already did. `gte_core` may still
transitively need SDL/ImGui-adjacent types at this point (via
`Window.cpp`/`Application.cpp` still living inside it) — that's expected and
fixed later (Phases 10-14). The ONLY goal of this phase: two real archives
exist, one depends on the other, nothing has moved between them incorrectly
yet, and the one known, confirmed ODR/link-order hazard is closed.

## Step 2: The Situation / The Problem

Read the root `CMakeLists.txt` in full, current state (post-Phase-8). The
temporary `if(TRUE) ... endif()` block (Phase 8's placeholder) currently
adds ~60 Editor files into `gte_core`'s own `target_sources()`. This phase
replaces that entirely.

**Read the comment directly above that block first** — as of this
strategy's own double-check pass, the real `CMakeLists.txt` says, verbatim:
*"Exactly one of these two branches gets compiled in, depending on
GTE_ENABLE_EDITOR - both define the same gte::CreateEditorLayer() factory
function (see src/Editor/EditorLayer.h), so there's never an ODR
conflict."* That comment is TRUE TODAY (only one of `NullEditorLayer.cpp`/
`ImGuiEditorLayer.cpp` ever compiles) and BECOMES FALSE the moment this
phase's own split is done: `gte_core` keeps `NullEditorLayer.cpp` in its own
unconditional list (Step 5 below, needed for a future Player host); the new
`gte_editor` target carries `ImGuiEditorLayer.cpp`, ALWAYS (Rule 4 — no
on/off switch). Both archives always link into this repo's own single
executable from this phase onward, so both files' identically-named
`gte::CreateEditorLayer()` definitions would exist in the same final link,
forever, with no guaranteed compiler/linker error (a static archive silently
never extracts an already-resolved symbol's member — this risks silently
resolving to whichever implementation the linker's archive scan order
happens to satisfy first, up to and including this repo's own primary
authoring executable silently booting with a no-op Editor UI). Step 4 below
is the required, deliberate fix — do not skip it and do not defer it to a
later phase; it must land in the SAME phase as the two-archive split, before
this phase's own full-ctest checkpoint.

## Step 3: The Plan

1. Read `CMakeLists.txt` in full. Identify the EXACT current file list
   inside the (now-hardcoded) former `GTE_ENABLE_EDITOR` block — this is
   your move-list. Also confirm the current `else()` branch still adds only
   `src/Editor/NullEditorLayer.cpp` to `gte_core`.
2. Delete the entire `if(TRUE) ... else() ... endif()` block.
3. Read `src/Editor/EditorLayer.h`'s final line
   (`std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer);`)
   and `src/Editor/NullEditorLayer.cpp`'s matching definition in full,
   current state.
4. **Rename the ODR hazard away (Locked Design Decision #9, required in
   this same phase, before Step 5 below)**:
   - In `src/Editor/EditorLayer.h`: keep the existing
     `CreateEditorLayer(Window&, Renderer&)` declaration exactly as-is (this
     remains the ONE function `EditorHost`/`Application` ever calls — Phase
     15 confirms this). Add a SECOND, distinctly-named declaration right
     next to it:
     ```cpp
     // Always-available, zero-ImGui/zero-SDL-dependency fallback living
     // inside gte_core itself (src/Editor/NullEditorLayer.cpp) - the ONE
     // thing a future Player host (linking gte_core alone, never seeing
     // gte_editor's source) can call to get a working, no-op IEditorLayer.
     // Never called anywhere in THIS repo - EditorHost/Application always
     // call the real CreateEditorLayer() above instead, which after this
     // phase's CMake split resolves unambiguously to gte_editor's own
     // ImGuiEditorLayer.cpp - there is no longer a second definition of
     // THAT name anywhere in the link.
     std::unique_ptr<IEditorLayer> CreateNullEditorLayer(Window& window, Renderer& renderer);
     ```
   - In `src/Editor/NullEditorLayer.cpp`: rename its own
     `CreateEditorLayer(Window&, Renderer&)` function definition to
     `CreateNullEditorLayer(Window&, Renderer&)` — a pure rename, zero body
     change.
   - Confirm via `search_in_dir` for `CreateEditorLayer` across the whole
     repo that the ONLY remaining definition of that exact name is in
     `src/Editor/ImGuiEditorLayer.cpp`, and that nothing in `src/` still
     expects `NullEditorLayer.cpp` to provide it.
5. Add `src/Editor/NullEditorLayer.cpp` to `gte_core`'s OWN unconditional
   `target_sources()` list — `gte_core` itself must not assume its caller
   is always `gte_editor` (a future Player host still needs
   `CreateNullEditorLayer()` to resolve to something).
6. Create a new `add_library(gte_editor STATIC ...)` target, unconditionally
   (no `option()` gating whether it exists at all — it is ALWAYS built).
   `target_sources(gte_editor PRIVATE <the exact move-list from Step 1,
   PLUS every new Editor-side file created by Phases 2-7:
   FrameDebuggerReplayPasses.cpp, FrameDebuggerDrawRecording.cpp,
   EditorGpuMemoryNameOverlay.h/.cpp, EditorSceneIOCapability.h/.cpp,
   EditorUiCapabilityImpl.h/.cpp, EditorAssetImportCapabilityImpl.h/.cpp,
   EditorGpuDrivenBatchTestCapabilityImpl.h/.cpp (if created)>)`.
7. `target_link_libraries(gte_editor PUBLIC gte_core)`.
   `target_link_libraries(gte_editor PRIVATE imgui imguizmo)`.
8. Find wherever the executable target (`GreatTamanaEngine`) currently links
   `gte_core` — change it to link `gte_editor` instead (never both
   explicitly; `PUBLIC` propagation from Step 7 already brings `gte_core`
   in transitively). Confirm there is no remaining direct
   `target_link_libraries(GreatTamanaEngine ... gte_core ...)` line.
9. Update `tests/CMakeLists.txt`: the test binary must now always link
   `gte_editor` too (not `gte_core` alone), since `Editor/*Tests.cpp` (if
   any exist — confirm via `browse_dir`/`search_in_dir` on `tests/`) needs
   symbols that now permanently live there.
10. Compile-check first with an INCREMENTAL build to catch obvious mistakes
    fast. Once that succeeds, this phase requires the ONE exception to the
    "no full build" rule for most phases:
    **run a FULL clean rebuild** (delete/reconfigure the `build` directory
    or use `cmake --build build --clean-first`, confirm the actual
    convention this repo uses first) **and a FULL `ctest` regression pass**
    (`cd build && ctest -C Debug --output-on-failure`). This is Checkpoint 1
    from `PHASE0`'s locked decision — do not skip it, do not substitute an
    incremental check here.
11. If the full rebuild or full test pass reveals a broken link/missing
    symbol, diagnose it — it almost certainly means Step 1's move-list
    missed a file, a Bucket A/B adapter file from Phases 4-7 was never
    added to `gte_editor`'s source list, or Step 4's rename missed a call
    site. Fix directly in this same phase (this is still "mechanical file
    movement plus one rename" territory, not new design).
12. Live smoke check: `run_app_background` the rebuilt executable, confirm
    it boots to the REAL Editor UI (docked ImGui panels, not a blank
    window — this is the concrete, visible symptom the ODR fix in Step 4
    prevents), `gte_send_request` a screenshot of the Game View, confirm
    rendering is visually unchanged from before this campaign started.

## Files Touched

- Root `CMakeLists.txt` (major restructuring)
- `tests/CMakeLists.txt`
- `src/Editor/EditorLayer.h` (add the `CreateNullEditorLayer()` declaration)
- `src/Editor/NullEditorLayer.cpp` (rename its definition to match)

## Definition of Done

- `gte_core` and `gte_editor` exist as two real, separate CMake targets.
- Exactly ONE definition of `gte::CreateEditorLayer()` exists in the entire
  repository (`ImGuiEditorLayer.cpp`), and exactly ONE definition of
  `gte::CreateNullEditorLayer()` exists (`NullEditorLayer.cpp`) — confirmed
  via `search_in_dir`.
- Full clean rebuild succeeds. Full `ctest` regression pass succeeds (100%
  pass rate, matching or exceeding the pre-campaign baseline pass count —
  record the exact before/after test counts in the completion report,
  mirroring how prior campaigns in this repo document e.g. "1753 tests,
  100% passing").
- Live boot + Game View screenshot confirms unchanged rendering, AND
  confirms the REAL ImGui Editor UI is what actually shows on screen (not a
  blank/no-op window).
- `PHASE9_COMPLETION_REPORT.md` (including the full before/after test
  counts) + git commit.

## Out of Scope

Do not touch `Window`/SDL inversion yet (Phases 10-11), do not extract
`Core`'s class body yet (Phases 12-13). `gte_core` still legitimately needs
SDL/ImGui-adjacent code inside it after this phase — that's expected and
handled next.
