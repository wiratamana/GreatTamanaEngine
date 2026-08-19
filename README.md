# GreatTamanaEngine

A raw game engine built from scratch.

## Goal

The plan is to develop a raw game engine from scratch, with the very foundation
built on **SDL3** (the new generation after SDL2) for window and event handling.

## Architecture

The very first architecture layers the engine like this:

```
SDL -> Application -> Window and Renderer -> Game
```

- **Application** is the only layer that knows about SDL directly. It owns the
  main loop and is responsible for initializing/shutting down SDL.
- **Window** and **Renderer** are custom objects that act as an abstraction
  layer on top of SDL. Other layers (like Game) interact with these custom
  objects instead of touching SDL directly.
- **Game** sits on top of Window and Renderer, and has no direct knowledge of
  SDL either.

At this stage, Window and Renderer will still internally depend on SDL
objects — that's okay for now. The abstraction can be tightened later as the
engine evolves.

### Event handling

SDL's raw event stream never reaches `Game` (or anything else past
`Application`) directly. Each frame, `Application::Run()` polls SDL and turns
every event into the engine's own vocabulary before anything else sees it:

```
SDL_Event -> EventTranslator -> gte::Event -> InputState.Apply() + Game::OnEvent()
                                                                 \-> Game::Update(dt, InputState)
```

- **`EventTranslator`** (`src/Application/EventTranslator.h/.cpp`) is the only
  other place besides `Application` itself allowed to touch `SDL_Event`. It
  translates each raw SDL event into `gte::Event` (`src/Event/Event.h`), using
  engine-owned `KeyCode`/`MouseButton` enums instead of SDL's — so nothing
  past this point ever needs to know SDL exists.
- **`InputState`** (`src/Input/InputState.h/.cpp`) tracks continuous,
  queryable input state (held keys/buttons, mouse position/delta), built up
  by applying translated events. It's passed into `Game::Update()` for
  polling-style input, e.g. `input.IsKeyDown(KeyCode::W)` for movement while a
  key is held.
- **`Game::OnEvent(const Event&)`** is called once per translated event, for
  discrete/one-shot reactions (window resized, a key just pressed, quit) —
  as opposed to the continuous polling done via `InputState` in `Update()`.

### Math

`src/Math/` (`Vec2`/`Vec3`/`Vec4`/`Mat4`/`Quat`) is a from-scratch math
library — no GLM dependency, the same "own the core data model" philosophy
as the hand-rolled ECS below. `Mat4` is column-major/column-vector (matches
GLSL's `mat4` layout exactly, so `Mat4::Data()` uploads to a push
constant/uniform with zero transpose) and the engine's coordinate system is
left-handed, Y-up, Z-forward.

### Rendering

`Renderer` (`src/Renderer/Renderer.h/.cpp`) owns a real Vulkan pipeline built
on top of a set of small RAII wrappers under `src/Renderer/Vulkan/`
(`VulkanInstance` -> `VulkanSurface` -> `VulkanDevice` -> `VulkanSwapchain`),
using **dynamic rendering** (no `VkRenderPass`/`VkFramebuffer`) instead of
SDL's `SDL_Renderer`. Its public surface is still just `Clear()`/`Present()`,
plus `RenderOffscreen()`/`CreateRenderTexture()` for drawing into an
off-screen `RenderTexture` instead of the swapchain — the primitive behind
the Editor's Unity-style "Game"/"Scene" panels (a camera renders into a
`RenderTexture`, which the Editor displays inside an `ImGui::Image()` panel).
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
"Entity-Component-System" below for how something else (`RenderSystem`)
owns/addresses them by handle. `Pipeline` also now carries one push
constant range (a single `mat4 model`, vertex stage), and
`Renderer::Submit()`/`FrameRecorder::Submit()` take an optional model
matrix (`Mat4::Identity()` by default) recorded via `vkCmdPushConstants`
right before each draw — see `Shaders/Triangle.vert`'s matching
`layout(push_constant)` block.

### Entity-Component-System (ECS)

