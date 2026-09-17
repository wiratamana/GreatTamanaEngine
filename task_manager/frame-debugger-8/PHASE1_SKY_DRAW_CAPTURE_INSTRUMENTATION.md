# PHASE1 — Sky Draw Capture Instrumentation

_Child of `PHASE0_MASTER_STRATEGY.md` (READ THAT FIRST). Depends on nothing
else in this campaign. PHASE3 depends on this phase's new
`FrameDebuggerDrawRecord::isSkyBackgroundDraw` field and
`DescribeSkyBackgroundPipelineState()` function existing._

## Step 1 — The Goal

Give the engine a real, first-class way to record "the Sky Background draw
just happened" - mirroring `FrameDebuggerCaptureContext::RecordEntityDraw()`'s
own existing shape as closely as possible - and wire the ONE real call site
that actually invokes it, in production, every frame a capture is armed.

## Step 2 — The Situation

See `PHASE0_MASTER_STRATEGY.md` Step 2, points 1, 2, 4, and 6 in full - this
phase implements the fix for points 2 and 4 (a real, distinct pipeline state
description) and point 4 (the actual instrumentation call site).

Files this phase touches:
- `src/Editor/FrameDebuggerCapture.h` (struct + method declarations)
- `src/Editor/FrameDebuggerCapture.cpp` (method bodies)
- `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h` (new, tiny,
  always-compiled static identifier function)
- `src/Application/RenderPasses.cpp` (the one real call site, inside
  `AddGameViewPass()`)
- `src/Game/Game.h` (doc-comment-only touch-up - v2 addendum, see Step 3.6)
- `tests/Editor/FrameDebuggerCaptureTests.cpp` (new Tier-1 tests)

## Step 3 — The Plan

### 3.1 — `src/Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h`: a real, permanent shader identifier

Add a new `public` static member function to the `AtmosphereSkyBackgroundRenderer`
class (right after the existing `Draw()` declaration, before `Reset()`):

```cpp
    // frame-debugger-8 campaign, PHASE1 - a real, permanent, hand-verified
    // identifier for the vert+frag shader pair this class's own
    // EnsurePipeline() actually loads ("shaders/AtmosphereSkyBackground.vert.spv"/
    // ".frag.spv" - see that method's own ReadShaderFile() calls) - mirrors
    // Pipeline::DebugName()'s own "X.vert/X.frag" naming convention for a
    // normal mesh Pipeline (e.g. "Mesh.vert/Mesh.frag"), so the Frame
    // Debugger's new Sky Background capture leaf (see
    // task_manager/frame-debugger-8/PHASE1_SKY_DRAW_CAPTURE_INSTRUMENTATION.md)
    // can label itself with a REAL, verifiable fact instead of an invented
    // cosmetic name like "Sky Background" - this campaign's own Locked
    // Design Decision 2 (PHASE0_MASTER_STRATEGY.md). Every future caller
    // that needs to identify this pass must go through this function -
    // never duplicate this literal string a second time anywhere else. A
    // plain `static constexpr` - no VkDevice, no instance state, directly
    // Tier-1-testable with zero setup.
    static constexpr const char* ShaderDebugName() noexcept
    {
        return "AtmosphereSkyBackground.vert/AtmosphereSkyBackground.frag";
    }
```

This is the ONLY change to this file. Do not touch `Draw()`/`EnsurePipeline()`
themselves - this campaign never changes how the sky is actually rendered,
only how it's OBSERVED.

### 3.2 — `src/Editor/FrameDebuggerCapture.h`: new field + two new declarations

**(a)** Add a new field to `FrameDebuggerDrawRecord`, APPENDED at the very end
of the struct (Locked Design Decision 5, PHASE0):

```cpp
struct FrameDebuggerDrawRecord {
    std::uint32_t entityIndex = 0;
    std::uint32_t entityGeneration = 0;
    std::string displayName;
    std::string pipelineDebugName;
    std::string materialTextureDebugName;
    std::uint32_t triangleCount = 0;

    // frame-debugger-8 campaign, PHASE1 - true for the ONE real record
    // representing the Sky Background full-screen-triangle draw (see
    // FrameDebuggerCaptureContext::RecordSkyBackgroundDraw() below) -
    // false (the default) for every real per-ENTITY record
    // RecordEntityDraw() produces. `entityIndex`/`entityGeneration` are
    // left at their default (0, 0) and MUST be ignored by any reader when
    // this is true - there is no real ECS entity behind this record at
    // all (see FrameDebuggerData.cpp's BuildGameViewDrawRecordLeaf(), PHASE3
    // of this campaign, for the one place that already knows to branch on
    // this flag instead of reading entityIndex/entityGeneration).
    bool isSkyBackgroundDraw = false;
};
```

