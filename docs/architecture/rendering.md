# Rendering
> One-page overview of how a frame is drawn: layers, frame flow, formats and materials. Read this first, then the deep docs.
> Verified against code: 2026-10-30

## TL;DR
- Only `src/Renderer/` touches Vulkan. Everything else uses `gte::rg` (render graph) and `gte::fx` (feature kit).
- Each frame is two render graphs. The offscreen graph draws the views. The present graph blits and draws the UI.
- Scene color is linear HDR `RGBA16_Float`. The `ToneMapping` feature makes display colors at the end.
- Rendering uses Vulkan 1.3 dynamic rendering. There is no `VkRenderPass` and no `VkFramebuffer`.
- Two views exist: Primary (Game) and Secondary (Scene). Renderer code says `Primary` / `Secondary`. The editor says Game / Scene.

## Diagram
```text
 Editor / Game / Features(Editor/)     no Vulkan names, no Native()
        |
 src/Features/<Name>/                  only fx::, Math, ECS, Core basics
        |
 src/Core/FeatureKit  (gte::fx)        Vulkan-free verbs and handles
        |
 src/Renderer/RenderGraph (gte::rg)    Vulkan-free headers, Vulkan in .cpp
        |
 src/Renderer/*, src/Renderer/Vulkan/  the only place for Vk*, vk*, VK_*
```
The rule and its CI checks are in [AGENTS.md](../../AGENTS.md#vulkan-boundary-strict).

## How it works
Frame flow, as run by `EditorHost` and `Core`:

1. `Core::Update()` advances time and runs game logic. Before it, `EditorHost` drains HTTP command bridges.
2. `Renderer::BeginFrame()` clears the queued CPU `Submit()` draws.
3. `Core::BuildFrame()` runs the offscreen graph. It waits for the GPU, so the Game view is safe to read after it.
4. `IEditorLayer::BuildUI()` builds ImGui. The Render Graph panel and `/render_graph` get their data.
5. `Core::Present()` runs the present graph. Its GPU timing is read back later (pipelined).

Inside `BuildFrame()`:

1. Clear the per-frame claim sets, cull requests and cluster candidate list.
2. `Render.Prepare`: free unused GPU meshes and materials, pick the view targets.
3. Declare the offscreen graph: GPU skinning, cluster candidates, then all providers.
4. If a Frame Debugger capture is pending, add the replay passes.
5. Compile, record, submit, wait. Then finalize the view textures for sampling.

Providers are declared in two phases: `Before` and `After` the deferred passes. In each phase, `Once` providers go first.
Deferred descriptions are sorted by `RenderPassEvent`. Feature providers draw straight into the builder.

| Provider | Scope | Phase | Event tag |
|---|---|---|---|
| `GpuSkinning` | once | Before | `PreOpaques` |
| `ClearViewTarget` | per view | Before | `BeforeEverything` |
| `PreOpaqueFeatures` | per view | Before | `PreOpaques` |
| `RenderOpaque` | per view | Before | `Opaques` |
| `RenderTransparent` | per view (only with a scene overlay) | Before | `Transparents` |
| `OpaqueFeatures` | per view | After | `Opaques` |
| `PostOpaqueFeatures` | per view | After | `AfterOpaques` |
| `PostTransparentFeatures` | per view | After | `AfterTransparents` |
| `PluginRenderFeatures` | per view | After | `AfterEverything` |

The present graph has two providers: `PresentPrimaryView` and `Present`. See [Render graph](render-graph.md).

## Scene color and the display image
Per view, the shipped features run like this:

| Stage | What runs |
|---|---|
| PreOpaque | Frame stamps, Atmosphere LUTs, sun light, shadow cascades, scene lighting uniforms |
| Opaque | `RenderOpaque` (scene draw), `GpuDrivenScene` passes |
| PostOpaque | Sky background, aerial volume, shadow mask, GTAO, SSGI trace, volumetric volume |
| PostComposite | Blend chain by priority. See below. |

PostComposite chain, in linear HDR, lowest priority first:

| Priority | Feature pass |
|---|---|
| -1600 | `Ssgi.Composite` |
| -1500 | `Gtao.Composite` |
| -1000 | `AtmosphereComposite` |
| -500 | `Shadow.Composite` |
| -250 | `VolumetricLight.Composite` |
| +1000 | `ToneMapping` (last) |

- `ToneMapping` is the display transform. It does exposure, saturation, curve, sRGB encode and dither.
- After the chain, the view target holds display values in 0..1. The editor panel, `/get_game_view` and the swapchain blit copy it as is.
- With `ToneMapping` off, the view shows raw linear color.
- The chain uses `RGBA16_Float` for every private target, so nothing is clamped before tone mapping.

## Rendering without view panels
- With no Game or Scene target (no editor panel visible), `Core` renders the Game view into `DirectPrimaryView`.
- The target uses the scene format and follows the window size.
- It uses the same offscreen graph, so post features and `/get_game_view` still work.
- `PresentPrimaryView` copies it to the swapchain. `Present` then clears (if needed) and draws ImGui.

## Captures
`Encoding::HdrVisualization` turns `RGBA16_Float` into 8 bit:

| Mode | Used for |
|---|---|
| `DisplayReferred` | `GameView`, `SceneView`. Clamp to 0..1, no curve. `/get_game_view` always uses it. |
| `SceneReferred` | `GameViewComposited`, `SceneViewComposited`, `*VolumetricLight`, `FrameDebuggerReplayStep*`. Default tone map. |
| `DebugScaled` | Everything else, such as tiny LUTs. Fixed x400 exposure. |

`/get_texture` picks the mode from the texture name. See [HTTP cheat sheet](../debugging/http-cheat-sheet.md).

## Materials
Details: [PBR materials](../conventions/pbr-materials.md).

| Topic | Fact |
|---|---|
| Material set (set 0) | 5 bindings: base color, `MaterialParams` (48 bytes), normal, metallic-roughness, occlusion |
| Missing image | Replaced by a 1x1 default image, so every set is complete |
| Set pool | `MaterialSetPool`, 4096 sets. Warning above 75%. A lease frees the set with the material. |
| Vertex layouts | `PositionColor`, `PositionNormal`, `PositionNormalUv`, `PositionNormalUvTangent` and a no-input fullscreen layout. Depth-only pipelines exist per layout. |
| Pipeline kinds | `Untextured`, `Textured`, `TexturedAlphaTest`, `Pbr`, `PbrAlphaTest`. Chosen once per mesh part. |
| Texture upload | Batched: 64 MiB staging budget, max 256 images per chunk, one submit and one fence wait per chunk |
| Draw guard | `RenderSystem::Draw` skips a draw whose material set does not resolve. One warning per handle. |
| Release sweep | `Render.Prepare` frees unused meshes and materials after one bounded GPU drain |

## Key files
| File | Role |
|---|---|
| `src/Renderer/Renderer.h` | Facade. Device, swapchain, resource factories, captures, GPU waits. |
| `src/Core/Core.cpp` | `BuildFrame()`, `Present()`, provider registration |
| `src/Renderer/RenderGraph/` | The render graph. See [Render graph](render-graph.md). |
| `src/Core/FeatureKit/` | `gte::fx`, what render features use |
| `src/Renderer/FramePresenter.*`, `FrameRecorder.*` | Swapchain present and offscreen record |
| `src/Features/<Name>/` | One folder per render feature |
| `src/Shaders/` | Shared GLSL. Feature shaders sit in `src/Features/<Name>/Shaders/`. |

## Rules
1. Never name Vulkan outside `src/Renderer/`.
2. Never hardcode a format. Use `ScenePixelFormat()` for scene color and `ColorFormat()` only for the swapchain.
3. Every render target has a depth buffer. Do not add a target or pipeline without depth by habit.
4. Scene geometry pipelines use `ScenePixelFormat()`. Never record them into a swapchain-format target.
5. Disabled passes must not run or leave side effects. See [Render honesty](../conventions/render-honesty.md).

## Gotchas
- A pass whose output nobody reads is culled with no error.
- A provider that reads a deferred pass's texture must run in the `After` phase. Otherwise the writer is culled.
- If the offscreen graph throws, the direct primary target is not blitted. Present clears the swapchain instead. The error is logged once.
- `PostTransparent` and `PreUI` stages exist, but no shipped feature uses them.
- `Renderer` lists `RenderFeatureCompositor` as a friend. This is known debt. Do not copy it.

## Related
- [Render graph](render-graph.md) · [Render honesty](../conventions/render-honesty.md)
- [Frame Debugger](../debugging/frame-debugger.md) · [HTTP cheat sheet](../debugging/http-cheat-sheet.md)
- [Writing a render feature](../guides/writing-a-render-feature.md) · [Feature kit](../reference/feature-kit.md)
- [PBR materials](../conventions/pbr-materials.md) · [Render features](../reference/render-features/README.md)
