# PHASE2 — Generic Compute-Dispatch Event-Tree Discovery

## Parent -> `PHASE0_MASTER_STRATEGY.md`, read it first. Depends on PHASE1 already landed.

Branch: `feature/frame-debugger-impl`
Risk level: MEDIUM — contained to `src/Editor/`, but deliberately, explicitly BREAKS and replaces
`frame-debugger-3`/`frame-debugger-4`'s own previously-shipped tree shape and tests (Locked Design
Decision #6, `PHASE0_MASTER_STRATEGY.md`). Read that document's Step 3.2 before starting.

## Step 1: The Goal (Where are we going?)

`FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()` must discover **every real, surviving compute
pass in the current frame's `RenderGraphSnapshot`, generically, via PHASE1's new `isComputePass` flag** —
never again by hardcoding a pass's exact string name, and never again by depending on an externally-supplied
name list threaded in from three call sites away. Concretely, after this phase:

- The event tree becomes: root `"Game View"` -> an optional `"Compute Dispatches (Pre-GameView)"` group ->
  `"GameView"` leaf (unchanged) -> an optional `"Compute Dispatches (Post-GameView)"` group. Each group
  contains one leaf per real, surviving, `isComputePass == true` pass found in
  `graphSnapshot.passesInExecutionOrder` this frame, in real execution order, SPLIT by whether that pass's
  own real index in `passesInExecutionOrder` is before or after `"GameView"`'s own real index (v2 review
  finding — PHASE0's Locked Design Decision #8; a SINGLE group unconditionally placed after `"GameView"`
  would visually misrepresent GPU Skinning/every atmosphere LUT/volume pass, all of which genuinely run
  BEFORE `"GameView"` in real execution order, as if they ran after it) — covering GPU Skinning dispatches,
  every atmosphere LUT pass, the Aerial Perspective Composite pass, the Aerial Perspective Volume Debug-Slice
  pass, and Compute Blur Validation's own pass, ALL uniformly, with the exact same leaf-building function.
  Concretely, today's real passes split as: `"Compute Dispatches (Pre-GameView)"` = GPU Skinning pass(es),
  `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass`, `AtmosphereSkyViewLutPass`,
  `AtmosphereAerialPerspectiveVolumePass`; `"Compute Dispatches (Post-GameView)"` =
  `AtmosphereAerialPerspectiveVolumeDebugSlicePass`, `AtmosphereAerialPerspectiveCompositePass`,
  `ComputeBlurValidation` (this last one is Scene-View-related and only declared into the graph at all when
  the Editor's own "Show Compute Blur (debug)" toggle is on AND "Scene" is visible — see
  `Editor/ImGuiEditorLayer.cpp`'s `AddBlurValidationPass()` call site — so it will often simply be absent,
  which is correct, not a bug). Re-verify this exact split against the live source at implementation time,
  this repo churns fast.
- A brand-new compute pass added anywhere in this engine by a future campaign requires **zero further
  changes to this file** to appear correctly in the tree — the only thing a future pass author needs to do
  is call `builder.AddComputePass(...)` (which they already must do to dispatch a compute shader at all).
- Every read/write row is correctly labeled by its real `ResourceKind` (Texture/Buffer/VolumeTexture) using
  PHASE1's new `readKinds`/`writeKinds`, so a GPU-Skinning pass's buffer write is never mislabeled as a
  "Write Texture" row.
- The now-unneeded `gpuSkinningPassNamesThisFrame` plumbing is REMOVED end-to-end (this file's own
  parameter, `Panels/FrameDebuggerPanel.h`/`.cpp`'s own member/parameter, and `ImGuiEditorLayer.cpp`'s own
  call-site local) — verify nothing else in `ImGuiEditorLayer.cpp` still needs
  `game.CollectGpuSkinningDispatchRequests()`'s result for a DIFFERENT purpose before deleting that call
  entirely; if it turns out something else does still need it, keep the call but stop threading its result
  into `FrameDebuggerPanel::Build()`.