**(b)** Add a new pure function declaration, right after
`DescribeStandardPipelineState()`'s own declaration:

```cpp
// frame-debugger-8 campaign, PHASE1 - this engine's real, hand-verified
// Sky Background pipeline facts, cross-checked against
// AtmosphereSkyBackgroundRenderer.cpp's own EnsurePipeline() construction
// code at implementation time (never guessed) - GENUINELY DIFFERENT from
// DescribeStandardPipelineState()'s own values (Depth Test EQUAL not LESS,
// Depth Write OFF not ON - see that class's own header comment for why).
// Pure and free, exactly like DescribeStandardPipelineState() - directly
// Tier-1-testable.
FrameDebuggerStandardPipelineState DescribeSkyBackgroundPipelineState();
```

**(c)** Add a new `public` method to `FrameDebuggerCaptureContext`, right
after `RecordEntityDraw()`'s own declaration:

```cpp
    // frame-debugger-8 campaign, PHASE1 - records the ONE real Sky
    // Background draw call this frame's "GameView" pass issues (see
    // AddGameViewPass(), RenderPasses.cpp) - call this ONCE, immediately
    // after invoking the real `recordSkyBackground` callback, and ONLY
    // when this capture context is actually armed (mirrors every other
    // call site's own "only touch this when non-null" convention). Reuses
    // RecordDraw()'s own existing dedup/draw-call-count/last-view-
    // projection bookkeeping internally (Locked Design Decision 6,
    // PHASE0_MASTER_STRATEGY.md) - the sky uses the exact same view-
    // projection matrix every other draw in this pass used this frame, so
    // passing LastViewProjection() back into that shared bookkeeping is
    // correct, not a placeholder. `pipelineDebugName` should be
    // `AtmosphereSkyBackgroundRenderer::ShaderDebugName()` - a real,
    // permanent, non-fabricated identifier (never invent a cosmetic label
    // like "Sky Background" here - see PHASE0's Locked Design Decision 2).
    void RecordSkyBackgroundDraw(const std::string& pipelineDebugName);
```

### 3.3 — `src/Editor/FrameDebuggerCapture.cpp`: the three new bodies

**(a)** `DescribeSkyBackgroundPipelineState()` - add right after
`DescribeStandardPipelineState()`'s own body:

```cpp
FrameDebuggerStandardPipelineState DescribeSkyBackgroundPipelineState()
{
    // Every value below is transcribed directly from
    // AtmosphereSkyBackgroundRenderer.cpp's own EnsurePipeline() real,
    // hardcoded construction code (confirmed at implementation time -
    // never guessed):
    //   - colorBlendAttachment.blendEnable = VK_FALSE -> "Opaque (no blend)".
    //   - rasterizer.depthClampEnable left at its zero-initialized default
    //     (VK_FALSE) -> ZClip "On" (same as DescribeStandardPipelineState()).
    //   - depthStencil.depthTestEnable = VK_TRUE, depthCompareOp =
    //     VK_COMPARE_OP_EQUAL - "Equal", DELIBERATELY DIFFERENT from
    //     DescribeStandardPipelineState()'s own "Less" - this pass only
    //     ever survives at a pixel whose depth is still exactly the
    //     frame's own clear value (see that renderer class's own header
    //     comment for the full reasoning).
    //   - depthStencil.depthWriteEnable = VK_FALSE -> "Off", DELIBERATELY
    //     DIFFERENT from DescribeStandardPipelineState()'s own "On" -
    //     nothing is ever meant to occlude behind the sky.
    //   - rasterizer.cullMode = VK_CULL_MODE_NONE -> "None" (same).
    //   - depthStencil.stencilTestEnable = VK_FALSE, no stencil struct
    //     populated -> every stencil field "n/a (no stencil test)" (same).
    FrameDebuggerStandardPipelineState state;
    state.blendMode = "Opaque (no blend)";
    state.zClip = "On";
    state.zTest = "Equal";
    state.zWrite = "Off";
    state.cull = "None";
    state.stencilRef = "n/a (no stencil test)";
    state.stencilComp = "n/a (no stencil test)";
    state.stencilPass = "n/a (no stencil test)";
    state.stencilFail = "n/a (no stencil test)";
    state.stencilZFail = "n/a (no stencil test)";
    return state;
}
```

