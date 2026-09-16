# PHASE0 — MASTER STRATEGY: Compute Shader Dispatch as a Tier-1 Citizen of the Frame Debugger

Campaign folder: `task_manager/frame-debugger-5/`
Branch: `feature/frame-debugger-impl`
Depends on: `task_manager/frame-debugger-4/` (the dual-stage retained-capture + composite-aware preview
campaign — `src/Editor/FrameDebuggerHistory.h/.cpp`, `FrameDebuggerData.h/.cpp`,
`Panels/FrameDebuggerPanel.h/.cpp`), `task_manager/compute_shader/` (the campaign that built
`RenderGraphBuilder::AddComputePass()`/`ResourceAccess::ComputeShaderRead`/`ComputeShaderWrite` in the
first place), and `task_manager/atmosphere-scattering-1..4/` (the campaigns that built every atmosphere LUT
compute pass this campaign makes visible).

This is the **orchestrator** document. It contains no implementation instructions of its own — each child
phase (`PHASE1`..`PHASE5`) is a self-contained, independently-compilable chunk. Read this file first, then
work the phases **in numeric order** (each assumes every previous one already landed). Every phase file
below MUST open with "## Parent -> PHASE0_MASTER_STRATEGY.md, read it first" and end with its own fast
compile-check command plus a `PHASEn_COMPLETION_REPORT.md` write-up, exactly like `frame-debugger-4`'s own
precedent.

## STANDING RULE for every phase, every implementer, every delegated task (non-negotiable)

**If you hit a genuine design decision, ambiguity, or "which of two reasonable approaches" fork that this
document (or your own assigned phase document) does not already resolve explicitly, STOP and call the
`ask_questions` tool with concrete, easy-to-understand options BEFORE writing speculative code around it.**
This applies to every phase's own implementer AND to the double-check/review passes. Do not silently guess
at a product/design decision and move on — this whole campaign exists because a previous silent assumption
("only the passes I already know the name of matter") caused the exact bug this campaign fixes. Prefer
asking a short, concrete, multiple-choice question over inventing a plausible-sounding answer.