## Step 2: The Situation (Where are we now?)

See `PHASE0_MASTER_STRATEGY.md`'s Step 2.2/2.3/2.5 for the full root-cause detail already investigated. In
short: `BuildRealFrameDebuggerSnapshot()` (`src/Editor/FrameDebuggerData.cpp`) today builds an optional
`"GPU Skinning"` group (fed by the `gpuSkinningPassNamesThisFrame` parameter, via `BuildGpuSkinningLeaf()`),
always builds one `"GameView"` leaf (via `BuildGameViewLeaf()` — **this one is correct and stays
unchanged**, since `"GameView"` is the one real graphics/draw pass, never a compute dispatch), and
optionally builds ONE more hardcoded `"AtmosphereAerialPerspectiveCompositePass"` leaf (via
`BuildAerialPerspectiveCompositeLeaf()`, found via a literal `FindPassByName()` string match). This phase
deletes `BuildGpuSkinningLeaf()` and `BuildAerialPerspectiveCompositeLeaf()` outright, replacing both with
one new `BuildComputeDispatchLeaf()`.

Existing Tier-1 tests in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` assert the OLD, now-replaced
tree shape by name — at minimum (grep for these strings yourself before editing, this list may not be
exhaustive by the time you implement this): tests asserting a `"GPU Skinning"` group's exact children/labels,
and `AerialPerspectiveCompositePassProducesThirdLeafAfterGameView`/`NoAerialPerspectiveCompositePassAddsNoThirdLeaf`/
`AllThreeGroupsAppearTogetherInRealExecutionOrder` (added by the `frame-debugger-4` campaign, per its own
`CAMPAIGN_COMPLETION_REPORT.md`). **These must be explicitly rewritten to assert the NEW split
`"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"` group shape (v2 review finding —
PHASE0's Locked Design Decision #8), never silently left red, never silently deleted without a replacement
covering the same real behavior.**

## Step 3: The Plan

### 3.1 New tree shape, precisely

```
root "Game View" (isDrawCall=false)
  children[0] = "Compute Dispatches (Pre-GameView)" group   <- NEW, present ONLY when at least one real
      (isDrawCall=false)                                       isComputePass==true, non-culled pass survived
      children[i] = BuildComputeDispatchLeaf(pass)              this frame AND its own index in
        for every SURVIVING COMPUTE pass whose own index          passesInExecutionOrder is < gameViewIndex.
        in graphSnapshot.passesInExecutionOrder is STRICTLY
        LESS THAN gameViewIndex, IN REAL EXECUTION ORDER
  children[1] = "GameView" leaf                             <- BuildGameViewLeaf(), UNCHANGED
  children[2] = "Compute Dispatches (Post-GameView)" group  <- NEW, same rule as children[0] but for every
      (isDrawCall=false)                                       surviving compute pass whose own index is
      children[i] = BuildComputeDispatchLeaf(pass)             STRICTLY GREATER THAN gameViewIndex.
        for every SURVIVING COMPUTE pass whose own index
        in graphSnapshot.passesInExecutionOrder is STRICTLY
        GREATER THAN gameViewIndex, IN REAL EXECUTION ORDER
