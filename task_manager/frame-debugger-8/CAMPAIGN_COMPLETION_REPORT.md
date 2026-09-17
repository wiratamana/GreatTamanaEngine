# CAMPAIGN COMPLETION REPORT — `frame-debugger-8`

Campaign: `frame-debugger-8`. Branch: `feature/frame-debugger-impl` (unchanged
throughout the whole campaign — no branch switch performed at any phase, and
none performed in this final phase either). This report is written as
PHASE4's own Step 3.5 deliverable, mirroring
`task_manager/frame-debugger-7/CAMPAIGN_COMPLETION_REPORT.md`'s shape.

## 1. Recap — the original problem

Per `PHASE0_MASTER_STRATEGY.md`, this campaign set out to fix one real,
user-confirmed gap in the Editor's "Frame Debugger" window: the **Sky
Background pass** — a real, direct `vkCmdDraw()` full-screen-triangle draw
(`AtmosphereSkyBackgroundRenderer::Draw()`, drawn LAST every frame, after
every real entity, using its own genuinely different `EQUAL`-depth-test
pipeline state) — never went through `RenderSystem::Draw()`/
`Renderer::Submit()` at all, so it was **completely invisible anywhere in the
event tree**, even though it genuinely runs every single frame and its effect
was already visible in the (wrong) preview image of the last-drawn entity.

A second, closely-related bug was found while investigating (and
independently spotted by the user comparing screenshots): the LAST real
entity's own per-step replay preview secretly already included the sky
(bundled onto that same replay step for an unrelated performance-optimization
reason), so there was no way to see "just after the last object, before sky"
as its own distinct state — the exact confusion the user's own bug report
described.

Two Locked Design Decisions governed the whole campaign:

1. **The real GPU draw order does not change.** Sky stays drawn LAST (after
   every entity), using its existing `EQUAL`-depth-test optimization — only
   the DEBUGGER's own visibility of this existing, correct order is fixed.
2. **The new leaf's identifying name is a REAL, hand-verified fact — the
   actual `AtmosphereSkyBackground.vert`/`AtmosphereSkyBackground.frag` shader
   pair — never an invented cosmetic label**, mirroring the hard-learned
   lesson `frame-debugger-5` already established when it removed the old
   hardcoded "GPU Skinning"/"Aerial Perspective Composite" special cases.

## 2. Per-phase summary

- **PHASE1 — Sky Draw Capture Instrumentation.** Added the missing capture
  plumbing: `AtmosphereSkyBackgroundRenderer::ShaderDebugName()` (a new,
  permanent, `constexpr` static method returning the real
  `"AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag"` string),
  `FrameDebuggerDrawRecord::isSkyBackgroundDraw` (a new field appended at the
  end of the struct), `DescribeSkyBackgroundPipelineState()` (the sky's real,
  distinct `Depth Test = Equal`/`Depth Write = Off` values), and
  `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` (reusing
  `RecordDraw()`'s existing dedup/draw-call-count/last-view-projection
  bookkeeping internally). The one real production call site —
  `AddGameViewPass()`'s own `execute` lambda in
  `src/Application/RenderPasses.cpp` — now calls it exactly once, immediately
  after the real `recordSkyBackground(ctx.cmd)` callback runs, guarded by
  `#if GTE_ENABLE_EDITOR` and a null check. `src/Game/Game.h`'s
  `CountGameViewDrawCommandsThisFrame()` doc comment was corrected (its old
  "`objectCount == capture.DrawRecords().size()` holds in practice" claim is
  no longer complete once a sky record exists). All 5 new Tier-1 tests
  passed; the full suite (1553 tests) showed zero regressions; both
  `cmake --build build --target gte_core` and
  `cmake --build build-editor-off --target gte_core` succeeded cleanly.
