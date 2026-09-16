# PHASE2 — Make the Real "Aerial Perspective Composite" Pass Visible in the Event Tree

## Parent -> PHASE0_MASTER_STRATEGY.md, read it first. Depends on PHASE1 already landing.

Campaign folder: `task_manager/frame-debugger-4/`
Branch: `feature/frame-debugger-impl`
Risk level: LOW — a pure, additive data-model change, mirroring an existing precedent
(`BuildGpuSkinningLeaf()`) almost exactly. Touches exactly one production file pair
(`src/Editor/FrameDebuggerData.h`/`.cpp`) plus its test file. Does **not** touch
`Panels/FrameDebuggerPanel.cpp`, `FrameDebuggerHistory.h`/`.cpp`, or `ImGuiEditorLayer.cpp` at all — PHASE1's
own picking rule (Step 3.5 of that phase) already generalizes correctly to whatever this phase adds, with
zero further change.

---

## Step 1: The Goal (Where are we going?)

The Frame Debugger's left-hand event tree gains a new, real, selectable leaf — `"Aerial Perspective
Composite"` — sibling to the existing `"GameView"` leaf, sourced from the SAME already-real
`gte::rg::RenderGraphSnapshot` this campaign's builder already reads. Selecting it shows real read/write
texture names (e.g. `"GameView"` read, `"GameViewComposited"` written) and real GPU timing, with
`"n/a (compute pass)"` blend/Z/stencil rows (mirroring the existing `"GPU Skinning"` leaf's own honest
"this is a compute dispatch, not a draw call" framing) — and, thanks to PHASE1, its preview box shows the
TRUE final, atmosphere-composited image. An engineer inspecting the tree can now literally see, in black and
white, that atmosphere compositing genuinely ran this frame — not just infer it from the final pixels
looking foggy. **Naming clarification (added during this campaign's own full double-check pass, matching
PHASE0_MASTER_STRATEGY.md's own Step 1 clarification):** the TREE ROW ITSELF displays `leaf.name`, which
Step 3.1 below sets to the real, raw pass name `"AtmosphereAerialPerspectiveCompositePass"` (mirroring
`BuildGpuSkinningLeaf()`'s own raw-name convention exactly) — `"Aerial Perspective Composite"` only ever
appears as `details.passName` in the Inspector pane's own "Pass" row once this leaf is selected (its
`details.eventLabel` is the separate, shorter `"Compute Composite"` operation-kind label - see Step 3.1
below), never as the tree label. Do not expect the tree row text itself to read
`"Aerial Perspective Composite"`.

## Step 2: The Situation (Where are we now?)

After PHASE1, `FrameDebuggerHistory` already retains the correct post-composite image
(`entry.compositedPreview`), and `EnsurePreviewDescriptor()`'s picking rule already reads: "the literal
`"GameView"` leaf selected → pre-composite; anything else (including nothing selected) → post-composite,
falling back to pre-composite". **This means the new leaf this phase adds requires ZERO changes to the
picking logic — it only needs to exist in the tree at all**, and merely by virtue of NOT being named
`"GameView"`, selecting it will automatically show `entry.compositedPreview` for free.

The real render-graph pass this leaf is built from already exists and is already named exactly, verified
live in `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`'s
`AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`:

```cpp
builder.AddComputePass(
    "AtmosphereAerialPerspectiveCompositePass",
    [sourceColorHandle, aerialPerspectiveVolumeHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
        pass.ReadTexture(sourceColorHandle, rg::ResourceAccess::ShaderRead);
        pass.ReadTexture(sourceColorHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
        pass.ReadVolumeTexture(aerialPerspectiveVolumeHandle, rg::ResourceAccess::ShaderRead);
        pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
    },
    ...);
```

`gte::rg::BuildRenderGraphSnapshot()` already turns this into a real `RenderGraphPassSnapshot` with
`name == "AtmosphereAerialPerspectiveCompositePass"`, real `readNames`/`writeNames` (plain, already-resolved
strings — confirmed shape in `src/Renderer/RenderGraph/RenderGraphSnapshot.h`), and a real `PassGpuStats`
(a real `GpuTimingSample` + an always-empty `DrawStats`, since a compute dispatch never issues a draw call —
exactly like every GPU-skinning pass already reports today). This pass name is a single, hardcoded,
compile-time literal — unlike the GPU-skinning group's per-model dynamic names, `BuildRealFrameDebuggerSnapshot()`
can simply look for this ONE literal name directly; no new function parameter is needed.

## Step 3: The Plan

### 3.1 `src/Editor/FrameDebuggerData.cpp` — new `BuildAerialPerspectiveCompositeLeaf()` builder

Add a new anonymous-namespace helper immediately after the existing `BuildGameViewLeaf()`:

```cpp
// Builds the real "Aerial Perspective Composite" pass LEAF node - see
// BuildRealFrameDebuggerSnapshot()'s own header-comment tree-shape
// description. frame-debugger-4 campaign, PHASE2. Mirrors
// BuildGpuSkinningLeaf()'s own "n/a (compute pass)" blend/Z/stencil
// convention (a real compute dispatch, never a draw call), but - unlike
// GPU Skinning, which never samples a material texture at all - this pass
// DOES have real, meaningful texture reads/writes worth showing (it is the
// whole reason this leaf exists: to make the atmosphere-compositing step
// visible), sourced directly from `pass.readNames`/`pass.writeNames` -
// never fabricated or re-derived from anywhere else.
FrameDebuggerEventNode BuildAerialPerspectiveCompositeLeaf(const rg::RenderGraphPassSnapshot& pass, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    // Mirrors BuildGpuSkinningLeaf()'s own leaf.name = pass.name convention
    // exactly - the real, raw render-graph pass name, never a prettified
    // invented one (this campaign's own honesty rule - see AGENTS.md,
    // "Testability & Regression Safety" and PHASE0_MASTER_STRATEGY.md's own
    // "never fabricate" precedent throughout this whole feature).
    leaf.name = pass.name; // "AtmosphereAerialPerspectiveCompositePass"
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Compute Composite";
    // A friendlier grouping label, distinct from the raw pass name above -
    // mirrors BuildGpuSkinningLeaf()'s own details.passName = "GPU Skinning"
    // precedent (a real pass's own raw name vs. a short, human-readable
    // category label are allowed to differ).
    details.passName = "Aerial Perspective Composite";

    // shaderName - a real, hardcoded fact (there is exactly ONE compute
    // shader this pass ever dispatches - see AtmosphereLutRenderer::
    // AddAerialPerspectiveCompositePass()'s own renderer.Dispatch() call,
    // Shaders/AtmosphereAerialPerspectiveComposite.comp) - never fabricated,
    // mirrors DescribeStandardPipelineState()'s own "duplicate a real
    // hardcoded engine constant with a comment documenting what it must be
    // kept in sync with" precedent (FrameDebuggerCapture.cpp/.h).
    details.shaderName = "AtmosphereAerialPerspectiveComposite.comp";

    details.blendMode = "n/a (compute pass)";
    details.zClip = "n/a (compute pass)";
    details.zTest = "n/a (compute pass)";
    details.zWrite = "n/a (compute pass)";
    details.cull = "n/a (compute pass)";
    details.stencilRef = "n/a (compute pass)";
    details.stencilComp = "n/a (compute pass)";
    details.stencilPass = "n/a (compute pass)";
    details.stencilFail = "n/a (compute pass)";
    details.stencilZFail = "n/a (compute pass)";

    // textures - real read/write resource names, straight from this exact
    // pass's own already-resolved RenderGraphPassSnapshot fields - never
    // aggregated from FrameDebuggerCaptureContext (this pass is not a
    // per-draw-call thing at all, unlike the "GameView" leaf).
    for (const std::string& readName : pass.readNames) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Read Texture";
        texture.valueLabel = readName;
        details.textures.push_back(std::move(texture));
    }
    for (const std::string& writeName : pass.writeNames) {
        FrameDebuggerTextureProperty texture;
        texture.name = "Write Texture";
        texture.valueLabel = writeName;
        details.textures.push_back(std::move(texture));
    }

    // vectors - real GPU timing, exactly like BuildGpuSkinningLeaf()'s own
    // identical convention (0.0ms whenever GPU timing is Absent/Unsupported
    // - see that function's own comment for the full reasoning, unchanged
    // here).
    FrameDebuggerVectorProperty timing;
    timing.name = "GPU Time (ms)";
    timing.x = static_cast<float>(pass.stats.timing.milliseconds);
    details.vectors.push_back(timing);

    // matrices deliberately left empty - real: this pass's own real
    // invViewProjection/cameraWorldPosition push constants are genuinely
    // camera-derived, but PHASE0's own Non-Goals explicitly scope this leaf
    // to pass-level read/write/timing facts only, mirroring GPU Skinning's
    // own "no per-draw camera matrix" precedent for a compute pass (neither
    // pass is drawing anything with a rasterizer-consumed matrix).

    leaf.details = std::move(details);
    return leaf;
}
```

### 3.2 `src/Editor/FrameDebuggerData.cpp` — wire it into `BuildRealFrameDebuggerSnapshot()`

Immediately after the existing `root.children.push_back(BuildGameViewLeaf(*gameViewPass, capture,
nextEventIndex++));` line, add:

```cpp
// frame-debugger-4 campaign, PHASE2 - the real atmosphere-compositing
// step, sibling to "GameView" above, in real execution order (it always
// runs strictly AFTER "GameView" in the same frame - see
// AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()'s own
// pass.ReadTexture(sourceColorHandle, ...) dependency declaration).
// Mirrors the "GPU Skinning" group's own "only add if a real matching pass
// was actually found this frame" discipline - a graphSnapshot from before
// this feature existed, or a hypothetical future build where atmosphere
// compositing genuinely did not run, must never produce a fake, empty, or
// misleading leaf.
const rg::RenderGraphPassSnapshot* aerialPerspectiveCompositePass =
    FindPassByName(graphSnapshot.passesInExecutionOrder, "AtmosphereAerialPerspectiveCompositePass");
if (aerialPerspectiveCompositePass != nullptr) {
    root.children.push_back(BuildAerialPerspectiveCompositeLeaf(*aerialPerspectiveCompositePass, nextEventIndex++));
}
```

`FindPassByName()` already exists (used for `"GameView"` a few lines above in the same function) — reuse it
directly, no new helper needed.

**Final resulting tree shape** (root "Game View" group's children, in order): optional `"GPU Skinning"`
group, `"GameView"` leaf, optional `"Aerial Perspective Composite"` leaf — exactly matching real
execution order top to bottom.

### 3.3 `src/Editor/FrameDebuggerData.h` — doc-comment update only

Update `BuildRealFrameDebuggerSnapshot()`'s own header-comment tree-shape description (the paragraph
starting "Tree shape produced otherwise: ...") to mention the new optional trailing leaf, e.g.:

> ... followed by exactly one LEAF for the real `"GameView"` pass itself, followed by an OPTIONAL final
> LEAF for the real `"AtmosphereAerialPerspectiveCompositePass"` (present only when that exact pass name is
> found in `graphSnapshot` this frame) — see PHASE0_MASTER_STRATEGY.md's Locked Design Decisions #1/#2/#6/#7
> for the full reasoning (pass-level granularity, pass-scoped facts, Game-View-only scope), and the
> `frame-debugger-4` campaign's own PHASE0_MASTER_STRATEGY.md for why this specific trailing leaf exists.

### 3.4 `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` — new test cases

Add at least these three new `TEST(...)` cases (append at the end of the file, inside the existing anonymous
namespace):

1. **`AerialPerspectiveCompositePassProducesThirdLeafAfterGameView`** — a `graphSnapshot` containing
   `"GameView"` then `"AtmosphereAerialPerspectiveCompositePass"` (with hand-set `readNames = {"GameView"}`,
   `writeNames = {"GameViewComposited"}`, and a hand-set `stats.timing` — check `GpuTimingSample`'s own
   shape in `src/Renderer/GpuTiming.h` for how to construct a "present" sample in a test, mirroring however
   `RenderGraphSnapshotTests.cpp` or `ProfilerPanelDataTests.cpp` already do it for an existing test, if
   any precedent exists — if none does, a default/absent `GpuTimingSample` is a perfectly valid, honest
   fallback for this test too). Assert: `root.children.size() == 2` (or 3 if combined with a GPU-skinning
   fixture in the same test), the LAST child's `.name == "AtmosphereAerialPerspectiveCompositePass"`,
   `.eventIndex` is the highest in the tree (sequential, after `"GameView"`'s own), `.details->passName ==
   "Aerial Perspective Composite"`, `.details->eventLabel == "Compute Composite"`,
   `.details->blendMode == "n/a (compute pass)"`, `.details->textures.size() == 2` with the read entry's
   `.valueLabel == "GameView"` and the write entry's `.valueLabel == "GameViewComposited"`, and
   `snapshot.totalEventCount` incremented by exactly one more than the equivalent
   no-composite-pass scenario.
2. **`NoAerialPerspectiveCompositePassAddsNoThirdLeaf`** — a `graphSnapshot` containing only `"GameView"`
   (no `"AtmosphereAerialPerspectiveCompositePass"` at all, exactly like every EXISTING test fixture in this
   file today) — assert `root.children.size()` stays exactly what it already was before this phase (1, for
   the simplest existing fixture) — this is really just re-confirming the existing tests still pass
   unmodified (they will, since none of them include this new pass name), but an explicit test naming this
   exact non-regression makes the "only add if found" contract self-documenting for future readers.
3. **`AllThreeGroupsAppearTogetherInRealExecutionOrder`** — a `graphSnapshot` containing, in this exact
   order, one GPU-skinning pass, `"GameView"`, then `"AtmosphereAerialPerspectiveCompositePass"` — assert
   `root.children.size() == 3` and the three children appear in exactly that order (`"GPU Skinning"` group
   first, `"GameView"` leaf second, `"AtmosphereAerialPerspectiveCompositePass"` leaf third), with
   `eventIndex` values strictly increasing left-to-right across the whole tree (0, 1, 2, ... — reusing the
   existing `TwoGpuSkinningPassesProduceGroupPlusGameViewLeaf` test's own eventIndex-sequencing-assertion
   style).

### 3.5 Compile-check

```
cmake --build build --target GreatTamanaEngineTests
ctest --test-dir build -R FrameDebuggerSnapshotBuilderTest --output-on-failure
```

All `FrameDebuggerSnapshotBuilderTest`-prefixed cases (existing + new) must pass. Do not run the full suite
yet — PHASE3's job.

### 3.6 What this phase deliberately does NOT do

- It does not change `Panels/FrameDebuggerPanel.cpp`, `FrameDebuggerHistory.h`/`.cpp`, or
  `ImGuiEditorLayer.cpp` at all (see this document's own top-of-file risk note) — PHASE1's picking rule
  already generalizes correctly.
- It does not add a "Compute Composite" special case anywhere in `BuildInspectorPane()`'s
  `selectedEventIsGpuSkinning`/`showPreviewTexture` logic — this new leaf's own preview box SHOULD show a
  real texture (unlike GPU Skinning), so it correctly falls through the EXISTING `!selectedEventIsGpuSkinning`
  check as `true` (showable) with zero further change needed there either.
- It does not update `AGENTS.md`/`docs/conventions/frame-debugger.md`/`TODO.md` — PHASE3's job, once both
  PHASE1 and PHASE2 have landed.

### 3.7 Completion report

Write `task_manager/frame-debugger-4/PHASE2_COMPLETION_REPORT.md` covering the exact new leaf's final field
values (cross-checked against a live `graphSnapshot` if a runtime smoke test is convenient this phase), the
new test cases added and their pass/fail result, and confirmation the compile-check in 3.5 succeeded. Then
commit (`git_add` + `git_commit`) code + tests + report together.
