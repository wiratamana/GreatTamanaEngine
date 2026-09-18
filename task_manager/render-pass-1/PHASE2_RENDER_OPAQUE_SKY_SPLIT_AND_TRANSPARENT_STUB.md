# PHASE2: Split `"GameView"` into `RenderOpaque` + `DrawSkyBackground`, add the `RenderTransparent` stub

_Child of `PHASE0_MASTER_STRATEGY.md`. Depends on `PHASE1_RENDER_PASS_CORE_ABSTRACTION.md`
already being merged (needs `AddRenderPass()`/`PassKind`/`RenderPassCategory`).
Part of the `render-pass-1` campaign._

## Step 1: The Goal

Turn today's single, monolithic `"GameView"` pass (which both draws every
mesh AND draws the sky background, fused into one `vkCmdBeginRendering`/
`vkCmdEndRendering` bracket, with the sky draw only visible to the Frame
Debugger via a hand-written special case) into THREE real, separate,
individually-declared Render Graph passes, executed back-to-back against
the exact same color+depth target:

1. **`"RenderOpaque"`** — the engine's built-in default mesh-drawing pass.
   Draws every entity with a `MeshRenderer` (today: literally every one —
   there is no transparency flag yet, see PHASE0 Step 2 point 6). Clears
   color+depth (this is the FIRST pass to touch the target this frame).
2. **`"DrawSkyBackground"`** — draws the atmosphere sky background. Runs
   SECOND (this is load-bearing, not cosmetic — see the Locked Design
   Decision below), `VK_ATTACHMENT_LOAD_OP_LOAD`s both color and depth
   (never clears), and relies on `AtmosphereSkyBackgroundRenderer`'s own
   already-existing `EQUAL` depth-test pipeline state to only paint pixels
   Opaque didn't already touch.
3. **`"RenderTransparent"`** — a scaffolded, currently-always-empty
   extension point for a FUTURE transparency feature, mirroring the
   already-proven `AddGpuSkinningPasses()` "return nothing, declare
   nothing, when there's genuinely nothing to do" pattern. Runs BETWEEN
   Sky Background and the Aerial Perspective composite pass (the
   conventional forward-rendering order: opaque → sky → transparent →
   post-processing).

## Step 2: The Situation

- `AddGameViewPass()` (`src/Application/RenderPasses.cpp`) is the single
  function responsible for all of today's fused behavior. Its `execute`
  lambda: calls `renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw)`,
  then `game.Render(renderer, aspectWidthOverHeight, nullptr,
  frameDebuggerCapture)` (draws every mesh), then
  `renderer.EndGraphPassRecording()`, THEN, still inside the same pass,
  calls `recordSkyBackground(ctx.cmd)` if set, and (Editor builds only)
  `frameDebuggerCapture->RecordSkyBackgroundDraw(...)`.
- `recordSkyBackground` is built by
  `AtmospherePassSequence.cpp`'s `MakeRecordSkyBackgroundCallback()`,
  which ultimately closes over `AtmosphereLutRenderer::DrawSkyBackground(
  renderer, cmd, viewProjection, atmosphereParameters, frameUniforms,
  skyViewLutName, skyExposure)` — a real, direct `vkCmdDraw()` call
  (`AtmosphereSkyBackgroundRenderer::Draw()`) using a genuinely different
  Pipeline (`Depth Test = Equal`, `Depth Write = Off` — see
  `docs/conventions/frame-debugger.md`'s `frame-debugger-8` campaign
  section for the exact values, already hand-transcribed in
  `DescribeSkyBackgroundPipelineState()`, `src/Editor/FrameDebuggerCapture.h/.cpp`
  — that function stays correct and reusable for the new pass unchanged).
- `Application::Run()` (`Application.cpp`) calls, in this exact order,
  inside the Game-View `if (gameTarget != nullptr)` block: resolves
  `gameEyeWorldPosition`/`gameViewProjection`, calls
  `AddAtmosphereViewLutPasses(...)`, builds `recordGameSkyBackground` via
  `MakeRecordSkyBackgroundCallback(...)`, imports `"GameView"` as
  `h`, calls `AddGameViewPass(b, m_game, m_renderer, h, aspect,
  gpuSkinningBuffers, recordGameSkyBackground, frameDebuggerCapture)`,
  pushes `h` onto `outputs`, THEN (conditionally) declares the Frame
  Debugger replay passes, THEN calls `AddAtmosphereCompositePass(...)`.