## Phase list

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_RENDERGRAPH_COMPUTE_DISPATCH_CHOKEPOINT_INFRASTRUCTURE.md` | The real "choke point" fix: gives `RenderGraphBuilder::AddComputePass()` a REAL, tracked meaning (today it is a no-op cosmetic alias of `AddPass()`) by adding `PassRecord::isComputePass`/`RenderGraphPassSnapshot::isComputePass`, plus per-read/write `ResourceKind` tagging (`readKinds`/`writeKinds`, parallel to the existing `readNames`/`writeNames`) so any consumer can tell a texture write from a buffer write from a volume-texture write without guessing. Zero behavior change to rendering itself — pure, additive metadata. |
| 2 | `PHASE2_GENERIC_COMPUTE_DISPATCH_EVENT_TREE_DISCOVERY.md` | Replaces EVERY hand-written, per-pass-name special case in `FrameDebuggerData.cpp` (`"GPU Skinning"` name-list, hardcoded `"AtmosphereAerialPerspectiveCompositePass"` lookup) with ONE generic algorithm that discovers every real, surviving compute pass in the current frame's `RenderGraphSnapshot` via PHASE1's new `isComputePass` flag, and builds one uniform "Compute Dispatch" leaf per pass, split into two new tree groups (`"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"`) positioned by each pass's own real position relative to `"GameView"` (v2 review finding — Locked Design Decision #8, `PHASE0_MASTER_STRATEGY.md`; a single group unconditionally after `"GameView"` would misrepresent passes that really ran before it, e.g. GPU Skinning/every atmosphere LUT pass). A future compute pass added anywhere in this engine appears in the Frame Debugger automatically, with ZERO further `FrameDebuggerData.cpp` changes ever required again. |
| 3 | `PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md` | Fixes the "selecting a new compute leaf still shows the whole Game View image, not that pass's own output" gap PHASE2 alone would leave: `FrameDebuggerHistory` eagerly retains a real GPU copy of EVERY compute pass's own 2D-texture write, every single capture, keyed by pass name, sourced from the already-existing `RenderGraphDebugTextureRegistry`. The Inspector's preview box now shows the SELECTED compute pass's own real output whenever one is selected. |
| 4 | `PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md` | The one remaining gap PHASE3 cannot close with a plain image copy: a compute pass whose only visual write is a 3D volume texture (today: the Aerial Perspective froxel volume). Reuses the ALREADY-SHIPPED `VolumeTexturePreviewRenderer` ray-march thumbnail renderer (the same code `GET /get_texture` already uses for a volume) to produce a real ray-marched 2D preview, retained into the same per-pass preview mechanism PHASE3 built. |
| 5 | `PHASE5_TESTS_DOCS_FULL_BUILD_AND_LIVE_VERIFICATION.md` | Widens/adds Tier-1 tests for every prior phase, corrects every doc claim this campaign makes stale (`AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md`), then a full clean build (both `GTE_ENABLE_EDITOR` configs) + full `ctest` regression + a live, HTTP-automation-driven, screenshot-verified smoke test proving every atmosphere LUT compute pass (Transmittance/Multi-Scattering/Sky-View/Aerial-Perspective-Volume/Aerial-Perspective-Composite) now appears as its own selectable leaf, each showing its own correct, distinct real output image. |

---

## Step 1: The Goal (Where are we going?)

The Editor's "Frame Debugger" window must make **every real compute-shader dispatch that ran this frame**
a first-class, trackable, inspectable citizen of the render-graph event tree — not just the two or three
compute passes some previous campaign happened to remember to hand-write a special case for. Concretely,
after this campaign:

- Literally **every** compute pass that survives culling in the current captured frame's `RenderGraphSnapshot`
  appears as its own real, selectable leaf in the Frame Debugger's event tree — GPU Skinning dispatches, the
  Atmosphere Transmittance LUT pass, the Atmosphere Multi-Scattering LUT pass, the Atmosphere Sky-View LUT
  pass, the Aerial Perspective froxel volume pass, the Aerial Perspective Composite pass, the Aerial
  Perspective Volume Debug-Slice pass, the Compute Blur Validation pass, and any compute pass a future
  campaign adds — with **zero further hand-written code in `FrameDebuggerData.cpp` required** for that
  future pass to show up. This is the literal meaning of "a choke point for compute shader dispatch": one
  single place (`RenderGraphBuilder::AddComputePass()`) that every real compute dispatch in this engine
  already funnels through becomes the one place that makes it observable, forever, automatically.
- Each such leaf shows real, honest, pass-scoped facts — its real raw pass name, its real read/write
  resource names (correctly labeled as a texture, a buffer, or a volume texture — never mislabeled), and
  its real GPU timing — exactly like every existing leaf already does, never a fabricated placeholder.
- Selecting any one of these leaves shows **that pass's own real output image** in the Inspector's preview
  box — not, as today, always the same whole-frame Game View image regardless of which leaf is selected.
  A 2D texture output is shown directly; a 3D volume-texture output (the Aerial Perspective froxel volume)
  is shown via a real ray-marched thumbnail, reusing this engine's own already-shipped volume-preview
  renderer.
- Every existing Tier-1 test either still passes or is deliberately, explicitly updated (never silently
  broken) to match the new, unified, generic tree shape — this campaign explicitly REPLACES
  `frame-debugger-4`'s own hardcoded `"AtmosphereAerialPerspectiveCompositePass"` special case and
  `frame-debugger-3`'s own hardcoded `"GPU Skinning"` name-list special case with one unified mechanism (a
  deliberate, user-approved breaking change to that specific tree shape — see Locked Design Decision #2/#6
  below).
- Both `GTE_ENABLE_EDITOR=ON`/`=OFF` configurations still build cleanly, and a full `ctest` regression pass
  stays at 100%.

## Step 2: The Situation (Where are we now?) — full root-cause investigation

Investigated directly in this repository, on `feature/frame-debugger-impl` (every file/line reference below
was correct at investigation time — re-read the live source at implementation time, this repo churns fast).

### 2.1 `RenderGraphBuilder::AddComputePass()` is a "PURELY COSMETIC" no-op alias — literally, by its own doc comment

`src/Renderer/RenderGraph/RenderGraphBuilder.h`:

```cpp
// Phase 6 of the compute-shader campaign ... - a thin, PURELY COSMETIC alias of AddPass() above,
// with zero behavioral difference: a pass's behavior is entirely determined by what it declares in
// `reads`/`writes` (via `setup`), never by which entry point created it.
template <typename SetupFn, typename ExecuteFn>
void AddComputePass(const char* name, SetupFn&& setup, ExecuteFn&& execute)
{
    AddPass(name, std::forward<SetupFn>(setup), std::forward<ExecuteFn>(execute));
}
```

`PassRecord` (`RenderGraphTypes.h`) has **no field anywhere** recording "this pass is a compute dispatch".
`RenderGraphPassSnapshot` (`RenderGraphSnapshot.h`, the struct the Editor's Frame Debugger and "Render
Graph" panel both actually read) inherits the same gap. There is today **no way, anywhere in this engine,
to generically ask "which passes that ran this frame were compute dispatches"** — the only way to know is
to already know every compute pass's exact string name in advance and grep for it by name.

### 2.2 Confirmed: there are (at least) 8 real `AddComputePass()` call sites today, and the Frame Debugger only ever shows 3 of them

```
Application/RenderPasses.cpp:161        AddGpuSkinningPasses()               - one pass PER skinned model, name = request.name
Editor/ComputeBlurValidation.cpp:108    ComputeBlurValidation::AddPass()     - "BoxBlur" (validation/debug tool)
Renderer/Atmosphere/AtmosphereLutRenderer.cpp:236   AddTransmittanceLutPass()            - "AtmosphereTransmittanceLutPass"
Renderer/Atmosphere/AtmosphereLutRenderer.cpp:321   AddMultiScatteringLutPass()          - "AtmosphereMultiScatteringLutPass"
Renderer/Atmosphere/AtmosphereLutRenderer.cpp:457   AddSkyViewLutPass()                  - "AtmosphereSkyViewLutPass"
Renderer/Atmosphere/AtmosphereLutRenderer.cpp:594   AddAerialPerspectiveVolumePass()     - "AtmosphereAerialPerspectiveVolumePass"
Renderer/Atmosphere/AtmosphereLutRenderer.cpp:757   AddAerialPerspectiveCompositePass()  - "AtmosphereAerialPerspectiveCompositePass"
Renderer/Atmosphere/AtmosphereLutRenderer.cpp:936   AddAerialPerspectiveVolumeDebugSlicePass() - "AtmosphereAerialPerspectiveVolumeDebugSlicePass"
```

`src/Editor/FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()` — the ONLY function that decides
what appears in the Frame Debugger's event tree — handles exactly THREE of these, all by hardcoded name:

1. `"GameView"` (the one real GRAPHICS pass — correctly special-cased, this is fine and stays).
2. Whatever pass names appear in a caller-supplied `gpuSkinningPassNamesThisFrame` vector (sourced, several
   layers up, from `Game::CollectGpuSkinningDispatchRequests()` — a completely separate, parallel source of
   truth from the render graph itself, kept in sync only by convention/care, never structurally guaranteed).
3. `FindPassByName(graphSnapshot.passesInExecutionOrder, "AtmosphereAerialPerspectiveCompositePass")` — one
   single, literal string, hardcoded by the `frame-debugger-4` campaign specifically for that one pass.

**The other five real compute passes — Transmittance LUT, Multi-Scattering LUT, Sky-View LUT, Aerial
Perspective Volume, Aerial Perspective Volume Debug-Slice, plus Compute Blur Validation's own pass — never
appear anywhere in the Frame Debugger's tree, ever, regardless of whether they ran this frame.** This is
the user-reported bug this whole campaign exists to fix: "I don't think it's catching all compute shader
dispatch operations... it involves creating multiple LUT textures using compute shader."

### 2.3 Even the ONE compute pass the Frame Debugger already shows only "works" by coincidence, not by design

`FrameDebuggerHistoryEntry` (`src/Editor/FrameDebuggerHistory.h`) retains exactly TWO images per captured
frame: `preview` (the pre-atmosphere-composite `"GameView"` copy) and `compositedPreview` (the
post-composite `"GameViewComposited"` copy). `Panels/FrameDebuggerPanel.cpp`'s preview-picking logic
(`ChooseFrameDebuggerPreviewSource()`, `FrameDebuggerData.cpp`) only ever chooses between these same two
whole-frame images, regardless of which tree leaf is selected (except the literal `"GameView"` leaf, which
picks `preview`). Selecting the existing `"AtmosphereAerialPerspectiveCompositePass"` leaf happens to show
the CORRECT image today only because that one pass's own write target genuinely IS
`"GameViewComposited"` — the exact same texture `compositedPreview` already retains for an entirely
different reason (it's the frame's final output). **If a leaf were naively added for, say, the
Transmittance LUT pass today, selecting it would show the wrong image** — the whole Game View, not the
256x256 Transmittance LUT texture that pass actually wrote. There is currently **no generic mechanism at
all** for "show me THIS specific pass's own real output texture."

### 2.4 The infrastructure needed to fix 2.3 generically ALREADY EXISTS, mostly unused for this purpose

`RenderGraph` (`src/Renderer/RenderGraph/RenderGraph.h`) already owns two passive, name-keyed registries,
updated automatically every single `ExecuteCompiledGraph()` call, in both `ExecuteTimingMode` regimes,
for **every** texture/volume-texture any pass ever `CreateTexture()`s/`ImportTexture()`s/
`ImportVolumeTexture()`s:

- `RenderGraphDebugTextureRegistry` (`m_debugTextures`) — queryable via `RenderGraph::DebugTextureSnapshotFor(name)`,
  already the backing store for `GET /get_texture`/`GET /list_textures`.
- `RenderGraphDebugVolumeTextureRegistry` (`m_debugVolumeTextures`) — the volume-texture counterpart,
  queryable via `RenderGraph::DebugVolumeTextureSnapshotFor(name)`.

Both already carry the CURRENT physical `RenderTarget`/`VolumeTarget` (image, view, extent, format) AND the
tracked `ResourceState` (layout/stage/access) for every name known this session. `FrameDebuggerPanel`
already holds a `const rg::RenderGraph* m_frameRenderGraph` (stashed every `Build()` call, already used
today, see `Panels/FrameDebuggerPanel.h`) — so this registry is ALREADY reachable from exactly the place
that needs it. Nothing new needs to be invented here; PHASE3 wires existing infrastructure together instead
of building a new one from scratch.

There is also an already-shipped, already-correct volume-to-2D-thumbnail ray-march renderer,
`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp` (used today by `Application.cpp` to serve
`GET /get_texture` for a volume texture) — PHASE4 reuses this verbatim rather than writing a new one.

### 2.5 A subtle correctness gap PHASE1 must also close: read/write names carry no resource-KIND tag

`RenderGraphPassSnapshot::readNames`/`writeNames` (`RenderGraphSnapshot.h`) are plain `std::vector<std::string>`
built from `ResourceUsageName()` (`RenderGraphSnapshot.cpp`), which resolves EITHER a `TextureHandle` OR a
`BufferHandle` OR a `VolumeTextureHandle`'s name into the exact same flat string list, with **no tag
distinguishing which kind each entry actually is**. Confirmed live: GPU Skinning's own compute pass
declares `pass.WriteBuffer(handle, ...)` (`Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()`) — its
own output buffer's name ends up in `writeNames` exactly like a real texture write would, with nothing
anywhere distinguishing "this name is a buffer, not previewable as an image" from "this name is a real 2D
texture, previewable directly." Building a fully generic "Read Texture"/"Write Texture" row label (PHASE2)
or a fully generic "go fetch this write name's current texture for preview" step (PHASE3) both need this
distinction to be correct, not just "probe every registry and hope only one answers." PHASE1 closes this
gap with two new, purely-additive parallel vectors (`readKinds`/`writeKinds`) — confirmed via a full grep
that only `Editor/FrameDebuggerData.cpp` and `Editor/Panels/RenderGraphPanel.cpp` consume `readNames`/
`writeNames` in production, and both only ever join them into display strings — so this addition carries
no risk to any existing consumer.

## Step 3: The Plan (detailed strategy)

### 3.1 Architecture at a glance (after this campaign)

```
RenderGraphBuilder::AddComputePass("AnyComputePassName", setup, execute)      <- THE CHOKE POINT (PHASE1)
    |  sets PassRecord::isComputePass = true (the ONE new fact recorded here, forever, for every future
    |  compute pass too - nothing else about this call changes)
    v