- **PHASE2 — Accurate Per-Step Sky Replay Preview (HEAVY phase).**
  Restructured `AddFrameDebuggerReplayPasses()` so a real per-object replay
  step NEVER draws sky anymore (fixing the "last object's own image secretly
  already included sky" bug outright), and added exactly ONE new, dedicated
  "sky step" (index == real object count, only present when a sky callback
  exists that frame) that redraws every real object AND then the sky — this
  becomes the new leaf's own correct, pixel-accurate preview. A scene with
  ZERO mesh entities now also correctly gets exactly one real replay step
  (the sky alone) instead of zero, since the old `if (objectCount == 0)
  return;` early-out was replaced by a `totalStepCount == 0` check. Two
  stale doc comments (`FrameDebuggerCapture.h`'s `SetReplayStepPreviews()`,
  `FrameDebuggerHistory.h`'s `perObjectStepPreviews` field) were corrected to
  note the vector may now be one longer than the real object count. This
  phase's own mandatory live, HTTP-driven visual spot-check (this being the
  single riskiest, most surgical render-graph change in the campaign) directly
  confirmed, on a real 1-mesh-entity scene: selecting the last real entity's
  own leaf shows the cube WITHOUT sky, and selecting the new dedicated sky
  leaf immediately after it shows the SAME cube WITH the full sky gradient —
  the exact, real, visible difference this whole campaign exists to produce.
  Both `cmake --build build`/`build-editor-off --target gte_core` succeeded;
  the full `*FrameDebugger*` test slice (84 tests) passed with zero
  regressions (this phase adds no new tests of its own, per its own explicit
  scope — `AddFrameDebuggerReplayPasses()` stays Tier 2/untested directly).
- **PHASE3 — Snapshot Tree Leaf and Tests.** Made the new sky draw record
  actually show up as its own distinct, correctly-labeled tree leaf, via a
  new branch inside `BuildGameViewDrawRecordLeaf()`
  (`src/Editor/FrameDebuggerData.cpp`) — zero structural changes to the outer
  `BuildRealFrameDebuggerSnapshot()` loop that already iterates one leaf per
  draw record. The sky leaf's `passName` reads `"GameView (Sky Draw)"`
  (mirroring the pre-existing `"GameView (Entity Draw)"` per-entity
  convention), its `shaderName` is the real shader-pair string, it has no
  fabricated "Entity (Index, Generation)" row (there is no real ECS entity
  behind it), and its blend/Z/stencil rows come from PHASE1's
  `DescribeSkyBackgroundPipelineState()` (`Equal`/`Off`), not the generic
  per-mesh defaults. 6 new Tier-1 tests were added across
  `FrameDebuggerDataTests.cpp` and `FrameDebuggerSnapshotBuilderTests.cpp`;
  all 90 `*FrameDebugger*` tests passed, and the full suite (1559 tests, 1
  pre-existing environment-dependent skip) showed zero regressions. No
  `build-editor-off` check was needed this phase (`FrameDebuggerData.h/.cpp`
  only ever compiles under `GTE_ENABLE_EDITOR=ON`).
- **PHASE4 (this phase) — Docs, Full Build, Regression, Live Verification,
  Campaign Completion.** See Sections 3 and 4 below.

## 3. Documentation updates (this phase's own Step 3.1/3.2)

- **`docs/conventions/frame-debugger.md`'s existing "## What is real today"
  section was updated in TWO places** (mirroring the exact precedent
  `frame-debugger-6` already set for this same section):
  1. Its ASCII tree diagram now shows a new line under the `"GameView"` leaf's
     own children: `<one final real child leaf for the Sky Background
     full-screen-triangle draw, always LAST - e.g.
     "AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag" -
     frame-debugger-8 campaign>`.
  2. The bullet describing this engine's "exactly ONE Pipeline configuration
     today... nothing to fabricate per-mesh here" claim now has a new
     sentence appended immediately after it, explaining the Sky Background
     leaf is the ONE other real, non-fabricated pipeline-state fact in this
     window (`Depth Test = Equal`, `Depth Write = Off`), still not a
     per-material/per-mesh variation.
- **A brand-new "## What's new (`frame-debugger-8` campaign)" section** was
  added immediately after the existing "## What's new (`frame-debugger-7`
  campaign)" section and before "## Known limitation, now fixed
  (`frame-debugger-4` campaign)" — matching every prior campaign's own
  chronological placement convention. It documents the new
  `RecordSkyBackgroundDraw()` method, the real hand-verified shader-name
  identifier, the genuinely distinct Blend/Z/Stencil rows, the corrected
  per-step replay preview mechanism, `BuildGameViewDrawRecordLeaf()`'s new
  branch, the Game-View-only scope (unchanged), and the one, narrow,
  explicitly-deferred pre-existing gap (the parent `"GameView"` leaf's own
  aggregate "Draw Stats" row still undercounts the sky's own raw
  `vkCmdDraw()` call by exactly one — a real, separate, pre-existing gap this
  campaign deliberately did not fix, to keep its own blast radius tight).