- `RenderGraphBuilder::PassBuilder::WriteColorAttachment(handle,
  clearColor = std::nullopt)`/`WriteDepthStencilAttachment(handle,
  clearDepth = std::nullopt)` ALREADY support "don't clear, LOAD existing
  contents" simply by omitting the optional clear-value argument — no
  Render Graph infrastructure change is needed to make a second pass
  write the same target without erasing the first pass's pixels. This is
  exactly what makes the split mechanically safe.
- `RenderSystem::Draw()`/`Game::Render()` have a `maxDrawCount` cutoff
  parameter (added by the `frame-debugger-7` campaign) — irrelevant to
  this phase's real passes (only ever used by the Frame Debugger's own
  replay passes, see PHASE5), but do not confuse it with anything here.

## Step 3: The Plan

### 3.1 — `src/Application/RenderPasses.h`/`.cpp`: rename + split

Rename `AddGameViewPass()` to **`AddRenderOpaquePass()`**. Its new body:

- Declares via `builder.AddRenderPass("RenderOpaque", rg::PassKind::Graphics,
  rg::ViewScope::GameView, rg::RenderPassCategory::General, setup,
  execute)`.
- `setup`: `pass.WriteColorAttachment(gameViewTarget, kGameClearColor)`,
  `pass.WriteDepthStencilAttachment(gameViewTarget, kGameClearDepth)`,
  `DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers)` — UNCHANGED
  from today.
- `execute`: `renderer.BeginGraphPassRecording(...)`, `game.Render(...)`,
  `renderer.EndGraphPassRecording()` — UNCHANGED. **REMOVE** the
  `recordSkyBackground`/`frameDebuggerCapture->RecordSkyBackgroundDraw(...)`
  block entirely from this function — it moves to the new function below.
