# render-pass-1 Campaign — Full Double-Check (2nd Iteration) Report

_Written before any C++ implementation of the `render-pass-1` campaign.
Scope: a full, skeptical re-review of ALL 8 strategy files
(`PHASE0_MASTER_STRATEGY.md` through `PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`),
per this task's own instructions, following on from the earlier,
PHASE4-focused `PHASE4_PRECHECK_REPORT.md`. No C++ code was changed — only
strategy documents. Stayed on the current branch throughout._

## What was read

- `README.md`, `AGENTS.md` (repo root).
- `PHASE4_PRECHECK_REPORT.md` first, as instructed, then every phase file in
  order: `PHASE0_MASTER_STRATEGY.md`, `PHASE1_RENDER_PASS_CORE_ABSTRACTION.md`,
  `PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`,
  `PHASE3_ATMOSPHERE_PASSES_MIGRATION.md`,
  `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`,
  `PHASE5_REMAINING_PASSES_MIGRATION.md`,
  `PHASE6_APPLICATION_ORCHESTRATION_CLEANUP_AND_DOCS.md`,
  `PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`.
- Real, current source, to independently re-derive (never just trust) every
  factual claim made across all 8 files:
  `src/Renderer/RenderGraph/RenderGraphTypes.h`,
  `src/Renderer/RenderGraph/RenderGraphBuilder.h`,
  `src/Renderer/RenderGraph/RenderGraphSnapshot.h`,
  `src/Application/RenderPasses.h`/`.cpp`,
  `src/Application/AtmospherePassSequence.h`,
  `src/Renderer/Atmosphere/AtmosphereLutRenderer.h`/`.cpp`,
  `src/Editor/FrameDebuggerData.h`/`.cpp`,
  `src/Editor/FrameDebuggerCapture.h`,
  `src/Game/RenderSystem.h`,
  `src/Editor/ComputeBlurValidation.cpp`,
  `src/Application/Application.cpp` (the real Game-View `build`-lambda call
  order, read directly, lines ~585-770),
  plus targeted `search_in_dir` sweeps for `AddGameViewPass`/
  `AddFrameDebuggerReplayPasses`/`AddAtmosphereCompositePass`/
  `AddAtmosphereViewLutPasses`/`.AddPass(`/`.AddComputePass(`/`isTransparent`/
  `CountGameViewDrawCommandsThisFrame`/`GetRegistry`/`GameView` across
  `src/` and `tests/`, and a check of `tests/Renderer/RenderGraph/` +
  `tests/Editor/` for every test file each phase references.

## Every factual claim checked against real source

Every concrete claim made across all 8 phase files about today's code was
independently re-derived from the real files, not trusted at face value:

- `PassRecord::isComputePass` (still a plain `bool`, not yet `PassKind`) and
  `PassRecord` having no `category` field yet — confirmed.
- `RenderGraphBuilder::AddPass()`/`AddComputePass()`'s exact 3-arg/4-arg
  (`ViewScope`) overload shapes — confirmed, no `AddRenderPass()` exists yet.
- `RenderGraphPassSnapshot::isComputePass`/`viewScope` (no `category` field
  yet) — confirmed.
- `AddGameViewPass()`'s exact current body (fused Opaque + Sky draw, the
  `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw()` hack, the
  `#if GTE_ENABLE_EDITOR` guard) — confirmed byte-for-byte against
  `RenderPasses.cpp`.
- `AddFrameDebuggerReplayPasses()`'s exact signature, its plain
  `builder.AddPass(passName, rg::ViewScope::GameView, setup, execute)` call
  (not yet `AddRenderPass()`) — confirmed.
