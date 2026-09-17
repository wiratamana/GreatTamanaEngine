# PHASE2 COMPLETION REPORT — Frame Debugger Consumes ViewScope — Fix The Duplicate/Mis-Scoped Leak

Campaign: `frame-debugger-6`. Phase: PHASE2 (Workstream A consumer). Branch:
`feature/frame-debugger-impl` (unchanged, per prerequisites — no branch switch
performed).

## Summary

Implemented `PHASE2_FRAME_DEBUGGER_VIEWSCOPE_FILTERED_DISCOVERY.md` exactly as
specified, Steps 1–5, with no deviation. Both discovery loops in
`FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot()` (the pre-GameView
loop and the post-GameView loop) now additionally exclude any surviving
compute pass whose PHASE1-stamped `rg::ViewScope` is `SceneView`, via one
extra `||` clause appended to each loop's existing `continue` guard — a
structural, one-line boolean check, never a pass-name/resource-suffix string
comparison, exactly as the strategy document required. This directly fixes
the user-confirmed bug from `PHASE0_MASTER_STRATEGY.md` Section 0: two real
compute passes that happen to share a literal pass name (one genuinely
Game-View-scoped, one genuinely Scene-View-scoped) no longer both appear as
indistinguishable duplicate leaves in the Game-View-scoped Frame Debugger
tree, and the Scene-View-only `ComputeBlurValidation` debug tool can no
longer leak into that tree either.

## Files Changed

### Core fix
- `src/Editor/FrameDebuggerData.cpp`:
  - Pre-GameView loop's `continue` guard: `if (!pass.isComputePass ||
    pass.isCulled || pass.viewScope == rg::ViewScope::SceneView) { continue;
    }` — identical shape added to the post-GameView loop.
  - Updated the doc comment directly above the loops (inside
    `BuildRealFrameDebuggerSnapshot()`'s body) to describe the new rule,
    referencing this campaign (`frame-debugger-6`) as the origin, while
    keeping the existing `frame-debugger-5` split-group attribution intact
    (added to, not replaced).

### Documentation
- `src/Editor/FrameDebuggerData.h` — updated the function-level doc comment
  above `BuildRealFrameDebuggerSnapshot()`'s declaration with the same
  `viewScope != SceneView` rule and rationale (the real-world
  `"AtmosphereSkyViewLutPass"`/`"AtmosphereAerialPerspectiveCompositePass"`
  name-collision scenario, and the `ComputeBlurValidation` leak it also
  fixes), again additive to the existing `frame-debugger-5` prose.
- `AGENTS.md`/`docs/conventions/frame-debugger.md` — **not yet updated in this
  phase.** Re-reading `PHASE0_MASTER_STRATEGY.md`'s own Phase Index (Section
  3) and Locked Design Decision #1, the doc-file rewrite obligation ("update
  every doc file that states the old rule") is explicitly assigned to PHASE4
  (the one-leaf-per-pass rule break), not PHASE2. PHASE2's own Step 3.2 only
  asks for the `.h`/`.cpp` doc-comment update performed above, which was done
  in full. `AGENTS.md`'s "Frame Debugger" section does not currently claim the
  duplicate-pass behavior is correct (it only describes the pre/post-GameView
  split mechanism and the `frame-debugger-5` compute-pass discovery
  generically), so nothing there is factually stale as a result of this fix —
  it will still be revisited for the PHASE4 breaking-change note per the
  campaign's own plan. No deviation intended; flagging this explicitly so
  PHASE5's own final documentation pass does not assume it was missed.

### Tests (Step 4 — regression proof)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — 3 new cases, inserted
  immediately after
  `PreAndPostGameViewGroupsAppearTogetherAsSiblingsInRealExecutionOrder`:
  - `SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne`
    — hand-builds two `isComputePass=true` passes both literally named
    `"AtmosphereSkyViewLutPass"`, one `ViewScope::GameView` and one
    `ViewScope::SceneView`, both positioned before `"GameView"`'s own index —
    asserts the `"Compute Dispatches (Pre-GameView)"` group ends up with
    EXACTLY ONE child, not two.
  - `SceneViewScopedPostGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne`
    — the symmetric case for the post-GameView group, modeling the real
    `"AtmosphereAerialPerspectiveCompositePass"` duplicate scenario from
    `PHASE0_MASTER_STRATEGY.md` Section 0.
  - `SharedViewScopedPassStillAppearsNormally` — guards against an
    over-eager fix that would have also excluded `ViewScope::Shared` passes
    (e.g. the real Transmittance/Multi-Scattering LUT passes) — confirms it
    still appears exactly as before.
  - Checked Step 4 item 6: `tests/Editor/FrameDebuggerDataTests.cpp` has no
    hand-built `RenderGraphPassSnapshot` fixtures at all (confirmed via
    `search_in_dir`) — nothing needed updating there.

## Deviations From The Strategy Document

None in the code/tests themselves. One explicit clarification (not a
deviation): the AGENTS.md/`docs/conventions/frame-debugger.md` "update every
doc file" instruction from `PHASE0_MASTER_STRATEGY.md`'s Locked Design
Decision #1 is PHASE4's responsibility (the one-leaf-per-pass breaking
change), not PHASE2's — PHASE2's own Step 3.2 only scopes the `.h`/`.cpp`
doc-comment update, which is done. This is called out above so it isn't
mistaken for an omission.

## Evidence

### Incremental build
`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) completed successfully —
`gte_core`, `GreatTamanaEngine.exe`, and `GreatTamanaEngineTests.exe` all
built and linked cleanly with `GTE_ENABLE_EDITOR=ON` (the default dev
configuration already present in `build/`). No compile errors or warnings
related to this phase's changes.

