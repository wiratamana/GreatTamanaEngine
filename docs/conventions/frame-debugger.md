# Frame Debugger

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The `frame-debugger-2` campaign
(`task_manager/frame-debugger-2/PHASE0_MASTER_STRATEGY.md`, seven phases) gave
the Editor a new **"Frame Debugger"** window, visually modeled on Unity's own
Frame Debugger, as GUI-only scaffolding. The `frame-debugger-3` campaign
(`task_manager/frame-debugger-3/PHASE0_MASTER_STRATEGY.md`, eight phases) then
wired REAL data into every one of that scaffolding's seams: real pass-level
frame capture, a real multi-frame history ring buffer, real shader/blend/Z/
stencil/texture/vector/matrix property reflection, a real Channels/Levels
preview-compositing pipeline, and full HTTP automation. A follow-up campaign,
`frame-debugger-4` (`task_manager/frame-debugger-4/PHASE0_MASTER_STRATEGY.md`,
three phases), then fixed a real, confirmed bug: the retained preview this
window ever showed was always the PRE-atmosphere-composite image, and the real
atmosphere-compositing pass was entirely invisible in the event tree. A second
follow-up campaign, `frame-debugger-5`
(`task_manager/frame-debugger-5/PHASE0_MASTER_STRATEGY.md`, five phases), then
fixed a second, separately-confirmed bug: only 3 of the 8+ real compute-shader
dispatches this engine actually issues (GPU Skinning, and one single
hardcoded `"AtmosphereAerialPerspectiveCompositePass"` special case) ever
appeared anywhere in the event tree at all — every Atmosphere LUT pass
(Transmittance/Multi-Scattering/Sky-View), the Aerial Perspective Volume pass,
the Aerial Perspective Volume Debug-Slice pass, and Compute Blur Validation's
own pass were completely invisible, regardless of whether they ran that frame
— see "Known limitation, now fixed (`frame-debugger-5` campaign)" below for the
full story. A third follow-up campaign, `frame-debugger-6`
(`task_manager/frame-debugger-6/PHASE0_MASTER_STRATEGY.md`, five phases), gave
`"GameView"` real, individually selectable per-entity child leaves — see
"What's new (`frame-debugger-6` campaign)" below. A fourth follow-up campaign,
`frame-debugger-7` (`task_manager/frame-debugger-7/PHASE0_MASTER_STRATEGY.md`,
seven phases), then fixed TWO more real, confirmed bugs (a wrong-first-capture
timing bug, and the preview box never actually showing "the screen as it
looked right after THIS object was drawn") via two explicit, user-approved
BREAKING CHANGES — removing the multi-frame history ring buffer entirely, and
replacing the per-compute-pass distinct-texture preview with a unified,
genuinely per-step accumulated-image mechanism — see "What's new
(`frame-debugger-7` campaign)" below for the full story. **This is the
CURRENT, real system** — follow these rules whenever touching this window,
its data model, its capture instrumentation, or its cross-wiring with the
Pause/Resume toolbar or the embedded HTTP server:

## What's new (`render-pass-1` campaign)

The `render-pass-1` campaign
(`task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md`, seven phases) is not a
Frame-Debugger-focused bug-fix campaign like every one above — it is a
broader, engine-wide refactor of HOW every render/compute/blit operation in
this engine declares itself to the Render Graph (a new, uniform
`RenderGraphBuilder::AddRenderPass()` chokepoint, replacing the old ad-hoc
`AddPass()`/`AddComputePass()` free-function sprawl — see
[the top-level `AGENTS.md`](../../AGENTS.md#render-pass-system) for the
engine-wide summary). Two of its phases (PHASE2/PHASE4) directly, and
deliberately, change THIS window's own tree shape — an explicit, user-approved
BREAKING CHANGE to the tree shape every earlier campaign above (`frame-
debugger-2` through `frame-debugger-9`) documented:

- **The old monolithic `"GameView"` pass no longer exists at all.** It used to
  both draw every real entity AND, immediately afterward, inside the exact
  SAME `vkCmdBeginRendering`/`vkCmdEndRendering` bracket, hand-fuse in the Sky
  Background full-screen-triangle draw — visible in this tree only via the
  `frame-debugger-8` campaign's own `FrameDebuggerDrawRecord::
  isSkyBackgroundDraw` flag / `FrameDebuggerCaptureContext::
  RecordSkyBackgroundDraw()` bridge call (described further down this file,
  in the `frame-debugger-8` section — kept as accurate PAST-tense history,
  not current behavior). `render-pass-1`'s own PHASE2 split this single pass
  into THREE real, separate `RenderGraphBuilder` passes, declared back-to-back
  against the exact same imported `"GameView"` render-target handle (the
  TEXTURE name — completely unchanged, still whatever the "Game" panel/
  `GET /get_game_view` ultimately reads from):
  - **`"RenderOpaque"`** — the built-in default mesh-drawing pass. This is
    simply the old `"GameView"` leaf, RENAMED, with the exact same real
    per-entity child-leaf mechanism `frame-debugger-6` already gave it
    (unchanged — see below).
  - **`"DrawSkyBackground"`** — a REAL, separate, individually selectable
    leaf now, with no fabricated draw-record hack behind it at all. `LOAD`s
    both the color and depth attachments (rather than clearing them) so it
    only paints pixels `"RenderOpaque"` didn't already cover, relying on
    `AtmosphereSkyBackgroundRenderer`'s own `EQUAL`-depth-test pipeline
    against the depth buffer `"RenderOpaque"` just wrote.
  - **`"RenderTransparent"`** — a genuine, currently ALWAYS-EMPTY scaffold
    pass. There is still no transparency concept anywhere in this engine's
    ECS/render pipeline (`MeshRenderer` has no `isTransparent`/`renderQueue`
    field yet), so this pass never actually declares anything and never
    appears as a tree leaf today — a clean, real, already-wired drop-in point
    for a genuine future transparency campaign, not a speculative
    implementation of transparency itself.
- **A new `"Compute LUT"` tree group** now sits ahead of, and separate from,
  the pre-existing generic `"Compute Dispatches (Pre-GameView)"` group: the
  Atmosphere Transmittance/Multi-Scattering/Sky-View/Aerial-Perspective-Volume
  LUT compute passes (tagged the new `RenderPassCategory::AtmosphereLut`) are
  pulled out of that old, unlabeled generic bucket into their own clearly-
  named heading, in real execution order, always presented FIRST regardless
  of real interleaving with any other pre-view compute pass. Non-atmosphere
  pre-view compute passes (e.g. GPU Skinning) stay exactly where they always
  were, in the generic `"Compute Dispatches (Pre-GameView)"` SIBLING group,
  unmerged with `"Compute LUT"`.
- **`BuildRealFrameDebuggerSnapshot()`'s hardcoded `"GameView"`-literal pivot
  search is gone.** It now looks up `"RenderOpaque"` instead — the ONE
  remaining hardcoded pass-name string literal anywhere in that function —
  and walks forward from there (a new "view region" walk) collecting every
  surviving, non-`SceneView`, non-`Debug`-category `Graphics`-kind pass it
  finds as a real sibling tree leaf (today: `"RenderOpaque"` itself, then
  `"DrawSkyBackground"`, then `"RenderTransparent"` whenever it is ever
  real), stopping at the first surviving `Compute`-kind pass — replacing the
  old single hardcoded `"GameView"` leaf lookup outright. This walk also
  correctly SKIPS `AddFrameDebuggerReplayPasses()`'s own N debug-only replay
  passes (tagged `RenderPassCategory::Debug`), which sit structurally inside
  this exact index range on an explicit capture-trigger frame — they never
  leak into the tree as spurious extra leaves.
- **The current, full, as-shipped tree shape is documented below** (see "What
  is real today" immediately below). No preview-picking rule, no Channels/
  Levels behavior, no HTTP route, and no per-entity child-leaf mechanism
  changed as part of this campaign — only the PASS-LEVEL shape around
  `"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` did.

See `task_manager/render-pass-1/PHASE0_MASTER_STRATEGY.md` and each
`PHASEn_COMPLETION_REPORT.md` in that same folder for the full seven-phase
writeup, including PHASE4's own live, HTTP-driven, screenshot-verified proof
of this exact new tree shape end-to-end.

## What is real today

- **Capture is genuinely real, and PASS-LEVEL for every pass EXCEPT
  `"RenderOpaque"` itself — `"RenderOpaque"` now ALSO gets one real,
  individually selectable CHILD leaf per real per-entity draw call it issued
  that frame** (`frame-debugger-6` campaign, PHASE4 — see "What's new
  (`frame-debugger-6` campaign)" below; renamed from `"GameView"` by the
  `render-pass-1` campaign's own PHASE2/PHASE4 — see "What's new
  (`render-pass-1` campaign)" above). This is an explicit, user-approved
  BREAKING change to the historical "one leaf per PASS, never one leaf per
  mesh/entity" rule `frame-debugger-2`'s own `PHASE0_MASTER_STRATEGY.md`
  originally locked (its own Locked Design Decision #1) — do not mistake the
  now-per-entity `"RenderOpaque"` children for a mistake or a regression;
  every OTHER pass in this tree (every compute dispatch, `"DrawSkyBackground"`,
  and `"RenderOpaque"` itself as a pass-level row) remains exactly one leaf
  per pass, unchanged.
- **Every real compute-shader dispatch that ran this frame is now a
  first-class, AUTOMATICALLY DISCOVERED tree citizen — never a hand-maintained
  per-pass-name special case** (`frame-debugger-5` campaign). The
  `render-pass-1` campaign's own PHASE4 rewrote this discovery to build the
  WHOLE tree (not just the compute groups) generically from real `PassKind`/
  `RenderPassCategory`/`ViewScope`/execution-order metadata (see "What's new
  (`render-pass-1` campaign)" above). The tree is:

  ```
  "Game View" (root)
    |-- "Compute LUT"                          (only present if >=1 child)
    |     |-- <every surviving AtmosphereLut-category compute pass BEFORE
    |     |     "RenderOpaque"'s own execution-order index, in real
    |     |     execution order - e.g. AtmosphereTransmittanceLutPass,
    |     |     AtmosphereMultiScatteringLutPass, AtmosphereSkyViewLutPass,
    |     |     AtmosphereAerialPerspectiveVolumePass>
    |-- "Compute Dispatches (Pre-GameView)"    (only present if >=1 child)
    |     |-- <every surviving, NON-AtmosphereLut-category compute pass BEFORE
    |     |     "RenderOpaque"'s own execution-order index - e.g. every
    |     |     GPU-skinning dispatch request active this frame>
    |-- "RenderOpaque" leaf                     (the built-in mesh-drawing pass)
    |     |-- <one real child leaf PER real per-entity draw call this pass
    |     |     issued this frame - e.g. "terrain (Entity 2)",
    |     |     "SmokeTestCube (Entity 3)" - frame-debugger-6 campaign, PHASE4>
    |-- "DrawSkyBackground" leaf                 (a REAL, separate, individually
    |                                             selectable leaf - no more
    |                                             isSkyBackgroundDraw hack)
    |-- "RenderTransparent" leaf                 (only ever appears once this
    |                                             currently-always-empty
    |                                             scaffold pass is real - never
    |                                             today)
    |-- "Compute Dispatches (Post-GameView)"    (only present if >=1 child)
          |-- <every surviving compute pass AFTER the "RenderOpaque"/
          |     "DrawSkyBackground"/"RenderTransparent" view region - e.g.
          |     AtmosphereAerialPerspectiveCompositePass>
  ```

  Concretely, on a typical frame with a Sun light present: `"Compute LUT"`
  holds `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass`,
  `AtmosphereSkyViewLutPass`, `AtmosphereAerialPerspectiveVolumePass`;
  `"Compute Dispatches (Pre-GameView)"` holds every GPU-skinning dispatch
  request active that frame (`RenderPasses.cpp`'s `AddGpuSkinningPasses()`,
  only present at all when at least one is active); and `"Compute Dispatches
  (Post-GameView)"` holds `AtmosphereAerialPerspectiveCompositePass` (and, if
  the Editor's own "Show Compute Blur (debug)" toggle is on — a genuinely
  SceneView-scoped tool, excluded from this Game-View-only tree by its own
  `ViewScope`/`RenderPassCategory::Debug` tagging — it never appears here).
  Every group is only ever added at all when it has at least one real
  surviving child this frame (never an empty, misleading group) — a single
  unconditional group placed after the view region would misrepresent which
  passes really ran before it (e.g. `"DrawSkyBackground"` samples the
  Sky-View LUT, so that LUT pass must already have run). Discovery is fully
  generic: `RenderGraphBuilder::AddRenderPass()` (the `render-pass-1`
  campaign's own unified chokepoint, replacing the old separate `AddPass()`/
  `AddComputePass()` free functions) is the one real "choke point" every
  render/compute/blit operation in this engine now funnels through, and it
  stamps a real, structurally-tracked `RenderGraphPassSnapshot::kind`
  (`rg::PassKind::Graphics`/`Compute` — RENAMED from the older plain `bool
  isComputePass` by this same campaign's own PHASE1) plus a
  `RenderGraphPassSnapshot::category` (`rg::RenderPassCategory::General`/
  `AtmosphereLut`/`GpuSkinning`/`Debug`) on every pass — `FrameDebuggerData.cpp`'s
  `BuildRealFrameDebuggerSnapshot()` walks every surviving (`isCulled ==
  false`), non-`SceneView` pass in the current frame's real
  `RenderGraphSnapshot` and routes it into the correct group/leaf purely from
  this real, structural metadata — a future compute (or graphics) pass added
  anywhere in this engine appears here automatically, with **zero further
  Frame-Debugger-specific code ever required for it to show up.** A
  compute-dispatch leaf's `passName`/`shaderName`/tree-row text are all the
  pass's own RAW, real name (never a fabricated friendly label like the old,
  long-removed `"GPU Skinning"`/`"Aerial Perspective Composite"` special
  cases used to invent), and every read/write row is labeled by its own real
  `ResourceKind` (`"Read Texture"`/`"Read Buffer"`/`"Read Volume Texture"`/
  `"Write Texture"`/`"Write Buffer"`/`"Write Volume Texture"`, from
  `RenderGraphPassSnapshot::readKinds`/`writeKinds`) — never mislabeled,
  never guessed by probing multiple registries. A culled pass never appears
  anywhere in the tree (it did not really run this frame).
- **Capture is snapshot-ON-DEMAND, never continuous — and, as of
  `frame-debugger-7`, genuinely DEFERRED by exactly one frame.** A "frame" is
  captured once a real trigger fires (Enable's false→true edge, a Step while
  Enabled, or the explicit "Capture" button/HTTP route) — but the actual
  capture work now runs on the NEXT `Build()` call, not synchronously inside
  the trigger's own click handler, so it is always built from a frame whose
  rendering had the capture context correctly armed for its ENTIRE duration
  (the Bug 1 fix — see "What's new (`frame-debugger-7` campaign)" below).
  Once captured, it stays frozen/inspectable until the next trigger fires, or
  until Disable/Resume clears it (see below). `FrameDebuggerPanel` never
  rebuilds its displayed snapshot on every ImGui frame.
- **Scope is Game View ONLY, permanently.** `"SceneView"`/`"Present"` passes
  are deliberately excluded from the Frame Debugger's own captured event tree,
  even though they exist in the same underlying `RenderGraphSnapshot` the
  "Render Graph" panel already displays in full. Relaxing this filter to cover
  Scene View/Present is a clean, well-isolated future extension point (a
  single line in the snapshot builder's filter), not attempted by any
  campaign so far.
- **Exactly ONE captured frame is ever held in memory — no multi-frame
  history (`frame-debugger-7` campaign, an explicit, user-approved BREAKING
  CHANGE relative to `frame-debugger-3`/`frame-debugger-4`, which introduced
  and then grew an 8-slot ring buffer).** `FrameDebuggerCurrentCapture`
  (`src/Editor/FrameDebuggerHistory.h/.cpp` — file name kept, class renamed)
  holds a single `std::optional<FrameDebuggerHistoryEntry>`, exposed via
  `HasCapture()`/`CurrentEntry()`/`Clear()`. There is no cursor, no "Frame N
  of M" scrubbing, and no `GET /frame_debugger/step_history` route anymore —
  Unity's own Frame Debugger does not remember past frames either. Turning
  "Enable" off, or resuming playback while still Enabled, immediately frees
  the captured frame's retained GPU textures via `Clear()` (RAII —
  `std::optional::reset()`).
- **Shader/pass-state "reflection" is real, but PASS-scoped, aggregated
  across every real draw call that pass issued that frame — never
  per-individual-mesh.** For the `"RenderOpaque"` leaf: `shaderName` lists
  every DISTINCT real `Pipeline` debug name actually used that frame (see
  `Pipeline::DebugName()`/`GpuResourceFactory::CreatePipeline()`'s cosmetic
  `debugName` parameter), `textures` lists every DISTINCT real bound
  `MaterialTexture` debug name, `vectors` includes the real clear color and
  real aggregate `DrawStats` (draw-call count / triangle count, sourced from
  the render graph's own snapshot, never re-derived), `matrices` includes the
  real view-projection matrix that pass actually rendered with that frame,
  and blend/Z/stencil rows report this engine's real, single, constant
  `Pipeline` configuration
  (`DescribeStandardPipelineState()`, `src/Editor/FrameDebuggerCapture.h/.cpp`)
  — this engine has exactly ONE Pipeline configuration today
  (`Pipeline.cpp`: no blend, `VK_COMPARE_OP_LESS` depth test, no stencil
  test anywhere), so there is nothing to fabricate per-mesh here; a genuine
  future per-material blend/Z/stencil VARIATION would need its own follow-up
  campaign, not just a data-plumbing change. The `"DrawSkyBackground"` leaf
  (`frame-debugger-8` campaign, now a real, separate pass — see "What's new
  (`render-pass-1` campaign)" above) is the ONE other real, non-fabricated
  pipeline-state fact in this window — it reports its own genuinely different
  `DescribeSkyBackgroundPipelineState()` values (`Depth Test = Equal`, `Depth
  Write = Off`, both DELIBERATELY different from every mesh's `Less`/`On`) —
  still not a per-MATERIAL/per-mesh variation (there is still exactly one
  Pipeline configuration for every real mesh draw), simply a second, distinct,
  real draw TYPE this tree now honestly distinguishes. Every compute-dispatch
  leaf's
  blend/Z/stencil rows all read `"n/a (compute pass)"` (it never issues a draw
  call), and its `vectors` include its own real GPU timing sample whenever one
  is present.
- **Preview reconstruction is now a genuine per-step accumulated RE-RENDER,
  not just a copy right after the fact — and, as of `frame-debugger-7`, every
  leaf (compute pass OR per-object draw) shows the SAME kind of image: the
  real Game View exactly as it looked with only the steps up to and including
  that one applied.** This REPLACES the `frame-debugger-5` campaign's
  "show this compute pass's own distinct output texture" mechanism outright —
  an explicit, user-approved BREAKING CHANGE (the raw per-pass texture pixels
  themselves are still inspectable elsewhere, e.g. the "Render Graph" panel /
  `GET /get_texture` — no diagnostic capability was actually lost, it just
  moved out of this window's own big preview box).

  On every explicit capture trigger, `AddFrameDebuggerReplayPasses()`
  (`src/Application/RenderPasses.h/.cpp`) declares N brand-new, debug-only,
  self-contained Render Graph passes — one per real object the real
  `"RenderOpaque"` pass draws that frame — each one redrawing objects `[0..i]`
  FROM SCRATCH into its own dedicated destination `RenderTexture` (the LAST
  pass also draws the sky background, exactly mirroring the real passes' own
  ordering). This is a deliberate O(N²) total draw-call cost across all N
  passes, chosen over an O(N) shared-target-plus-mid-pass-copy scheme because
  it needs ZERO new Render Graph/`PassContext`/`Renderer` API surface and can
  NEVER modify or corrupt the real, always-on `"RenderOpaque"`/
  `"DrawSkyBackground"` passes — an accepted cost since this work only ever
  runs once per explicit, human-triggered capture (see "Still-deferred future
  work" below for the O(N) alternative, explicitly deferred rather than
  attempted). The resulting N images are retained in
  `FrameDebuggerCaptureContext::ReplayStepPreviews()` (transient) and moved
  into permanent storage, `FrameDebuggerHistoryEntry::perObjectStepPreviews`,
  by `FrameDebuggerCurrentCapture::CaptureFrame()`.

  `BuildRealFrameDebuggerSnapshot()` stamps every node it builds with a new
  `FrameDebuggerStepPreviewKind` — `NotYetDrawn` (a Pre-GameView compute leaf —
  honestly "nothing drawn to the screen yet", never a fabricated image),
  `PerObjectStep` (one of `"RenderOpaque"`'s own per-entity children —
  `stepPreviewIndex` selects which of the N replay images), `PreComposite`
  (the `"RenderOpaque"`/`"DrawSkyBackground"`/`"RenderTransparent"` leaves
  themselves, or a Post-GameView leaf strictly before the real atmosphere-
  composite pass), or `PostComposite` (the composite pass itself, anything
  after it, or nothing selected). A rewritten
  `ChooseFrameDebuggerPreviewSource()` picks the actual image from exactly
  three retained sources — `preview` (pre-composite whole-frame),
  `compositedPreview` (post-composite whole-frame), or
  `perObjectStepPreviews[stepPreviewIndex]` — never fabricating one:
  `NotYetDrawn` always wins outright; `PerObjectStep` shows its own retained
  replay image or nothing at all (deliberately NEVER falls back to the
  whole-frame image — this is what makes the Bug 2 fix honest: no atmosphere
  fog, no later objects, ever, for this bucket); `PreComposite` always shows
  the pre-composite `preview`; `PostComposite` prefers `compositedPreview`,
  falling back to `preview` only when absent.
- **Channels (All/R/G/B/A) and Levels are functionally real**, driven by a
  dedicated preview-compositing module
  (`src/Editor/FrameDebuggerPreviewProcessing.h/.cpp`, mirrored by
  `Shaders/FrameDebuggerPreview.comp`): a pure, Tier-1-tested CPU oracle
  (`ApplyFrameDebuggerPreviewTransform()`) applies the Levels remap first,
  then Channel isolation (replicated grayscale, alpha forced opaque), and the
  GPU compute shader mirrors it exactly for the actual on-screen preview,
  dispatched only when the current Channel/Levels state is non-neutral and
  only when something actually changed since the last dispatch (never every
  ImGui frame). This deliberately does **NOT** reuse `/get_texture`'s existing
  `channel=color|depth` query parameter — a different, unrelated meaning (see
  [Networking](networking.md)).
- **The whole feature is drivable end-to-end over the embedded HTTP server,
  with the window forced onto the MAIN ImGui viewport whenever opened this
  way** — closing the "manual-verification limitation" both `frame-debugger-1`
  and `frame-debugger-2` had to accept as a documented gap. See "HTTP
  automation" below.

## What's new (`frame-debugger-6` campaign)

`frame-debugger-6` (`task_manager/frame-debugger-6/PHASE0_MASTER_STRATEGY.md`,
five phases) started from one vague user report ("load scene existing scene
from project, the terrain seems not get registered on frame debugger"), which
turned out to be two real, independent findings: a confirmed bug (this engine
runs a SEPARATE per-view copy of several Atmosphere compute passes for the
Editor's own Scene View, but both copies were registered under the exact same
literal pass NAME, producing duplicate/indistinguishable leaves, and even
leaking a genuinely Scene-View-only debug tool, `"ComputeBlurValidation"`,
into this Game-View-ONLY tree), and the user's own real underlying ask in
their own words: **"frame debugger dont have exact step where terrain got
drawn"**.

- **The duplicate/mis-scoped-pass bug is fixed at the root** (PHASE1/PHASE2) —
  a new, structurally-tracked `gte::rg::ViewScope` enum (`Shared`/`GameView`/
  `SceneView`) is stamped once, at the same "choke point"
  `RenderGraphBuilder::AddPass()`/`AddComputePass()` calls that already exist,
  by the handful of Application-layer call sites that build a genuinely
  per-view pass (the Sky-View LUT, Aerial Perspective Volume, Aerial
  Perspective Composite, `ComputeBlurValidation`) — never a
  pass-name/resource-suffix string comparison. `FrameDebuggerData.cpp`'s
  `BuildRealFrameDebuggerSnapshot()` now additionally excludes any surviving
  compute pass whose `viewScope` is `SceneView` from both Pre-/Post-GameView
  discovery loops, permanently closing this leak for any future pass that
  correctly stamps its own `ViewScope` too.
- **`"GameView"` now has real, individually selectable per-entity CHILD
  leaves — the actual user-facing feature** (PHASE3/PHASE4) — see "What is
  real today" above for the current tree shape. A new, never-deduplicated
  `FrameDebuggerCaptureContext::DrawRecords()` list (PHASE3) captures one
  `FrameDebuggerDrawRecord` per real draw call `RenderSystem::Draw()` issues
  this frame (the real ECS `Entity`, its resolved display name, its own real
  Pipeline/MaterialTexture debug names, and its own real per-draw triangle
  count), and `BuildGameViewDrawRecordLeaf()` (PHASE4,
  `src/Editor/FrameDebuggerData.cpp`) turns each one into a real child leaf
  of the `"GameView"` node, e.g. `"terrain (Entity 2)"` — clicking it shows
  THAT draw's own real shader name and real triangle count (not the whole
  pass's aggregate). `FrameDebuggerPanel::RenderEventNode()` (PHASE4) was
  fixed so a selectable leaf that ALSO has children (this new `"GameView"`
  shape) actually renders its children on screen — previously, every leaf
  this engine ever built had no children, so this code path silently assumed
  `isDrawCall == true` meant "no children", a real gotcha this phase's own
  double-check pass caught before it could ship as a bug.
- **What We Will NOT Do (explicit scope limit, unchanged from PHASE0):** no
  isolated/cropped/masked preview image of just one entity's own pixels —
  selecting a per-entity leaf still falls back to the existing whole-frame
  `compositedPreview`/`preview` image via the UNCHANGED
  `ChooseFrameDebuggerPreviewSource()` rule. Getting a real, isolated per-mesh
  image would need a stencil/ID-buffer or a full draw-call-level
  command-buffer replay — a separate, NOT-yet-approved future feature.
  Relatedly, a per-entity leaf's own `FrameDebuggerEventDetails::passName` is
  deliberately never the literal string `"GameView"` (it reads
  `"GameView (Entity Draw)"` instead) specifically so it can never collide
  with `FrameDebuggerPanel::EnsurePreviewDescriptor()`'s existing
  `isViewingGameViewLeaf = (details->passName == "GameView")` exact-string
  check — a real self-contradiction risk this campaign's own double-check
  pass found and fixed in its own strategy document BEFORE implementation,
  not a shipped bug.

See `task_manager/frame-debugger-6/CAMPAIGN_COMPLETION_REPORT.md` for the full
five-phase writeup plus the live, HTTP-driven, screenshot-verified proof both
the duplicate-pass bug is gone and the new per-entity `terrain` leaf is
selectable, shows correct data, and the preview box correctly still shows the
composited whole-frame image rather than colliding with the `"GameView"`
leaf's own pre-composite-only preview rule.

## What's new (`frame-debugger-7` campaign)

`frame-debugger-7` (`task_manager/frame-debugger-7/PHASE0_MASTER_STRATEGY.md`,
seven phases) fixed two more real, user-confirmed bugs in this window: **Bug
1 — wrong first capture** (load a scene → open Frame Debugger → press
"Enable" → the very first captured frame was missing objects entirely — no
terrain row at all — while pressing Enable a SECOND time always captured
correctly), and **Bug 2 — wrong preview image per selected item** (clicking an
object in the event tree, e.g. `"SmokeTestCube"`, never showed the screen as
it looked right after THAT object was drawn — it always showed the same
whole-frame image, with everything already drawn, exactly unlike Unity's own
Frame Debugger).

- **Bug 1's fix — a genuinely deferred capture trigger.**
  `Application::Run()` calls `m_editorLayer->PrepareFrameDebuggerCaptureContext()`
  BEFORE `Game::Render()` runs, every frame — that call only arms the real
  capture context (returns non-null) when `ctx.frameDebuggerWindowOpen &&
  m_enabled` are BOTH already true at that point in the frame. But the Enable
  checkbox itself is only actually clicked LATER, inside
  `Build()`/`BuildToolbarRow()`/`ApplyEnabledEdge()` — by which point that
  SAME frame's rendering has already run with a `nullptr` capture pointer,
  since `m_enabled` was still `false` when the frame started. The old code
  called `TriggerCapture()` immediately, right there in the click handler,
  building a snapshot from that already-stale frame — real render-graph/pass
  data, but ZERO `FrameDebuggerDrawRecord`s, hence no per-entity children
  under `"GameView"`. The fix: ALL THREE real capture triggers (the Enable
  false→true edge, a Step while Enabled, and the "Capture" button/HTTP route)
  now only ARM a pending flag (`m_pendingCaptureTrigger`); the actual
  `TriggerCapture()` call is deferred to the NEXT `Build()` call, via a
  two-bool pending/serviced handshake
  (`m_pendingCaptureTrigger`/`m_replayServicedThisFrame`,
  `IEditorLayer::ConsumePendingFrameDebuggerReplayRequest()`) consumed EARLY
  in `Application::Run()`'s own offscreen `build` lambda (before
  `Game::Render()` runs) and acted on LATE, at the top of `Build()` (after
  that same frame's rendering already happened) — so the capture is always
  built from a frame whose entire rendering ran with the capture context
  correctly armed from the start.
- **The removal of the multi-frame history ring buffer** — see "What is real
  today" above (an explicit, LOCKED, user-approved BREAKING CHANGE relative to
  `frame-debugger-3`/`frame-debugger-4`, which introduced and grew it).
  Exactly ONE captured frame is now ever retained
  (`FrameDebuggerCurrentCapture`), cleared immediately whenever "Enable" is
  unticked or playback resumes while still Enabled.
- **The removal of the per-compute-pass distinct-texture preview** (the
  `frame-debugger-5` campaign's own mechanism) — see "What is real today"
  above (an explicit, LOCKED, user-approved BREAKING CHANGE). Replaced by the
  new unified "accumulated Game View as of this exact step" preview shared by
  EVERY leaf, compute pass or per-object draw alike. Raw per-pass texture
  pixel inspection remains fully possible elsewhere (the "Render Graph" panel
  / `GET /get_texture`) — no diagnostic capability was actually lost, it only
  moved out of this window's own big preview box.
- **Bug 2's fix — the new per-object replay-rendering mechanism** (this
  campaign's heaviest, riskiest phase — see "What is real today" above for
  the full mechanism). N self-contained, debug-only Render Graph passes, each
  redrawing objects `[0..i]` from scratch into its own dedicated destination
  texture, run ONLY on an explicit capture-trigger frame and NEVER modify the
  real, always-on `"GameView"` pass. This was chosen over a shared-target-
  plus-mid-pass-copy scheme because `rg::PassContext` has no way to obtain a
  raw `VkImage` to copy from mid-pass without widening a type shared by every
  other pass in the engine — a broader, higher-risk change than this
  feature's own scope justified. The accepted cost is O(N²) total draw calls
  across all N replay passes (and O(N) extra Game-View-resolution render
  targets) — paid only once per explicit, human-triggered capture, never every
  ordinary gameplay frame.
- **Two real bugs were found and fixed during this campaign's own mandatory
  visual spot-check** (`PHASE3_COMPLETION_REPORT.md`), not just by code
  review: (a) the very first implementation of the N replay passes never
  added their destination textures to the render graph's own root output set,
  so `RenderGraphCompiler`'s backward-reachability culling silently culled
  every one of them — their `execute` lambdas never ran, and each destination
  texture showed genuine uninitialized VRAM garbage (confirmed via a
  temporary debug PNG dump) until `AddFrameDebuggerReplayPasses()` was
  changed to return every destination handle for the caller to add to
  `outputs`; (b) `GET /frame_debugger/capture` (the HTTP mirror of the
  "Capture" button) was initially missed when widening the deferred-trigger
  mechanism to all three triggers, so a SECOND capture taken via that one
  route silently carried zero replay-step images — fixed by making it arm
  `m_pendingCaptureTrigger` exactly like the hand-driven button, instead of
  calling `TriggerCapture()` directly.

See `task_manager/frame-debugger-7/PHASE0_MASTER_STRATEGY.md` and each
`PHASEn_COMPLETION_REPORT.md` in that same folder for the full seven-phase
writeup, and `CAMPAIGN_COMPLETION_REPORT.md` (written at the close of Phase 7)
for the final, live, HTTP-driven, screenshot-verified proof both bugs are
fixed.

## What's new (`frame-debugger-8` campaign)

`frame-debugger-8` (`task_manager/frame-debugger-8/PHASE0_MASTER_STRATEGY.md`,
four phases) fixed one more real, user-confirmed gap: the Sky Background pass
- a real, direct `vkCmdDraw()` full-screen-triangle draw
(`AtmosphereSkyBackgroundRenderer::Draw()`, drawn LAST every frame, after
every real entity, using its own genuinely different `EQUAL`-depth-test
pipeline state so it only paints pixels nothing else touched yet) - never
went through `RenderSystem::Draw()`/`Renderer::Submit()` at all, so it was
COMPLETELY INVISIBLE anywhere in the event tree, even though it genuinely
runs every single frame. A second, closely-related bug was found (and
independently spotted by the user, comparing the last object's own preview
image against the object drawn just before it) while investigating: the LAST
real entity's own per-step preview image secretly ALREADY included the sky,
bundled in for an unrelated technical reason, with no way to see "just after
the last object, before sky" as its own distinct state.

- **A new `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` method**
  (`src/Editor/FrameDebuggerCapture.h/.cpp`) is now called exactly once, from
  `AddGameViewPass()`'s own `execute` lambda (`src/Application/RenderPasses.cpp`),
  immediately after the real `recordSkyBackground` callback runs - the same
  "only touch the capture context when it's actually armed" discipline every
  other call site already follows. It reuses `RecordDraw()`'s own existing
  dedup/draw-call-count/last-view-projection bookkeeping internally (the sky
  uses the exact same view-projection matrix every other draw in the pass
  used that frame), and appends one new `FrameDebuggerDrawRecord` marked
  `isSkyBackgroundDraw = true` - a new field appended at the end of that
  struct, alongside the real per-entity records `RecordEntityDraw()` already
  produces, always LAST (mirroring the real GPU draw order:
  every entity, then sky).
- **The new leaf's own identifying name is a REAL, hand-verified fact, never
  an invented cosmetic label.** `AtmosphereSkyBackgroundRenderer::ShaderDebugName()`
  (a new, permanent, `constexpr` static method) returns the actual real
  shader-file-pair string this pass genuinely loads,
  `"AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag"` - this is
  both the new tree row's own display name AND its Inspector "Shader" row.
  This deliberately follows the exact same hard-learned lesson the
  `frame-debugger-5` campaign already established when it REMOVED the old
  hardcoded "GPU Skinning"/"Aerial Perspective Composite" cosmetic-label
  special cases (see that campaign's own section above) - never fabricate a
  friendly name detached from a real, checkable fact. Its own `passName` row
  reads `"GameView (Sky Draw)"`, mirroring the pre-existing
  `"GameView (Entity Draw)"` per-entity-leaf convention exactly (a real,
  structural "which pass did this happen inside" fact, distinct from the
  literal `"GameView"` pass leaf's own name, for the same
  string-collision-avoidance reason `frame-debugger-6`'s own PHASE4
  originally documented).
- **The new leaf's own Blend/Z/Stencil Inspector rows are genuinely
  different from every mesh's**, via a new, dedicated
  `DescribeSkyBackgroundPipelineState()` function
  (`src/Editor/FrameDebuggerCapture.h/.cpp`), hand-transcribed directly from
  `AtmosphereSkyBackgroundRenderer.cpp`'s own real
  `VkPipelineDepthStencilStateCreateInfo` construction: `Depth Test = Equal`
  (not `Less`) and `Depth Write = Off` (not `On`) - the ONE other place in
  this whole engine (besides the single, constant
  `DescribeStandardPipelineState()` every mesh leaf reuses) that reports a
  genuinely different real Pipeline configuration in this window.
- **A real, correctly-ordered replay preview for the new leaf** - the
  `frame-debugger-7` campaign's own per-object replay-rendering mechanism
  (`AddFrameDebuggerReplayPasses()`, `src/Application/RenderPasses.cpp`) is
  restructured so the sky is NEVER drawn inside a per-object replay step
  anymore (fixing the "last object's own image secretly already included
  sky" bug outright) - instead, exactly ONE new, dedicated replay step is
  added whenever a sky callback exists that frame, redrawing every real
  object and then the sky, becoming the new leaf's own correct,
  pixel-accurate "Game View as of right after the sky was drawn" preview
  image (pixel-identical to the whole-frame `preview`, by construction - the
  same useful internal cross-check the OLD, buggy code's own comment already
  noted, now genuinely isolated onto its own step instead of incorrectly
  fused onto the last object's). A scene with ZERO mesh entities now also
  correctly gets exactly one real replay step (the sky alone), instead of
  zero - the old code's own `if (objectCount == 0) return;` early-out used
  to skip replay entirely for an empty scene, silently hiding the sky there
  too.
- **`BuildGameViewDrawRecordLeaf()` (`src/Editor/FrameDebuggerData.cpp`)
  branches on the new `isSkyBackgroundDraw` flag** to build this
  differently-shaped leaf (no fabricated "Entity (Index, Generation)" row,
  since there is no real ECS entity behind it) - the existing
  `BuildRealFrameDebuggerSnapshot()` loop that walks
  `capture.DrawRecords()` needed ZERO structural changes at all; it already
  iterates the sky record correctly simply because
  `RecordSkyBackgroundDraw()` appends it in true chronological (real GPU)
  order.
- **Scope stayed Game View ONLY, per this feature's own permanent rule** -
  the underlying `AtmosphereSkyBackgroundRenderer::Draw()` function is
  genuinely shared/identical for both the Game View and the Editor's own
  Scene View (same real Vulkan pipeline, same shader files) - only the Frame
  Debugger's own CAPTURE call site differs, and it was only ever wired into
  `AddGameViewPass()` to begin with; `AddSceneViewPass()` remains untouched
  by this campaign, exactly like every other Game-View-only mechanism this
  window already has.
- **A known, EXPLICITLY DEFERRED, pre-existing, narrow gap this campaign did
  NOT fix**: the parent `"GameView"` leaf's own aggregate "Draw Stats
  (Calls, Tris)" row (sourced from `RenderGraphPassSnapshot::stats.drawStats`,
  fed only by `Renderer::Submit()`'s own bookkeeping) still does not count
  the sky's own raw `vkCmdDraw()` call, since that call happens outside any
  `Renderer::Submit()`/`BeginGraphPassRecording()` bracket - a real, narrow,
  separate gap from the one this campaign fixed (the sky's OWN dedicated
  leaf's own "Triangle Count" row is correct in isolation; only the parent
  aggregate undercounts by exactly one draw call). Left as a clean, isolated
  future item, not silently forgotten.

See `task_manager/frame-debugger-8/CAMPAIGN_COMPLETION_REPORT.md` for the
full four-phase writeup plus the live, HTTP-driven, screenshot-verified proof
both the new Sky Background leaf and the corrected per-step replay preview
work end-to-end.

## What's new (`frame-debugger-9` campaign)

`frame-debugger-9` (`task_manager/frame-debugger-9/PHASE0_MASTER_STRATEGY.md`, four
phases) is a pure Quality-of-Life follow-up, adding three user-requested improvements
with no other behavior change:

- **The step-preview image now respects its own real aspect ratio** (PHASE1) - a new,
  Tier-1-tested pure helper, `ComputeAspectFitImageRect()`
  (`src/Editor/FrameDebuggerData.h/.cpp`), computes a centered, uniformly-scaled
  letterbox/pillarbox rect (solid black bars on the short axis, via a
  `ImGuiCol_ChildBg` push around the whole preview child window) instead of the old
  `ImGui::Image(descriptor, avail)` full-stretch call. Verified live: a 417x333 Game
  View texture now renders correctly pillarboxed (solid black bars left/right) inside
  its wider preview box, for both the whole-frame step preview and the
  `"Nothing drawn yet at this point in the frame."` placeholder branch.
- **The frame-step slider is a real, interactive control** (PHASE2) - mouse-draggable
  (`ImGui::SliderInt()` reporting every in-progress drag value, not just on release)
  and Left/Right-arrow-key-nudgeable while focused (`ImGui::IsKeyPressed(...,
  /*repeat=*/true)`), in addition to the pre-existing tree-row click and `GET
  /frame_debugger/select_event` HTTP route - all four paths now funnel through one
  new chokepoint, `FrameDebuggerPanel::SetSelectedEventIndex()`, which also releases
  any currently-shown Feature-2 one-shot preview whenever the selection genuinely
  changes.
- **Any render-graph-registered texture a compute-dispatch leaf's own ShaderProperties
  tab lists (Read/Write Texture, Read/Write Volume Texture rows) can now actually be
  viewed** (PHASE3) via a small "View" button next to its row - a strictly ON-DEMAND,
  ONE-SHOT preview (never a continuously live view): a 2D texture is read back via
  `Renderer::CaptureImagePixels()` (the exact same primitive `GET /get_texture` already
  uses, including its BGRA/HDR conversion rules) and re-uploaded into a freshly-owned
  `Texture2D`; a volume texture is ray-marched via the pre-existing
  `VolumeTexturePreviewRenderer` (unchanged) and uploaded the same way. Displayed until
  the user dismisses it ("Back to Step Preview"), selects a different event, takes a new
  Capture, or Disables/Resumes - never cached or kept continuously up to date. A new
  `FrameDebuggerTextureProperty::kind`/`isRenderGraphResource` pair (structural, not a
  string match) gates which rows get a "View" button - never a `rg::ResourceKind::Buffer`
  row, and never a "Material Texture" row (per-entity/mesh asset textures - a different,
  non-render-graph system) - a clean, documented, deliberately out-of-scope future item,
  not silently forgotten.

Live verification (this campaign's own PHASE4) confirmed Features 1 and 3 together via
`GET /get_swapchain` screenshots after driving `/frame_debugger/open`,
`/frame_debugger/enable?value=true`, and several `/frame_debugger/select_event?index=N`
calls in sequence - the preview box correctly pillarboxes the real 417x333 Game View
texture, the frame-step slider/label track the selection exactly (e.g. "3 of 8"/"6 of
8"), and the tree highlight plus Event Details section stay in sync. Feature 2's own
"View" button/ShaderProperties tab click-through could not be additionally exercised in
this HTTP-only automation environment (no mouse/keyboard input-injection tool available
for this native SDL/Vulkan window) - it rests on the same thorough code-review-level
verification, plus the extended Tier-1 `FrameDebuggerSnapshotBuilderTest` coverage for
`kind`/`isRenderGraphResource`, that PHASE3's own completion report already documents.

See `task_manager/frame-debugger-9/PHASE0_MASTER_STRATEGY.md` and each
`PHASEn_COMPLETION_REPORT.md`/`CAMPAIGN_COMPLETION_REPORT.md` in that same folder for the
full four-phase writeup and live verification evidence.

## Known limitation, now fixed (`frame-debugger-4` campaign)

For the entire lifetime of this feature up through `frame-debugger-3`, the
retained preview this window ever showed was **always** the PRE-atmosphere-
composite `"GameView"` image, never the real, final, atmosphere-composited
one — checking "Enable" and looking at the preview box never showed the
atmosphere-scattering/aerial-perspective fog effect, even though the "Game"
panel / `GET /get_game_view` right next to it always did. Two independent,
compounding root causes: (1) `ImGuiEditorLayer::BuildUI()` fed
`FrameDebuggerPanel::Build()` the pre-composite `m_gameView` texture, never
the real, final `m_gameViewComposited` texture the "Game" panel itself
already preferred; and (2) the real, separate
`"AtmosphereAerialPerspectiveCompositePass"` render-graph pass that actually
produces the composited image was never looked up or shown anywhere in the
event tree, so there was no way to even discover that compositing happened.
`task_manager/frame-debugger-4/PHASE0_MASTER_STRATEGY.md` (three phases) fixed
both: `FrameDebuggerHistory` now retains both images per captured frame
(PHASE1), and (at the time) the compositing pass became a real, hardcoded,
selectable `"AtmosphereAerialPerspectiveCompositePass"` tree leaf (PHASE2) —
this one-off special case was itself REPLACED by `frame-debugger-5`'s fully
generic mechanism (see immediately below) — see
`task_manager/frame-debugger-4/CAMPAIGN_COMPLETION_REPORT.md` for the full
three-phase writeup plus the live, HTTP-driven, screenshot-verified proof the
fix actually worked at the time.

## Known limitation, now fixed (`frame-debugger-5` campaign)

Even after `frame-debugger-4`'s fix above, the event tree only ever showed
THREE of this engine's 8+ real compute-shader dispatches, each hardcoded by
exact string name: `"GameView"` itself (a graphics pass, correctly always
shown), whatever pass names appeared in a caller-supplied
`gpuSkinningPassNamesThisFrame` list (sourced from a completely separate,
parallel path, `Game::CollectGpuSkinningDispatchRequests()`, kept in sync with
the render graph only by convention, never structurally guaranteed), and the
one, single, literal `"AtmosphereAerialPerspectiveCompositePass"` string.
**Every atmosphere LUT pass (Transmittance/Multi-Scattering/Sky-View), the
Aerial Perspective Volume pass, the Aerial Perspective Volume Debug-Slice
pass, and Compute Blur Validation's own pass were completely invisible in the
tree, no matter whether they ran that frame or not** — the literal bug this
campaign exists to fix ("I don't think it's catching all compute shader
dispatch operations... it involves creating multiple LUT textures using
compute shader"). Even the one compute pass that WAS shown
(`"AtmosphereAerialPerspectiveCompositePass"`) only showed the correct preview
image by pure coincidence — its own write target happened to be
`"GameViewComposited"`, the exact same texture `compositedPreview` already
retained for an unrelated reason; there was no generic mechanism at all for
"show me THIS specific pass's own real output texture," so naively adding a
leaf for, say, the Transmittance LUT pass would have shown the wrong image.

`task_manager/frame-debugger-5/PHASE0_MASTER_STRATEGY.md` (five phases) fixed
this at the root: `RenderGraphBuilder::AddComputePass()` — the one real choke
point every compute dispatch in this engine already funnels through — now
stamps a real `PassRecord::isComputePass`/`RenderGraphPassSnapshot::isComputePass`
flag (PHASE1), `FrameDebuggerData.cpp` discovers every surviving compute pass
generically via that flag and builds the split `"Compute Dispatches
(Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"` groups (PHASE2, see
"What is real today" above), `FrameDebuggerHistory::CaptureFrame()` eagerly
retains a real per-pass 2D-texture preview for every one of them (PHASE3), and
the one remaining gap — a compute pass whose only visual write is a 3D volume
texture — gets a real ray-marched thumbnail by reusing the already-shipped
`VolumeTexturePreviewRenderer` (PHASE4). The OLD `gpuSkinningPassNamesThisFrame`
name-list mechanism and the OLD hardcoded
`FindPassByName(..., "AtmosphereAerialPerspectiveCompositePass")` special case
are both REMOVED entirely, not merely supplemented — this was an explicit,
user-approved BREAKING change to the previously-shipped tree shape (see
`task_manager/frame-debugger-5/PHASE0_MASTER_STRATEGY.md`'s Locked Design
Decision #6). See `task_manager/frame-debugger-5/CAMPAIGN_COMPLETION_REPORT.md`
for the full five-phase writeup plus the live, HTTP-driven, screenshot-verified
proof every atmosphere LUT compute pass now appears as its own selectable leaf
with its own correct, distinct real output image.

## The data model and panel (structure, largely unchanged in shape from
`frame-debugger-2`)

- **`src/Editor/FrameDebuggerData.h/.cpp`** is the pure, ImGui-free data
  model - `FrameDebuggerTextureProperty`/`FrameDebuggerVectorProperty`/
  `FrameDebuggerMatrixProperty`/`FrameDebuggerEventDetails`/
  `FrameDebuggerEventNode`/`FrameDebuggerRenderTargetInfo`/
  `FrameDebuggerSnapshot`, plus `BuildRealFrameDebuggerSnapshot()` (the real
  builder, reshaping `gte::rg::RenderGraphSnapshot` +
  `FrameDebuggerCaptureContext` into a real, non-empty snapshot filtered to
  Game-View-only passes, with the generic compute-dispatch discovery/split
  described above — `BuildPlaceholderFrameDebuggerSnapshot()` still exists and
  is still used as the honest empty-tree fallback when nothing has been
  captured yet this session), `FormatFrameStepperLabel()`,
  `FormatFrameHistoryLabel()` (now unused in production code since
  `frame-debugger-7` removed the "Frame History" mini-toolbar it fed — still
  compiles and still has its own passing test, left alone rather than deleted
  since nothing required removing it), `ClampSelectedEventIndex()`,
  `FindEventDetailsByIndex()`, `FormatVectorProperty()`,
  `FormatMatrixProperty()`, and (`frame-debugger-7` campaign) a new
  `FrameDebuggerStepPreviewKind` enum (`NotYetDrawn`/`PerObjectStep`/
  `PreComposite`/`PostComposite`) that `BuildRealFrameDebuggerSnapshot()` now
  stamps onto every `FrameDebuggerEventDetails` it builds (plus a
  `stepPreviewIndex` for `PerObjectStep` nodes), and a rewritten
  `ChooseFrameDebuggerPreviewSource(bool hasEntry, FrameDebuggerStepPreviewKind
  stepPreviewKind, bool hasPreview, bool hasCompositedPreview, bool
  hasPerObjectStepPreviewAtIndex)` (the pure "which retained image should the
  preview box show" decision — see "What's new (`frame-debugger-7` campaign)"
  below for the full picking rule). The `frame-debugger-5` campaign's own
  `CollectComputePassTextureWrites()`/`CollectComputePassVolumeTextureWrites()`
  (the per-compute-pass distinct-texture-write discovery helpers) are GONE —
  removed outright by `frame-debugger-7`, an explicit, user-approved BREAKING
  CHANGE (see below). Mirrors `JobsPanelData.h`/`ProfilerPanelData.h`'s
  own "small, dedicated, directly-testable reshaping module" precedent (see
  [Testability & Regression Safety](../../AGENTS.md#testability--regression-safety)).
  Tier-1-tested by `tests/Editor/FrameDebuggerDataTests.cpp` and
  `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp`.
- **`src/Editor/FrameDebuggerCapture.h/.cpp`** — `FrameDebuggerCaptureContext`,
  a plain, dumb, per-frame recorder threaded through
  `Renderer::Submit()`/`RenderSystem::Draw()` as a defaulted, LAST,
  `nullptr`-by-default parameter (zero overhead whenever disarmed — no
  string formatting, no vector work happens on the overwhelmingly common
  "not currently capturing" frame), aggregating every DISTINCT real
  `Pipeline`/`MaterialTexture` debug name, every real per-entity
  `FrameDebuggerDrawRecord`, and the last real view-projection matrix used
  this frame. Also home to `DescribeStandardPipelineState()` (see above) and,
  as of `frame-debugger-7`, `SetReplayStepPreviews()`/`ReplayStepPreviews()` —
  transient, per-frame storage for the N per-object replay images (see
  below; moved out into permanent storage by
  `FrameDebuggerCurrentCapture::CaptureFrame()` before the next armed
  frame's `Reset()` would otherwise discard them). `RenderSystem::Draw()`'s
  own new `maxDrawCount` cutoff parameter (the mechanism the N replay passes
  use to redraw only their own first `i+1` objects) stayed a plain inline
  loop `break` rather than being extracted into its own pure helper — there
  is no second boolean/branch to combine it with, so extracting it would
  have been pure ceremony (see
  `task_manager/frame-debugger-7/PHASE6_COMPLETION_REPORT.md` for the full
  reasoning). Tier-1-tested by `tests/Editor/FrameDebuggerCaptureTests.cpp`.
- **`src/Editor/FrameDebuggerHistory.h/.cpp`** — file name kept as-is
  (`frame-debugger-7` deliberately minimized include churn), but the class
  itself is now `gte::FrameDebuggerCurrentCapture` (renamed from
  `FrameDebuggerHistory` — the 8-slot ring buffer is GONE, an explicit,
  user-approved BREAKING CHANGE, see "What's new (`frame-debugger-7`
  campaign)" below). Exactly ONE `std::optional<FrameDebuggerHistoryEntry>
  m_current` slot exists — `HasCapture()`/`CurrentEntry()`/`Clear()` replace
  the old `Count()`/`CursorIndex()`/`StepCursor()` cursor arithmetic entirely.
  `FrameDebuggerHistoryEntry` (the payload struct name itself — kept
  unchanged) retains `preview`/`compositedPreview` (`frame-debugger-4`
  campaign) plus a new `perObjectStepPreviews` (`frame-debugger-7` — one
  real, retained GPU `RenderTexture` per object drawn this capture's
  `"GameView"` pass, in the SAME order as
  `FrameDebuggerCaptureContext::DrawRecords()`, moved in from
  `FrameDebuggerCaptureContext::ReplayStepPreviews()` by `CaptureFrame()`).
  The `frame-debugger-5` campaign's own `computePassPreviews` field (one
  retained texture per compute-dispatch pass's OWN distinct write) is GONE —
  REPLACED by the unified `perObjectStepPreviews`-and-whole-frame-image
  picking rule every leaf now shares (see below); the whole HDR-round-trip/
  volume-ray-march capture code `CaptureFrame()` used to run for that
  mechanism is removed along with it. Tier-1-tested by
  `tests/Editor/FrameDebuggerHistoryTests.cpp` (the pure `HasCapture()`/
  `Clear()` state machine of a fresh instance — `CaptureFrame()`'s own body
  stays Tier-2/untested directly, since it needs a live `VkDevice`, same as
  before this campaign).
- **`src/Editor/FrameDebuggerPreviewProcessing.h/.cpp`** — the real
  Channels/Levels compositing (see above). Tier-1-tested by
  `tests/Editor/FrameDebuggerPreviewProcessingTests.cpp`.
- **`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`** is the panel itself — a
  small STATEFUL CLASS (not a free function), following the same
  stateful-panel exception `RenderGraphPanel`/`ProfilerPanel`/
  `BoneViewerWindow` already established (see
  [Editor Module Structure](editor-module-structure.md)). It owns the
  "Enable" toggle (`m_enabled`), the left-hand event-tree pane's persisted
  splitter width, the currently-selected leaf event's index
  (`m_selectedEventIndex`, now genuinely reachable via real tree-row clicks
  OR via the HTTP `select_event` command), the real capture context
  (`m_captureContext`), the single real captured frame (`m_currentCapture`,
  type `FrameDebuggerCurrentCapture` — `frame-debugger-7` renamed this from
  `m_history`), the real Channels/Levels state
  (`m_channel`/`m_levelsBlack`/`m_levelsWhite`), its own preview-texture ImGui
  descriptor (self-owned, mirroring `BoneViewerWindow`'s own "owns its own
  `ImGui_ImplVulkan_AddTexture()` descriptor" precedent), its own
  `FrameDebuggerPreviewRenderer` instance, and (`frame-debugger-7`)
  `m_lastPreviewChoice` (`FrameDebuggerPreviewSourceChoice`, cached each time
  `EnsurePreviewDescriptor()` runs, so `BuildInspectorPane()` knows which
  placeholder/real-image state to render). Called explicitly by name from
  `ImGuiEditorLayer::BuildUI()` — no `IEditorPanel` interface. As of
  `frame-debugger-5`, it no longer takes a `gpuSkinningPassNamesThisFrame`
  parameter anywhere — GPU Skinning passes are discovered exactly like every
  other compute pass now (see above). As of `frame-debugger-7`, none of the
  three real capture triggers (Enable-edge / Step / "Capture" button, HTTP
  routes included) call `TriggerCapture()` synchronously anymore — all three
  only set `m_pendingCaptureTrigger`, consumed one frame later via the
  two-bool `m_pendingCaptureTrigger`/`m_replayServicedThisFrame` handshake
  described above.
- **An ON-DEMAND FLOATING WINDOW, not part of the default dock layout, and
  NOT listed in `EditorPanelCatalog.h`.** Still deliberate (mirroring
  `BoneViewerWindow`'s own precedent) — `EditorPanelCatalog.h` exists purely
  to keep `DockLayout.cpp`'s *permanently-docked default layout* names in
  sync with `GET /activate_tab`/`GET /list_tabs`; this window's own HTTP
  automation goes through its OWN dedicated `/frame_debugger/*` route family
  instead (see below), never through `/activate_tab`.
- **Opened/closed via the "Window" top-level menu** (hand-driven) OR via
  `GET /frame_debugger/open` (HTTP-driven) — both ultimately flip the same
  shared `EditorContext::frameDebuggerWindowOpen` bool; only the HTTP path
  also arms the one-shot main-viewport pin (see "Main-viewport pinning"
  below).
- **The "Enable" checkbox auto-engages the existing Pause/Resume toolbar AND
  arms a deferred capture trigger** (see
  [Time and Playback Pause](time-and-playback-pause.md)): turning "Enable" ON
  sets `ctx.playbackPaused = true` (the same field `PlaybackControls.cpp`'s
  own "Pause" button writes) and sets `m_pendingCaptureTrigger = true`, both
  via a shared `ApplyEnabledEdge()` helper used identically by the
  hand-driven checkbox and the HTTP `SetEnabledFromCommand()` entry point, so
  the two paths can never silently diverge. As of `frame-debugger-7`, the
  real capture no longer happens synchronously inside this same click
  handler — it is deferred to the next `Build()` call, by which point a full
  frame's worth of rendering has already happened with the capture context
  correctly armed for its ENTIRE duration (see "What's new
  (`frame-debugger-7` campaign)" below for why — this is the actual Bug 1
  fix). Turning "Enable" back OFF still deliberately does **not** auto-resume
  playback, unchanged from `frame-debugger-2` — it now ALSO calls
  `m_currentCapture.Clear()`, freeing the captured frame's retained GPU
  textures immediately (a NEW lifecycle rule, `frame-debugger-7` — exactly
  one captured frame is ever held in memory, matching Unity's own Frame
  Debugger, which does not remember past frames either). Resuming playback
  while still Enabled ALSO clears the captured data the same way (checked at
  the very top of `Build()`), but leaves `m_enabled` itself untouched in both
  cases — only the captured DATA disappears.
- **The "Editor" mode combo remains a purely cosmetic, permanently-disabled
  stub** — this engine still has no Play/Edit-mode split, so there is nothing
  real for it to switch between.
- **The event tree pane shows a real, non-empty tree the moment a real
  capture has happened**, falling back to "No frame captured yet." whenever
  nothing has been captured this session, whenever Disable/Resume just
  cleared the previous capture (`frame-debugger-7`), or during the one real
  frame between the Enable checkbox's false→true edge and its deferred
  capture landing (`frame-debugger-7`, see below). The event-details section
  shows real Shader/Pass/Blend/Z-state/Stencil/Textures/Vectors/Matrices data
  once a real leaf is selected, falling back to "No event selected." only
  when nothing is selected, and the preview box shows a distinct third
  message, `"Nothing drawn yet at this point in the frame."`, for a
  Pre-GameView compute leaf specifically (`frame-debugger-7`'s `NotYetDrawn`
  bucket — see below).

## HTTP automation (`frame-debugger-3` PHASE7)

A brand-new, fully independent sibling of `EditorUiCommandBridge`,
`FrameDebuggerCommandBridge` (`src/Application/FrameDebuggerCommandBridge.h/.cpp`
— never an extension of `EditorUiCommandKind`, per `AGENTS.md`'s own rule that
a genuinely new KIND of request gets its own bridge), backs seven routes under
`src/Network/NetworkRoutes.h/.cpp`/`NetworkServer.cpp` (`frame-debugger-7`
removed the original eighth route, `GET /frame_debugger/step_history` —
see "What's new (`frame-debugger-7` campaign)" below):

- `GET /frame_debugger/open` — opens the window (a `false -> true`
  PROGRAMMATIC transition also arms a genuine ONE-SHOT main-viewport pin, see
  below; never affects the manual "Window > Frame Debugger" menu item's own
  drag-anywhere freedom).
- `GET /frame_debugger/enable?value=true|false` — flips "Enable", exactly
  mirroring the checkbox's own `ApplyEnabledEdge()` behavior (as of
  `frame-debugger-7`, the false→true edge only ARMS a deferred capture
  trigger — see below — it no longer captures synchronously).
- `GET /frame_debugger/capture` — arms an explicit new capture trigger
  (equivalent to clicking "Capture"), consumed on the very next armed frame
  (`frame-debugger-7` — previously captured synchronously).
- `GET /frame_debugger/select_event?index=<n>` — selects a leaf event by
  index (`-1` deselects).
- `GET /frame_debugger/set_channel?value=all|r|g|b|a` — sets the Channels
  isolation (completely separate name/value space from `/get_texture`'s own
  `channel=color|depth`, per the Locked Design Decision above).
- `GET /frame_debugger/set_levels?black=<f>&white=<f>` — sets the Levels
  remap range.
- `GET /frame_debugger/state` — a flat, no-`"success"`-wrapper JSON dump of
  every piece of live state (`enabled`/`hasCapturedFrame`/`selectedEventIndex`/
  `totalEventCount`/`channel`/`levelsBlack`/`levelsWhite`/`windowOpen`) — the
  `historyCount`/`historyCursor` fields the multi-frame history ring buffer
  used to report are GONE (`frame-debugger-7` — there is no cursor/count left
  to report anymore, only a single bool: is a frame currently captured or
  not). Every OTHER route's response ALSO includes this same `"state"` object
  (nested under `success`), so a caller can assert state without a second
  round-trip after every command.

Every route goes through the SAME bridge/pump mechanism (mutex + condition
variable, `SubmitAndWait()` from the network thread,
`TryPeekPendingCommandRequest()`/`FulfillCommand()` pumped once per frame from
`Application::Run()`, at the same point `EditorUiCommandBridge`'s own pump
runs) — including `/state`, which could theoretically read directly, but this
codebase's own "never touch engine/Editor-owned mutable state from the
network thread except through a reviewed bridge" rule (see
[Networking](networking.md)) is deliberately kept with zero exceptions.

### Main-viewport pinning

`ImGuiEditorLayer.cpp` enables `ImGuiConfigFlags_ViewportsEnable` for the whole
Editor, meaning any floating, undocked window (including this one) can
normally be dragged into its own independent OS-level platform window — which
`GET /get_swapchain` categorically cannot see, since it only ever reads back
the MAIN window's own swapchain image. `FrameDebuggerPanel::RequestOpenWindow()`
sets a one-shot `m_pinToMainViewportNextOpen` flag ONLY on a genuine
programmatic `false -> true` open (never on the manual "Window > Frame
Debugger" menu item, which keeps its normal drag-anywhere freedom); `Build()`
then calls `ImGui::SetNextWindowViewport(...)` +
`ImGui::SetNextWindowPos(mainViewport->WorkPos, ImGuiCond_Always)` +
`ImGui::SetNextWindowSize(...)` (clamped to the main viewport's own work area)
before `ImGui::Begin()`, then clears the flag — guaranteeing the window is
visible to `GET /get_swapchain` the very first captured frame after an
HTTP-driven open.

## Testing this feature

Every layer above (`FrameDebuggerCaptureContext` — including its
`RecordEntityDraw()`/`ReplayStepPreviews()` bookkeeping, the real snapshot
builder and its generic compute-dispatch discovery PLUS its new
`FrameDebuggerStepPreviewKind`/`stepPreviewIndex` assignment, the
single-capture `FrameDebuggerCurrentCapture` state machine
(`HasCapture()`/`Clear()`), the preview compositing CPU oracle, the
step-kind-aware preview-source picking rule
(`ChooseFrameDebuggerPreviewSource()`), the command bridge, and every HTTP
query parser/response builder) has dedicated Tier-1 test coverage — see
`tests/Editor/FrameDebugger*Tests.cpp`,
`tests/Application/FrameDebuggerCommandBridgeTests.cpp`, and the
`ParseFrameDebugger*`/`BuildFrameDebugger*` cases in
`tests/Network/NetworkRoutesTests.cpp`. `RenderSystem::Draw()`'s own new
`maxDrawCount` iteration cutoff stayed a plain inline loop `break` rather than
a separately-tested pure helper (there is no second boolean/branch to combine
it with — extracting it would have been pure ceremony), and
`AddFrameDebuggerReplayPasses()` itself (the new N-replay-pass Render Graph
declaration) needs a live `Renderer`/`RenderGraph` and stays Tier 2/untested
directly, same as `FrameDebuggerCurrentCapture::CaptureFrame()` always has
been — both were instead proven correct via a manual, screenshot-verified
visual spot-check during their own phase
(`task_manager/frame-debugger-7/PHASE3_COMPLETION_REPORT.md`), the same
accepted verification bar this codebase already uses for Tier 2 Render Graph
work (see `AGENTS.md`, "Testability & Regression Safety").

A full, genuine, HTTP-driven, screenshot-verified end-to-end smoke test (open →
enable → auto-capture → select event → explicit capture → step history → set
channel → set levels, each step visually confirmed via `GET /get_swapchain`)
was part of the `frame-debugger-3` campaign's own closing verification — see
`task_manager/frame-debugger-3/PHASE8_COMPLETION_REPORT.md` and
`CAMPAIGN_COMPLETION_REPORT.md` for the full evidence (note: the
`step_history` route it exercised no longer exists as of `frame-debugger-7` —
see "What's new (`frame-debugger-7` campaign)" below). This finally closed the
manual-verification gap `frame-debugger-1`/`frame-debugger-2` both had to
accept — there is no longer any manual-verification limitation for this
feature. The `frame-debugger-4` campaign's own follow-up live smoke test (see
`task_manager/frame-debugger-4/PHASE3_COMPLETION_REPORT.md`/
`CAMPAIGN_COMPLETION_REPORT.md`) re-ran the same HTTP-automation-driven
approach specifically to prove the atmosphere-compositing bug fix, the
`frame-debugger-5` campaign's own closing phase
(`task_manager/frame-debugger-5/PHASE5_COMPLETION_REPORT.md`/
`CAMPAIGN_COMPLETION_REPORT.md`) ran it once more to prove every atmosphere LUT
compute pass appears as its own selectable leaf with its own correct, distinct
real output image, and the `frame-debugger-7` campaign's own closing phase
(`task_manager/frame-debugger-7/PHASE7_LIVE_VERIFICATION_FULL_BUILD_AND_CAMPAIGN_COMPLETION.md`/
`CAMPAIGN_COMPLETION_REPORT.md`) is where this feature's own two newly-fixed
bugs — the wrong-first-capture timing bug and the per-object
accumulated-preview mechanism — get their own final, live, HTTP-driven,
screenshot-verified proof.

## Still-deferred future work

**True per-pass "stop"/breakpoint execution control** — pausing the GPU
mid-frame at a specific compute dispatch boundary for live step-through
inspection — remains explicitly out of scope for every campaign so far. Every
real compute dispatch is now "trackable" (a real, inspectable tree leaf with
its own real output preview), and "stoppable" today means the EXISTING
Enable/Step/Capture frame-level controls (`frame-debugger-3`'s own PHASE3) let
an engineer freeze a specific frame and walk its compute passes one at a time
in the tree — a genuine mid-command-buffer GPU pause/breakpoint would need
this engine's `Renderer::ImmediateSubmit()`/render-graph model to support a
partial, resumable command-buffer submission, which it does not today. See
`TODO.md`'s "Frame Debugger" section for this item tracked as a named, still-
deferred future item.

**An O(N) shared-target replay optimization** — `frame-debugger-7`'s own
per-object replay-rendering mechanism (see "What's new (`frame-debugger-7`
campaign)" below) deliberately pays an O(N²) total draw-call cost (N replay
passes, redrawing objects `[0..i]` from scratch each time) rather than an O(N)
scheme that would draw each object exactly once into one shared,
incrementally-accumulated scratch target and copy an intermediate result out
after each draw. That O(N) scheme was investigated and explicitly deferred
(`task_manager/frame-debugger-7/PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md`'s
own Step 2) because it would require widening `rg::PassContext` with a new
raw-`VkImage`-copy capability shared by every other pass in the engine — a
higher-risk, broader-blast-radius change than this feature's own scope
justified for work that only ever runs once per explicit, human-triggered
capture. Worth revisiting only if a real scene's object count ever makes the
O(N²) cost noticeably slow in practice — not a problem observed so far.
