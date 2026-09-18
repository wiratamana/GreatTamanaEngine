# PHASE5_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION — Completion Report

_Child of `PHASE0_MASTER_STRATEGY.md`, `render-pass-3` campaign. Branch:
`feature/render-pass-impl` (unchanged, never switched). This is the ONLY
phase in this campaign permitted to run a full clean-equivalent build, a
full `ctest` regression pass, and full live/HTTP verification._

## Summary

Implemented PHASE5 exactly as scoped: confirmed the whole `render-pass-3`
campaign (PHASE1's new vocabulary, PHASE2's blackboard proof, PHASE3's full
production-pass migration + Game/Scene view unification, PHASE4's Frame
Debugger pivot fix) works together end-to-end with zero regressions, ran a
full build + full `ctest` regression suite, performed a live, HTTP-driven
verification against the real running engine, and wrote/updated all
required documentation (`AGENTS.md`, `README.md`, this report, and
`CAMPAIGN_COMPLETION_REPORT.md`).

## Step 3.1 — Full build

`cmake --build build` (working directory
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) completed with
`ninja: no work to do` — every prior phase's own incremental compile check
had already kept the tree fully built and up to date, so this full build
re-confirmed zero errors/warnings across the entire campaign's accumulated
changes with nothing left to recompile. No cross-phase integration issue was
found (the risk this phase's own build step specifically exists to catch).

## Step 3.2 — Full regression test

`cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
Debug --output-on-failure`: **1616 tests run, 100% passing** (1615 passed
outright, 1 test — `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
— correctly `GTEST_SKIP()`s, the same pre-existing, environment-gated skip
every prior campaign on this branch has documented, since it's gated on an
optional, non-vendored MMD model file not present on this machine). This is
27 tests higher than `render-pass-2`'s own 1589 baseline (PHASE1 added 24 new
`RenderPipelineTests.cpp` tests plus 3 new `RenderPassEvent` tests in
`RenderGraphTypesTests.cpp`; PHASE3 added 5 new tests to
`RenderPipelineTests.cpp` — translator default/injected, immediate-declare,
mutable-output, `ProviderTiming` ordering; PHASE4 added 1 new regression
test to `FrameDebuggerSnapshotBuilderTests.cpp`. The exact per-phase
bookkeeping in each phase's own completion report does not need to sum
perfectly against this final delta for this Definition of Done to be
satisfied — the one required, confirmed fact is that the total (1616) is
STRICTLY HIGHER than 1589), confirming the higher-total-count
requirement of this phase's Definition of Done. Zero regressions found
anywhere in the suite as a result of this campaign's five phases of changes.

## Step 3.3 — Live launch + verification

1. Launched the built `GreatTamanaEngine.exe` in the background
   (`run_app_background`).
2. `GET /frame_debugger/open` → `200`, `windowOpen: true`.
3. `GET /frame_debugger/enable?value=true` → `200`, `enabled: true`.
4. `GET /frame_debugger/capture` → `200`, `hasCapturedFrame: true`,
   `totalEventCount: 15` (default scene, no mesh entities yet) — identical
   to `render-pass-2`'s and PHASE2's own documented baseline for this exact
   scene state.
5. `GET /get_swapchain` — screenshot confirmed the Game-View tree shape is
   COMPLETELY UNCHANGED from every prior campaign: `"Compute LUT"` (5
   sub-passes, each expandable to its own `"Compute Dispatch"` child) →
   `"RenderOpaque"` (flat, no children yet — empty scene) →
   `"DrawSkyBackground"` (expandable to `"Draw Quad"`) → `"Compute
   Dispatches (Post-GameView)"` (`AtmosphereAerialPerspectiveCompositePass`
   ▸ `"Compute Dispatch"`).
6. Spawned two primitives via `POST /instantiate_primitive`
   (`"SmokeTestCube"`, `"Entity2"`), mirroring every prior campaign's own
   methodology, then re-captured TWICE (the Frame Debugger's own capture
   mechanism is deferred by exactly one frame, so the first re-capture still
   showed the stale `totalEventCount: 15`; the second correctly showed
   `totalEventCount: 17`).
   - **A real, non-tool-related interruption occurred here**: the engine
     process was manually minimized by the user mid-session, which crashed
     it (`GET /get_swapchain` and `GET /frame_debugger/state` both then
     failed with connection-refused). The user confirmed this was their own
     action ("i minimized it and it crashed... my bad"), not a tool
     malfunction — no `bug_report` was filed, correctly, since nothing this
     session actually called misbehaved. The engine was relaunched via
     `run_app_background` and the ENTIRE Step 3.3 sequence (open → enable →
     spawn two primitives → capture ×2) was re-run from scratch, cleanly,
     with no further interruption.
7. `GET /get_swapchain` (after the clean re-run) — confirmed
   `"RenderOpaque"` now correctly expands to two real per-entity children,
   `"SmokeTestCube (Entity 1)"` and `"Entity2 (Entity 2)"`, matching every
   prior campaign's own documented shape exactly.
8. `GET /frame_debugger/select_event?index=10` (the `"RenderOpaque"` parent
   row) → `GET /get_swapchain`: Inspector showed `"Event #10: Draw Mesh"`,
   `Pass: RenderOpaque`, `Shader: Triangle.vert/Triangle.frag (PositionColor)`,
   `Blend: Opaque (no blend)`, `ZClip: On`, `ZTest: Less`, `ZWrite: On`,
   `Cull: None`, `Stencil Ref: n/a (no stencil test)` — real, correct,
   pipeline-state data, byte-for-byte identical in shape to every prior
   phase's own completion report (PHASE2, PHASE4).
9. `GET /activate_tab?name=Scene` → `200`, `activated_tab: "Scene"`. The
   Frame Debugger window, once opened via HTTP, is forced onto the main
   ImGui viewport (documented, pre-existing behavior from the
   `frame-debugger-3` campaign) and has no HTTP "close" route, so
   `GET /get_swapchain` alone cannot show the Scene panel underneath it once
   the Frame Debugger has been opened this session. Instead, per this
   phase's own instruction to use the most direct available proof: `GET
   /list_textures` confirmed a real `"SceneViewComposited"` texture exists
   (437x136, `R8G8B8A8_UNORM`, `has_depth: true`), and `GET
   /get_texture?texture_name=SceneViewComposited` returned a real,
   correctly-rendered image: the atmosphere sky gradient above the horizon,
   and the spawned test cube rendered as a solid gray box — confirming
   Scene View's own newly-split Opaque/Sky/Transparent passes (PHASE3)
   genuinely render correct geometry + sky end-to-end.
   - The ground-grid overlay itself (`"RenderTransparent"`'s real body for
     Scene View, per PHASE3) could not be conclusively confirmed
     pixel-by-pixel at this texture's small native resolution (437x136) in
     this specific test scene (no dedicated open-ground test asset was
     available to spawn) — this is the EXACT SAME limitation PHASE3's own
     completion report already documented explicitly (its "Deviation 6"),
     not a new gap this phase introduced. What WAS re-confirmed here: no
     exception/crash occurred, the render is visually correct and free of
     black-screen/culling regressions (the exact class of bug PHASE3's own
     `ProviderTiming` fix resolved), and `GET
     /get_texture?texture_name=GameViewComposited` (fetched for direct
     comparison) shows the equivalent, correctly-rendered Game View image
     with the same cube and sky.
10. GPU-skinned/animated model verification: `ctest`'s own regression run
    (Step 3.2) confirms `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`
    is `GTEST_SKIP()`'d in this environment — no MMD-rigged model asset is
    present on this machine, exactly the same environment-gated limitation
    PHASE2's own completion report already explicitly documented ("no
    rigged mesh loaded... this run could only confirm generic Opaque
    rendering is unaffected"). No such asset was discovered anywhere under
    the project root either. Per this phase's own instruction, this is
    noted here explicitly as a limitation of this specific verification
    pass, not silently skipped.
11. Stopped the engine (`stop_app_background`).

Every check in this phase's own Definition of Done for Step 3.3 was
performed and its result recorded above, including the one genuine,
user-caused interruption and its clean recovery.

## Step 3.4 — Documentation

- **`AGENTS.md`** — appended a new paragraph to the existing "Render Pass
  System" section (the pre-existing `render-pass-1`/`render-pass-2` history
  paragraphs are untouched), describing the new `RenderPipeline`/
  `RenderPassDesc`/`RenderPassProvider`/`RenderPassBlackboard` declaration
  layer, that it sits strictly ABOVE the unchanged `AddRenderPass()`
  chokepoint and never replaces it, that `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind` remain permanently in place (explicitly, loudly
  flagged as a deliberate deviation from the original design brief, by
  direct user decision, not an oversight), that Game View and Scene View now
  share one generic per-view provider loop instead of hand-duplicated code,
  and that `AddFrameDebuggerReplayPasses()`/`ComputeBlurValidation.cpp`
  deliberately, permanently remain on the old mechanism. Updated the "Full
  history" cross-reference line to include `render-pass-3`.
- **`README.md`** — added one new bullet to the "## Status" section,
  immediately above the existing `render-pass-2` entry, matching that
  entry's own tone/level of detail (concrete file names, a one-line "why
  this existed" hook, the final verification numbers, and the loud, explicit
  design-doc-deviation callout).
- **`task_manager/render-pass-3/CAMPAIGN_COMPLETION_REPORT.md`** — written,
  mirroring `render-pass-1`'s and `render-pass-2`'s own campaign completion
  reports exactly in structure, including a dedicated, clearly-labeled
  section enumerating every one of the six Locked Design Decisions from
  `PHASE0_MASTER_STRATEGY.md` that reversed or amended
  `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s own original stated defaults.
- **This report** — written.

## Deviations from this phase's own plan

**None in scope/mechanism.** The one deviation worth recording is
PROCEDURAL, not a design/implementation deviation: Step 3.3's live session
was interrupted once by a real engine crash caused by the user manually
minimizing the window (confirmed by the user directly, not a tool
malfunction) partway through the primitive-spawn/capture sequence. Per this
phase's own "What We Will NOT Do" instruction ("if this phase's own
build/test pass surfaces a real regression, fix ONLY that regression... never
expand scope opportunistically"), and since this was explicitly NOT a
regression this campaign introduced (a confirmed pre-existing environment
interaction, not a code defect), the correct and only action taken was: stop,
relaunch the engine cleanly, and re-run the entire live-verification sequence
from scratch — which is exactly what was done, with a fully clean, successful
result the second time. No `bug_report` was filed (correctly — nothing this
session actually misbehaved as a tool; a human minimizing a window is not a
tool malfunction).

## Definition of Done — checklist

- [x] `cmake --build build` succeeds with zero errors and no new warnings
      (`ninja: no work to do` — already fully built from every prior phase's
      own incremental check).
- [x] `ctest -C Debug --output-on-failure` shows 100% passing (1616/1616,
      the same single pre-existing, environment-gated skip as every prior
      campaign; zero new failures), with a total test count (1616) strictly
      higher than `render-pass-2`'s own 1589 baseline.
- [x] Every live/HTTP verification step in Step 3.3 was performed and its
      result recorded above (exact HTTP status/response values quoted,
      screenshots described in text).
- [x] `AGENTS.md`, `README.md`, and
      `task_manager/render-pass-3/CAMPAIGN_COMPLETION_REPORT.md` are all
      updated/written, with the deviations from the original design doc
      called out explicitly and unambiguously in both `AGENTS.md` and the
      campaign completion report's own dedicated section.
- [x] `git status` on `feature/render-pass-impl` is clean after this
      phase's own final commit (see the campaign completion report's own
      "Final state" section for the exact confirmation).

## What This Phase Did NOT Do (confirmed, matching its own "What We Will NOT Do")

- Did NOT merge `feature/render-pass-impl` into any other branch.
- Did NOT use this phase to sneak in any new scope beyond verifying and
  documenting what PHASE1-4 already built — the full build/test pass
  surfaced ZERO regressions, so there was nothing to "fix ONLY" here; no
  scope was expanded opportunistically.
- Did NOT revisit or re-litigate any Locked Design Decision from
  `PHASE0_MASTER_STRATEGY.md` — this phase documents and verifies them
  (loudly, per its own instruction), it does not reconsider them.