```

`gameViewIndex` is simply the position of the real `"GameView"` pass within
`graphSnapshot.passesInExecutionOrder` (a plain loop with a counter, found alongside the existing
`FindPassByName()` lookup — or compute it directly instead of calling `FindPassByName()` twice). Since this
function's own discovery loop already SKIPS every culled pass (`pass.isCulled == true` never produces a
leaf), and `"GameView"` itself is never culled in practice (it is this whole snapshot's own precondition for
producing a non-empty result at all — see this function's existing "no `graphSnapshot` GameView pass ->
empty result" early-return), comparing against its raw vector index is a reliable, stable "before/after"
signal: `passesInExecutionOrder`'s own surviving-passes prefix is already in TRUE execution order (see
`RenderGraphSnapshot.h`'s own struct comment), so a smaller index always means "ran earlier, chronologically,
this same frame." Either group is added to `root.children` ONLY when it actually has at least one child
(mirrors the deleted "GPU Skinning" group's own "never an empty, misleading group" rule, applied to both new
groups independently — it is entirely normal, expected, and correct for a given frame to have children in one
group but not the other, e.g. `"Compute Dispatches (Post-GameView)"` being absent whenever Compute Blur
Validation's own debug toggle is off and no Aerial-Perspective-related pass explicitly ran that specific
capture).

### 3.2 `BuildComputeDispatchLeaf()` — the one new, fully generic leaf builder

Replaces `BuildGpuSkinningLeaf()` and `BuildAerialPerspectiveCompositeLeaf()` (delete both). New signature
and shape (adapt to this file's own existing helper-function style/anonymous-namespace convention):

```cpp
// frame-debugger-5 campaign, PHASE2 - the ONE generic leaf builder for
// ANY real compute dispatch (RenderGraphPassSnapshot::isComputePass ==
// true) that survived this frame, whatever its name - GPU Skinning, every
// atmosphere LUT pass, Aerial Perspective Composite, the Aerial
// Perspective Volume Debug-Slice pass, Compute Blur Validation, and any
// future compute pass this engine ever adds. Mirrors BuildGpuSkinningLeaf()/
// BuildAerialPerspectiveCompositeLeaf()'s own former "n/a (compute pass)"
// blend/Z/stencil convention exactly (a compute dispatch never issues a
// draw call), but is the SINGLE, UNIFIED replacement for both of those
// deleted, name-specific functions - see PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #6.
FrameDebuggerEventNode BuildComputeDispatchLeaf(const rg::RenderGraphPassSnapshot& pass, int eventIndex)
{
    FrameDebuggerEventNode leaf;
    leaf.name = pass.name; // The real, raw render-graph pass name - never fabricated/prettified.
    leaf.isDrawCall = true;
    leaf.eventIndex = eventIndex;

    FrameDebuggerEventDetails details;
    details.eventIndex = eventIndex;
    details.eventLabel = "Compute Dispatch";
    // No separate "friendly label" - the real, raw pass name IS the passName
    // too, for every compute leaf uniformly (this is what makes this
    // function correct for a pass this file has never heard of before -
    // see this phase's own Step 3.4 for the OPTIONAL cosmetic-label idea
    // this deliberately does NOT implement by default).
    details.passName = pass.name;
    // shaderName - honestly, this generic function has no way to know a
    // real compute pass's exact .comp shader FILE name (that fact lived
    // only in the deleted, hand-written BuildAerialPerspectiveCompositeLeaf()'s
    // own hardcoded string) - the real, raw pass NAME is used here instead,
    // which is always true and never fabricated. See this phase's own Step
    // 3.4 for the OPTIONAL improvement path if a nicer shader-file label is
    // wanted for specific, well-known passes later.
    details.shaderName = pass.name;

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

    // textures - real read/write resource names, EACH LABELED BY ITS REAL
    // KIND (PHASE1's new readKinds/writeKinds) so a buffer write (e.g. GPU
    // Skinning's own output buffer) is never mislabeled as a texture.
    for (std::size_t i = 0; i < pass.readNames.size(); ++i) {
        FrameDebuggerTextureProperty texture;
        texture.name = ReadRowLabelForKind(i < pass.readKinds.size() ? pass.readKinds[i] : rg::ResourceKind::Texture);
        texture.valueLabel = pass.readNames[i];
        details.textures.push_back(std::move(texture));
    }
    for (std::size_t i = 0; i < pass.writeNames.size(); ++i) {
        FrameDebuggerTextureProperty texture;
        texture.name = WriteRowLabelForKind(i < pass.writeKinds.size() ? pass.writeKinds[i] : rg::ResourceKind::Texture);
        texture.valueLabel = pass.writeNames[i];
        details.textures.push_back(std::move(texture));
    }

    FrameDebuggerVectorProperty timing;
    timing.name = "GPU Time (ms)";
    timing.x = static_cast<float>(pass.stats.timing.milliseconds);
    details.vectors.push_back(timing);

    leaf.details = std::move(details);
    return leaf;
}
```

Add two small local helpers (anonymous namespace) `ReadRowLabelForKind()`/`WriteRowLabelForKind()` that
`switch` exhaustively over `rg::ResourceKind` (NO `default:` case — mirrors `IsWriteAccess()`/`ToString()`'s
own established exhaustive-switch convention in `RenderGraphTypes.cpp`, so a FUTURE fourth `ResourceKind`
enumerator fails to compile here until updated) and return `"Read Texture"`/`"Read Buffer"`/
`"Read Volume Texture"` (and the `"Write ..."` counterparts) respectively.

### 3.3 `BuildRealFrameDebuggerSnapshot()` — the new generic discovery loop

Replace the existing GPU-Skinning-group block and the existing hardcoded
`FindPassByName(..., "AtmosphereAerialPerspectiveCompositePass")` block with a small pre/post SPLIT loop (v2
review finding — PHASE0's Locked Design Decision #8 — replaces this section's ORIGINAL single-group plan; see
this file's own Step 3.1 above for the full "why split" reasoning), inserted around the existing
`"GameView"` leaf append (which itself stays completely unchanged):

```cpp
// frame-debugger-5 campaign, PHASE2 - THE generic replacement for the
// former "GPU Skinning" group AND the former hardcoded
// "AtmosphereAerialPerspectiveCompositePass" special case (see
// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #6). Every real,
// surviving compute pass this frame becomes one leaf here, in real
// execution order, whatever its name - "GameView" itself is structurally
// excluded (it is never isComputePass==true, since it's declared via plain
// AddPass()/WriteColorAttachment(), never AddComputePass()). SPLIT into a
// pre/post pair (Locked Design Decision #8, v2 review finding) so a pass
// that genuinely ran BEFORE "GameView" (e.g. GPU Skinning, every atmosphere
// LUT pass) is never shown as if it ran after it.
const int gameViewIndex = static_cast<int>(gameViewPass - graphSnapshot.passesInExecutionOrder.data());

