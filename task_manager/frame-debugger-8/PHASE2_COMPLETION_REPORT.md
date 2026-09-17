# PHASE2 — Completion Report: Accurate Per-Step Sky Replay Preview (HEAVY PHASE)

_Reports to `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE2_ACCURATE_PER_STEP_SKY_REPLAY_PREVIEW.md` (v2, including its addenda)
in full._

## Summary

Restructured `AddFrameDebuggerReplayPasses()`
(`src/Application/RenderPasses.cpp`) exactly per the phase document's own
Step 3.1 "exact new shape" code, fixing the confirmed bug where the LAST
per-object replay step secretly already included the Sky Background draw
(making it impossible to see "every real object, but not yet sky" as its own
distinct state). The function now builds `totalStepCount = objectCount +
(includeSkyStep ? 1 : 0)` replay steps: every real per-object step `i` in
`[0, objectCount)` NEVER draws sky (redraws objects `[0..i]` only, exactly as
before), and exactly ONE new, dedicated "sky step" (index `objectCount`,
only present when a sky callback exists this frame) redraws every real
object AND then the sky - pixel-identical to the whole-frame `preview`. As a
natural side effect of replacing the old `if (objectCount == 0) return;`
early-out with the new `totalStepCount == 0` check, a scene with ZERO mesh
entities now also gets exactly one real replay step (the sky alone) instead
of zero.

The function's signature is byte-for-byte unchanged; the one call site
(`Application.cpp`, ~line 691) needed zero changes, confirmed by re-reading
it after the edit.

## What was done (maps 1:1 to the phase document's Step 3)

- **3.1 — `src/Application/RenderPasses.cpp`**: replaced
  `AddFrameDebuggerReplayPasses()`'s entire body with the phase document's own
  exact new shape - `includeSkyStep`/`totalStepCount` replacing the old
  `objectCount == 0` early return, both loops/reserves now bounded by
  `totalStepCount`, `isSkyStep`/`maxDrawCount` computed once per iteration
  outside the inner `execute` lambda and captured by value, and the old
  buggy `if (i + 1 == objectCount && recordSkyBackground)` branch fully
  replaced by `if (isSkyStep && recordSkyBackground)` (not merely moved -
  the old branch lived inside the per-object condition; the new one is keyed
  off a real, precomputed per-step boolean that is only ever true for the one
  dedicated sky step). Every pre-existing structural/precedent comment
  (`ReplayStepPassName()`'s own "never a per-call temporary" note, the
  `destinations.reserve()` "ESSENTIAL - pointer-stable" note, the
  `frameDebuggerCapture` "always nullptr here" correctness note) was kept
  verbatim alongside the new PHASE2-specific comments the phase document
  itself specifies. `ReplayStepPassName()`/`ReplayStepPassNamePool()` were not
  touched at all (already generically support any index). The `#else`
  unused-parameter-suppression branch is unchanged (same parameter list, no
  signature change).
- **3.4 (v2 addendum) — two stale doc-comment fixes**, landed in the same
  commit as 3.1:
  - `src/Editor/FrameDebuggerCapture.h` - appended the specified new sentence
    to `SetReplayStepPreviews()`'s own doc comment (immediately after "...so a
    caller must never read these textures back before this whole Execute()
    call has returned.", before the blank spacer line and the "IMPORTANT
    ordering requirement" paragraph), explaining `N` may now be one greater
    than the real object count.
  - `src/Editor/FrameDebuggerHistory.h` - appended the equivalent new sentence
    to `FrameDebuggerHistoryEntry::perObjectStepPreviews`'s own field doc
    comment, immediately before the field declaration itself.

## Deviations from the plan

None. The implementation is a literal, line-for-line match of the phase
document's own Step 3.1 code block (confirmed by direct comparison), and both
Step 3.4 doc-comment fixes were applied to the exact same paragraphs the
phase document identified, appending rather than restructuring, exactly as
instructed.

## Compile check results (this phase's own scope)

- `cmake --build build --target gte_core` — **succeeded** (8/8 steps, clean;
  only the files that actually depend on the changed headers/source
  recompiled: `FrameDebuggerCapture.cpp`, `RenderSystem.cpp`,
  `FrameDebuggerData.cpp`, `FrameDebuggerHistory.cpp`, `RenderPasses.cpp`,
  `FrameDebuggerPanel.cpp`, `ImGuiEditorLayer.cpp`, then link).
