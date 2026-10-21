# Rendering

_Part of [GreatTamanaEngine](../../README.md)'s architecture docs. See
[docs/README.md](../README.md) for the full documentation index._

`Renderer` (`src/Renderer/Renderer.h/.cpp`) owns a real Vulkan pipeline built
on top of a set of small RAII wrappers under `src/Renderer/Vulkan/`
(`VulkanInstance` -> `VulkanSurface` -> `VulkanDevice` -> `VulkanSwapchain`),
using **dynamic rendering** (no `VkRenderPass`/`VkFramebuffer`) instead of
SDL's `SDL_Renderer`. Its public surface is still just `Clear()`/`Present()`,
plus `RenderOffscreen()`/`CreateRenderTexture()` for drawing into an
off-screen `RenderTexture` instead of the swapchain — the primitive behind
the Editor's Unity-style "Game"/"Scene" panels, each with its OWN
`RenderTexture` now (see [Editor / Debug UI](editor-debug-ui.md)) that the entity holding
the active `Camera` component renders into, which the Editor then displays
inside its own `ImGui::Image()` panel.
Vulkan itself is accessed exclusively through **volk** (a dynamic meta-loader,
see `BUILDING.md`) — nothing in the engine links a classic Vulkan loader
import lib or calls `vulkan.h` functions directly without going through it.
GPU memory is allocated exclusively through **VMA** (Vulkan Memory
Allocator, see `BUILDING.md`) via `VulkanAllocator`
(`src/Renderer/Vulkan/VulkanAllocator.h/.cpp`) — an RAII wrapper owning a
single `VmaAllocator` that `Renderer` creates once alongside its
instance/device and hands to every GPU resource type
(`RenderTexture`/`Buffer` today) to create its images/buffers through
(`vmaCreateImage`/`vmaCreateBuffer`) instead of each one hand-rolling its own
memory-type lookup and alloc/bind/free calls. `Buffer`
(`src/Renderer/Buffer.h/.cpp`) is the general-purpose GPU buffer primitive
for vertex/index/uniform/staging data, created via
`Renderer::CreateBuffer()`/`CreateDeviceLocalBuffer()` — see
`BufferMemoryUsage` (`Buffer.h`) for the `GpuOnly` (device-local,
not CPU-mappable) vs. `CpuToGpu`/`GpuToCpu` (persistently host-mapped)
distinction. `Renderer::CreateDeviceLocalBuffer()` covers the common
"static GPU-only buffer initialized once" case (vertex/index buffers) by
uploading through a temporary staging `Buffer` and copying it in via
`Renderer::ImmediateSubmit()` — a general one-time-submit-and-wait command
buffer helper, also reusable for future one-off GPU work (e.g. image layout
transitions, mipmap generation) outside the per-frame `Present()`/
`RenderOffscreen()` recording. `Mesh`/`Pipeline` themselves are still
returned by value from `Renderer::CreateMesh()`/`CreatePipeline()`
unchanged — `Renderer` has zero knowledge that an ECS exists; see
[Entity-Component-System](ecs.md) for how something else (`RenderSystem`)
owns/addresses them by handle. `Pipeline` carries one push constant range: a
`mat4 model` immediately followed by a `mat4 viewProj` (vertex stage, 128
bytes total — the guaranteed minimum `maxPushConstantsSize` on every
conformant Vulkan implementation), and `Renderer::Submit()`/
`FrameRecorder::Submit()` take an optional model matrix AND an optional
view-projection matrix (both `Mat4::Identity()` by default) recorded via
`vkCmdPushConstants` right before each draw as
`pc.viewProj * pc.model * vec4(position, 1.0)` — see
`Shaders/Triangle.vert`'s matching `layout(push_constant)` block. A scene
with no active `Camera` pushes an identity `viewProj`, preserving this
engine's original "vertices already authored directly in clip space"
triangle-demo behavior.
`Mesh` (`src/Renderer/Mesh.h`) now optionally carries a real INDEX buffer
alongside its vertex buffer (a second, indexed constructor — the original
non-indexed one is unchanged and still what every built-in primitive shape
uses), and `Pipeline` (`src/Renderer/Pipeline.h/.cpp`) picks its vertex
binding/attribute description via a `VertexLayout` enum —
`PositionColor` (the original `Vertex.h`, the default, used everywhere
today except below) or `PositionNormal` (a new `MeshVertex.h`: position +
a real per-vertex normal, no color). `FrameRecorder` issues
`vkCmdDrawIndexed` instead of `vkCmdDraw` whenever the submitted `Mesh` has
an index buffer. This is what lets a real imported mesh (see
[Asset Pipeline](asset-pipeline.md)) be drawn through the SAME shared `Renderer`/`RenderSystem`
path as everything else — a `*.gta` `AssetType::Mesh` payload's
positions/normals/triangle indices are uploaded as-is (no per-triangle
vertex duplication) and rendered via a small, always-compiled "grey clay"
shader pair (`Shaders/Mesh.vert/.frag` — sun light + hemisphere ambient + cascade shadows from `DirectionalLighting.glsl`,
no textures yet, since a Mesh asset carries no material data — see
[Asset Pipeline](asset-pipeline.md)), through `Game::CreateMeshEntityFromGtaFile()`
(`src/Game/Game.h/.cpp` — mirrors `CreatePrimitiveEntity()` below).
Every render target (the swapchain, or a `RenderTexture`) is paired with a
real **`DepthBuffer`** (`src/Renderer/DepthBuffer.h/.cpp`) at a format
queried once from the physical device (`VulkanDevice::PickDepthFormat()`,
surfaced as `Renderer::DepthFormat()` — the exact depth counterpart to
`ColorFormat()`, same "Render Target Format Matching" discipline), and
`Pipeline` always depth-tests/writes (`VK_COMPARE_OP_LESS`). The swapchain
gets one `DepthBuffer` per swapchain image (`FramePresenter`, indexed by
swapchain image index rather than frame-in-flight slot — the same "a
just-acquired image is guaranteed free to reuse" guarantee
`VulkanFrameSync`'s render-finished semaphores already rely on); each
`RenderTexture` (Editor "Game"/"Scene" views) owns one companion
`DepthBuffer` of its own, resized alongside its color image. This is what
actually makes real (non-coplanar) 3D geometry — the built-in primitive
shapes below — render correctly occluded instead of drawing in whatever
order it happened to be submitted in; the engine's original hardcoded
triangle demo never exposed this gap since its geometry was always flat and
non-overlapping in screen space.

## Scene color and the display image

Scene color is linear HDR in one engine-wide format, `Renderer::ScenePixelFormat()` (RGBA16_Float). Material shaders
(`DirectionalLighting.glsl`) and the sky background write linear radiance; they know nothing about exposure, curves or sRGB.
The swapchain format (`Renderer::ColorFormat()`) is used only by what draws into the swapchain (ImGui, the final blit) and
by the mesh preview.

Per view (Game and Scene), the frame is:

1. Scene draw into the view target (RGBA16F), cleared with the linear `kSceneClearColor`.
2. PostOpaque features (shadow mask, GTAO occlusion, SSGI trace, volumetric volume, atmosphere LUTs).
3. PostComposite chain in priority order, all in linear HDR: `Ssgi.Composite` (-1600), `Gtao.Composite` (-1500),
   `AtmosphereComposite` (-1000), `Shadow.Composite` (-500, debug views only while shadows are applied in materials),
   `VolumetricLight.Composite` (-250), `ToneMapping` (+1000, last).
4. `ToneMapping` is the display transform: exposure, saturation, curve (Clamp, ACES, PBR Neutral), sRGB encode and dither.
   After the chain the view target holds display values in 0..1. The Editor panel, `/get_game_view` and the swapchain blit
   copy it unchanged. With ToneMapping off, the view shows raw linear color.

The compositor's seed, accumulators and private targets are RGBA16F too, so no feature clamps or quantizes the scene before
tone mapping.

### Rendering without view panels

When the Editor gives no Game or Scene target (no Editor, or both panels hidden), `Core` renders the Game view into its own
`DirectGameView` target (scene format, resized with the window) through the same offscreen graph, so post features run and
`/get_game_view` works. The `PresentGameView` pass (`PresentBlit.vert/.frag`) then copies it to the swapchain before the
Present pass records ImGui. `AddPresentPass()` only clears the swapchain (when no direct target was blitted) and records the UI.
GPU skinning runs once, in the offscreen graph.

### Captures

`Encoding::HdrVisualization` picks how an RGBA16F image becomes 8 bit: `DisplayReferred` (clamp, no curve) for `GameView` and
`SceneView`, `SceneReferred` (the default tone map) for scene-referred previews such as `GameViewComposited` and Frame Debugger
steps, and `DebugScaled` (fixed exposure, for tiny-valued LUTs) for everything else. `/get_game_view` always uses
`DisplayReferred`; `/get_texture` classifies by texture name.

## Materials

Details and conventions: [PBR Materials](../conventions/pbr-materials.md).

- **Material set (set 0, 5 bindings).** 0 base color, 1 `MaterialParams` (48 bytes), 2 normal, 3 metallic-roughness,
  4 occlusion. A missing image is replaced by a 1x1 default image, so every set is complete and the old shaders that read
  only bindings 0 and 1 stay valid. `MaterialTexture` holds the four images (`MaterialImages`, indexed by
  `MaterialImageRole`), the params buffer and a `MaterialSetLease`.
- **Set lease.** `MaterialSetPool` (`FREE_DESCRIPTOR_SET`, 4096 sets, 75% warning) allocates under a mutex. The lease frees
  the set and unregisters the handle when the material is destroyed.
- **Vertex layouts.** `PositionNormalUvTangent` (`MeshVertexTangent`, 48 bytes) joins the old layouts. `BuildVertexInputState`
  has no `default:` label, so a new layout cannot silently get the wrong vertex input. Depth-only pipelines exist for every
  layout in `fx::SceneShaderSet` (`DepthOnlyLayouts()`).
- **Pipeline kinds.** `MeshAssetPipelineKind` (`Untextured`, `Textured`, `TexturedAlphaTest`, `Pbr`, `PbrAlphaTest`) is
  chosen once per mesh part; `MeshAssetGpuCatalog::EnsurePipeline` builds the pipeline from one table.
- **Scene block.** `SceneLightingUniformData` carries the eye position for every view (shadow data or not) and the material
  debug view in `groundAmbientAndMaterialDebugView.w`.
- **Batched texture upload.** `GpuResourceFactory::CreateMaterialImages` uploads images in chunks (64 MiB staging budget, at
  most 256 images): one staging buffer, one command buffer, one submit and one fence wait per chunk. `CreateTexture2D` is the
  one-image case of the same path. A failed fence wait is logged, the device is drained once and the call throws.
  `Renderer::GetUploadStats()` returns the submit, image and staging byte counters.
- **Draw guard.** `RenderSystem::Draw` skips a draw whose pipeline needs a material set that does not resolve (one warning per
  handle), so a stale handle never binds an empty set 0.
- **Release sweep.** `Core::BuildFrame()` calls `Game::ReleaseUnusedGpuMaterials()` before any pass is recorded. Unused
  materials are destroyed through `RenderSystem::UnregisterTexture` after one `Renderer::DrainGpuForResourceRelease()`.
