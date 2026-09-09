# PHASE4_RENDERGRAPH_INTEGRATION — Wiring The Grid Into The Real Frame

> Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: `PHASE3_SCENE_GRID_RENDERER.md`
> (needs a working `SceneGridRenderer::Draw()`).
>
> **This is the one phase that changes real, already-running per-frame
> behavior.** By the time this phase starts, Phase 1-3 are each already
> independently proven to compile/behave correctly in isolation — this
> phase is "just" wiring an already-correct piece into the existing frame
> loop, following several already-proven precedents in this exact codebase
> (`AddBlurValidationPass()`/`FinalizeBlurValidationForSampling()`,
> `AddPresentPass()`'s `recordImGui` callback) rather than inventing a new
> integration shape.

## Step 1: The Goal (Where are we going?)

After this phase, opening the Editor and looking at the "Scene" panel shows
the grid, correctly depth-tested against any spawned geometry, exactly as
designed in Phase 0-3 — with **zero** change to "Game" view behavior, "Game"
view code paths, or release-build (`GTE_ENABLE_EDITOR=OFF`) behavior/binary
size.

## Step 2: The Situation / The Problem (Where are we now?)

- `src/Application/Application.cpp`'s `Run()` builds the offscreen
  RenderGraph's passes inside one lambda (see the block starting around
  `RenderTexture* gameTarget = m_editorLayer->GameViewTarget();`), calling
  `AddGameViewPass()` then, if `sceneTarget != nullptr`,
  `AddSceneViewPass()` (`src/Application/RenderPasses.cpp`) followed
  immediately by `m_editorLayer->AddBlurValidationPass(...)` — **this is
  the exact call site this phase edits.**
- `AddSceneViewPass()`'s current `execute` lambda
  (`src/Application/RenderPasses.cpp`) is:
  ```cpp
  [&game, &renderer, aspectWidthOverHeight, sceneViewProjection](rg::PassContext& ctx) {
      renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
      game.Render(renderer, aspectWidthOverHeight, &sceneViewProjection);
      renderer.EndGraphPassRecording();
  }
  ```
  This is still inside the SAME `vkCmdBeginRendering`/`vkCmdEndRendering`
  bracket that `RenderGraph::ExecuteCompiledGraph()` opened for this pass
  (that bracket is NOT what `BeginGraphPassRecording()`/
  `EndGraphPassRecording()` control — those two only toggle whether
  `Renderer::Submit()` routes its draw directly against `ctx.cmd` instead of
  queuing into the legacy `FrameRecorder` list; see `Renderer.h`'s own doc
  comment on both). **This means any extra raw Vulkan draw call issued
  after `EndGraphPassRecording()` returns, but still inside this same
  `execute` lambda, lands inside the exact same open rendering bracket,
  correctly depth-tested against whatever `Game::Render()` just drew.**
  `AddPresentPass()`'s own `execute` lambda already proves this exact
  pattern works today (`recordImGui(ctx.cmd)` is called right after its own
  `EndGraphPassRecording()`, still inside the same bracket, and ImGui's
  chrome correctly draws on top of whatever was rendered directly into the
  swapchain that same call).
- `IEditorLayer` (`src/Editor/EditorLayer.h`) is the ONLY seam
  `src/Application/` is allowed to depend on for anything Editor-specific
  (see `AGENTS.md`, Clean Architecture) — `RenderPasses.cpp`/`.h`
  themselves must never `#include` anything under `src/Editor/` directly.
  This is why the new callback threaded into `AddSceneViewPass()` must stay
  a plain, opaque `std::function`, with `Application.cpp` (which DOES
  already hold `m_editorLayer`) supplying the actual closure that calls
  into it — exactly mirroring how `AddPresentPass()`'s own `recordImGui`
  parameter is a plain `std::function<void(VkCommandBuffer)>` supplied by
  `Application.cpp`, never a `RenderPasses.cpp`-internal detail.

## Step 3: The Plan

### 3.1 — `src/Editor/EditorLayer.h`: one new pure-virtual method

