# PHASE2 — Accurate Per-Step Sky Replay Preview (HEAVY PHASE)

_Child of `PHASE0_MASTER_STRATEGY.md` (READ THAT FIRST). Depends on nothing
from PHASE1 to COMPILE, but is functionally meaningless without it (PHASE1's
new `RecordSkyBackgroundDraw()` is what makes `capture.DrawRecords()` and
this phase's own replay-destination ordering agree - see PHASE0's
"Cross-phase invariant" section). PHASE3 depends on this phase's new replay
step actually existing and being pixel-correct._

**This phase is flagged HEAVY** (see `PHASE0_MASTER_STRATEGY.md`'s own
"Delegation plan" section) - it restructures an existing, already-subtle
render-graph-DECLARATION function
(`AddFrameDebuggerReplayPasses()`) that has a real, confirmed history of
subtle ordering/culling bugs in this exact codebase (see
`docs/conventions/frame-debugger.md`'s own `frame-debugger-7` campaign
write-up: "two real bugs were found and fixed during this campaign's own
mandatory visual spot-check", both inside this SAME function). It gets its
own dedicated double-check pass before the whole-campaign double-check runs.

## Step 1 — The Goal

Stop the Sky Background draw from ever being silently bundled into the LAST
per-object replay step's own image. Instead, add exactly ONE new, dedicated
replay pass whose own accumulated image is "every real object AND the sky,
all drawn" - a genuinely distinct, individually-selectable state from
"every real object, but not yet the sky" (the corrected last-object step).

## Step 2 — The Situation

`src/Application/RenderPasses.cpp`'s `AddFrameDebuggerReplayPasses()`
(roughly lines 139-237 today) currently:
1. Early-returns immediately if `objectCount == 0` (meaning a scene with NO
   mesh entities gets ZERO replay steps at all today - not even one for the
   sky, even though the sky still genuinely renders that frame. This is a
   second, smaller gap this phase also fixes, for free, as a natural
   consequence of the redesign below).
2. Builds exactly `objectCount` destination `RenderTexture`s and exactly
   `objectCount` debug-only render-graph passes, indexed `0..objectCount-1`.
3. Each pass `i` redraws real objects `[0..i]` via
   `game.Render(renderer, aspectWidthOverHeight, nullptr, nullptr,
   /*maxDrawCount=*/i + 1)`.
4. **The bug**: pass `objectCount - 1` (the LAST one) ALSO calls
   `recordSkyBackground(ctx.cmd)` inside its own `execute` lambda, right
   after its own `game.Render()` call - so ITS OWN destination texture
   secretly contains "every object + sky", not "every object, right as of
   this step" like every other pass's own image does.
5. Hands the whole `destinations` vector to
   `capture.SetReplayStepPreviews()` - which, unchanged, will later be moved
   verbatim into `FrameDebuggerHistoryEntry::perObjectStepPreviews` by
   `FrameDebuggerCurrentCapture::CaptureFrame()`
   (`src/Editor/FrameDebuggerHistory.cpp` - this phase does NOT need to touch
   that file's own `.cpp` body at all; it already generically handles a
   `std::vector` of any length. Its sibling header, `FrameDebuggerHistory.h`,
   DOES need a small doc-comment fix - see Step 3.4 below).

The one, single call site (`src/Application/Application.cpp`, around line
691) that invokes this function passes `objectCount =
m_game.CountGameViewDrawCommandsThisFrame()` (a real, pre-computed ECS entity
count, resolved at RENDER-GRAPH-DECLARATION time, BEFORE the real `"GameView"`
pass has actually executed this frame - this is why `objectCount` can never
be sourced from `capture.DrawRecords().size()` here, which is still empty
at declaration time). **This call site itself needs NO changes this
phase** - its own `objectCount`/`recordSkyBackground` arguments are already
exactly what the redesigned function below needs.

## Step 3 — The Plan

### 3.1 — The exact new shape of `AddFrameDebuggerReplayPasses()`

Signature stays byte-for-byte IDENTICAL (no caller anywhere needs to change -
confirmed only one call site exists, `Application.cpp`). Only the BODY
changes. Full replacement body:

```cpp
std::vector<rg::TextureHandle> AddFrameDebuggerReplayPasses(rg::RenderGraphBuilder& builder, Game& game,
    Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground, RenderTexture& gameTarget,
    FrameDebuggerCaptureContext& capture)
{
    std::vector<rg::TextureHandle> destHandles;
#if GTE_ENABLE_EDITOR
    // frame-debugger-8 campaign, PHASE2 - `includeSkyStep`/`totalStepCount`
    // REPLACE the old `if (objectCount == 0) return;` early-out. A real
    // scene with ZERO mesh entities (e.g. Camera + Directional Light only)
    // still genuinely draws the sky every frame - it must still get
    // exactly one real, selectable replay step, not zero (this is a real,
    // confirmed, second fix this phase makes as a natural side effect of
    // the redesign below - see PHASE0_MASTER_STRATEGY.md's own Definition
    // of Done, "zero mesh entities" bullet).
    const bool includeSkyStep = static_cast<bool>(recordSkyBackground);
    const std::size_t totalStepCount = objectCount + (includeSkyStep ? 1 : 0);
    if (totalStepCount == 0) {
        return destHandles;
    }
    destHandles.reserve(totalStepCount);

    // Same width/height/format as the real GameView target, read directly
    // off `gameTarget` (never hardcoded - see AGENTS.md's "Render Target
    // Format Matching") - `gameTarget` itself is NEVER written to here.
    const VkExtent2D extent = gameTarget.Extent();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    const VkFormat format = gameTarget.Format();

    std::vector<RenderTexture> destinations;
    destinations.reserve(totalStepCount); // ESSENTIAL - every destHandle below imports a POINTER-STABLE
                                           // Target() from this vector; it must never reallocate after this point.
    for (std::size_t i = 0; i < totalStepCount; ++i) {
        char debugNameBuffer[48];
        std::snprintf(debugNameBuffer, sizeof(debugNameBuffer), "FrameDebuggerReplayStep%zuColor", i);
        char depthDebugNameBuffer[48];
        std::snprintf(depthDebugNameBuffer, sizeof(depthDebugNameBuffer), "FrameDebuggerReplayStep%zuDepth", i);
        destinations.push_back(
            renderer.CreateRenderTexture(width, height, format, debugNameBuffer, depthDebugNameBuffer));
    }

    for (std::size_t i = 0; i < totalStepCount; ++i) {
        const char* passName = ReplayStepPassName(i); // NEVER a per-call temporary - see this file's own pool comment.

        const rg::TextureHandle destHandle =
            builder.ImportTexture(passName, destinations[i].Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        // frame-debugger-8 campaign, PHASE2 - THE fix. `isSkyStep` is true
        // for EXACTLY ONE index: the extra, dedicated step this phase adds
        // (only reachable when includeSkyStep is true, and only ever equal
        // to `objectCount` - i.e. the very last index in [0, totalStepCount)).
        // Every OTHER index (a real per-object step, i in [0, objectCount))
        // NEVER draws sky, no matter what - this is the actual bug fix:
        // the OLD code's own "if (i + 1 == objectCount && recordSkyBackground)"
        // branch inside the per-object loop is GONE, not just moved.
        const bool isSkyStep = includeSkyStep && (i == objectCount);
        // Real per-object steps redraw objects [0..i] (maxDrawCount = i+1,
        // UNCHANGED from before). The one dedicated sky step redraws EVERY
        // real object (maxDrawCount = objectCount) and then, ADDITIONALLY,
        // the sky - matching the real "GameView" pass's own true order
        // (every entity, then sky, see AddGameViewPass()).
        const std::size_t maxDrawCount = isSkyStep ? objectCount : (i + 1);

        builder.AddPass(passName, rg::ViewScope::GameView,
            [destHandle, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(destHandle, kGameClearColor);
                pass.WriteDepthStencilAttachment(destHandle, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
            },
            [&game, &renderer, aspectWidthOverHeight, maxDrawCount, isSkyStep, recordSkyBackground](
                rg::PassContext& ctx) {
                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                // frameDebuggerCapture is ALWAYS nullptr here, NEVER the
                // real, armed capture context - see this function's own
                // pre-existing correctness-critical comment (unchanged
                // reasoning, still applies): RecordDraw()/RecordEntityDraw()
                // are not idempotent/deduplicated by draw identity, so
                // feeding a real capture pointer into these N+1 replay
                // passes' own game.Render() calls would silently balloon
                // capture.DrawRecords() into O(N^2) duplicated entries.
                game.Render(renderer, aspectWidthOverHeight, nullptr, /*frameDebuggerCapture=*/nullptr, maxDrawCount);
                renderer.EndGraphPassRecording();
                // Sky is drawn ON EXACTLY ONE dedicated step now - the
                // frame-debugger-8 campaign's own fix for the "last
                // object's own preview silently already included sky"
                // bug (see PHASE0_MASTER_STRATEGY.md Step 2, point 5).
                if (isSkyStep && recordSkyBackground) {
                    recordSkyBackground(ctx.cmd);
                }
            });

        destHandles.push_back(destHandle);
    }

    capture.SetReplayStepPreviews(std::move(destinations));
#else
    (void)builder;
    (void)game;
    (void)renderer;
    (void)aspectWidthOverHeight;
    (void)objectCount;
    (void)gpuSkinningOutputBuffers;
    (void)recordSkyBackground;
    (void)gameTarget;
    (void)capture;
#endif
    return destHandles;
}
```

### 3.2 — Line-by-line diff summary (for the implementer's own sanity check)

- `if (objectCount == 0) { return destHandles; }` -> replaced by the new
  `includeSkyStep`/`totalStepCount`/`if (totalStepCount == 0)` block.
- Every literal `objectCount` used as a LOOP BOUND (the two `for` loops, the
  two `.reserve()` calls) -> replaced by `totalStepCount`.
- The per-object loop's inner `execute` lambda's capture list
  `[&game, &renderer, aspectWidthOverHeight, i, objectCount, recordSkyBackground]`
  -> replaced by `[&game, &renderer, aspectWidthOverHeight, maxDrawCount, isSkyStep, recordSkyBackground]`
  (both `maxDrawCount`/`isSkyStep` are computed ONCE per loop iteration,
  OUTSIDE the lambda, then captured BY VALUE - exactly like `i` used to be -
  so each of the `totalStepCount` closures still gets its own correct,
  independent value).
- The old, buggy `if (i + 1 == objectCount && recordSkyBackground) { recordSkyBackground(ctx.cmd); }`
  -> replaced by `if (isSkyStep && recordSkyBackground) { recordSkyBackground(ctx.cmd); }`.
- Nothing about `ReplayStepPassName()`/`ReplayStepPassNamePool()` changes at
  all (already generically supports any index).
- Nothing about the `#else` unused-parameter-suppression branch changes
  (same parameter list as before - no signature change).

### 3.3 — Why this is correct (self-check before moving on)

- **Real per-object steps (`i` in `[0, objectCount)`) NEVER draw sky, ever -
  including the last one.** This directly fixes the confirmed bug (PHASE0
  Step 2, point 5): selecting the last real entity's own leaf will now show
  an image genuinely WITHOUT the sky.
- **The one new sky step's own image is pixel-identical to
  `FrameDebuggerHistoryEntry::preview`** (the real, whole "GameView" pass's
  own retained pre-composite image) - both redraw every real object then the
  sky, in the same order, same camera. This was already true for the OLD
  last-object step by the old code's own comment; it is simply now
  correctly isolated onto its OWN dedicated step instead of being
  incorrectly fused onto the last object's step.
- **A zero-entity scene still gets exactly one real step** (`totalStepCount
  == 1`, `isSkyStep == true` for `i == 0 == objectCount`, `maxDrawCount ==
  objectCount == 0` - `game.Render(..., maxDrawCount=0)` draws nothing, then
  sky draws) - a real, honest "just the sky" replay image, not a missing
  leaf.
- **`capture.DrawRecords()` (produced by the REAL, non-replay "GameView"
  pass, PHASE1's own new `RecordSkyBackgroundDraw()` call) and this
  function's own `destinations`/`destHandles` ordering agree index-for-index**
  - both are "every real entity, in Game::Render()'s own draw order, then
    (if a sky callback exists) the sky, always last" - satisfying PHASE0's
  own "Cross-phase invariant" exactly, with ZERO explicit synchronization
  code required between the two (they are simply both derived from the same
  real facts: `objectCount`/`recordSkyBackground`).

### 3.4 — Fix two stale doc comments this phase's own change makes incomplete (v2 addendum)

Two EXISTING header doc comments describe `perObjectStepPreviews`/
`ReplayStepPreviews()` as strictly "one [entry] per real object" - true
before this phase, no longer the complete picture once the new dedicated sky
step (Step 3.1 above) exists. Fix both, in the SAME commit as Step 3.1:

**(a)** `src/Editor/FrameDebuggerCapture.h` - `SetReplayStepPreviews()`'s own
doc comment currently reads (in part): "Hands over N real, retained
RenderTexture objects - one per real object drawn this frame's 'GameView'
pass". Append one sentence to that same paragraph (do not restructure it):

```cpp
    // frame-debugger-8 campaign, PHASE2 - as of this campaign, N may be ONE
    // GREATER than the real object count: AddFrameDebuggerReplayPasses()
    // (src/Application/RenderPasses.cpp) now appends exactly one additional,
    // dedicated "sky step" destination (index == the real object count)
    // whenever a Sky Background draw callback exists this frame, holding
    // "every real object AND the sky" rather than one more per-object state
    // - see that function's own doc comment in RenderPasses.h for the full
    // "entities first, sky last" ordering contract.
```

**(b)** `src/Editor/FrameDebuggerHistory.h` - `FrameDebuggerHistoryEntry::
perObjectStepPreviews`'s own field doc comment currently reads (in part):
"One real, retained GPU RenderTexture per real object drawn this capture's
'GameView' pass". Append an equivalent one-sentence note there too:

```cpp
    // frame-debugger-8 campaign, PHASE2 - as of this campaign, this vector
    // may hold exactly ONE MORE entry than the real object count: the last
    // entry is a dedicated "sky step" image (every real object AND the sky)
    // whenever a Sky Background draw callback existed for this capture -
    // see FrameDebuggerCapture.h's own SetReplayStepPreviews() doc comment
    // for the full contract this vector is moved in from.
```

Neither change touches any code, only comments - keep them out of the diff
that touches `AddFrameDebuggerReplayPasses()`'s own body if you prefer two
separate, clearly-labeled commits, but land both in this same phase.

## Compile check for this phase

```
cmake --build build --target gte_core
```

**v2 addendum - also confirm the `GTE_ENABLE_EDITOR=OFF` configuration still
compiles**, since this phase again touches `src/Application/RenderPasses.cpp`
(a CORE, always-compiled file) - lower risk than PHASE1's own change (this
phase's edits stay entirely inside the PRE-EXISTING `#if GTE_ENABLE_EDITOR`
block `AddFrameDebuggerReplayPasses()` already had, and the function's own
signature/`#else` branch are both unchanged), but still cheap, quick
insurance matching every prior `frame-debugger-N` campaign's own habit of
checking this at multiple phases, not only the final one:

```
cmake --build build-editor-off --target gte_core
```

This phase adds no new tests of its own (there is no live `Renderer`/
`RenderGraph` in a Tier-1 test - matches this exact function's own
pre-existing, documented "Tier 2/untested directly" status, see
`docs/conventions/frame-debugger.md`'s own "Testing this feature" section).
Instead, do a live, HTTP-driven visual spot-check RIGHT NOW, in this same
phase (mirrors `frame-debugger-7` PHASE3's own precedent of catching two real
bugs exactly this way before they could ship):

1. `run_app_background` the engine executable, open a scene with at least
   one mesh entity (or use whatever scene is already the project's standard
   smoke-test scene).
2. Drive the Frame Debugger over HTTP: `GET /frame_debugger/open`, `GET
   /frame_debugger/enable?value=true`.
3. `GET /frame_debugger/state` to find the LAST entity leaf's own
   `eventIndex` and the new sky step's own `eventIndex` (one past it, IF
   PHASE3 has already landed a leaf for it - if PHASE3 has NOT landed yet,
   confirm indirectly instead: `GET /frame_debugger/select_event?index=<last
   entity's own index>` then `GET /get_swapchain` and visually confirm the
   Frame Debugger's own preview box no longer shows the sky for that
   specific selection - compare against a second capture of the actual
   `"GameView"` leaf itself, which SHOULD still show the sky, per
   `FrameDebuggerStepPreviewKind::PreComposite`'s own unchanged rule).
4. `stop_app_background` when done.

If PHASE3 has not landed yet when this spot-check runs, it is still possible
to prove PHASE2 alone is correct by selecting the last real entity's own
existing leaf and confirming its preview no longer includes sky - the new
extra step will simply not have its own tree row to click on until PHASE3
lands (its underlying image data is still real and correctly ordered either
way, since `perObjectStepPreviews` is a plain vector indexed purely by
position).

## Definition of Done (this phase only)

- `AddFrameDebuggerReplayPasses()`'s signature is unchanged; its body matches
  Step 3.1 above exactly.
- A real per-object step NEVER draws sky (confirmed via the live spot-check
  described in the "Compile check for this phase" section above).
- Exactly one dedicated sky step exists whenever `recordSkyBackground` is
  truthy, drawing every real object then the sky.
- A zero-entity scene still produces exactly one real replay step (the sky
  step alone).
- `Application.cpp`'s own call site needs zero changes (confirmed by reading
  it again after this phase's edit - its `objectCount`/`recordSkyBackground`
  arguments are untouched).
- Both stale doc comments (Step 3.4) are fixed.
- `cmake --build build-editor-off --target gte_core` succeeds (v2 addendum).

## Report

Write `PHASE2_COMPLETION_REPORT.md` in this same folder (include the live
spot-check's own findings/screenshots-described-in-text), then
`git_add`/`git_commit`.