The engine's Scene/World data model lives under `src/ECS/`: `Entity`
(cheap, generational index+id, never a pointer/string), `EntityManager`
(id allocation/recycling), `ComponentStorage<T>` (a sparse-set pool per
component type), and `Registry` (owns one of each). Rolled by hand rather
than via a third-party library (EnTT), the same "own the core data model"
choice as `src/Math/` not depending on GLM. `Transform`
(`ECS/Components/Transform.h`) and `MeshRenderer`
(`ECS/Components/MeshRenderer.h`) are the two components that exist today —
both plain data, no behavior, no GPU/SDL ownership of their own.
`MeshRenderer` references a mesh/pipeline purely by handle
(`MeshHandle`/`PipelineHandle`, `src/Renderer/MeshHandle.h`/
`PipelineHandle.h`) — the exact same cheap, generational, index+generation
shape as `Entity` and `GpuResourceHandle`, minted by a generic
`ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`) rather than ever
embedding a live `Mesh`/`Pipeline` in a component.

`RenderSystem` (`src/Game/RenderSystem.h/.cpp`) is the one piece of the
engine allowed to depend on both the ECS world and `Renderer` — the same
"only one layer crosses this boundary" rule this engine already applies to
SDL (only `Application` touches it directly). `Renderer` itself never
depends on ECS in any way: `Submit()` takes a plain `Mat4`, never an
`Entity`/`Registry`. `RenderSystem::CollectRenderables()` is a pure
function (every entity with a `MeshRenderer` becomes one `DrawCommand`,
using its `Transform`'s world matrix if present) that needs nothing but a
`Registry` — no live Renderer/GPU device — so it's unit-tested exactly like
the rest of ECS (see `TESTING.md`). `RenderSystem::Draw()` is the one
non-pure step that resolves those handles against its own
`ResourcePool<Mesh, MeshHandle>`/`ResourcePool<Pipeline, PipelineHandle>`
and calls `Renderer::Submit()`. `Game` no longer holds a hardcoded
`Pipeline`/`Mesh` pair at all — it owns a `Registry` + `RenderSystem` and
just creates entities/components.

### Editor / Debug UI

An optional in-engine Editor module lives under `src/Editor/`, gated by the
`GTE_ENABLE_EDITOR` CMake option (`ON` by default). `Application` only ever
talks to the `IEditorLayer` interface (`src/Editor/EditorLayer.h`); exactly
one of two implementations gets compiled in, selected purely by which `.cpp`
CMake adds:

- **`ImGuiEditorLayer`** (real, `GTE_ENABLE_EDITOR=ON`) — owns the Dear ImGui
  context (fetched from ImGui's **docking** branch — see
  `cmake/FetchImGui.cmake` — with `ImGuiConfigFlags_DockingEnable` set) plus
  its SDL3 and Vulkan backends (routed through volk), and a `RenderTexture`
  that Game's camera renders into for the "Game"/"Scene" panels. Lays out a
  Unity-style default arrangement the first time it runs (built once via the
  `DockBuilder` API, then left to the user/`imgui.ini` afterwards): a
  full-viewport `DockSpace` with a top menu bar (`File > Exit`, wired to
  `IEditorLayer::WantsExit()` so `Application::Run()` can end its main loop
  the same way closing the OS window does), **"Hierarchy"** docked left,
  **"Inspector"** docked right, and **"Scene"**/**"Game"** tabbed together in
  the remaining center — drag the "Scene" tab out to split it side-by-side
  with "Game" at any time, exactly like Unity. "Hierarchy" lists every entity
  that has a `Transform` (via `Game::GetRegistry()` — the Editor's only,
  read/write, view into Game's ECS world) and lets you select one; "Inspector"
  shows/edits the selected entity's `Transform` (position/rotation/scale) and
  displays its `MeshRenderer` handles read-only. **Current limitation:**
  there is no dedicated editor scene camera yet (no `Camera`
  component/view-projection matrix at all), so "Scene" simply displays the
  same texture as "Game" for now — a real, independently-orbitable Scene
  camera is a natural follow-up once `Camera` exists as an ECS component.
- **`NullEditorLayer`** (`GTE_ENABLE_EDITOR=OFF`) — every method is a no-op;
  `GameViewTarget()` always returns `nullptr`, meaning "render straight to
  the swapchain, fullscreen". This is what makes `-DGTE_ENABLE_EDITOR=OFF` a
  genuine release/final-game build: no ImGui fetch, no ImGui sources
  compiled, no ImGui symbols linked at all — not just a runtime flag.

`Game` never depends on the Editor at all, in either direction — that's what
keeps turning the Editor off a zero-touch operation for gameplay code; the
Editor only ever *observes*/edits Game's ECS world through
`Game::GetRegistry()`, a public accessor Game exposes without knowing or
caring who calls it.

## Building

See **[BUILDING.md](BUILDING.md)** for prerequisites and build instructions.

## Testing

See **[TESTING.md](TESTING.md)** for how to build and run the test suite.

## Status

Early foundation stage, but past the basic-scaffolding phase for several
pieces:

- Window/Renderer/Game scaffolding is in place, and event handling flows
  through `EventTranslator`/`InputState` as described above instead of raw
  SDL events reaching `Game` directly.
- `Renderer` owns a real Vulkan pipeline (instance/device/swapchain/command
  buffers, using dynamic rendering) instead of SDL's `SDL_Renderer`, including
  off-screen rendering into a `RenderTexture` for Editor panels.
- The Editor module is wired up end-to-end: Dear ImGui (docking branch)'s
  SDL3 + Vulkan backends are integrated behind `IEditorLayer`, with a full
  Unity-style docked layout — top menu bar (`File > Exit`), "Hierarchy"
  (left), "Inspector" (right), and "Scene"/"Game" tabbed in the center, all
  freely rearrangeable/splittable via ImGui docking. "Game" displays Game's
  camera output via a `RenderTexture` ("Scene" shares the same texture for
  now — see "Editor / Debug UI" above for why); "Hierarchy"/"Inspector" list
  and edit entities/components straight from Game's ECS world via
  `Game::GetRegistry()`. Toggling `GTE_ENABLE_EDITOR` fully includes/excludes
  the whole module, down to CMake never fetching or compiling ImGui at all
  when it's off.
- GPU memory allocation goes through **VMA** (Vulkan Memory Allocator) via
  the `VulkanAllocator` RAII wrapper (`src/Renderer/Vulkan/`) — `Renderer`
  owns a single `VmaAllocator`. `RenderTexture` creates its `VkImage` through
  `vmaCreateImage`/`vmaDestroyImage`, and `Buffer`
  (`src/Renderer/Buffer.h/.cpp`) creates `VkBuffer`s through
  `vmaCreateBuffer`/`vmaDestroyBuffer` — both replacing what used to be a
  manual `FindMemoryType()` + `vkAllocateMemory`/`vkBindMemory`/`vkFreeMemory`
  dance. `Renderer::CreateBuffer()`/`CreateDeviceLocalBuffer()` cover
  host-mapped (uniform/staging) and device-local-via-staging-upload
  (vertex/index) buffers respectively; `Renderer::ImmediateSubmit()` is the
  reusable one-shot command buffer helper behind the latter. Verified with a
  runtime smoke test (mapped-buffer round-trip + a full staging-buffer ->
  device-local-buffer copy) actually executing against a live Vulkan device,
  and building cleanly with both `GTE_ENABLE_EDITOR` `ON` and `OFF`.
- A from-scratch **Math library** (`src/Math/`: `Vec2`/`Vec3`/`Vec4`/`Mat4`/
  `Quat`) backs everything above and below — no GLM dependency. Fully
  unit-tested (multiply/transpose/inverse/`LookAtLH`/`PerspectiveFovLH_ZO`,
  `Quat` slerp/nlerp/axis-angle/Euler round-trips) against hand-verified
  exact values.
- A hand-rolled **Entity-Component-System** (`src/ECS/`: `Entity`/
  `EntityManager`/`ComponentStorage<T>`/`Registry`) is the engine's Scene/
  World data model — no third-party ECS library (EnTT), same "own the core
  data model" choice as Math. `Transform` and `MeshRenderer` are the two
  components that exist today. Fully unit-tested, including
  generation-guarded stale-handle safety.
- The ECS is wired all the way into actual rendering, not just present as
  inert data: `RenderSystem` (`src/Game/RenderSystem.h/.cpp`) is the one
  class allowed to depend on both the ECS world and `Renderer` — `Renderer`
  itself gained zero ECS awareness in the process. A generic
  `ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`) mints
  generational `MeshHandle`/`PipelineHandle` values a `MeshRenderer`
  component can safely hold instead of ever embedding a live GPU resource.
  `Pipeline` carries a push-constant `mat4 model`, threaded through
  `Renderer::Submit()`/`FrameRecorder` down to `vkCmdPushConstants`, so each
  entity's `Transform` genuinely drives where it's drawn. `Game` builds a
  small demo scene (three entities sharing one mesh/pipeline, positioned via
  `Transform` alone) proving the whole ECS -> `RenderSystem` -> `Renderer`
  pipeline end to end — verified both by the test suite
  (`RenderSystem::CollectRenderables()`'s pure ECS -> draw-command logic)
  and visually (three independently-positioned triangles on screen).