FrameDebuggerEventNode preGameViewGroup;
preGameViewGroup.name = "Compute Dispatches (Pre-GameView)";
preGameViewGroup.isDrawCall = false;

FrameDebuggerEventNode postGameViewGroup;
postGameViewGroup.name = "Compute Dispatches (Post-GameView)";
postGameViewGroup.isDrawCall = false;

for (int i = 0; i < static_cast<int>(graphSnapshot.passesInExecutionOrder.size()); ++i) {
    const rg::RenderGraphPassSnapshot& pass = graphSnapshot.passesInExecutionOrder[static_cast<std::size_t>(i)];
    if (!pass.isComputePass || pass.isCulled) {
        continue;
    }
    FrameDebuggerEventNode& targetGroup = (i < gameViewIndex) ? preGameViewGroup : postGameViewGroup;
    targetGroup.children.push_back(BuildComputeDispatchLeaf(pass, nextEventIndex++));
}

// Only add either group at all if something real actually survived this
// frame in THAT half - mirrors the deleted "GPU Skinning" group's own
// "only add if at least one real pass actually matched" discipline,
// applied independently to each half (it is entirely normal/expected for
// only one half to have children on a given captured frame).
if (!preGameViewGroup.children.empty()) {
    root.children.push_back(std::move(preGameViewGroup));
}
root.children.push_back(BuildGameViewLeaf(*gameViewPass, capture, nextEventIndex++));
if (!postGameViewGroup.children.empty()) {
    root.children.push_back(std::move(postGameViewGroup));
}
```

**IMPORTANT (`nextEventIndex` ordering caveat):** the pre-GameView loop above assigns `eventIndex` values to
pre-GameView leaves BEFORE `"GameView"` itself gets its own `eventIndex`, and the post-GameView loop assigns
its leaves' `eventIndex` values AFTER — this makes `eventIndex` itself monotonically increasing in the same
true chronological order the tree now visually displays, which is a deliberate, desirable property (matches
how `eventIndex` already behaves for every other leaf in this tree), not an incidental side effect to
undo. Do not reorder these three blocks relative to each other.

**Also note:** `gameViewIndex` above is computed via pointer arithmetic against `gameViewPass` (already a
`const rg::RenderGraphPassSnapshot*` obtained a few lines earlier via `FindPassByName(...)`) — this is safe
specifically because `gameViewPass` always points INTO `graphSnapshot.passesInExecutionOrder`'s own
contiguous backing storage (never a copy), which is guaranteed by `FindPassByName()`'s own
`return &pass;` shape (a reference into the vector being iterated, see this file's own existing
`FindPassByName()`). If you prefer not to rely on pointer arithmetic, an equally correct alternative is a
plain index-returning helper (e.g. `FindPassIndexByName()`) instead — either is fine, just pick one and use
it consistently; do not compute `gameViewIndex` two different ways in the same function.

Also **drop the `gpuSkinningPassNamesThisFrame` parameter** from `BuildRealFrameDebuggerSnapshot()`'s own
signature (`FrameDebuggerData.h`) and update its doc comment's "Tree shape produced otherwise" description
to match the new shape above. Delete the now-unused `Contains()` helper if nothing else in this file uses it
after this change (grep first — confirm before deleting).

### 3.4 OPTIONAL (genuinely optional — ask before spending real effort here): a small cosmetic friendly-label lookup

A future reader may want the Inspector's "Pass" field to show something nicer than the raw pass name for a
handful of well-known passes (e.g. `"AtmosphereAerialPerspectiveCompositePass"` -> `"Aerial Perspective
Composite"`, matching what the OLD, deleted special case used to show). This is explicitly NOT required for
correctness — the raw pass name is always truthful and is what "fully generic, never fabricate" already
demands. If you want to add a tiny, explicitly-optional `static` name->friendly-string lookup purely for
cosmetic Inspector polish (used ONLY for `details.passName`'s display value, never for tree-matching/test
logic, and falling back to the raw name for any pass not in the table), that is fine to add — but if you are
at all unsure whether this is worth the extra code/test surface, **call `ask_questions` and ask the user
directly** rather than guessing either way.