- Signature DROPS the `recordSkyBackground` parameter entirely (it is no
  longer this pass's concern) — update `RenderPasses.h`'s declaration to
  match, and update its doc comment.

Add a brand-new function, **`AddDrawSkyBackgroundPass()`**:

```cpp
// Render Pass campaign (task_manager/render-pass-1), PHASE2 - the Sky
// Background draw, now a REAL, separate Render Graph pass in its own
// right (previously hand-fused inside AddGameViewPass()'s own execute
// lambda - see PHASE0_MASTER_STRATEGY.md's own Step 2 for the full
// history of why that was a hack). MUST be declared AFTER
// AddRenderOpaquePass() in the SAME builder call, against the SAME
// `gameViewTarget` handle - this pass deliberately does NOT clear either
// attachment (VK_ATTACHMENT_LOAD_OP_LOAD for both color and depth),
// relying on AtmosphereSkyBackgroundRenderer's own EQUAL-depth-test
// pipeline (see DescribeSkyBackgroundPipelineState(),
// src/Editor/FrameDebuggerCapture.h/.cpp) to only paint pixels
// AddRenderOpaquePass() didn't already cover. A true no-op (declares
// NOTHING) if `recordSkyBackground` is empty (mirrors
// AddGpuSkinningPasses()'s own "add nothing when nothing to do" rule) -
// this can legitimately happen if a future caller has no sky to draw at
// all.
void AddDrawSkyBackgroundPass(rg::RenderGraphBuilder& builder, Renderer& renderer, rg::TextureHandle gameViewTarget,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground,
    FrameDebuggerCaptureContext* frameDebuggerCapture = nullptr);
```

Its body:
- If `!recordSkyBackground`, return immediately without declaring
  anything.
- `builder.AddRenderPass("DrawSkyBackground", rg::PassKind::Graphics,
  rg::ViewScope::GameView, rg::RenderPassCategory::General, setup,
  execute)`.
- `setup`: `pass.WriteColorAttachment(gameViewTarget)` (no clear value —
  `std::nullopt`), `pass.WriteDepthStencilAttachment(gameViewTarget)` (no
  clear value).
- `execute`: `renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw)`,
  `recordSkyBackground(ctx.cmd)`, THEN (Editor builds only, guarded by
  `#if GTE_ENABLE_EDITOR` exactly like the OLD code did)
  `if (frameDebuggerCapture != nullptr) {
  frameDebuggerCapture->RecordSkyBackgroundDraw(AtmosphereSkyBackgroundRenderer::ShaderDebugName()); }`,
  THEN `renderer.EndGraphPassRecording()`.
- **IMPORTANT — Frame Debugger cleanup note for PHASE4**: once this pass
  is real and generically discoverable (PHASE4), the
  `isSkyBackgroundDraw`/`FrameDebuggerDrawRecord` special-casing this
  phase still temporarily keeps (`RecordSkyBackgroundDraw()` itself) can
  be DELETED outright, since a real, separate `"DrawSkyBackground"` pass
  makes that entire mechanism redundant — PHASE4 owns removing it. This
  phase (PHASE2) does NOT delete `RecordSkyBackgroundDraw()` yet, to keep
  this phase's own blast radius narrow and independently compilable/
  testable; it is fine (and expected) for `RecordSkyBackgroundDraw()` to
  keep being called by the new pass exactly as shown above, as a
  deliberate, temporary bridge PHASE4 later removes once the Frame
  Debugger no longer needs it.

### 3.2 — `RenderSystem::CollectTransparentRenderables()` + `AddRenderTransparentPass()`

Add to `src/Game/RenderSystem.h`/`.cpp`:

```cpp
// Render Pass campaign, PHASE2 - the transparency-equivalent of
// CollectRenderables() above. Always returns an EMPTY vector today - there
// is no isTransparent/renderQueue concept anywhere on MeshRenderer yet
// (see PHASE0_MASTER_STRATEGY.md's own Step 2, point 6) - this exists
// purely as the real, structural drop-in point a FUTURE transparency
// feature extends, mirroring CollectRenderables()'s own exact shape so
// that future work is a pure additive change to MeshRenderer + a real
// filter added HERE, never a new parallel mechanism.
static std::vector<DrawCommand> CollectTransparentRenderables(Registry& registry);
```//`CollectTransparentRenderables()`'s body is simply `return {};` today,
with a comment explaining why (never delete `registry`'s parameter name
even though it's unused this way — keep the signature stable for the
future).

Add to `src/Application/RenderPasses.h`/`.cpp`:

```cpp
// Render Pass campaign, PHASE2 - the built-in "Render Transparent" pass -
// a real, permanent call site wired into Application::Run(), currently
// ALWAYS a no-op (mirrors AddGpuSkinningPasses()'s own "return/declare
// nothing when there is genuinely nothing to do" pattern) since
// RenderSystem::CollectTransparentRenderables() always returns empty
// today. A future transparency campaign's own job is to make THIS
// function's own body do real work once MeshRenderer gains a real
// isTransparent/renderQueue flag - this campaign's job is only to make
// sure the call site, the pass name, and its correct position in the
// frame (after DrawSkyBackground, before the Aerial Perspective composite
// pass) already exist and are already wired end-to-end.
void AddRenderTransparentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    rg::TextureHandle gameViewTarget, float aspectWidthOverHeight);
```

Body: call `RenderSystem::CollectTransparentRenderables(game.GetRegistry())`
(a new, small, PUBLIC forwarding method may be needed on `Game` — mirror
`Game::CountGameViewDrawCommandsThisFrame()`'s own existing precedent for
"a small, real forwarding accessor Application needs"); if empty, return
immediately without declaring a pass at all. (When it becomes non-empty in
some future campaign, the real body will look exactly like
`AddRenderOpaquePass()`'s own shape — declare via `AddRenderPass()`,
`ViewScope::GameView`, no clear values (LOAD, drawing on top of Opaque +
Sky), and drive its own draw loop over the transparent command list. DO
NOT implement that real body now — it is explicitly out of scope, see
"What We Will NOT Do" below.)

### 3.3 — `Application.cpp`: rewire the call sites

Inside the existing Game-View `if (gameTarget != nullptr) { ... }` block,
change the sequence from:
```
AddGameViewPass(b, m_game, m_renderer, h, aspect, gpuSkinningBuffers, recordGameSkyBackground, frameDebuggerCapture);
outputs.push_back(h);
```
to:
```
AddRenderOpaquePass(b, m_game, m_renderer, h, aspect, gpuSkinningBuffers, frameDebuggerCapture);
outputs.push_back(h);
AddDrawSkyBackgroundPass(b, m_renderer, h, recordGameSkyBackground, frameDebuggerCapture);
AddRenderTransparentPass(b, m_game, m_renderer, h, aspect);
```
(`outputs.push_back(h)` only needs to happen once — `h` is the SAME
`TextureHandle` all three passes write; the Render Graph's own reachability
culling keeps every pass that touches a kept root's resource alive,
mirroring how `AddAtmosphereCompositePass()`'s read of `h` already keeps
`AddGameViewPass()`'s write alive today.) Leave the Frame Debugger replay
block and `AddAtmosphereCompositePass(...)` call immediately following
this exactly where they are today — no reordering relative to those.

`AddSceneViewPass()` (the Scene-View sibling) is explicitly OUT OF SCOPE
for this split per `docs/conventions/frame-debugger.md`'s own long-
standing "Scope is Game View ONLY, permanently" rule for the Frame
Debugger — but for RENDERING correctness/consistency, `AddSceneViewPass()`
should ALSO keep drawing sky background fused inline exactly as it does
today (it is not part of the Frame Debugger's tree at all, so there is
zero benefit to splitting it, and doing so would be pure unnecessary risk
for a view this whole campaign's own scope never touches). Leave
`AddSceneViewPass()` completely untouched by this phase.

### 3.4 — Profiling stats aggregation

`Application.cpp`'s existing
`m_renderGraph.LastKnownStatsFor("GameView")` call (used to feed
`Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::GameView, ...)`)
now needs stats from what is TWO (soon three) real passes instead of one.
Change this call site to sum `LastKnownStatsFor("RenderOpaque")` +
`LastKnownStatsFor("DrawSkyBackground")` (+ `"RenderTransparent"`, once
it ever produces real stats) into one combined `rg::PassGpuStats` before
handing it to `SetGpuPassDrawStats()`/`SetGpuPassTiming()` — add a small,
pure, Tier-1-testable helper (e.g. `CombinePassGpuStats(const
std::vector<rg::PassGpuStats>&)` living in `Application.cpp`'s own
anonymous namespace, or `RenderGraphSnapshot.h` if you judge it more
broadly useful) that sums `drawStats.drawCallCount`/`triangleCount` and
picks a sensible combined `GpuTimingSample` (e.g. sum the millisecond
values when both are `Present`, otherwise fall back to whichever one IS
present — document your exact rule in the completion report). This
FIXES a previously-documented, explicitly-deferred gap
(`docs/conventions/frame-debugger.md`'s `frame-debugger-8` section: "the
parent `GameView` leaf's own aggregate 'Draw Stats' row... does not
count the sky's own raw `vkCmdDraw()` call") as a natural side effect —
call this out explicitly in this phase's completion report.

## Definition of Done

- `"RenderOpaque"` and `"DrawSkyBackground"` are two real, separately
  named passes declared via `AddRenderPass()`, both writing the SAME
  `gameViewTarget` handle, in that order, the second one never clearing.
- `AddRenderTransparentPass()` exists, is wired into `Application.cpp` in
  the correct position, and is a genuine no-op today (confirmed: adding a
  scene with mesh entities and observing the Render Graph panel/Frame
  Debugger shows NO `"RenderTransparent"` pass at all yet — exactly
  mirroring how `AddGpuSkinningPasses()` today adds nothing in CPU-skinning
  mode).
- The rendered image is PIXEL-IDENTICAL to before this phase (same
  meshes, same sky, same depth behavior) — verify with an incremental
  build + `run_app_background` + a `gte_send_request /get_game_view`
  screenshot compared by eye against a screenshot taken before this
  phase's changes.
- `Profiling::GpuPass::GameView`'s stats now correctly include the sky
  draw's own draw call/triangle count (previously undercounted by
  exactly one draw call — see 3.4).
- Incremental compile succeeds; completion report + git commit as usual.

## What We Will NOT Do

- Do NOT implement real transparency (sorting, blending, a real
  `isTransparent` flag on `MeshRenderer`, back-to-front sorting, alpha
  blend pipeline state) — `AddRenderTransparentPass()`'s real body stays
  `return` immediately, forever, until a genuinely separate future
  campaign picks this up.
- Do NOT touch `AddSceneViewPass()` — Scene View keeps its existing fused
  sky-background-inline behavior untouched (see 3.3).
- Do NOT change the ORDER of Opaque vs. Sky — Opaque must stay FIRST
  (the `EQUAL` depth-test optimization inside
  `AtmosphereSkyBackgroundRenderer` depends on it). This is a deliberate,
  documented divergence from the exact literal order the user's own
  example list wrote them in (`Draw Sky LUT` before `Render Opaque`) —
  that list was illustrative, not a strict ordering requirement (see
  PHASE0's own Step 1 note about this).
- Do NOT yet touch the Frame Debugger's tree-building logic
  (`FrameDebuggerData.cpp`) — it still hardcodes a search for the literal
  pass name `"GameView"`, which no longer exists after this phase. That
  is fine and EXPECTED to temporarily leave the Frame Debugger's tree
  slightly wrong/incomplete for `"RenderOpaque"`/`"DrawSkyBackground"`
  specifically — PHASE4 owns fixing this properly, generically, once
  every pass this campaign cares about has already been migrated
  (PHASE3). Do not attempt a partial, throwaway fix here.
