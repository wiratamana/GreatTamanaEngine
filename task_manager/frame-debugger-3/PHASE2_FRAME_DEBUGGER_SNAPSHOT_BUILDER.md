# PHASE2 — The real `FrameDebuggerSnapshot` builder

## Parent -> `PHASE0_MASTER_STRATEGY.md` (READ THIS FIRST — Locked Design Decisions #1, #6, #7).
## Depends on: `PHASE1_RENDERER_CAPTURE_INSTRUMENTATION.md` (already landed).

## Step 1: The Goal

Turn ONE frame's worth of real data — `gte::rg::RenderGraphSnapshot` (already exists, already
real) plus PHASE1's `FrameDebuggerCaptureContext`/`DescribeStandardPipelineState()` — into a real,
non-empty `FrameDebuggerSnapshot` (the exact struct `frame-debugger-2` already built the whole UI
against), filtered to ONLY the Game View's own passes, with zero changes required to
`Panels/FrameDebuggerPanel.cpp`'s own rendering code (per that campaign's own explicit promise —
see `docs/conventions/frame-debugger.md`'s "glue seams" section).

## Step 2: The Situation

- `gte::rg::BuildRenderGraphSnapshot(const CompiledGraph&, const CompiledGraphInput&,
  statsLookup)` (`src/Renderer/RenderGraph/RenderGraphSnapshot.h/.cpp`) already produces a real
  `RenderGraphSnapshot` — `passesInExecutionOrder` (each with `name`, `isCulled`, `readNames`,
  `writeNames`, `stats.drawStats`, `stats.timing`) and `resources`. This is called ONCE per real
  graph `Execute()` (see wherever `RenderGraphPanel`/`ProfilerPanel` currently source their own
  copy — likely `RenderGraph::LastSnapshot(...)`, confirm the exact accessor at implementation
  time) — this campaign's builder consumes THAT SAME real snapshot, it does not invent a second,
  competing capture mechanism.
