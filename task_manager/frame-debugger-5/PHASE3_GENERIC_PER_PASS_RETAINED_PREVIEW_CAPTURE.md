# PHASE3 — Generic Per-Pass Retained Preview Capture (2D Textures)

## Parent -> `PHASE0_MASTER_STRATEGY.md`, read it first. Depends on PHASE1 and PHASE2 already landed.

Branch: `feature/frame-debugger-impl`
Risk level: MEDIUM-HIGH — touches live Vulkan barrier/copy code (`FrameDebuggerHistory::CaptureFrame()`'s
`ImmediateSubmit()` lambda), mirroring `frame-debugger-4`'s own PHASE1 (flagged HIGH-risk in that campaign
for the exact same reason). Read `frame-debugger-4/PHASE1_DUAL_STAGE_RETAINED_CAPTURE_AND_PREVIEW_SELECTION.md`
first for the existing precedent this phase extends.

## Step 1: The Goal (Where are we going?)

Selecting any leaf under either of the new `"Compute Dispatches (Pre-GameView)"`/
`"Compute Dispatches (Post-GameView)"` groups (PHASE2) must show **that pass's own real
output texture**, not the whole-frame Game View image it falls back to today. Concretely, after this phase:

- `FrameDebuggerHistory::CaptureFrame()` eagerly retains a fresh GPU copy of EVERY real compute pass's own
  2D-texture write, on every single real capture trigger (Enable-edge / Step / explicit Capture button) —
  never lazily, never only for the one leaf the user happens to click (Locked Design Decision #4,
  `PHASE0_MASTER_STRATEGY.md`).
- These retained copies are sourced from the ALREADY-EXISTING `RenderGraphDebugTextureRegistry`
  (`RenderGraph::DebugTextureSnapshotFor(name)`) — no new registry, no new tracking mechanism, just a new
  consumer of infrastructure that already exists and is already kept correctly up to date every frame.
- Selecting a compute-dispatch leaf whose own write name resolves to a real, retained 2D texture shows THAT
  texture; the whole-frame `preview`/`compositedPreview` picking rule is otherwise completely unchanged
  (nothing-selected and the `"GameView"` leaf itself behave exactly as `frame-debugger-4` already left
  them).
- A compute pass whose only write is a BUFFER (e.g. GPU Skinning) has no retained texture at all (correctly
  — a buffer has no visual preview) and the Inspector falls back to its existing "Not available yet"
  empty-preview state, unchanged.
- Volume-texture writes (the Aerial Perspective froxel volume) are explicitly deferred to PHASE4 — this
  phase's own capture loop must SKIP `ResourceKind::VolumeTexture` writes without erroring, leaving a clear
  marker/TODO for PHASE4 to fill in, never a crash or a silently-wrong 2D interpretation of 3D data.

## Step 2: The Situation (Where are we now?)

`FrameDebuggerHistoryEntry` (`src/Editor/FrameDebuggerHistory.h`) has exactly two optional retained textures,
`preview`/`compositedPreview`, populated by `FrameDebuggerHistory::CaptureFrame(Renderer&, const
FrameDebuggerSnapshot&, RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource)` — a single
`renderer.ImmediateSubmit()` call doing two transition-copy-transition-back sequences (see that method's own
body, `FrameDebuggerHistory.cpp`). `Panels/FrameDebuggerPanel.cpp`'s `EnsurePreviewDescriptor()` picks
between them via the pure `ChooseFrameDebuggerPreviewSource()` function (`FrameDebuggerData.cpp`), which
only ever knows about `hasEntry`/`hasPreview`/`hasCompositedPreview`/`isViewingGameViewLeaf` — it has no
concept of "a specific OTHER leaf is selected and has its own texture."

`RenderGraph` (`src/Renderer/RenderGraph/RenderGraph.h`) already exposes
`std::optional<rg::DebugTextureSnapshot> DebugTextureSnapshotFor(const std::string& name) const` — returning
a `DebugTextureSnapshot{ name, regime, target (RenderTarget: image/imageView/extent/format), hasDepth,
colorState, depthState, lastUpdatedFrameCounter }` for ANY texture name ever seen this session, in either
`ExecuteTimingMode` regime, already kept correct every real frame. `FrameDebuggerPanel` already stashes
`const rg::RenderGraph* m_frameRenderGraph` every `Build()` call (`Panels/FrameDebuggerPanel.h`) — already
reachable from exactly where `TriggerCapture()` (which calls `CaptureFrame()`) runs.

PHASE2 already gives every compute-dispatch leaf a `FrameDebuggerEventNode` with `.name == pass.name` and a
`.details->textures` list containing `{name: "Write Texture", valueLabel: <realWriteName>}` rows (and
`"Write Buffer"`/`"Write Volume Texture"` rows for the other two kinds) — this is already enough
information, walked recursively, to know exactly which pass names have a real 2D-texture write worth
eagerly capturing, without needing any NEW data threaded in from further upstream.