### 3.5 `Panels/FrameDebuggerPanel.h`/`.cpp` and `ImGuiEditorLayer.cpp` — drop the dead plumbing

- `FrameDebuggerPanel::Build()`'s `gpuSkinningPassNamesThisFrame` parameter, and its own
  `m_frameGpuSkinningPassNames` member, are no longer needed by anything and should be removed — but first
  confirm (grep `FrameDebuggerPanel.cpp` for `m_frameGpuSkinningPassNames`) that nothing else in that file
  still reads it for a DIFFERENT purpose (e.g. some other display row unrelated to tree-building) before
  deleting; if genuinely unsure, `ask_questions`.
- `ImGuiEditorLayer.cpp`'s `BuildUI()` call site currently builds a local `gpuSkinningPassNames` vector from
  `game.CollectGpuSkinningDispatchRequests()` purely to pass it into `Build()`. Confirm (grep this same
  function/file) whether `CollectGpuSkinningDispatchRequests()`'s result, or that local vector, is used for
  ANYTHING else in this function before deleting the call entirely — if it is genuinely dead once this
  parameter is removed, delete both the local and the now-unnecessary call; if something else still needs
  it, keep the call but simply stop threading its result into `FrameDebuggerPanel::Build()`.

### 3.6 Tests — update AND add