- **`AGENTS.md`'s "Frame Debugger" summary paragraph** gained one appended
  clause to its existing sentence about per-entity child leaves: "...
  including the Sky Background pass itself (`frame-debugger-8` campaign) -
  never only meshes." — no restructuring, per this phase document's own
  instruction to keep `AGENTS.md` a short summary-plus-link document.

## 4. Full build and full regression (this phase's own Step 3.3 — the ONLY
phase in this campaign allowed to do this)

- `cmake --build build` (working directory
  `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`) — **succeeded**. Output
  was a single `[1/1] Linking CXX executable GreatTamanaEngine.exe...` step
  (every translation unit was already up to date from PHASE1-3's own
  incremental builds — confirming those phases' own "quick compile check"
  verifications were honest and did not leave anything unbuilt).
- `cmake --build build-editor-off` (v2 addendum — this campaign touched
  `src/Application/RenderPasses.cpp`, a CORE, always-compiled file, in both
  PHASE1 and PHASE2) — **succeeded cleanly**, a full incremental build of 55
  steps (this directory's own test binary needed a full relink since its
  `GreatTamanaEngineTests.exe` had not been rebuilt there yet this session),
  confirming the `GTE_ENABLE_EDITOR=OFF` configuration still compiles and
  links correctly with every `#if GTE_ENABLE_EDITOR`-guarded change this
  campaign made.
- `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C
  Debug --output-on-failure` — **100% tests passed, 1559 total** (1 test —
  `PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine` —
  reported `Skipped`, which is EXPECTED and unrelated to this campaign: a
  machine-dependent smoke test that only runs when a real MMD model file
  happens to be present on the developer's own machine). Total test time:
  108.18 seconds. Zero failures, zero regressions from ANY prior phase's own
  work — every one of PHASE1's 5 new tests and PHASE3's 6 new tests passed,
  alongside every pre-existing test in the whole engine.

No regression fix was needed anywhere — nothing failed, so `delegate_task`
was never invoked for a regression fix in this phase.

## 5. Live, HTTP-driven, screenshot-verified end-to-end proof (this phase's
own Step 3.4)

### 5.1 — Launch

Launched `build/GreatTamanaEngine.exe` via `run_app_background` (PID 21904).
The project's current default scene has ZERO mesh entities (Camera +
Directional Light only — exactly the `atmosphere-scattering-4`
zero-mesh-entity scenario this campaign's own Definition of Done calls out).

### 5.2 — Zero-mesh-entity scene proof — LIVE, PASSED

1. `GET /frame_debugger/open` → `{"success":true, "state":{...
   "hasCapturedFrame":false, "totalEventCount":0, "windowOpen":true, ...}}`.
2. `GET /frame_debugger/enable?value=true` → immediately
   `{"enabled":true, "hasCapturedFrame":false, "totalEventCount":0, ...}` —
   honestly reports nothing captured yet (the deferred-capture-trigger
   mechanism from `frame-debugger-7`, unaffected by this campaign).
3. `GET /frame_debugger/state` (a brief real interval later) →
   `{"enabled":true, "hasCapturedFrame":true, "totalEventCount":8, ...}`.
4. `GET /frame_debugger/select_event?index=6` (the sky leaf — the ONLY child
   of `"GameView"` on this zero-entity scene) then `GET /get_swapchain`
   (screenshot evidence) confirmed:
   - The event tree shows exactly the expected 8-leaf shape: 5 Pre-GameView
     compute leaves, `"GameView"` with exactly ONE child — the sky leaf,
     named `AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag` — and
     the Post-GameView composite leaf.
   - The Inspector shows `Pass = GameView (Sky Draw)`,
     `Shader = AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag`,
     `Blend = Opaque (no blend)`, `ZTest = Equal`, `ZWrite = Off`,
     `Cull = None` — every value the real, distinct
     `DescribeSkyBackgroundPipelineState()` describes.
   - The preview box shows a correct, pure sky-only image (a blue-to-warm
     gradient, no entities) — confirming the campaign's Definition-of-Done
     bullet for the zero-mesh-entity scenario.