**(b)** `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` - add right
after `RecordEntityDraw()`'s own body:

```cpp
void FrameDebuggerCaptureContext::RecordSkyBackgroundDraw(const std::string& pipelineDebugName)
{
    // Reuses RecordDraw()'s own existing dedup/draw-call-count/last-view-
    // projection bookkeeping (Locked Design Decision 6,
    // PHASE0_MASTER_STRATEGY.md) - this is what makes the PARENT "GameView"
    // leaf's own aggregate `shaderName` (built by joining
    // capture.PipelineDebugNames() in FrameDebuggerData.cpp's
    // BuildGameViewLeaf()) correctly include the sky's own real shader pair
    // too, alongside whatever mesh shaders ran this frame.
    RecordDraw(pipelineDebugName, /*materialTextureDebugName=*/std::string(), m_lastViewProjection);

    FrameDebuggerDrawRecord record;
    record.displayName = pipelineDebugName;
    record.pipelineDebugName = pipelineDebugName;
    // A real, honest fact - AtmosphereSkyBackgroundRenderer::Draw() issues
    // exactly ONE vkCmdDraw(cmd, 3, 1, 0, 0) call (one full-screen
    // triangle, 3 vertices) - never a placeholder/invented number.
    record.triangleCount = 1;
    record.isSkyBackgroundDraw = true;
    m_drawRecords.push_back(std::move(record));
}
```

**(c)** No change needed to `Reset()` - it already clears `m_drawRecords`
wholesale (which will now include sky records too, exactly as intended).

### 3.4 — `src/Application/RenderPasses.cpp`: the one real call site

Add an `#include` (unconditional - the Atmosphere feature is always compiled,
per `AGENTS.md`'s "Atmosphere Scattering" section) near the top, alongside
the existing includes:

```cpp
#include "../Renderer/Atmosphere/AtmosphereSkyBackgroundRenderer.h"
```

Then, inside `AddGameViewPass()`'s own `execute` lambda (the ONLY place this
phase changes - do not touch `AddSceneViewPass()`, `AddPresentPass()`, or
`AddFrameDebuggerReplayPasses()`, which is PHASE2's own job), change:

```cpp
            renderer.EndGraphPassRecording();
            if (recordSkyBackground) {
                recordSkyBackground(ctx.cmd);
            }
        });
```

to:

```cpp
            renderer.EndGraphPassRecording();
            if (recordSkyBackground) {
                recordSkyBackground(ctx.cmd);
#if GTE_ENABLE_EDITOR
                // frame-debugger-8 campaign, PHASE1 - the Sky Background
                // pass is a real, direct vkCmdDraw() full-screen-triangle
                // draw (AtmosphereSkyBackgroundRenderer::Draw()) that
                // bypasses RenderSystem::Draw()/Renderer::Submit() entirely
                // (see that class's own header comment) - so, unlike every
                // per-entity draw (RenderSystem::Draw()'s own existing
                // RecordEntityDraw() call site), it was previously
                // completely INVISIBLE to the Frame Debugger, even though
                // it genuinely runs every frame. This one, explicit call
                // site is the fix - see
                // task_manager/frame-debugger-8/PHASE0_MASTER_STRATEGY.md
                // for the full root-cause trail. Guarded exactly like this
                // same file's own AddFrameDebuggerReplayPasses() body (see
                // this file's own top-of-file comment) - a real dereference
                // of an Editor-only type in this CORE, always-compiled
                // file.
                if (frameDebuggerCapture != nullptr) {
                    frameDebuggerCapture->RecordSkyBackgroundDraw(AtmosphereSkyBackgroundRenderer::ShaderDebugName());
                }
#endif
            }
        });
```

This is CORRECTNESS-CRITICAL positioning: the call must happen strictly
AFTER `recordSkyBackground(ctx.cmd)` (so it only ever records a draw that
really was just issued) and it must stay inside `AddGameViewPass()` only -
never copy this into `AddSceneViewPass()` (Locked Design Decision 3,
PHASE0).

**IMPORTANT, HIGH-RISK STEP (v2 addendum) — this is exactly the class of
change `FrameDebuggerCapture.h`'s own header comment warns about**: a new
`#if GTE_ENABLE_EDITOR`-guarded dereference of `frameDebuggerCapture` was just
added to `RenderPasses.cpp`, a CORE, always-compiled file. Before moving on to
Step 3.5, do the `build-editor-off` compile check in this phase's own
"Compile check" section below NOW (not deferred to PHASE4) - a mistake here
(e.g. an unguarded reference, or a missing `#endif`) would compile fine in the
normal `build` directory (where `GTE_ENABLE_EDITOR=ON`) while silently
breaking the `GTE_ENABLE_EDITOR=OFF` configuration.

### 3.5 — `tests/Editor/FrameDebuggerCaptureTests.cpp`: new Tier-1 tests

Open this file, find the existing tests for `DescribeStandardPipelineState()`
and `RecordEntityDraw()`/`RecordDraw()` for the exact assertion style/test
framework macros already in use (this codebase's existing convention - copy
it exactly), then add:

1. **`DescribeSkyBackgroundPipelineStateReturnsRealDistinctValues`** - asserts
   `zTest == "Equal"`, `zWrite == "Off"`, and that every OTHER field
   (`blendMode`, `zClip`, `cull`, all four stencil fields) matches
   `DescribeStandardPipelineState()`'s own equivalent values exactly (proving
   the two functions only genuinely differ where they should).