## Step 3: The Plan

### 3.1 New data: `FrameDebuggerHistoryEntry::computePassPreviews`

```cpp
// frame-debugger-5 campaign, PHASE3 - one retained GPU copy per real
// compute-dispatch pass this capture found with at least one real
// 2D-texture write (see FrameDebuggerData.cpp's new "Compute Dispatches"
// group, PHASE2). Keyed by the pass's own real, raw name (matches
// FrameDebuggerEventNode::name for that leaf exactly) so
// EnsurePreviewDescriptor() (Panels/FrameDebuggerPanel.cpp) can look up
// "does the CURRENTLY SELECTED leaf have its own retained preview" by
// simple name comparison. A pass with NO real 2D-texture write (a
// buffer-only write, e.g. GPU Skinning; or - until PHASE4 lands - a
// volume-texture-only write) simply has NO entry here at all - never a
// fake/empty one.
struct FrameDebuggerComputePassPreview {
    std::string passName;
    RenderTexture preview; // Always populated for an entry that exists at all - see CaptureFrame()'s own body.
};
```

Add `std::vector<FrameDebuggerComputePassPreview> computePassPreviews;` to `FrameDebuggerHistoryEntry`
(`FrameDebuggerHistory.h`), right after `compositedPreview`.

### 3.2 `FrameDebuggerHistory::CaptureFrame()` — widened signature

```cpp
void CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph, const FrameDebuggerSnapshot& snapshot,
    RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource);
```

(`const rg::RenderGraph&` is the new parameter — forward-declare `namespace rg { class RenderGraph; }` at
the top of `FrameDebuggerHistory.h`, mirroring how `Panels/FrameDebuggerPanel.h` already forward-declares it
today.) Update the ONE call site, `FrameDebuggerPanel::TriggerCapture()` (`Panels/FrameDebuggerPanel.cpp`),
to pass `*m_frameRenderGraph` (already stashed every `Build()` call — confirm it is non-null before
dereferencing, mirroring how `m_frameGameView`/etc. are already guarded at that same call site).

### 3.3 `CaptureFrame()`'s new body — discover write-texture names, then copy them all inside the SAME `ImmediateSubmit()`

**Step A — v2 review finding, REVISED from this section's original plan (read this before implementing):**
the original draft of this section proposed walking `snapshot.rootNodes` (the Editor's own already-built
`FrameDebuggerEventNode` tree) and STRING-MATCHING each leaf's `.details->textures[i].name` against the
literal label `"Write Texture"` that PHASE2's `WriteRowLabelForKind()` happens to produce for a
`ResourceKind::Texture` write. **Do not do this** — it silently, fragilely couples this phase's own discovery
logic to PHASE2's own display-string wording (a purely cosmetic Inspector-facing convention that could
reasonably be reworded by a future change with zero build/test signal that this phase's own capture logic
just silently broke). The exact SAME information is ALREADY available in fully-typed, non-stringly form,
one call away: `renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback)` returns the
very same `rg::RenderGraphSnapshot` that `FrameDebuggerPanel::TriggerCapture()` already built THIS SAME call
(right before it called `BuildRealFrameDebuggerSnapshot()`) — its `passesInExecutionOrder` entries carry
`isComputePass`/`isCulled`/`writeKinds`/`writeNames` directly, with no string-label round-trip needed at all.

So: **Step A (pure, CPU-side, before touching Vulkan at all)** — call
`renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback)` to get `graphSnapshot`, then
walk `graphSnapshot.passesInExecutionOrder` directly: for every pass where `pass.isComputePass == true` and
`pass.isCulled == false`, find the FIRST index `i` where `pass.writeKinds[i] == rg::ResourceKind::Texture`
(explicitly skip `Buffer`/`VolumeTexture`-kind writes this phase, per this phase's own Step 1 scope — a pass
with NO `Texture`-kind write at all is simply excluded from this list entirely), and collect
`{pass.name, pass.writeNames[i]}`. Build a small local
`std::vector<std::pair<std::string /*passName*/, std::string /*writeTextureName*/>>` from this — this is
ALSO now trivially, directly Tier-1-testable as a pure function taking an `rg::RenderGraphSnapshot` (no
`FrameDebuggerSnapshot`/Editor-tree dependency at all), so prefer extracting it as a named, standalone
function in `FrameDebuggerData.h`/`.cpp` (or a new small file) from the start, rather than leaving it
`CaptureFrame()`-local and only "considering" extraction later (see this file's own Step 3.6, which this
revised approach makes considerably easier to act on positively). **Known, accepted limitation:** only the
FIRST `Texture`-kind write per pass is captured — no real pass in this engine has more than one today, but if
a future pass ever writes two 2D textures, only the first gets a retained preview; this is an acceptable,
explicitly-documented simplification, not a silent gap.