- `cmake --build build-editor-off --target gte_core` (v2 addendum) —
  **succeeded** (2/2 steps, clean — only `RenderPasses.cpp` recompiled, then
  link) — confirms this phase's edits, which stay entirely inside the
  pre-existing `#if GTE_ENABLE_EDITOR` block, compile and link cleanly with
  `GTE_ENABLE_EDITOR=OFF` too.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded (this
  phase adds no new tests of its own, per the phase document's own explicit
  statement — `AddFrameDebuggerReplayPasses()` needs a live
  `Renderer`/`RenderGraph` and stays Tier 2/untested directly). Ran the full
  Frame-Debugger test slice (`--gtest_filter=*FrameDebugger*`, 84 tests) as an
  extra regression check even though nothing in this phase touched
  Tier-1-tested code: **all 84 passed, zero regressions** — confirms PHASE1's
  new tests, and every pre-existing Frame Debugger test, are unaffected by
  this phase's `RenderPasses.cpp`-only change.
- Also built `cmake --build build --target GreatTamanaEngine` (the full
  executable) so the live spot-check below had an up-to-date binary to run —
  this is a normal targeted build of one runnable target needed to satisfy
  this phase's own mandatory live-verification step, not the full
  multi-target `cmake --build build` reserved for PHASE4.

## Live, HTTP-driven visual spot-check (this phase's actual verification)

Ran `build/GreatTamanaEngine.exe` in the background and drove the Frame
Debugger entirely over its HTTP routes, per the phase document's own
"Compile check for this phase" section:

1. **`GET /frame_debugger/open`** then **`GET /frame_debugger/enable?value=true`**
   — the project's current default scene has ZERO mesh entities (Camera +
   Directional Light only, exactly the `atmosphere-scattering-4`
   zero-mesh-entity scenario referenced by PHASE0). `GET
   /frame_debugger/state` confirmed `hasCapturedFrame: true`,
   `totalEventCount: 8`.
2. **`GET /get_swapchain`** showed the full event tree for this zero-entity
   scene:
   ```
   Game View
     |-- Compute Dispatches (Pre-GameView)
     |     |-- AtmosphereTransmittanceLutPass         (index 0)
     |     |-- AtmosphereMultiScatteringLutPass       (index 1)
     |     |-- AtmosphereSkyViewLutPass                (index 2)
     |     |-- AtmosphereAerialPerspectiveVolumePass   (index 3)
     |     |-- AtmosphereAerialPerspectiveVolumeDebugSlicePass (index 4)
     |-- GameView                                      (index 5)
     |     |-- AtmosphereSkyBackground.vert/...frag     (index 6, the new sky
     |     |                                             draw record from
     |                                                   PHASE1, already
     |                                                   visible as a generic
     |                                                   per-draw-record leaf
     |                                                   even before PHASE3's
     |                                                   own dedicated
     |                                                   labeling lands)
     |-- Compute Dispatches (Post-GameView)
           |-- AtmosphereAerialPerspectiveCompositePass (index 7)
   ```
   Selecting index 5 (`GameView` itself) and index 6 (the sky draw leaf)
   both showed the identical accumulated sky image (a real, honest "just the
   sky" replay image on a scene with zero mesh entities, matching PHASE0's
   own Definition of Done bullet for this exact scenario — `totalStepCount ==
   1`, `isSkyStep == true` for `i == 0 == objectCount`,
   `maxDrawCount == objectCount == 0`).