2. **`RecordSkyBackgroundDrawAppendsOneMarkedRecord`** - construct a fresh
   `FrameDebuggerCaptureContext`, call `RecordSkyBackgroundDraw("Test.vert/Test.frag")`,
   assert `DrawRecords().size() == 1`, `DrawRecords()[0].isSkyBackgroundDraw == true`,
   `DrawRecords()[0].pipelineDebugName == "Test.vert/Test.frag"`,
   `DrawRecords()[0].triangleCount == 1`.
3. **`RecordSkyBackgroundDrawAfterEntityDrawsAppendsAtTheEnd`** - call
   `RecordEntityDraw()` twice (any plausible fake args, mirroring an existing
   test's own fixture pattern) then `RecordSkyBackgroundDraw()` once, assert
   `DrawRecords().size() == 3` and that `DrawRecords()[2].isSkyBackgroundDraw
   == true` while `DrawRecords()[0]`/`DrawRecords()[1]` are both `false` -
   this is a direct regression test for this campaign's own cross-phase
   ordering invariant (`PHASE0_MASTER_STRATEGY.md`'s "Cross-phase invariant"
   section).
4. **`RecordSkyBackgroundDrawFeedsPipelineDebugNamesAndDrawCallCount`** -
   call `RecordSkyBackgroundDraw("Test.vert/Test.frag")` on a fresh context,
   assert `PipelineDebugNames()` contains `"Test.vert/Test.frag"` and
   `DrawCallCount() == 1` (proving the reused `RecordDraw()` bookkeeping
   really ran).
5. **`ResetClearsSkyBackgroundRecordsToo`** - call `RecordSkyBackgroundDraw()`
   then `Reset()`, assert `DrawRecords().empty()`.

### 3.6 — `src/Game/Game.h`: fix a stale doc-comment invariant this phase's own change breaks (v2 addendum)

`Game::CountGameViewDrawCommandsThisFrame()`'s existing doc comment (right
above its own body) currently reads, in part:

> "...so `objectCount == capture.DrawRecords().size()` holds in practice -
> but if that ever stops being true..."

This claim is TRUE today, but becomes FALSE the instant this phase's own
`RecordSkyBackgroundDraw()` call site (Step 3.4) starts running in
production: `capture.DrawRecords()` now ALSO contains the one, extra,
non-entity sky record whenever a sky callback exists that frame, so
`capture.DrawRecords().size()` becomes `objectCount + 1` in the normal case
(`objectCount` alone only ever counted `MeshRenderer`-bearing entities - see
that same function's own body, `RenderSystem::CollectRenderables(m_registry).size()`,
which this phase does not change). Left uncorrected, this comment would
silently mislead a future maintainer into believing a now-false invariant.