RenderGraphCompiler::Compile() -> CompiledGraph   (culling logic UNCHANGED)
    v
RenderGraphSnapshot::BuildRenderGraphSnapshot()   <- copies isComputePass/readKinds/writeKinds through (PHASE1)
    v
RenderGraphSnapshot::passesInExecutionOrder[]     <- every surviving compute pass is now SELF-DESCRIBING
    v
FrameDebuggerData.cpp::BuildRealFrameDebuggerSnapshot()      <- PHASE2: ONE generic loop over
    |     every isComputePass==true, non-culled pass -> BuildComputeDispatchLeaf(pass)
    v
root "Game View"
  |-- "Compute Dispatches (Pre-GameView)" group (NEW, present only when >=1  <- v2 REVIEW FINDING (see Locked
  |     surviving compute pass's own real index in graphSnapshot precedes       Design Decision #8 below): a
  |     GameView's own real index - e.g. GPU Skinning, every atmosphere         SINGLE group unconditionally
  |     LUT pass, the Aerial Perspective Volume pass)                          placed after "GameView" would
  |       |-- <PassNameA> leaf                                                 misrepresent these passes'
  |       |-- ... (every real PRE-GameView compute pass, in real               real chronological position -
  |       |         execution order)                                          split into a pre/post pair
  |-- "GameView" leaf                         (unchanged - the one real graphics/draw pass)
  |-- "Compute Dispatches (Post-GameView)" group (NEW, present only when >=1
  |     surviving compute pass's own real index in graphSnapshot follows
  |     GameView's own real index - e.g. the Aerial Perspective Composite
  |     pass, the Aerial Perspective Volume Debug-Slice pass)
        |-- <PassNameB> leaf                     - replaces "GPU Skinning" group AND the old hardcoded
        |-- ... (every real POST-GameView          "AtmosphereAerialPerspectiveCompositePass" special case
        |         compute pass, in real            - one leaf per REAL surviving compute pass, in real
        |         execution order)                 execution order, whatever its name, forever
        v
FrameDebuggerHistory::CaptureFrame()      <- PHASE3: eagerly retains a real GPU copy of EVERY compute
    |                                          pass's own 2D-texture write this same capture, keyed by
    |                                          pass name, sourced from RenderGraphDebugTextureRegistry
    |                                          (already-existing infra, just newly consumed here)
    v
FrameDebuggerHistoryEntry { snapshot, preview, compositedPreview, computePassPreviews[] }  <- NEW field
    v
ChooseFrameDebuggerPreviewSource()   <- PHASE3 widens this pure decision function: a selected compute
    |                                   leaf with a real retained preview wins over the whole-frame image
    v
Selecting ANY compute-dispatch leaf shows THAT PASS'S OWN real output
    (a 2D texture directly, PHASE3; a 3D volume texture via a real ray-marched thumbnail reusing
     VolumeTexturePreviewRenderer, PHASE4)
```

### 3.2 Locked Design Decisions (from the user's own answers during this campaign's design review —
do not relitigate these during implementation; if a phase document's plan conflicts with one of these, the
PHASE document is wrong and must be fixed, not this list)

1. **Add a real `isComputePass` flag inside the RenderGraph core data model** (`PassRecord` ->
   `RenderGraphPassSnapshot`), not a heuristic guess confined to `FrameDebuggerData.cpp`. This is the
   literal "choke point" the user asked for — every future compute pass, anywhere in this engine, is
   automatically discoverable with zero Frame-Debugger-specific code ever needing to change again.
2. **Fold GPU Skinning into the same generic auto-discovery mechanism.** The externally-supplied
   `gpuSkinningPassNamesThisFrame` name list is REMOVED as the mechanism that decides what appears in the
   tree — GPU Skinning passes are discovered exactly like every other compute pass, via `isComputePass`,
   because they already go through `builder.AddComputePass()` today (confirmed,
   `Application/RenderPasses.cpp`'s `AddGpuSkinningPasses()`).
3. **Validation/debug-only compute passes (Compute Blur Validation, the Aerial Perspective Volume Debug
   Slice pass) are INCLUDED in the automatic sweep too**, whenever they genuinely ran this frame — this is
   a deliberate, explicit user choice ("fully generic, catches literally every compute dispatch"), not an
   oversight to fix later.
4. **Eager retained-texture capture**: `FrameDebuggerHistory::CaptureFrame()` copies EVERY real compute
   pass's own 2D-texture write into a retained GPU texture on EVERY real capture trigger (Enable-edge /
   Step / explicit Capture button) — never a lazy "only copy the one the user happens to click" scheme.
   This mirrors the existing `preview`/`compositedPreview` cost tier exactly ("at most once per real
   capture trigger, never per real frame" — see `FrameDebuggerHistory`'s own header comment) and keeps
   every leaf's preview always instantly available with no extra round-trip once a leaf is clicked.
5. **The Aerial Perspective froxel volume's own preview reuses the EXISTING ray-march thumbnail renderer**
   (`src/Renderer/VolumeTexturePreviewRenderer.h/.cpp`, already shipped and already used by
   `GET /get_texture`) rather than a placeholder "not available" string, and rather than writing a new
   raymarcher from scratch.
6. **REPLACE, do not preserve, the old hardcoded `"AtmosphereAerialPerspectiveCompositePass"` special case
   and the old hardcoded `"GPU Skinning"` name-list special case.** Compute-shader dispatch becomes the
   Frame Debugger's own Tier-1 citizen: every real compute dispatch, tracked, uniformly, and (per Non-Goal
   3.4 below) laying the groundwork for it to become individually inspectable/steppable in a future
   campaign. This is an explicit, user-approved BREAKING change to `frame-debugger-3`/`frame-debugger-4`'s
   own previously-shipped tree shape and to their own existing Tier-1 tests, which PHASE2/PHASE5 must
   update (never silently leave red/stale).
7. **Ask, don't guess, whenever a genuine implementation-detail fork appears that isn't already pinned
   down explicitly by a phase document** (see this file's own "STANDING RULE" above) — most concretely
   flagged today at: (a) whether to also keep a small, OPTIONAL, purely-cosmetic "friendly label" lookup
   table for a handful of well-known pass names in the Inspector (PHASE2 — genuinely optional, not required
   for correctness), and (b) exactly which existing GPU-texture-from-raw-CPU-pixels upload helper to reuse
   for the volume-texture ray-march thumbnail's own retained copy (PHASE4).
8. **(Added during this campaign's v2/second-iteration double-check review — a genuine tree-ordering
   question the user explicitly asked to be resolved by whoever caught it, rather than pre-deciding it
   blind.) The "Compute Dispatches" group is SPLIT into two, `"Compute Dispatches (Pre-GameView)"` and
   `"Compute Dispatches (Post-GameView)"`, positioned strictly before/after the `"GameView"` leaf
   respectively, according to each surviving compute pass's own real index in
   `graphSnapshot.passesInExecutionOrder` relative to the real `"GameView"` pass's own index** — NOT a single
   group unconditionally placed after `"GameView"` (that would have visually misrepresented GPU Skinning and
   every atmosphere LUT/volume pass, all of which genuinely run BEFORE `"GameView"` in real execution order —
   `"GameView"`'s own Sky-Background sub-draw samples the Sky-View LUT, so the LUT must already exist by
   then — as if they ran AFTER it, actively misleading for a tool whose entire purpose is showing real
   execution order). Each of the two groups is only added at all when it would have at least one real child
   (mirrors every other group's own "never an empty, misleading group" rule elsewhere in this tree) — see
   PHASE2's own Step 3.1/3.3 for the exact split algorithm. (NOTE: this is `frame-debugger-5`'s own eighth
   Locked Design Decision — unrelated to, and not to be confused with, `frame-debugger-4`'s own separately-
   numbered "Locked Design Decision #8" that PHASE3/PHASE4 below separately cite by name for the "one single
   ImmediateSubmit() call" rule; that one lives in a DIFFERENT campaign's own PHASE0 document.)

### 3.3 Non-Goals (explicitly out of scope for this campaign)

- **True per-pass "stop"/breakpoint execution control** (pausing the GPU mid-frame at a specific compute
  dispatch boundary for live step-through inspection). The user's own framing ("must record everything
  compute shader do on editor, and make it trackable and stoppable") is interpreted THIS campaign as
  "trackable" = every real compute dispatch is now a real, inspectable tree leaf with its own real output
  preview (delivered in full by PHASE1-4), and "stoppable" = the EXISTING Enable/Step/Capture frame-level
  controls (`frame-debugger-3`'s own PHASE3) already let an engineer freeze a specific frame and walk its
  compute passes one at a time in the tree — a genuine mid-command-buffer GPU pause/breakpoint is a much
  larger undertaking (this engine's `Renderer::ImmediateSubmit()`/render-graph model has no concept of a
  partial, resumable command-buffer submission today) and is called out here explicitly as a **candidate
  for a FUTURE campaign**, not attempted in this one. If, during implementation, this interpretation feels
  wrong or too narrow, **ask the user directly via `ask_questions`** rather than silently expanding scope.
- Any change to the actual atmosphere-scattering math, shader files, or GPU compute logic themselves — this
  campaign only touches how the Frame Debugger OBSERVES already-correct compute dispatches, never what they
  compute.
- Any change to `RenderGraphCompiler`'s culling/dependency-ordering logic, or to `RenderGraph::Execute()`'s
  own barrier/attachment-recording logic — `isComputePass` (PHASE1) is purely descriptive metadata, read by
  nothing inside `RenderGraph.cpp`/`RenderGraphCompiler.cpp` itself.
- Any change to `GET /get_texture`, `GET /list_textures`, `RenderGraphDebugTextureRegistry`, or
  `VolumeTexturePreviewRenderer`'s own public behavior — this campaign only adds NEW consumers of them from
  the Editor side, never modifies their existing contracts.
- Scene-View Frame Debugger capture of any kind — unchanged, still out of scope (matches
  `frame-debugger-3`/`frame-debugger-4`'s own long-standing Locked Design Decision).
- Per-individual-draw-call/per-invocation granularity inside a compute leaf — still pass-level, exactly
  like every other leaf in this tree.

### 3.4 File-change inventory (full campaign, across all phases — see each phase file for its own exact
per-phase slice)

- `src/Renderer/RenderGraph/RenderGraphTypes.h` (PHASE1 — `PassRecord::isComputePass`)
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` (PHASE1 — `AddComputePass()` now sets the new flag)
- `src/Renderer/RenderGraph/RenderGraphSnapshot.h`/`.cpp` (PHASE1 — `RenderGraphPassSnapshot::isComputePass`
  + new `readKinds`/`writeKinds` parallel vectors)
- `src/Editor/FrameDebuggerData.h`/`.cpp` (PHASE2 — the new generic `BuildComputeDispatchLeaf()` replacing
  `BuildGpuSkinningLeaf()`/`BuildAerialPerspectiveCompositeLeaf()`; PHASE3 — widened
  `FrameDebuggerPreviewSourceChoice`/`ChooseFrameDebuggerPreviewSource()`)
- `src/Editor/FrameDebuggerHistory.h`/`.cpp` (PHASE3 — new `computePassPreviews` field + `CaptureFrame()`'s
  widened signature/body; PHASE4 — the volume-texture ray-march branch)
- `src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp` (PHASE2 — drops the now-dead `gpuSkinningPassNamesThisFrame`
  plumbing; PHASE3 — `EnsurePreviewDescriptor()`'s widened picking call)
- `src/Editor/ImGuiEditorLayer.cpp` (PHASE2 — the `Build()` call-site simplification once the GPU-skinning
  name list is no longer needed for tree-building)
- `tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp`/`RenderGraphBuilderTests.cpp`/`RenderGraphSnapshotTests.cpp`
  (PHASE1)
- `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` (PHASE2 — new tests + REQUIRED updates to existing
  tests asserting the old, now-replaced tree shape)
- `tests/Editor/FrameDebuggerHistoryTests.cpp`/`FrameDebuggerDataTests.cpp` (PHASE3/PHASE4)
- `AGENTS.md`, `docs/conventions/frame-debugger.md`, `README.md`, `TODO.md` (PHASE5 — doc corrections)
- `task_manager/frame-debugger-5/PHASE1_COMPLETION_REPORT.md` .. `PHASE5_COMPLETION_REPORT.md`,
  `CAMPAIGN_COMPLETION_REPORT.md` (written as each phase lands)

### 3.5 Note on review depth (for the double-check / 2nd iteration)

**PHASE1 is this campaign's single highest-risk, highest-blast-radius phase** — it is the only one that
touches core `src/Renderer/RenderGraph/` files consumed by EVERY render pass in this engine (graphics and
compute alike), not just Editor/Frame-Debugger-local code. A mistake here (e.g. accidentally flipping
`isComputePass` for a graphics pass, or breaking `ResourceUsageName()`'s existing resolution) would be a
silent, engine-wide correctness bug, not a narrow Editor-only one. **A dedicated double-check pass for
PHASE1 alone, in isolation, before the full campaign-wide double-check, is strongly recommended** — see this
campaign's own 2nd-iteration workflow for exactly how that extra check is scheduled. PHASE2-4 are
comparatively contained (`src/Editor/` only), though PHASE2 carries real regression risk to
`frame-debugger-3`/`frame-debugger-4`'s own previously-shipped tests (see Locked Design Decision #6).

### 3.6 Order of work

Work phases 1 -> 5 strictly in order; each does a fast compile check before moving on. PHASE5 is the only
one that does a full clean build (both `GTE_ENABLE_EDITOR=ON` and `=OFF`) + full `ctest` regression + a
live, HTTP-automation-driven runtime smoke test. See each phase file for its own exact compile-check command
and file-change inventory.