- Rewrite every existing test in `tests/Editor/FrameDebuggerSnapshotBuilderTests.cpp` that asserted the OLD
  `"GPU Skinning"` group shape or the OLD hardcoded `"AtmosphereAerialPerspectiveCompositePass"` leaf shape
  to instead assert against the NEW split `"Compute Dispatches (Pre-GameView)"`/
  `"Compute Dispatches (Post-GameView)"` groups (grep the file first for `"GPU Skinning"` and
  `"AtmosphereAerialPerspectiveCompositePass"`/`"Aerial Perspective Composite"` to find every affected test by
  name before touching anything) — note the OLD GPU-Skinning-style tests, which fabricated a
  `RenderGraphPassSnapshot` appearing BEFORE `"GameView"` in `passesInExecutionOrder`, now land under
  `"Compute Dispatches (Pre-GameView)"`, while the OLD Aerial-Perspective-Composite-style tests (a pass
  appearing AFTER `"GameView"`) now land under `"Compute Dispatches (Post-GameView)"`.
- Add new tests covering: (a) a frame with a PRE-GameView buffer-write compute pass (GPU-Skinning-style) AND
  a PRE-GameView texture-write compute pass together, in real execution order, both landing correctly under
  ONE `"Compute Dispatches (Pre-GameView)"` group; (a2) a frame with a POST-GameView compute pass landing
  correctly under `"Compute Dispatches (Post-GameView)"`, as a SIBLING of (not nested inside) the pre-GameView
  group; (a3) a frame with compute passes on BOTH sides of `"GameView"` at once produces all three
  (`"Compute Dispatches (Pre-GameView)"`, `"GameView"`, `"Compute Dispatches (Post-GameView)"`) as three
  top-level siblings, in that exact order; (b) a frame with zero compute passes at all produces NEITHER
  `"Compute Dispatches (...)"` group (not an empty one, on either side); (b2) a frame with compute passes on
  only ONE side produces ONLY that one group, never a spurious empty group for the other side; (c) a culled
  compute pass is correctly EXCLUDED from either group (never shown as if it ran), on both the pre- and
  post-GameView side; (d) the read/write row labels are correct for each of Texture/Buffer/VolumeTexture
  kinds (using PHASE1's new `readKinds`/`writeKinds`); (e) `eventIndex` values across the whole tree are
  monotonically increasing in true chronological order (pre-GameView leaves' indices < `"GameView"`'s own
  index < post-GameView leaves' indices) — regression coverage for this section's own "ordering caveat" note
  above.
- If `Panels/FrameDebuggerPanel.h`'s own `gpuSkinningPassNamesThisFrame` parameter removal affects any test
  in `tests/Editor/FrameDebuggerCaptureTests.cpp` or similar, update those too.

### 3.7 What this phase deliberately does NOT do yet

Selecting any NEW `"Compute Dispatches (Pre-GameView)"`/`"Compute Dispatches (Post-GameView)"` child leaf
still falls back, this phase, to showing the
whole-frame composited/pre-composite Game View image (via the UNCHANGED `ChooseFrameDebuggerPreviewSource()`
picking rule) — this is a KNOWN, temporary, fully-expected state, closed completely by PHASE3. Do not treat
this as a bug to fix in this phase; do not add any preview-picking logic here.

### 3.8 Fast compile check

```
cmake --build build --target GreatTamanaEngineTests
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R FrameDebugger
```

Every `FrameDebuggerSnapshotBuilderTest`/`FrameDebuggerCaptureTest`/`FrameDebuggerDataTest`/
`FrameDebuggerHistoryTest` must pass (rewritten ones passing against their NEW assertions, untouched ones
passing unchanged).

### 3.9 Report

Write `task_manager/frame-debugger-5/PHASE2_COMPLETION_REPORT.md`, explicitly listing every test that was
REWRITTEN (old assertion vs. new assertion, in brief) vs. every test that is genuinely NEW, plus
confirmation of the `gpuSkinningPassNamesThisFrame` plumbing removal's own blast radius (what was checked
before deleting it). Commit via `git_add`/`git_commit`.