- `AddGpuSkinningPasses()`'s 3-arg `builder.AddComputePass(request.name, ...)`
  call (no `ViewScope`) and `AddPresentPass()`'s 3-arg `builder.AddPass(
  "Present", ...)` call — confirmed.
- `AtmosphereLutRenderer.h`/`.cpp`: all 6 real `builder.AddComputePass(...)`
  call sites (Transmittance/MultiScattering/SkyView/AerialPerspectiveVolume/
  AerialPerspectiveComposite/AerialPerspectiveVolumeDebugSlice), their exact
  pass-name string literals (`"AtmosphereTransmittanceLutPass"`,
  `"AtmosphereMultiScatteringLutPass"`, `"AtmosphereSkyViewLutPass"`,
  `"AtmosphereAerialPerspectiveVolumePass"`,
  `"AtmosphereAerialPerspectiveCompositePass"`,
  `"AtmosphereAerialPerspectiveVolumeDebugSlicePass"`), and which ones take
  an explicit `viewScope` parameter vs. defaulting to `Shared` — every one
  matches PHASE3's text exactly, including the diagram names PHASE4 uses.
- `Application.cpp`'s real Game-View call order (read directly, lines
  590-717): `AddAtmosphereSharedLutPasses` → `AddAtmosphereViewLutPasses` →
  `AddAerialPerspectiveVolumeDebugSlicePass` → `AddGameViewPass` →
  (conditionally) `AddFrameDebuggerReplayPasses` → `AddAtmosphereCompositePass`
  — confirms PHASE4's precheck-era claim about the replay passes sitting
  structurally inside the "view region" walk, and confirms the Compute LUT/
  debug-slice passes really do sit BEFORE the `"GameView"`/`"RenderOpaque"`
  pivot, matching PHASE4's tree-shape assumption.
- `FrameDebuggerData.cpp`: the literal `FindPassByName(..., "GameView")`
  lookup, `BuildGameViewLeaf()`'s `passName = "GameView"`,
  `BuildGameViewDrawRecordLeaf()`'s two branches (`"GameView (Sky Draw)"` /
  `"GameView (Entity Draw)"`) — confirmed, matching PHASE4's rename plan
  exactly (→ `"RenderOpaque"` / delete / `"RenderOpaque (Entity Draw)"`).
- `FrameDebuggerCapture.h`: `FrameDebuggerDrawRecord::isSkyBackgroundDraw`,
  `RecordSkyBackgroundDraw()`, `DescribeSkyBackgroundPipelineState()` all
  exist exactly as described — confirmed.
- `RenderSystem.h`: no `CollectTransparentRenderables()` yet, no
  `isTransparent`/`renderQueue` concept anywhere on `MeshRenderer` — both
  confirmed absent (`search_in_dir` for `isTransparent|renderQueue` in
  `MeshRenderer.h` returned zero hits), matching PHASE0/PHASE2's claims.
  `Game::GetRegistry()` and `Game::CountGameViewDrawCommandsThisFrame()`
  both already exist publicly — confirms PHASE2's own precedent references.
- `AGENTS.md` has no existing `## Render Graph`/`## Render Pass System`
  top-level section (only passing mentions) — confirmed, matching PHASE6's
  own claim exactly.
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp` and
  `tests/Editor/{FrameDebuggerDataTests,FrameDebuggerSnapshotBuilderTests,
  FrameDebuggerCaptureTests}.cpp` all already exist as named — confirmed
  (browsed both directories). Two additional Frame Debugger test files exist
  (`FrameDebuggerHistoryTests.cpp`, `FrameDebuggerPreviewProcessingTests.cpp`)
  but a targeted grep confirms neither references `"GameView"`/
  `isSkyBackgroundDraw`/`RecordSkyBackgroundDraw` at all, so PHASE4's
  3-file test-update list is genuinely complete, not missing these two.

**Conclusion: every factual claim about current code, across all 8 files,
checked out correct.** No incorrect fact was found anywhere in the set.

## Real gaps found and fixed (this iteration)

Only `PHASE5_REMAINING_PASSES_MIGRATION.md` (and one cross-referencing
clarification in `PHASE0_MASTER_STRATEGY.md`) needed a substantive edit.
Everything else in the 8-file set was already correct and internally
consistent — see "Files judged already correct" below.

### 1. PHASE5's own "final grep audit" (Step 3.5) and Definition of Done were factually incomplete — two real, permanent survivors were never named

**The problem:** `search_in_dir` for the literal substrings `.AddPass(` and
`.AddComputePass(` across the whole `src/` tree today returns exactly 13
hits: 5 in `RenderPasses.cpp` (GameView, the replay-pass loop, SceneView,
Present, GpuSkinning), 1 in `ComputeBlurValidation.cpp`, 6 in
`AtmosphereLutRenderer.cpp`, and 1 in `ImGuiEditorLayer.cpp`. PHASE1-5's own
migration plan accounts for 11 of these 13 (every one PHASE2/PHASE3/PHASE4/
PHASE5 itself actually migrates). The remaining 2 were UNACCOUNTED FOR in
PHASE5's own audit text, which claimed "the only remaining hits should be
(a) `RenderGraphBuilder` itself, (b) its own tests" — a claim that, taken
literally, is FALSE and would have been confusing/misleading for whoever
executes PHASE5:

1. **`AddSceneViewPass()`'s own `"SceneView"` pass** (`RenderPasses.cpp`,
   `builder.AddPass("SceneView", rg::ViewScope::SceneView, ...)`) is
   DELIBERATELY, PERMANENTLY excluded from this whole campaign —
   `PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`'s own Step 3.3
   already locks this in ("Leave `AddSceneViewPass()` completely untouched
   by this phase"), and no later phase ever revisits it. This means it will
   ALWAYS show up as a "remaining hit" in PHASE5's own grep audit, forever,
   which PHASE5's original wording did not anticipate at all — an
   implementer following the audit's literal instructions ("if you find a
   remaining production hit this phase's own plan didn't already name,
   migrate it too") could easily have been led to wrongly "fix" this by
   migrating `AddSceneViewPass()` onto `AddRenderPass()`, a real,
   unrequested scope expansion directly contradicting PHASE2's own Locked
   scope decision.
2. **`ImGuiEditorLayer.cpp`'s `m_blurValidation.AddPass(builder, renderer,
   sceneViewHandle, m_sceneView.Sampler(), sceneExtent)`** is a FALSE-POSITIVE
   grep hit — it is a call to `ComputeBlurValidation::AddPass()`'s own,
   unrelated CLASS METHOD (mirroring `AtmosphereLutRenderer`'s own
   `AddXxxLutPass()` naming convention), not a raw
   `RenderGraphBuilder::AddPass()`/`AddComputePass()` call at all; the
   substring `.AddPass(` simply also matches this unrelated method name.
   PHASE5's own 3.4 already correctly migrates the REAL builder call
   INSIDE `ComputeBlurValidation::AddPass()`'s own body — but the audit
   text gave no warning that the call SITE (`m_blurValidation.AddPass(...)`)
   would also show up as a grep hit needing to be recognized and dismissed,
   not "fixed."

**Fix applied**: `PHASE5_REMAINING_PASSES_MIGRATION.md`'s Step 3.5 now
explicitly lists these two as named, expected, permanent carve-outs (c)/(d)
alongside the pre-existing (a)/(b) (`RenderGraphBuilder` itself / its own
tests), with the exact reasoning for each spelled out so no implementer
mistakes either for a bug to fix. The Definition of Done bullet was updated
to match (now explicitly says "no remaining hits ... EXCEPT the two known,
permanent carve-outs from 3.5(c)/(d) ... both expected and NOT bugs").
`PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision #3 ("Migration scope")
also gained one clarifying sentence making the same `AddSceneViewPass()`
exception explicit at the master-strategy level, so a reader who only skims
PHASE0 (not PHASE5's own Step 3.5 detail) is not misled into thinking "full
migration" literally means every `AddPass()` call site in the engine,
including Scene View's own.

This is the one genuine, confirmed cross-phase inconsistency this iteration
found and fixed — verified by directly running the same kind of
`search_in_dir` sweep PHASE5's own Step 3.5 instructs, against the real,
current (pre-implementation) source tree, and confirming both extra hits
are real and exactly as described above.

## Files judged already correct — no edit made (with reasoning per file)

- **`PHASE0_MASTER_STRATEGY.md`** — accurate and internally consistent
  except for the one clarifying sentence added above (Locked Design
  Decision #3). Its Step 1 target tree diagram, Step 2 situation facts, and
  Step 3 phase index all check out against real source and against every
  child phase's own text. Not otherwise touched.
- **`PHASE1_RENDER_PASS_CORE_ABSTRACTION.md`** — left completely unchanged.
  Every claim about `PassRecord`/`RenderGraphPassSnapshot`'s current shape
  is correct, the new `AddRenderPass()` chokepoint's 4-arg/3-arg overload
  signatures are exactly what every later phase (2/3/4/5) assumes and calls
  with (verified argument-order match across all 5 real call sites those
  phases describe), and its own test-file guidance ("extend an existing
  `RenderGraphTypesTests.cpp`") correctly anticipates that file already
  existing. No gap found.
- **`PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`** — left
  completely unchanged (also independently re-verified by the earlier
  PHASE4 pre-check pass, which reached the same conclusion). Its exact
  description of `AddGameViewPass()`'s current fused body, the
  `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`/
  `AddRenderTransparentPass()` split, and its own Locked ordering
  requirement (Opaque before Sky, for the `EQUAL` depth-test optimization)
  are all correct and consistent with PHASE4's later assumptions about the
  exact same pass names.
- **`PHASE3_ATMOSPHERE_PASSES_MIGRATION.md`** — left completely unchanged.
  Every one of its 6 named Atmosphere pass migrations matches a real,
  confirmed `builder.AddComputePass(...)` call site in
  `AtmosphereLutRenderer.cpp`, with the exact right `viewScope`
  presence/absence and the exact right `AtmosphereLut` vs. `General`
  category split (5 LUT-family passes tagged `AtmosphereLut`, the Composite
  pass tagged `General`) that PHASE4's tree rework later depends on. No gap
  found.
- **`PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`** — left completely
  unchanged this iteration; already thoroughly reworked by the dedicated
  `PHASE4_PRECHECK_REPORT.md` pass before this double-check began (a real,
  confirmed ordering gap re: `AddFrameDebuggerReplayPasses()` sitting inside
  the "view region" walk, fixed there with a concrete bounded algorithm, a
  pulled-forward `RenderPassCategory::Debug` tagging step, and a new
  required Tier-1 test). This iteration independently re-verified every one
  of that precheck's own claims against the real source (the exact
  `Application.cpp` call order, the exact `"GameView (Entity Draw)"` /
  `"GameView (Sky Draw)"` literals, `DescribeSkyBackgroundPipelineState()`,
  the 3-file test list) and found all of it still accurate — no further
  edit needed.
- **`PHASE5_REMAINING_PASSES_MIGRATION.md`** — EDITED this iteration (see
  above). Every other part of this file (the GPU Skinning/Present/Compute
  Blur Validation migration plans themselves, its correct deference to
  PHASE4 for the replay-pass migration) was already accurate.
- **`PHASE6_APPLICATION_ORCHESTRATION_CLEANUP_AND_DOCS.md`** — left
  completely unchanged. Its claim that `AGENTS.md` has no existing
  `## Render Graph`/`## Render Pass System` section was independently
  re-verified (a targeted `search_in_dir` for "Render Graph" in `AGENTS.md`
  returns only 3 passing in-sentence mentions, no section heading) and
  found correct. Its dead-code cleanup list (`AddGameViewPass()`,
  `isSkyBackgroundDraw`, `RecordSkyBackgroundDraw()`) correctly matches
  everything PHASE2/PHASE4 actually introduce/remove. No gap found.
- **`PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md`** — left
  completely unchanged. Its build/test/live-verification commands match
  `README.md`/this task's own stated environment exactly, and its
  Definition of Done correctly gates on PHASE0's own target tree shape.

## Cross-file consistency checks specifically requested by this task

All explicitly called out in the task description, and all confirmed
consistent:

- **`AddRenderPass()`'s exact signature** — PHASE1 defines
  `AddRenderPass(name, PassKind, ViewScope, RenderPassCategory, setup,
  execute)` (plus a 3-arg-defaulting overload). PHASE2, PHASE3, PHASE4
  (3.3b), and PHASE5 (3.1/3.2/3.4) each call it with this EXACT parameter
  order, verified line-by-line across all four files — zero drift.
- **PHASE2's new pass names vs. PHASE4's assumptions** — `"RenderOpaque"`/
  `"DrawSkyBackground"`/`"RenderTransparent"` are used identically,
  character-for-character, in PHASE0's diagram, PHASE2's plan, and PHASE4's
  tree-rework (including the per-entity child leaf rename to `"RenderOpaque
  (Entity Draw)"`, which PHASE4 explicitly calls out and PHASE2 explicitly
  defers to PHASE4).
- **PHASE4's target tree shape vs. PHASE0's own Step 1 diagram** — identical
  group ordering and naming (`Compute LUT` → `Compute Dispatches
  (Pre-GameView)` → `RenderOpaque` → `DrawSkyBackground` →
  `RenderTransparent` → `Compute Dispatches (Post-GameView)`), with PHASE4
  explicitly noting and resolving the one cosmetic root-label difference
  (`"GameView"` vs. real `"Game View"` with a space) rather than leaving it
  as a silent discrepancy.
- **PHASE6's cleanup list vs. what earlier phases actually introduce** —
  correctly scoped to exactly what PHASE2/PHASE4 add/remove; does not
  reference or attempt to clean up `AddSceneViewPass()` (correctly, since
  that pass is untouched by the whole campaign).

## Judgment calls / ambiguity

No genuine design ambiguity requiring `ask_questions` was hit during this
review. The one real finding (PHASE5's incomplete grep-audit carve-out list)
had a single, mechanically-derivable fix straight from re-running the exact
audit PHASE5 itself already specifies against the real source tree — nothing
guessed, nothing requiring a user decision.

## Files changed this iteration

- `task_manager/render-pass-1/PHASE5_REMAINING_PASSES_MIGRATION.md` —
  overwritten in place: Step 3.5's grep audit and the Definition of Done now
  explicitly name and explain the two permanent grep-audit carve-outs
  (`AddSceneViewPass()`'s own pass, and the `ImGuiEditorLayer.cpp`
  `m_blurValidation.AddPass(...)` false-positive).
- `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md` — overwritten in
  place: Locked Design Decision #3 gained one clarifying sentence stating
  the same `AddSceneViewPass()` exception at the master-strategy level.
- `task_manager/render-pass-1/PHASE1_RENDER_PASS_CORE_ABSTRACTION.md`,
  `PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md`,
  `PHASE3_ATMOSPHERE_PASSES_MIGRATION.md`,
  `PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md`,
  `PHASE6_APPLICATION_ORCHESTRATION_CLEANUP_AND_DOCS.md`,
  `PHASE7_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md` — left
  completely unchanged; each judged already correct and implementation-ready
  as-is, with reasoning given per file above.
- No C++ source under `src/`/`tests/` was changed — this was a strategy-
  document-only review, per this task's own rules. No new `.md` file was
  created.

## Recommendation for future work (not acted on here, per this task's rules)

None. This campaign's 7-phase scope (PHASE1-7) is complete and consistent as
planned; no additional phase is recommended at this time. `PHASE7`'s own
"What We Will NOT Do" already correctly defers real transparency, async
compute, and breakpoint-level stepping to genuinely separate future
campaigns.