- Real pass NAMES this frame will actually contain (per PHASE0's Step 2): `"GameView"`,
  `"SceneView"`, `"Present"`, plus zero-or-more GPU-skinning compute pass names (one per skinned
  model currently in the scene, named from `AnimationSystem::GpuSkinningDispatchRequest::name` —
  confirm the exact naming convention used by `AddGpuSkinningPasses()` at implementation time, e.g.
  whether it's an entity name, a mesh name, or something else).
- **The Game-View-only filter (Locked Design Decision #7)** needs a real, defensible rule for
  "which of this frame's real passes are GPU-skinning passes that feed the Game View specifically"
  — since a scene could conceivably have `AddSceneViewPass()` reading from a DIFFERENT (or the
  SAME) set of GPU-skinning outputs. The simplest correct approach: always include the
  `"GameView"` pass itself, plus every pass in `passesInExecutionOrder` whose name matches one of
  `AnimationSystem::GpuSkinningDispatchRequest::name`'s real values for the CURRENT frame (i.e.
  cross-reference against `game.CollectGpuSkinningDispatchRequests()`'s own current-frame result,
  not a static/cached list) — confirm this reasoning holds once the exact real data is in hand
  during implementation; if GPU-skinning passes are ever NOT distinguishable from Scene-View-only
  ones by name alone, fall back to the simpler, still-correct rule of "GameView only, no GPU
  Skinning group at all" rather than guessing wrong and showing an unrelated Scene View compute
  pass under a "Game View" label.
- `FrameDebuggerEventNode`/`FrameDebuggerEventDetails`/`FrameDebuggerSnapshot`
  (`src/Editor/FrameDebuggerData.h`) are ALREADY exactly the right shape — group nodes
  (`isDrawCall == false`) with `children`, leaf nodes (`isDrawCall == true`) with a populated
  `details`. This phase populates real instances of these structs; it does not change their shape.

## Step 3: The Plan

### 3.1 New builder function(s)

Add to `src/Editor/FrameDebuggerData.h/.cpp` (extending the existing file, matching
`frame-debugger-2`'s own precedent of extending this exact file across multiple phases — PHASE1
and PHASE6 of that campaign both did the same):

```cpp
// Real inputs: the CURRENT frame's real RenderGraphSnapshot, the CURRENT frame's real
// FrameDebuggerCaptureContext (already Reset()/populated for this frame - see PHASE1), and
// DescribeStandardPipelineState()'s own real, constant result. Pure - no live VkDevice/Renderer,
// exactly like BuildRenderGraphSnapshot() itself.
FrameDebuggerSnapshot BuildRealFrameDebuggerSnapshot(
    const rg::RenderGraphSnapshot& graphSnapshot,
    const FrameDebuggerCaptureContext& capture,
    const std::vector<std::string>& gpuSkinningPassNamesThisFrame);
```

Internals:
1. Find the `"GameView"` entry in `graphSnapshot.passesInExecutionOrder` (by name). If absent
   (e.g. queried before the very first frame ever rendered), return an EMPTY snapshot — exactly
   the same "no frame captured yet" honesty `BuildPlaceholderFrameDebuggerSnapshot()` already
   modeled, never a synthetic placeholder row.
2. Build root structure: one root group node named `"Game View"` (`isDrawCall = false`).
   - If `gpuSkinningPassNamesThisFrame` is non-empty: add a child group node `"GPU Skinning"`
     containing one LEAF child per matching pass in `graphSnapshot.passesInExecutionOrder`
     (`isDrawCall = true`, a fresh, globally-unique `eventIndex`), whose `details` reports:
     `eventLabel = "Compute Dispatch"`, `shaderName` = that pass's own real name, `passName` =
     `"GPU Skinning"`, blend/Z/stencil rows = `"n/a (compute pass)"`, `vectors` containing the
     real `DrawStats`-adjacent numbers available for a compute pass today (confirm exactly what
     `PassGpuStats` reports for a compute pass at implementation time — likely just GPU timing, no
     `DrawStats`, since `Renderer::Dispatch()`'s own doc comment explicitly says it does NOT touch
     `PassGpuStats::drawStats`), `textures`/`matrices` left empty (real — a compute skinning pass
     does not sample a material texture or use a camera matrix).
   - Add one final LEAF child (a sibling of the `"GPU Skinning"` group, NOT nested inside it) for
     the real `"GameView"` pass itself (`isDrawCall = true`, the next globally-unique
     `eventIndex`), whose `details` is populated per Locked Design Decision #6:
     - `eventLabel = "Draw Mesh"`, `passName = "GameView"`.
     - `shaderName` = every DISTINCT pipeline debug name `capture` recorded this frame, joined
       (e.g. comma-separated) if more than one.
     - `textures` = one `FrameDebuggerTextureProperty` per DISTINCT material texture debug name
       `capture` recorded (`valueLabel` = that same name; `name` = a stable, generic label such as
       `"Material Texture"` — there is no per-slot `_MainTex`-style naming in this engine yet, so
       do not invent one).
     - `vectors` = at least one real entry for the real clear color (`kGameClearColor` from
       `RenderPasses.cpp` — reference that exact constant, do not re-guess it) and one for the
       real aggregate `DrawStats` (draw-call count, triangle count) taken directly from
       `graphSnapshot`'s own `"GameView"` pass entry's `stats.drawStats`.
     - `matrices` = one real entry (e.g. named `"ViewProjection"`) built from `capture`'s last
       recorded `viewProj` `Mat4` — `FrameDebuggerMatrixProperty::values` is a flat
       `std::array<float, 16>`; confirm `Mat4`'s own real memory layout (row-major vs.
       column-major, see `src/Math/Mat4.h`) before copying its elements across, so the displayed
       matrix is genuinely correct, not transposed.
     - Blend/Z/stencil rows = `DescribeStandardPipelineState()`'s real strings.
3. `totalEventCount` = the total number of LEAF nodes actually produced (2 if there is no GPU
   skinning this frame — just the `"GameView"` leaf — plus 1 per GPU-skinning leaf).
4. `renderTarget` = a real `FrameDebuggerRenderTargetInfo` — `name = "GameView"`, real
   `width`/`height` (from the Game View `RenderTexture`'s own current extent — confirm the exact
   accessor at implementation time, e.g. `RenderTarget::extent` or equivalent), `format` = a
   human-readable string derived from `Renderer::ColorFormat()` (reuse whatever
   `VkFormat`-to-string helper already exists in this codebase — search for one before writing a
   new one; `TextureListEntryView::format`'s own upstream resolution in `Application.cpp` is a
   likely existing precedent to copy).

### 3.2 Existing pure helpers are untouched

`FormatFrameStepperLabel()`, `ClampSelectedEventIndex()`, `FindEventDetailsByIndex()`,
`FormatVectorProperty()`, `FormatMatrixProperty()` all already work correctly against ANY real
tree shape (per `frame-debugger-2`'s own explicit design) — this phase must NOT modify any of
their signatures or behavior; it only produces real data for them to operate on.

### 3.3 Tier-1 testing

Extend `tests/Editor/FrameDebuggerCaptureTests.cpp` (or add a sibling
`tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — implementer's call) with hand-fabricated
`RenderGraphSnapshot`/`FrameDebuggerCaptureContext` fixtures (no live `VkDevice` needed, mirroring
`RenderGraphSnapshotTests.cpp`'s own existing "hand-built compiled graph in, real snapshot out"
precedent) covering: no `"GameView"` pass present -> empty result; `"GameView"` present with zero
GPU-skinning passes -> exactly one leaf, no `"GPU Skinning"` group at all; `"GameView"` present
with two GPU-skinning passes -> a `"GPU Skinning"` group with exactly two leaves plus the
`"GameView"` leaf, three total events, correct sequential `eventIndex` values across the whole
tree; two distinct pipeline/texture names recorded by `capture` produce two distinct
`FrameDebuggerTextureProperty`/comma-joined `shaderName` entries, not duplicates; the real matrix
values round-trip correctly through `FrameDebuggerMatrixProperty::values` (assert against a
hand-picked, non-identity `Mat4` so a row/column-major mixup would be caught).

### 3.4 What this phase explicitly does NOT do

- Does not wire this new builder into `FrameDebuggerPanel` yet (PHASE3/PHASE4's job — the panel
  keeps calling `BuildPlaceholderFrameDebuggerSnapshot()` until PHASE3's ring buffer exists to
  actually own/cache a real captured result).
- Does not add Scene View or Present passes to the tree under any circumstance (Locked Design
  Decision #7).
- Does not touch texture/preview-image concerns at all (PHASE3/PHASE6's job).

### 3.5 Compile check

Fast compile check: build `gte_core` and `GreatTamanaEngineTests`, run
`--gtest_filter=*FrameDebugger*`.

### 3.6 File-change inventory

Modified: `src/Editor/FrameDebuggerData.h`/`.cpp` (new builder function(s)),
`tests/Editor/FrameDebuggerCaptureTests.cpp` or a new sibling test file, `tests/CMakeLists.txt` if
a new test file was added.

Write `PHASE2_COMPLETION_REPORT.md` once done.