Add, right after the existing `FinalizeBlurValidationForSampling()`
declaration (same neighborhood — both are "per-frame Scene-view-adjacent
GPU work" methods):

```cpp
// Records the Editor's "Scene" panel infinite ground grid (see
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) directly
// against `cmd` - called by Application::Run() from INSIDE
// AddSceneViewPass()'s own execute callback (RenderPasses.cpp), i.e.
// still inside that pass's open vkCmdBeginRendering/vkCmdEndRendering
// bracket, immediately AFTER Game::Render() has already recorded the real
// scene geometry for this frame - this exact ordering is what makes the
// grid correctly depth-tested/occluded by scene objects already in front
// of it (see SceneGridRenderer::Draw()'s own doc comment).
// `sceneViewProjection` is the exact same combined view-projection matrix
// Game::Render() was just called with for this same pass (see
// IEditorLayer::SceneViewProjection()). Always a safe no-op for
// NullEditorLayer (a release build has no "Scene" panel, and this is
// simply never meaningfully reachable there either way, since
// SceneViewTarget() already always returns nullptr for it).
virtual void RenderSceneGrid(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection) = 0;
```

`EditorLayer.h` already `#include`s `"../Math/Mat4.h"` and forward-declares
`Renderer` — no new includes needed there. It does NOT currently forward
declare/include a Vulkan command-buffer type by name beyond `volk.h` via
its existing transitive includes — verify `VkCommandBuffer` is already
visible (it is, via `RenderGraphTypes.h`/`volk.h` already included
transitively for `AddBlurValidationPass()`'s own `VkExtent2D` parameter) —
if not, add `#include <volk.h>` directly.

### 3.2 — `src/Editor/NullEditorLayer.cpp`: trivial no-op override

Add alongside the existing `FinalizeBlurValidationForSampling` override:

```cpp
void RenderSceneGrid(Renderer& /*renderer*/, VkCommandBuffer /*cmd*/, const Mat4& /*sceneViewProjection*/) override { }
```

### 3.3 — `src/Editor/ImGuiEditorLayer.cpp`: the real implementation

Add `#include "SceneGridRenderer.h"` to this file's own include block (next
to the existing `#include "ComputeBlurValidation.h"`).

Add a new member, right next to the existing `EditorCamera m_sceneCamera;`
member (same "Scene-view-only, camera-adjacent" neighborhood):

```cpp
// The Editor's "Scene" panel infinite ground grid (see
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) - drawn
// every frame the "Scene" panel itself is visible/rendered, with no
// separate on/off toggle (a deliberate, confirmed design decision - see
// PHASE0_MASTER_STRATEGY.md's own "Locked Design Decisions"), exactly
// mirroring how Unity's own Scene grid has no such toggle either.
SceneGridRenderer m_sceneGrid;
```

Add the override itself, right next to the existing
`FinalizeBlurValidationForSampling()` override:

```cpp
// See IEditorLayer::RenderSceneGrid()'s own doc comment. Always called by
// Application::Run() whenever "Scene" was rendered at all this frame (see
// AddSceneViewPass()'s own new recordSceneOverlay parameter,
// PHASE4_RENDERGRAPH_INTEGRATION.md) - no additional visibility guard is
// needed here, since Application::Run() already only calls
// AddSceneViewPass() (and therefore only ever triggers this callback) when
# sceneTarget != nullptr, i.e. exactly when "Scene" is actually visible.
void RenderSceneGrid(Renderer& renderer, VkCommandBuffer cmd, const Mat4& sceneViewProjection) override
{
    m_sceneGrid.Draw(renderer, cmd, sceneViewProjection);
}
```

(Fix the stray `#` typo in the pasted comment above, same proofreading note
as earlier phases.)

No change is needed to `ImGuiEditorLayer`'s destructor — `m_sceneGrid` is a
plain (non-pointer/non-optional) member with its own correct RAII
destructor (`SceneGridRenderer::~SceneGridRenderer()` calls `Reset()`,
which safely `vkDeviceWaitIdle()`s before destroying its pipeline), so it
is torn down automatically, in the right relative order, exactly like
`m_blurValidation`'s own equivalent member requires no special destructor
handling either — verify this assumption by re-reading
`ImGuiEditorLayer`'s actual destructor before finishing this phase (search
for `~ImGuiEditorLayer` in that file) to confirm no ImGui-descriptor-style
manual cleanup is needed for this specific member (it should not be, since
`SceneGridRenderer` never allocates an ImGui descriptor of any kind).

### 3.4 — `src/Application/RenderPasses.h`: extend `AddSceneViewPass()`'s signature

Change:
```cpp
void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {});
```
to:
```cpp
void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {},
    const std::function<void(VkCommandBuffer, const Mat4&)>& recordSceneOverlay = {});
```

Update this function's own doc comment to mention the new parameter,
mirroring `AddPresentPass()`'s own `recordImGui` doc comment style exactly:
*"`recordSceneOverlay`, if set, is invoked once, immediately after
`Game::Render()`'s own draws for this pass finish (still inside this
pass's open dynamic-rendering bracket) - passed this pass's own `cmd` and
`sceneViewProjection` again, so a caller (Application::Run(), via
`IEditorLayer::RenderSceneGrid()`) can layer a Scene-view-only visual
overlay (the Editor's infinite ground grid -
task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) on top of
the real scene geometry, correctly depth-tested against it. Empty (the
default) draws nothing extra - the exact pre-existing behavior."*

### 3.5 — `src/Application/RenderPasses.cpp`: implement the new parameter

