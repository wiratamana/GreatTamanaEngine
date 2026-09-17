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
full story. **This is the CURRENT, real system** — follow these rules whenever
touching this window, its data model, its capture instrumentation, or its
cross-wiring with the Pause/Resume toolbar or the embedded HTTP server:

## What is real today

- **Capture is genuinely real, and PASS-LEVEL for every pass EXCEPT
  `"GameView"` itself — `"GameView"` now ALSO gets one real, individually
  selectable CHILD leaf per real per-entity draw call it issued that frame**
  (`frame-debugger-6` campaign, PHASE4 — see "What's new (`frame-debugger-6`
  campaign)" below). This is an explicit, user-approved BREAKING change to the
  historical "one leaf per PASS, never one leaf per mesh/entity" rule
  `frame-debugger-2`'s own `PHASE0_MASTER_STRATEGY.md` originally locked (its
  own Locked Design Decision #1) — do not mistake the now-per-entity
  `"GameView"` children for a mistake or a regression; every OTHER pass in
  this tree (every compute dispatch, and `"GameView"` itself as a pass-level
  row) remains exactly one leaf per pass, unchanged.
- **Every real compute-shader dispatch that ran this frame is now a
  first-class, AUTOMATICALLY DISCOVERED tree citizen — never a hand-maintained
  per-pass-name special case** (`frame-debugger-5` campaign). The tree is:

  ```
  "GameView" (root)
    |-- "Compute Dispatches (Pre-GameView)"   (only present if >=1 child)
    |     |-- <every surviving compute pass whose own real execution-order
    |     |     index in graphSnapshot.passesInExecutionOrder is BEFORE
    |     |     "GameView"'s own index, in real execution order>
    |-- "GameView" leaf                        (the one real graphics/draw pass)
    |     |-- <one real child leaf PER real per-entity draw call this pass
    |     |     issued this frame - e.g. "terrain (Entity 2)",
    |     |     "SmokeTestCube (Entity 3)" - frame-debugger-6 campaign, PHASE4>
    |-- "Compute Dispatches (Post-GameView)"  (only present if >=1 child)
          |-- <every surviving compute pass whose own real execution-order
          |     index is AFTER "GameView"'s own index, in real execution order>
  ```

  Concretely, on a typical frame with a Sun light present: `"Compute Dispatches
  (Pre-GameView)"` holds `AtmosphereTransmittanceLutPass`,
  `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`,
  `AtmosphereAerialPerspectiveVolumePass`, plus every GPU-skinning dispatch
  request active that frame (`RenderPasses.cpp`'s `AddGpuSkinningPasses()`),
  and `"Compute Dispatches (Post-GameView)"` holds
  `AtmosphereAerialPerspectiveCompositePass` (and, if the Editor's "Show
  Compute Blur (debug)" toggle is on, `"ComputeBlurValidation"`). Neither group
  is ever added at all when it would have zero real children (mirrors every
  other "never an empty, misleading group" rule elsewhere in this tree) — a
  single unconditional group placed after `"GameView"` would misrepresent
  which passes really ran before it (e.g. `"GameView"`'s own Sky-Background
  sub-draw samples the Sky-View LUT, so that LUT pass must already have run).
  Discovery is fully generic: `RenderGraphBuilder::AddComputePass()` is the one
  real "choke point" every compute dispatch in this engine already funnels
  through, and it now stamps a real, structurally-tracked
  `PassRecord::isComputePass` flag (survives into
  `RenderGraphPassSnapshot::isComputePass`, `RenderGraphTypes.h`/
  `RenderGraphSnapshot.h`) — `FrameDebuggerData.cpp`'s
  `BuildRealFrameDebuggerSnapshot()` walks every surviving (`isCulled == false`)
  `isComputePass == true` pass in the current frame's real
  `RenderGraphSnapshot` and builds one uniform `BuildComputeDispatchLeaf()` per
  pass — a future compute pass added anywhere in this engine appears here
  automatically, with **zero further Frame-Debugger-specific code ever
  required for it to show up.** A compute-dispatch leaf's `passName`/
  `shaderName`/tree-row text are all the pass's own RAW, real name (never a
  fabricated friendly label like the old, now-removed `"GPU Skinning"`/
  `"Aerial Perspective Composite"` special cases used to invent), and every
  read/write row is labeled by its own real `ResourceKind`
  (`"Read Texture"`/`"Read Buffer"`/`"Read Volume Texture"`/`"Write Texture"`/
  `"Write Buffer"`/`"Write Volume Texture"`, from
  `RenderGraphPassSnapshot::readKinds`/`writeKinds` — parallel vectors to the
  existing `readNames`/`writeNames`, PHASE1 of `frame-debugger-5`) — never
  mislabeled, never guessed by probing multiple registries. A culled compute
  pass never appears in either group (it did not really run this frame).
- **Capture is snapshot-ON-DEMAND, never continuous.** A "frame" is captured
  exactly once, on a real trigger (Enable's false→true edge, a Step while
  Enabled, or the explicit "Capture" button), and stays frozen/inspectable
  until the next trigger fires. `FrameDebuggerPanel` never rebuilds its
  displayed snapshot on every ImGui frame.
- **Scope is Game View ONLY, permanently.** `"SceneView"`/`"Present"` passes
  are deliberately excluded from the Frame Debugger's own captured event tree,
  even though they exist in the same underlying `RenderGraphSnapshot` the
  "Render Graph" panel already displays in full. Relaxing this filter to cover
  Scene View/Present is a clean, well-isolated future extension point (a
  single line in the snapshot builder's filter), not attempted by any
  campaign so far.
- **A REAL multi-frame history ring buffer exists**
  (`FrameDebuggerHistory`/`FrameDebuggerHistoryEntry`,
  `src/Editor/FrameDebuggerHistory.h/.cpp`), holding the last
  `kFrameDebuggerHistoryCapacity` (8) captured frames, each a full,
  independent, already-resolved `FrameDebuggerSnapshot` PLUS its own retained
  GPU copy textures of that historical frame's real output (see "Preview
  reconstruction" below for exactly how many, and of what, per slot). A
  "Frame History" Prev/Next mini-toolbar
  (`FrameDebuggerPanel::BuildFrameHistoryToolbarRow()`) scrubs across this ring
  buffer — a SEPARATE axis from the pre-existing "N of M" event-stepper row
  (position within the CURRENTLY-VIEWED captured frame's own event list); do
  not conflate the two.
- **Shader/pass-state "reflection" is real, but PASS-scoped, aggregated
  across every real draw call that pass issued that frame — never
  per-individual-mesh.** For the `"GameView"` leaf: `shaderName` lists every
  DISTINCT real `Pipeline` debug name actually used that frame (see
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
  campaign, not just a data-plumbing change. Every compute-dispatch leaf's
  blend/Z/stencil rows all read `"n/a (compute pass)"` (it never issues a draw
  call), and its `vectors` include its own real GPU timing sample whenever one
  is present.
- **Preview reconstruction is real, but still a cheap "copy right after the
  relevant real event finishes"** — not a full mid-pass draw-call replay.
  `FrameDebuggerHistory` retains, per captured frame:
  - `preview` — the true PRE-atmosphere-composite `"GameView"` output.
  - `compositedPreview` — the true POST-atmosphere-composite, final
    `"GameViewComposited"` output (`std::nullopt` only for a capture taken
    before the atmosphere-composite pass had ever produced anything yet this
    session — `frame-debugger-4` campaign).
  - `computePassPreviews` — (`frame-debugger-5` campaign) one retained GPU
    copy PER real, surviving compute-dispatch pass that had a real visual
    write this capture, keyed by that pass's own raw name: a pass whose FIRST
    `Texture`-kind write is found gets a direct GPU-to-GPU copy of that
    texture (sourced from the already-existing
    `RenderGraphDebugTextureRegistry`/`RenderGraph::DebugTextureSnapshotFor()`
    — no new registry needed); a pass whose only visual write is a
    `VolumeTexture` (today: the Aerial Perspective froxel volume) instead gets
    a real ray-marched 2D thumbnail, produced by reusing the ALREADY-SHIPPED
    `VolumeTexturePreviewRenderer::RenderPreview()` (`src/Renderer/
    VolumeTexturePreviewRenderer.h/.cpp` — the EXACT SAME renderer
    `GET /get_texture` already uses to preview a volume texture over HTTP,
    including the SAME shared interpretation-selection rule,
    `SelectVolumeTexturePreviewInterpretation()`) and uploading its raw RGBA8
    pixels into a fresh `RenderTexture`. A pass with neither kind of visual
    write (e.g. GPU Skinning's own `Buffer`-kind write) correctly gets **no**
    entry at all — a real, honest "not available" state, never a
    wrong/fabricated image. All of this is eager: every real capture trigger
    (Enable-edge / Step / explicit Capture) re-populates every retained
    texture, never a lazy "only copy the one the user happens to click" scheme
    — every leaf's own preview is instantly available the moment it's clicked.

  Selecting the literal `"GameView"` leaf shows the pre-composite `preview`
  image (preserving this feature's original "as of the exact point this event
  finished" semantics for that one leaf); selecting any compute-dispatch leaf
  that has its own retained `computePassPreviews` entry shows THAT PASS'S OWN
  real output image; selecting anything else — including nothing selected, or
  a compute-dispatch leaf with no retained preview of its own (e.g. GPU
  Skinning) — falls back to the post-composite, final `compositedPreview`
  image, i.e. the SAME pixels the "Game" panel / `GET /get_game_view` show.
  This rule is centralized in a pure, Tier-1-tested function,
  `ChooseFrameDebuggerPreviewSource()` (`src/Editor/FrameDebuggerData.h/.cpp`),
  which `Panels/FrameDebuggerPanel.cpp`'s `EnsurePreviewDescriptor()` calls
  into rather than re-implementing the decision inline.
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
  `FormatFrameHistoryLabel()`, `ClampSelectedEventIndex()`,
  `FindEventDetailsByIndex()`, `FormatVectorProperty()`,
  `FormatMatrixProperty()`, `ChooseFrameDebuggerPreviewSource()` (the pure
  "which retained image should the preview box show" decision — see "Preview
  reconstruction" above, widened by `frame-debugger-5` to also consider a
  selected compute-dispatch leaf's own retained preview), and
  (`frame-debugger-5` campaign) `CollectComputePassTextureWrites()`/
  `CollectComputePassVolumeTextureWrites()` (the two pure, CPU-side discovery
  functions `FrameDebuggerHistory::CaptureFrame()` uses to find which compute
  passes' own write textures/volume textures to retain a real copy of, reading
  `rg::RenderGraphSnapshot` directly rather than the Editor's own display
  strings). Mirrors `JobsPanelData.h`/`ProfilerPanelData.h`'s
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
  `Pipeline`/`MaterialTexture` debug name and the last real view-projection
  matrix used this frame. Also home to `DescribeStandardPipelineState()` (see
  above). Tier-1-tested by `tests/Editor/FrameDebuggerCaptureTests.cpp`.
- **`src/Editor/FrameDebuggerHistory.h/.cpp`** — the real ring buffer, now
  retaining THREE kinds of preview per slot: `FrameDebuggerHistoryEntry::preview`/
  `compositedPreview` (`frame-debugger-4` campaign, see "Preview
  reconstruction" above) plus `computePassPreviews` (`frame-debugger-5`
  campaign — one entry per real compute-dispatch pass with a visual write this
  capture, 2D-texture-copy OR volume-ray-march-thumbnail as appropriate).
  Tier-1-tested by `tests/Editor/FrameDebuggerHistoryTests.cpp` (the pure
  write-state/cursor-clamp arithmetic — `CaptureFrame()`'s own multi-copy
  `ImmediateSubmit()` body, plus the volume-ray-march branch, stay
  Tier-2/untested directly, since both need a live `VkDevice` — the PURE
  discovery logic each one depends on, `CollectComputePassTextureWrites()`/
  `CollectComputePassVolumeTextureWrites()`, is fully extracted and Tier-1-
  tested instead, see above).
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
  (`m_captureContext`), the real history ring buffer (`m_history`), the real
  Channels/Levels state (`m_channel`/`m_levelsBlack`/`m_levelsWhite`), its own
  preview-texture ImGui descriptor (self-owned, mirroring
  `BoneViewerWindow`'s own "owns its own `ImGui_ImplVulkan_AddTexture()`
  descriptor" precedent), and its own `FrameDebuggerPreviewRenderer`
  instance. Called explicitly by name from `ImGuiEditorLayer::BuildUI()` — no
  `IEditorPanel` interface. As of `frame-debugger-5`, it no longer takes a
  `gpuSkinningPassNamesThisFrame` parameter anywhere — GPU Skinning passes are
  discovered exactly like every other compute pass now (see above).
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
  triggers the first real capture** (see
  [Time and Playback Pause](time-and-playback-pause.md)): turning "Enable" ON
  sets `ctx.playbackPaused = true` (the same field `PlaybackControls.cpp`'s
  own "Pause" button writes) and calls `TriggerCapture()`, both via a shared
  `ApplyEnabledEdge()` helper used identically by the hand-driven checkbox and
  the HTTP `SetEnabledFromCommand()` entry point, so the two paths can never
  silently diverge. Turning "Enable" back OFF still deliberately does **not**
  auto-resume playback, unchanged from `frame-debugger-2`.
- **The "Editor" mode combo remains a purely cosmetic, permanently-disabled
  stub** — this engine still has no Play/Edit-mode split, so there is nothing
  real for it to switch between.
- **The event tree pane shows a real, non-empty tree the moment a real
  capture has happened**, falling back to "No frame captured yet." only in
  the honest "enabled, but nothing captured this session yet" state. The
  event-details section shows real Shader/Pass/Blend/Z-state/Stencil/
  Textures/Vectors/Matrices data once a real leaf is selected, falling back
  to "No event selected." only when nothing is selected.

## HTTP automation (`frame-debugger-3` PHASE7)

A brand-new, fully independent sibling of `EditorUiCommandBridge`,
`FrameDebuggerCommandBridge` (`src/Application/FrameDebuggerCommandBridge.h/.cpp`
— never an extension of `EditorUiCommandKind`, per `AGENTS.md`'s own rule that
a genuinely new KIND of request gets its own bridge), backs eight new routes
under `src/Network/NetworkRoutes.h/.cpp`/`NetworkServer.cpp`:

- `GET /frame_debugger/open` — opens the window (a `false -> true`
  PROGRAMMATIC transition also arms a genuine ONE-SHOT main-viewport pin, see
  below; never affects the manual "Window > Frame Debugger" menu item's own
  drag-anywhere freedom).
- `GET /frame_debugger/enable?value=true|false` — flips "Enable", exactly
  mirroring the checkbox's own `ApplyEnabledEdge()` behavior.
- `GET /frame_debugger/capture` — fires an explicit new capture (equivalent
  to clicking "Capture").
- `GET /frame_debugger/select_event?index=<n>` — selects a leaf event by
  index (`-1` deselects).
- `GET /frame_debugger/step_history?direction=prev|next` — scrubs the
  Frame-History ring-buffer cursor.
- `GET /frame_debugger/set_channel?value=all|r|g|b|a` — sets the Channels
  isolation (completely separate name/value space from `/get_texture`'s own
  `channel=color|depth`, per the Locked Design Decision above).
- `GET /frame_debugger/set_levels?black=<f>&white=<f>` — sets the Levels
  remap range.
- `GET /frame_debugger/state` — a flat, no-`"success"`-wrapper JSON dump of
  every piece of live state (`enabled`/`historyCount`/`historyCursor`/
  `selectedEventIndex`/`totalEventCount`/`channel`/`levelsBlack`/
  `levelsWhite`/`windowOpen`). Every OTHER route's response ALSO includes this
  same `"state"` object (nested under `success`), so a caller can assert
  state without a second round-trip after every command.

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

Every layer above (`FrameDebuggerCaptureContext`, the real snapshot builder
and its generic compute-dispatch discovery, the history ring buffer's pure
write-state/cursor arithmetic, the two per-pass write-collection functions,
the preview compositing CPU oracle, the composite-and-compute-pass-aware
preview-source picking rule (`ChooseFrameDebuggerPreviewSource()`), the
command bridge, and every new HTTP query parser/response builder) has
dedicated Tier-1 test coverage — see `tests/Editor/FrameDebugger*Tests.cpp`,
`tests/Application/FrameDebuggerCommandBridgeTests.cpp`, and the
`ParseFrameDebugger*`/`BuildFrameDebugger*` cases in
`tests/Network/NetworkRoutesTests.cpp`. A full, genuine, HTTP-driven,
screenshot-verified end-to-end smoke test (open → enable → auto-capture →
select event → explicit capture → step history → set channel → set levels,
each step visually confirmed via `GET /get_swapchain`) is part of this
feature's own closing verification — see
`task_manager/frame-debugger-3/PHASE8_COMPLETION_REPORT.md` and
`CAMPAIGN_COMPLETION_REPORT.md` for the full evidence. This finally closes the
manual-verification gap `frame-debugger-1`/`frame-debugger-2` both had to
accept — there is no longer any manual-verification limitation for this
feature. The `frame-debugger-4` campaign's own follow-up live smoke test (see
`task_manager/frame-debugger-4/PHASE3_COMPLETION_REPORT.md`/
`CAMPAIGN_COMPLETION_REPORT.md`) re-ran the same HTTP-automation-driven
approach specifically to prove the atmosphere-compositing bug fix, and the
`frame-debugger-5` campaign's own closing phase
(`task_manager/frame-debugger-5/PHASE5_COMPLETION_REPORT.md`/
`CAMPAIGN_COMPLETION_REPORT.md`) ran it once more, specifically proving every
atmosphere LUT compute pass now appears as its own selectable leaf under the
split `"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches
(Post-GameView)"` groups, each showing its own correct, distinct real output
image — the Transmittance LUT/Multi-Scattering LUT/Sky-View LUT leaves each
show their own distinct texture, the Aerial Perspective Volume leaf shows a
real ray-marched thumbnail, and the Aerial Perspective Composite leaf still
pixel-matches `GET /get_game_view`, exactly as `frame-debugger-4` already
proved for that one leaf.

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