Update the comment's own wording (keep everything else in that doc comment
about the "skipped DrawCommand" caveat unchanged) to read, in its place:

```cpp
    // task_manager/frame-debugger-8 campaign, PHASE1 - as of this campaign,
    // capture.DrawRecords().size() is NO LONGER always equal to
    // `objectCount` returned by this method: FrameDebuggerCaptureContext::
    // RecordSkyBackgroundDraw() (src/Editor/FrameDebuggerCapture.h/.cpp)
    // appends one additional, non-entity record whenever a Sky Background
    // draw callback exists this frame (see AddGameViewPass(),
    // src/Application/RenderPasses.cpp) - so, in the normal case,
    // `capture.DrawRecords().size() == objectCount + 1`. The pre-existing
    // caveat below (about a DrawCommand whose mesh/pipeline handle fails to
    // resolve) still applies identically to the ENTITY portion of that
    // count; the sky record itself is never affected by it (the sky draw
    // never goes through RenderSystem::Draw()/DrawCommand resolution at
    // all - see AtmosphereSkyBackgroundRenderer.h's own header comment).
```

Splice this new comment in immediately before the pre-existing "IMPORTANT
documented assumption..." paragraph (do not delete that paragraph - it is
still correct for the entity-only portion of the count, just no longer the
WHOLE story once the sky record exists too).

## Compile check for this phase (no full build yet)

Run a quick, targeted incremental build limited to the affected translation
units (not `ctest`, not a clean rebuild) - e.g.:

```
cmake --build build --target gte_core
cmake --build build --target GreatTamanaEngineTests
```

Then run just the new/changed test file's binary (or the whole
`GreatTamanaEngineTests` binary if that is this codebase's normal habit) and
confirm the 5 new tests above pass, plus every pre-existing
`FrameDebuggerCaptureTests.cpp` test still passes unchanged.

**v2 addendum - also confirm the `GTE_ENABLE_EDITOR=OFF` configuration still
compiles/links**, since Step 3.4 added a new `#if GTE_ENABLE_EDITOR`-guarded
dereference to `src/Application/RenderPasses.cpp`, a CORE, always-compiled
file (see `PHASE0_MASTER_STRATEGY.md`'s own "v2 addendum" section for why this
matters and why it is checked at THIS phase, not deferred to PHASE4). The
pre-existing `build-editor-off` directory is already configured with
`-DGTE_ENABLE_EDITOR=OFF` - no fresh `cmake` configure step is needed, just:

```
cmake --build build-editor-off --target gte_core
```

A successful compile+link here (no need to run its own tests - `gte_core`
alone is enough to prove the new guarded call site is correct in both
configurations) is required before considering this phase done.

## Definition of Done (this phase only)

- `AtmosphereSkyBackgroundRenderer::ShaderDebugName()` exists and returns the
  exact real shader-pair string.
- `FrameDebuggerDrawRecord::isSkyBackgroundDraw` exists, defaults to `false`,
  appended at the end of the struct.
- `DescribeSkyBackgroundPipelineState()` exists and returns the real, distinct
  Equal/Off values.
- `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()` exists, reuses
  `RecordDraw()` internally, and appends exactly one correctly-marked record.
- `AddGameViewPass()` calls it exactly once, in the right place, guarded by
  `#if GTE_ENABLE_EDITOR` and a null check - and nowhere else.
- All 5 new Tier-1 tests (Step 3.5) pass; no pre-existing test regresses.
- `Game.h`'s stale doc-comment invariant is corrected (Step 3.6).
- `cmake --build build-editor-off --target gte_core` succeeds (v2 addendum).
- Nothing under `src/Editor/FrameDebuggerData.h/.cpp` or
  `src/Application/RenderPasses.cpp`'s `AddFrameDebuggerReplayPasses()` is
  touched yet - those are PHASE3/PHASE2's own jobs respectively.

## Report

Write `PHASE1_COMPLETION_REPORT.md` in this same folder once done (mirroring
every prior `frame-debugger-N` campaign's own per-phase report convention),
then `git_add`/`git_commit` this phase's changes plus its own report.