Change `AddSceneViewPass()`'s body from:
```cpp
void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    builder.AddPass(
        "SceneView",
        [sceneViewTarget, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(sceneViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(sceneViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
        },
        [&game, &renderer, aspectWidthOverHeight, sceneViewProjection](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            game.Render(renderer, aspectWidthOverHeight, &sceneViewProjection);
            renderer.EndGraphPassRecording();
        });
}
```
to:
```cpp
void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer, const Mat4&)>& recordSceneOverlay)
{
    builder.AddPass(
        "SceneView",
        [sceneViewTarget, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(sceneViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(sceneViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
        },
        [&game, &renderer, aspectWidthOverHeight, sceneViewProjection, recordSceneOverlay](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            game.Render(renderer, aspectWidthOverHeight, &sceneViewProjection);
            renderer.EndGraphPassRecording();
            if (recordSceneOverlay) {
                recordSceneOverlay(ctx.cmd, sceneViewProjection);
            }
        });
}
```

**Do not** wire `recordSceneOverlay` through `ctx.recordDraw` for
`DrawStats`/GPU-triangle-count purposes in this phase — matching the
already-existing precedent that `AddPresentPass()`'s own `recordImGui`
overlay draw calls are likewise never counted in that pass's `DrawStats`
either (ImGui's own chrome draws are invisible to the Profiler's
draw-call/triangle counters today) — this is a deliberate, consistent
simplification, not an oversight; a future phase could add this later if
ever genuinely needed (see `PHASE5_POLISH_AND_VERIFICATION.md`'s own
"explicitly out of scope" note).

### 3.6 — `src/Application/Application.cpp`: supply the new callback

At the existing call site (inside the offscreen `RenderGraph::Execute()`
`build` lambda, in the `if (sceneTarget != nullptr) { ... }` block), change:
```cpp
const Mat4 sceneViewProjection = m_editorLayer->SceneViewProjection(aspect);
const rg::TextureHandle h =
    b.ImportTexture("SceneView", sceneTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
AddSceneViewPass(b, m_game, m_renderer, h, aspect, sceneViewProjection, gpuSkinningBuffers);
outputs.push_back(h);
```
to:
```cpp
const Mat4 sceneViewProjection = m_editorLayer->SceneViewProjection(aspect);
const rg::TextureHandle h =
    b.ImportTexture("SceneView", sceneTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
// The Editor's infinite ground grid (see
// task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md) - a
// plain std::function keeps RenderPasses.cpp itself completely
// Editor-agnostic (see AGENTS.md, Clean Architecture); only this call
// site (which already legitimately holds m_editorLayer) knows the real
// callback reaches into IEditorLayer::RenderSceneGrid().
const std::function<void(VkCommandBuffer, const Mat4&)> recordSceneGrid =
    [this](VkCommandBuffer cmd, const Mat4& viewProj) { m_editorLayer->RenderSceneGrid(m_renderer, cmd, viewProj); };
AddSceneViewPass(b, m_game, m_renderer, h, aspect, sceneViewProjection, gpuSkinningBuffers, recordSceneGrid);
outputs.push_back(h);
```

Everything after this (the existing `FinalizeRenderTextureForExternalSampling(offscreenCmd,
*sceneTarget);` call, the `AddBlurValidationPass()` call, etc.) is
completely unaffected — the grid is already fully composited into
`sceneTarget`'s color image by the time any of that runs, since it happened
strictly before this pass's own `vkCmdEndRendering` (still inside
`AddSceneViewPass()`'s own `execute` lambda).

### 3.7 — Sanity-check the RenderGraph `#include`s

`RenderPasses.h` already includes `<functional>` (for the pre-existing
`recordImGui` parameter on `AddPresentPass()`) — no new include needed
there. `Application.cpp` already includes `RenderPasses.h`. No other file
needs a new include for this phase.

### 3.8 — `Definition of Done` for this phase

- Full engine build (`GreatTamanaEngine` target, `GTE_ENABLE_EDITOR=ON`)
  succeeds.
- Launch the built engine (`run_app_background`), confirm via
  `gte_send_request("/get_swapchain")` or a screenshot that the "Scene"
  panel now shows a grey grid with red/blue axis lines through the origin,
  fading with distance, and that dragging/spawning a primitive (e.g. via
  Hierarchy's "Create 3D Object" > Cube) visibly OCCLUDES the grid lines
  directly underneath it (proving the depth test is correct) — then
  `stop_app_background` it.
- Build with `GTE_ENABLE_EDITOR=OFF` and confirm it still compiles/links
  cleanly with zero reference to any new file from this campaign in that
  configuration (this should already fall out for free, since every new
  file lives entirely inside `if(GTE_ENABLE_EDITOR)` CMake blocks, but
  confirm it explicitly as part of this phase's own compile check, not
  assumed).
- Run the existing automated test suite (`ctest`) and confirm zero
  regressions — this phase touches `RenderPasses.h/.cpp`/`Application.cpp`/
  `EditorLayer.h`/`NullEditorLayer.cpp`/`ImGuiEditorLayer.cpp`, none of
  which have their own dedicated automated tests today (Tier 2, per
  `TESTING.md`), so "zero regressions" here specifically means the whole
  pre-existing suite still passes unchanged.