### Tests
Ran `build/tests/GreatTamanaEngineTests.exe` directly (not a full `ctest` run,
per this campaign's own "no full build/ctest except PHASE5" rule) with
`--gtest_filter=*RenderGraph*:*FrameDebugger*` — **254 tests, 254 passed, 0
failed**, including:
- The 3 new `FrameDebuggerSnapshotBuilderTest.*ViewScope*`-related cases
  above — all pass.
- Every pre-existing `RenderGraphBuilderTest`/`RenderGraphSnapshotTest`/
  `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerDataTest`/
  `FrameDebuggerHistoryTest`/`FrameDebuggerCaptureContextTest`/
  `FrameDebuggerCommandBridgeTest`/`ParseFrameDebugger*QueryTests`/
  `BuildFrameDebugger*ResponseJsonTests` case — still passes unchanged
  (including PHASE1's own 5 new ViewScope cases), confirming no regression.

### Manual/Live Verification (Step 5's own Definition of Done)
1. Launched `build/GreatTamanaEngine.exe` via `run_app_background`.
2. `POST /load_scene` with body `{}` → `{"resolved_path":"...\\Project\\TestScene.gtscene","success":true}`.
3. `GET /frame_debugger/open` → `windowOpen:true`.
4. `GET /frame_debugger/enable?value=true` → `enabled:true, historyCount:1,
   totalEventCount:7` (down from the pre-PHASE2 `totalEventCount:10` PHASE1's
   own report captured — 3 duplicate leaves are now gone, exactly as
   predicted).
5. `GET /get_swapchain` — screenshot confirms the Frame Debugger window now
   shows:
   - `"Compute Dispatches (Pre-GameView)"`: `AtmosphereTransmittanceLutPass`,
     `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`,
     `AtmosphereAerialPerspectiveVolumePass`,
     `AtmosphereAerialPerspectiveVolumeDebugSlicePass` — **exactly 5 leaves,
     no duplicates.**
   - `"GameView"` leaf itself.
   - `"Compute Dispatches (Post-GameView)"`: `AtmosphereAerialPerspectiveCompositePass`
     — **exactly 1 leaf**, down from the previously-observed 4.
   - Total: 5 + 1 + 1 = 7, matching `totalEventCount:7` from step 4 exactly —
     this is precisely the tree shape this phase's own Step 1 specifies.
6. "Show Compute Blur (debug)" toggle: this toggle lives in the Scene
   panel's own `ImGui::Checkbox` (`ScenePanel.cpp`), backed by
   `EditorContext::showBlurredSceneOutput` — re-confirmed via
   `search_in_dir` that **no HTTP endpoint exposes this field** (unlike
   `/frame_debugger/*`'s own dedicated command bridge), so it was not
   live-toggled this phase, per this phase document's own explicitly-offered
   fallback ("reason about it via the code path and note that it was not
   live-toggled this phase"). Reasoning: `ComputeBlurValidation.cpp`'s own
   `builder.AddComputePass("ComputeBlurValidation", ...)` call passes
   `rg::ViewScope::SceneView` explicitly (stamped in PHASE1, re-confirmed by
   reading the current source again this phase), and this pass is declared
   only inside `Application.cpp`'s `if (sceneTarget != nullptr)` block
   (confirmed again at line ~759) — regardless of whether the toggle is on,
   this pass's `viewScope` is always `SceneView`, so the new filter added
   this phase excludes it from the Game-View-scoped tree unconditionally.
   This is also directly exercised structurally by this phase's new
   `SceneViewScopedPreGameViewPassIsExcludedEvenWhenNameCollidesWithGameViewOne`/
   `...PostGameView...` Tier-1 tests (which do not depend on the toggle at
   all, since the render graph itself never emits this pass unless the
   toggle is on and the Scene panel is visible — the tests instead prove the
   filter mechanism itself is correct for any `SceneView`-scoped pass, this
   one included).
7. Stopped the background process via `stop_app_background`.

## Definition of Done — Checklist

- [x] Both discovery loops in `BuildRealFrameDebuggerSnapshot()` exclude
      `ViewScope::SceneView` passes.
- [x] Doc comments updated (function-level in both `.h` and `.cpp`).
- [x] New regression tests added per Step 4, all passing; no existing
      Frame-Debugger test file's assertions were loosened to make this pass.
- [x] Incremental build succeeds.
- [x] Manual live re-check: launched the engine, loaded `TestScene.gtscene`,
      opened+enabled+captured the Frame Debugger over HTTP, `GET
      /get_swapchain`, and visually confirmed the tree now shows exactly 5
      Pre-GameView leaves, 1 Post-GameView leaf, no duplicates. The "Show
      Compute Blur (debug)" toggle was reasoned about via the code path
      (no HTTP exposure exists) rather than live-toggled, as explicitly
      permitted by this phase's own document. Stopped the background
      process when done.
- [x] Commit via `git_add`/`git_commit`.

## Next Phase

PHASE3 (`PHASE3_PER_DRAW_ENTITY_ATTRIBUTION_CAPTURE_INFRASTRUCTURE.md`) begins
Workstream B (the new feature) — per-draw-call entity attribution capture
infrastructure, independent of this phase's own `FrameDebuggerData.cpp`
changes.