Step B: for each pair, call `renderGraph.DebugTextureSnapshotFor(writeTextureName)`. If it returns
`std::nullopt` (defensive only — should not happen for a name PHASE2 just read straight out of the same
frame's own `graphSnapshot`, but never assume), skip it silently (no crash, no fake entry). Otherwise you
now have a real `DebugTextureSnapshot{ target, colorState, ... }` — this pass's CURRENT physical texture,
exactly analogous to `gameViewSource`/`compositedGameViewSource` in the existing code, just resolved by name
instead of handed in directly by parameter.

Step C: exactly mirror the EXISTING `preview`/`compositedPreview` creation+copy pattern (see
`FrameDebuggerHistory.cpp`'s current body) for each one: freshly (re)create a retained `RenderTexture` sized
to `target.extent`/`target.format`, with a debug name like
`"FrameDebuggerHistorySlot<writeIndex>Compute<sanitizedPassName>"`, then inside the SAME
`renderer.ImmediateSubmit()` lambda that already does the pre-composite/post-composite copies — append one
more transition-copy-transition-back sequence per collected pair, using `snapshot.colorState` (from the
`DebugTextureSnapshot`) as the SOURCE's real current state (NOT assuming `ShaderRead` the way the two
existing hardcoded copies do — this source's real current state must be read from the registry, since an
arbitrary compute pass's output may legitimately be left in a different tracked state than the Game View's
own `ShaderRead` convention). Restore the source back to that SAME state afterward (never leave it in
`TransferSrc` for a later graph-recorded frame to trip over) — mirroring
`AtmosphereLutRenderer::FinalizeAerialPerspectiveCompositeForSampling()`'s own "restore afterward"
discipline. **One single `ImmediateSubmit()` call for the WHOLE `CaptureFrame()` call, 2+N copies, never N
separate submissions** (this is `frame-debugger-4`'s OWN campaign's "Locked Design Decision #8" — a
DIFFERENT, unrelated numbered decision from this campaign's, `frame-debugger-5`'s, own PHASE0
`PHASE0_MASTER_STRATEGY.md` Locked Design Decision #8 about the pre/post-GameView tree split, PHASE2 — do not
confuse the two just because they share a number across two different documents).

If, in practice, you find a genuinely different current state is needed per source than described here
(e.g. the registry's own tracked state does not line up with what a real `vkCmdCopyImage` transfer-source
transition expects), **stop and `ask_questions`** rather than guessing at a barrier that could silently
corrupt a live texture's tracked state for the next real frame.

### 3.4 `ChooseFrameDebuggerPreviewSource()` — widen the pure picking function

`FrameDebuggerData.h`/`.cpp`'s enum and function grow one new case, backward-compatibly (every existing
input combination's existing output is UNCHANGED):

```cpp
enum class FrameDebuggerPreviewSourceChoice {
    None,
    Preview,
    CompositedPreview,
    ComputePassPreview, // NEW - the selected leaf's own retained compute-pass output texture.
};

FrameDebuggerPreviewSourceChoice ChooseFrameDebuggerPreviewSource(
    bool hasEntry, bool hasPreview, bool hasCompositedPreview, bool isViewingGameViewLeaf,
    bool hasSelectedComputePassPreview); // NEW parameter.
```

New rule (add this branch; every other branch's ORDER and OUTCOME stays identical to today):

```
if (!hasEntry) return None;
if (isViewingGameViewLeaf) return hasPreview ? Preview : None;               // UNCHANGED
if (hasSelectedComputePassPreview) return ComputePassPreview;                // NEW - wins over CompositedPreview
if (hasCompositedPreview) return CompositedPreview;                          // UNCHANGED
if (hasPreview) return Preview;                                              // UNCHANGED
return None;                                                                 // UNCHANGED
```

`hasSelectedComputePassPreview` is resolved by the CALLER (`Panels/FrameDebuggerPanel.cpp`'s
`EnsurePreviewDescriptor()`) as: is there a currently-selected leaf at all, AND does
`currentEntry->computePassPreviews` contain an entry whose `passName` matches that leaf's own real pass name
(`FrameDebuggerEventDetails::passName`, which PHASE2 now sets to the raw pass name for every compute leaf).
`EnsurePreviewDescriptor()` then, on `ComputePassPreview`, wraps that specific entry's own `preview.View()`
instead of `entry->preview`/`entry->compositedPreview` — mirror the exact existing "only re-wrap when the
underlying `VkImageView` actually changed" caching discipline already used for the other two cases.

**QoL note, not a required fix (call out explicitly in the completion report so a future reader isn't
surprised by it):** `"AtmosphereAerialPerspectiveCompositePass"` itself has a real `Texture`-kind write
(its own output, e.g. `"GameViewComposited"`) — the SAME texture `entry.compositedPreview` already retains
for an unrelated reason (frame-debugger-4's own dual-stage capture). After this phase, that one pass will
ALSO get its own, genuinely redundant, second retained copy of the exact same texture content under
`computePassPreviews`, and — per the picking rule above — `ComputePassPreview` wins over `CompositedPreview`
whenever that leaf is selected, so the displayed image is still byte-for-byte correct either way (both
copies are sourced from the same registry entry inside the same `CaptureFrame()` call). This is a small,
accepted, one-texture-sized memory/bandwidth duplication, not a correctness bug — do not "fix" it by trying
to special-case `"AtmosphereAerialPerspectiveCompositePass"` out of the generic Step 3.3 discovery loop (that
would reintroduce exactly the kind of hardcoded-name special case this whole campaign exists to remove); if
you want to eliminate the duplication anyway, the only acceptable way is a fully generic rule (e.g. "skip
adding a `computePassPreviews` entry whose write name is already one of `gameViewSource`'s/
`compositedGameViewSource`'s own current texture names"), and only if you are confident it is worth the extra
code/test surface — otherwise leave it be.

### 3.5 Widen Tier-1 tests for `ChooseFrameDebuggerPreviewSource()`

Add the new input dimension to every existing test in `tests/Editor/FrameDebuggerDataTests.cpp`'s
`ChooseFrameDebuggerPreviewSourceTest` (it must now cover the new 5-boolean input space, at minimum every
combination the phase-4 completion report of `frame-debugger-4` already enumerated for the old 4-boolean
space, PLUS new cases proving `hasSelectedComputePassPreview == true` wins over `hasCompositedPreview ==
true` whenever `isViewingGameViewLeaf == false`).

### 3.6 `tests/Editor/FrameDebuggerHistoryTests.cpp`

`CaptureFrame()` itself remains Tier-2 (no live `VkDevice` in this test file, per its own existing
convention) — but this phase's own revised Step 3.3/Step A (v2 review finding) already extracts the
"collect compute-pass-name/write-texture-name pairs" step as a standalone, named, pure function taking an
`rg::RenderGraphSnapshot` directly (NOT a `FrameDebuggerSnapshot`/Editor-tree walk — see Step 3.3's own
rationale for why), living in `FrameDebuggerData.h`/`.cpp` (or a new small file if that fits this codebase's
existing per-concern file-splitting convention better) — add REAL Tier-1 tests for it in
`FrameDebuggerSnapshotBuilderTests.cpp` (or `RenderGraphSnapshotTests.cpp`/a new dedicated test file,
whichever this pure function's own eventual home file suggests) covering: a compute pass with exactly one
`Texture`-kind write is found correctly; a compute pass with only a `Buffer`/`VolumeTexture`-kind write is
correctly excluded; a pass with a `Texture`-kind write that is ALSO culled (`isCulled == true`) is correctly
excluded; a non-compute pass (`isComputePass == false`, e.g. `"GameView"` itself) with a `Texture`-kind write
is correctly excluded (this function must never accidentally pick up the one real graphics pass). This
mirrors this codebase's own established "extract the pure decision, keep the Vulkan glue thin" convention
(see `ChooseFrameDebuggerPreviewSource()`'s own precedent, `frame-debugger-4` PHASE3).

### 3.7 What this phase deliberately does NOT do yet

Volume-texture writes (`ResourceKind::VolumeTexture`) are explicitly SKIPPED by this phase's own Step 3.3
Step A filter — selecting the Aerial Perspective Volume pass's own leaf still falls back to the whole-frame
image after this phase lands (a KNOWN, temporary, fully-expected state, closed completely by PHASE4). Do
not attempt any volume-texture handling in this phase.

### 3.8 Fast compile check

```
cmake --build build --target GreatTamanaEngineTests
cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure -R FrameDebugger
```

### 3.9 Report

Write `task_manager/frame-debugger-5/PHASE3_COMPLETION_REPORT.md`. Explicitly document: the exact new
`ImmediateSubmit()` body shape (how many copies, in what order), how many real compute-pass write textures
were found in a real, live, manually-triggered capture (a quick `gte_send_request`-driven sanity check
against a real running `GreatTamanaEngine.exe` is strongly encouraged here, even though this phase's own
compile-check is Tier-1/Tier-2 only — see `PHASE5`'s own full live-verification pass for the REQUIRED,
comprehensive version of this check). Commit via `git_add`/`git_commit`.