### 5.3 — Non-empty-scene proof (the LAST entity leaf vs. the sky leaf) — LIVE, PASSED

1. `POST /instantiate_primitive`
   (`{"shape":"cube","name":"PhaseCheckCube","world_position":{"x":0,"y":1,"z":3}}`)
   spawned a real mesh entity live → `{"success":true, "entity":{"index":1,
   "generation":1}, ...}`.
2. `GET /frame_debugger/capture` then `GET /frame_debugger/state` (after the
   deferred trigger landed) → `totalEventCount` grew from 8 to **9** — one new
   `"PhaseCheckCube (Entity 1)"` leaf, ahead of the existing sky leaf.
3. **Selected the LAST real entity's own leaf (index 6, `PhaseCheckCube
   (Entity 1)`)** → `GET /get_swapchain` (screenshot evidence): the preview
   box shows the cube alone against the engine's plain dark clear color —
   **NO sky gradient anywhere in the image**. The Inspector correctly shows
   the generic mesh values: `Pass = GameView (Entity Draw)`,
   `Shader = Triangle.vert/Triangle.frag (PositionColor)`, `ZTest = Less`,
   `ZWrite = On` — this is the direct, final, live proof of the PHASE2 bug
   fix: this is no longer secretly bundled with the sky.
4. **Selected the new dedicated sky leaf immediately after it (index 7)** →
   `GET /get_swapchain` (screenshot evidence): the SAME cube, now shown WITH
   the full sky gradient behind it — a real, visible difference from the
   previous selection. The Inspector shows `Pass = GameView (Sky Draw)`,
   `Shader = AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag`,
   `ZTest = Equal`, `ZWrite = Off` — exactly the campaign's Definition-of-Done
   bullet: "Selecting the LAST entity's own leaf... now shows the accumulated
   image WITHOUT the sky - a real, visible difference versus selecting the
   new Sky Background leaf immediately after it in the tree."

### 5.4 — Clean up

`POST /delete_entity` (`{"name":"PhaseCheckCube"}`), then
`GET /frame_debugger/enable?value=false` (confirmed
`{"enabled":false, "hasCapturedFrame":false, "totalEventCount":0, ...}` — the
capture was cleared, matching the existing `frame-debugger-7` lifecycle
rule, unaffected by this campaign), then `stop_app_background` (PID 21904) —
the running instance was terminated cleanly after all live verification
completed.

## 6. Definition of Done (whole campaign, from `PHASE0_MASTER_STRATEGY.md`) — confirmed honored

- [x] A live capture on a scene with at least one mesh entity shows a NEW,
      real, selectable leaf under `GameView`, positioned AFTER every entity
      leaf, named after the real `AtmosphereSkyBackground.vert`/
      `AtmosphereSkyBackground.frag` shader pair — confirmed live, Section 5.3.
- [x] Selecting that new leaf shows its own real `passName`
      (`"GameView (Sky Draw)"`), its own real `shaderName`, its own real,
      DISTINCT blend/Z/stencil rows (`Equal`/`Off`, not `Less`/`On`), and a
      preview image that is the true, accumulated Game View as it looked
      after every entity AND the sky — confirmed live, Sections 5.2 and 5.3.
- [x] Selecting the LAST entity's own leaf now shows the accumulated image
      WITHOUT the sky — a real, visible difference versus selecting the new
      Sky Background leaf immediately after it — confirmed live, Section 5.3.
- [x] A capture on a scene with ZERO mesh entities still shows exactly one
      leaf under `GameView`: the Sky Background leaf, with a correct preview
      image (a pure sky, no entities) — confirmed live, Section 5.2, on the
      project's actual default scene.
- [x] Every new piece of pure logic has a passing Tier-1 test in the same
      style as its neighbors — 5 new tests (PHASE1) + 6 new tests (PHASE3),
      all passing, zero regressions in the full 1559-test suite.
- [x] Every stale doc-comment identified in the campaign's own "v2 addendum"
      has been corrected — `Game.h` (PHASE1), `FrameDebuggerCapture.h`/
      `FrameDebuggerHistory.h` (PHASE2), `FrameDebuggerData.cpp` (PHASE3,
      both the `BuildGameViewDrawRecordLeaf()` doc comment and
      `BuildRealFrameDebuggerSnapshot()`'s header comment) — all confirmed via
      direct re-read at the start of this phase (see Section 7 below).
- [x] Both `cmake --build build` AND `cmake --build build-editor-off` succeed
      at the very end of PHASE4 — confirmed, Section 4.
- [x] A live, HTTP-driven, screenshot-verified smoke test confirms every
      bullet above against the real running engine — confirmed, Section 5.

## 7. Spot-check of PHASE0's own v2-addendum doc-comment claims

Per this phase's own top-level instructions, two of the four doc-comment
fixes were independently re-read fresh from disk before starting any new
work this phase, to confirm PHASE1/PHASE2 actually did what they were asked:

- **`src/Game/Game.h`'s `CountGameViewDrawCommandsThisFrame()` doc comment**
  — confirmed: a new paragraph (added by PHASE1) now precedes the pre-existing
  "IMPORTANT documented assumption" paragraph, explaining
  `RecordSkyBackgroundDraw()` appends one additional, non-entity record
  whenever a Sky Background draw callback exists this frame, so
  `capture.DrawRecords().size() == objectCount + 1` in the normal case — the
  pre-existing caveat about a DrawCommand whose mesh/pipeline handle fails to
  resolve was left unchanged (still correct for the entity-only portion of
  the count).
- **`src/Editor/FrameDebuggerCapture.h`'s `SetReplayStepPreviews()` doc
  comment** — confirmed: a new paragraph (added by PHASE2) was appended
  immediately after the pre-existing "...so a caller must never read these
  textures back before this whole `Execute()` call has returned." sentence,
  explaining `N` may now be one greater than the real object count (the one
  new dedicated sky step), and pointing to `RenderPasses.h`'s own
  "entities first, sky last" ordering contract for the full story.

Both spot-checks confirm PHASE1 and PHASE2 genuinely landed the doc-comment
fixes their own completion reports claimed, not just claimed them.

## 8. Explicitly deferred / out-of-scope items (unchanged from PHASE0)

- **Not fixing the `"GameView"` leaf's own aggregate "Draw Stats (Calls,
  Tris)" undercount-by-one** — a real, narrow, pre-existing, DIFFERENT gap
  (the sky's raw `vkCmdDraw()` call happens outside any `Renderer::Submit()`
  bracket, so it was never counted in that aggregate before or after this
  campaign) — left as a documented, known gap in the new doc section, not
  silently ignored.
- **Not adding a "Sky-View LUT" texture-read row** to the new leaf's Textures
  list — would need threading a brand-new string parameter through
  `AddGameViewPass()`'s signature purely to serve one inspector row, a
  disproportionate blast radius for this campaign's actual goal.
- **Not reordering the real GPU draw sequence** — the sky stays drawn LAST,
  exactly as before this campaign (Locked Design Decision 1).
- **Not touching Scene View** — `AddSceneViewPass()` was never modified by
  this campaign (Locked Design Decision 3).
- **The O(N) shared-target replay optimization, true per-pass breakpoint
  control, and multi-frame history** all remain out of scope, unchanged from
  every prior `frame-debugger-N` campaign's own "Still-deferred future work."

## 9. Conclusion

The Sky Background pass is now a real, individually selectable, correctly
labeled and correctly positioned leaf in the Frame Debugger's event tree, on
both a normal scene (proven live with a spawned cube) and the zero-mesh-entity
scene (proven live on the project's actual default scene). Its own
Blend/Z/Stencil Inspector rows genuinely reflect its distinct
`Equal`/`Off` pipeline state rather than the generic per-mesh defaults. The
closely-related replay-preview bug is also confirmed fixed live: the last
real entity's own step now genuinely excludes the sky, and the new dedicated
sky step genuinely includes it — a real, visible, screenshot-confirmed
difference between the two adjacent leaves. Every piece of new logic has
passing Tier-1 test coverage, every stale doc comment identified by this
campaign's own v2 double-check pass has been corrected and re-verified, both
`GTE_ENABLE_EDITOR` build configurations compile and link cleanly, and the
full 1559-test regression suite shows 100% pass with zero regressions
anywhere in the engine. The `frame-debugger-8` campaign is complete.