3. **Spawned a real mesh entity live** via `POST /instantiate_primitive`
   (`{"shape":"cube","name":"SpotCheckCube","world_position":{"x":0,"y":1,"z":0}}`)
   to also prove the fix on a non-empty scene, then `GET
   /frame_debugger/capture` to take a fresh capture (`totalEventCount` grew
   to 9 — one extra `"SpotCheckCube (Entity 1)"` leaf under `GameView`,
   ahead of the sky leaf).
   - **Selected the LAST real entity's own leaf (index 6, `SpotCheckCube
     (Entity 1)`)** — `GET /get_swapchain` showed the cube rendered against
     the engine's plain dark clear color (RGB 20,20,30), with **NO sky
     gradient or mountain silhouette anywhere in the image** — this is the
     actual bug fix, live-confirmed: before this phase, this exact selection
     would have shown the cube WITH the sky already composited behind it.
   - **Selected the new dedicated sky leaf immediately after it (index 7)**
     — `GET /get_swapchain` showed the SAME cube, now WITH the full sky
     gradient/mountain silhouette behind it — a real, visible difference from
     the previous selection, confirming the sky is now isolated onto its own
     distinct step.
   - Both screenshots were visually compared pixel-region-by-region (cube
     position/shape identical in both; only the background differs) —
     exactly the Definition-of-Done bullet: "Selecting the LAST entity's own
     leaf... now shows the accumulated image WITHOUT the sky - a real,
     visible difference versus selecting the new Sky Background leaf
     immediately after it in the tree."
4. Re-read `Application.cpp` (~line 691) after the edit to confirm its own
   call site truly needed zero changes — confirmed: `objectCount`/
   `recordSkyBackground`/`*gameTarget`/`*frameDebuggerCapture` arguments are
   byte-for-byte unchanged, and the existing "every returned destination
   TextureHandle MUST be appended to `outputs`" loop already handles however
   many handles `AddFrameDebuggerReplayPasses()` returns (previously always
   `objectCount`, now `totalStepCount`) with no changes needed.
5. Cleaned up: `POST /delete_entity` (`SpotCheckCube`),
   `GET /frame_debugger/enable?value=false`, then `stop_app_background`.

**Both Definition-of-Done bullets requiring a live scene with mesh entities
were verified directly against the real running engine — not just reasoned
about from the code — matching this phase's own instruction that the live
spot-check is the actual verification bar for this Tier-2 render-graph
change.**

## Definition of Done (this phase only) — verified

- [x] `AddFrameDebuggerReplayPasses()`'s signature is unchanged; its body
      matches Step 3.1 exactly (confirmed by direct line-for-line comparison
      against the phase document).
- [x] A real per-object step NEVER draws sky — confirmed live: selecting the
      last real entity's own leaf (`SpotCheckCube (Entity 1)`) shows no sky.
- [x] Exactly one dedicated sky step exists whenever `recordSkyBackground` is
      truthy, drawing every real object then the sky — confirmed live:
      selecting the sky leaf shows the cube WITH sky.
- [x] A zero-entity scene still produces exactly one real replay step (the
      sky step alone) — confirmed live on the project's actual default scene
      (Camera + Directional Light, zero mesh entities).
- [x] `Application.cpp`'s own call site needed zero changes — confirmed by
      re-reading it after this phase's edit.
- [x] Both stale doc comments (Step 3.4) are fixed
      (`FrameDebuggerCapture.h`/`FrameDebuggerHistory.h`).
- [x] `cmake --build build-editor-off --target gte_core` succeeds (v2
      addendum).

## Deviation / observation for the record (not a defect)

None discovered beyond what PHASE0/PHASE1 already anticipated. One purely
observational note for PHASE3: the sky draw record already appears as a
generic, selectable per-draw-record leaf under `GameView` even before
PHASE3 lands (visible in the spot-check above as
`"AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag"`), because
`BuildGameViewDrawRecordLeaf()`'s existing, unchanged loop already builds one
leaf per `FrameDebuggerCaptureContext::DrawRecords()` entry regardless of
`isSkyBackgroundDraw` — its `passName` currently reads the generic
`"GameView (Entity Draw)"` (not yet the distinct `"GameView (Sky Draw)"`),
and its Blend/Z/Stencil rows currently read the generic
`DescribeStandardPipelineState()` values (`ZTest = Less`, `ZWrite = On`, both
visible in the spot-check screenshots above) rather than PHASE1's own
`DescribeSkyBackgroundPipelineState()` (`Equal`/`Off`) — exactly as expected,
since wiring the sky-specific branch into `BuildGameViewDrawRecordLeaf()` is
explicitly PHASE3's own job, not this phase's. This is not a bug in PHASE2's
own scope; it is stated here only so PHASE3's own implementer has a
confirmed, live "before" baseline to compare against once its own leaf-naming
and pipeline-state branch lands.

## Notes for PHASE3 (next phase)

- The cross-phase invariant holds exactly as documented end-to-end, now
  confirmed LIVE (not just by code reading): `capture.DrawRecords()` and
  `AddFrameDebuggerReplayPasses()`'s own `destinations`/`destHandles`
  ordering agree index-for-index — entity leaf at index 6, sky leaf at index
  7, in a 1-mesh-entity scene captured during this phase's own spot-check.
- PHASE3's own job (`BuildGameViewDrawRecordLeaf()` in
  `src/Editor/FrameDebuggerData.h/.cpp`) can now proceed with confidence: the
  underlying per-step image data this phase produces is already correct and
  pixel-verified; PHASE3 only needs to make the sky leaf's own `passName`/
  `shaderName`/blend-Z-stencil rows use the real, distinct facts
  (`"GameView (Sky Draw)"`, `DescribeSkyBackgroundPipelineState()`) instead of
  the generic per-entity-draw defaults currently shown (see "Deviation /
  observation for the record" above for the exact current, generic values
  PHASE3 will be replacing).
- No blockers, no discovered deviations requiring a design re-think. This
  phase's scope was completed exactly as specified in the v2 phase document,
  and its live spot-check found no new bugs (unlike `frame-debugger-7`
  PHASE3, which caught two real bugs during its own equivalent spot-check —
  none were found here).
