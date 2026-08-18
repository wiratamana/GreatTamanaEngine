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
see Building below) — nothing in the engine links a classic Vulkan loader
import lib or calls `vulkan.h` functions directly without going through it.
GPU memory is allocated exclusively through **VMA** (Vulkan Memory
Allocator, see Building below) via `VulkanAllocator`
(`src/Renderer/Vulkan/VulkanAllocator.h/.cpp`) — an RAII wrapper owning a
single `VmaAllocator` that `Renderer` creates once alongside its
instance/device and hands to every GPU resource type (`RenderTexture` today)
to create its images/buffers through (`vmaCreateImage`/`vmaCreateBuffer`)
instead of each one hand-rolling its own memory-type lookup and
alloc/bind/free calls.

### Editor / Debug UI

An optional in-engine Editor module lives under `src/Editor/`, gated by the
`GTE_ENABLE_EDITOR` CMake option (`ON` by default). `Application` only ever
talks to the `IEditorLayer` interface (`src/Editor/EditorLayer.h`); exactly
one of two implementations gets compiled in, selected purely by which `.cpp`
CMake adds:

- **`ImGuiEditorLayer`** (real, `GTE_ENABLE_EDITOR=ON`) — owns the Dear ImGui
  context plus its SDL3 and Vulkan backends (routed through volk), and a
  `RenderTexture` that Game's camera renders into for the "Game" panel.
- **`NullEditorLayer`** (`GTE_ENABLE_EDITOR=OFF`) — every method is a no-op;
  `GameViewTarget()` always returns `nullptr`, meaning "render straight to
  the swapchain, fullscreen". This is what makes `-DGTE_ENABLE_EDITOR=OFF` a
  genuine release/final-game build: no ImGui fetch, no ImGui sources
  compiled, no ImGui symbols linked at all — not just a runtime flag.

`Game` never depends on the Editor at all, in either direction — that's what
keeps turning the Editor off a zero-touch operation for gameplay code.
Hierarchy/Inspector/Scene panels are the natural next additions once there's
an actual scene to inspect; currently only the "Game" panel exists.

## Building

Windows only, for now.

Prerequisites:

- **CMake 3.19+**
- **A C++20 toolchain CMake can generate for**, e.g. one of:
  - Visual Studio 2017+ (with the "Desktop development with C++" workload)
  - Ninja + MSVC/clang
  - MinGW-w64 (g++) + mingw32-make or Ninja
- `SDL3` is not vendored in this repo — CMake downloads the official prebuilt
  SDL3 headers/DLL/import lib straight from the SDL GitHub releases
  (https://github.com/libsdl-org/SDL) into `include/`, `SDL3.dll`, and
  `lib/SDL3.lib` on first configure (see `cmake/FetchSDL3.cmake`). These are
  gitignored — every clone fetches its own copy, so nothing SDL-related is
  committed to the repo. Subsequent configures reuse what was already
  downloaded and don't need the network again.
- Vulkan is likewise not vendored, and **no Vulkan SDK installation is
  required**. CMake fetches the official `Vulkan-Headers`
  (https://github.com/KhronosGroup/Vulkan-Headers) into `include/vulkan` and
  `include/vk_video`, plus `volk` (https://github.com/zeux/volk) — a tiny
  meta-loader — into `third_party/volk` (see `cmake/FetchVulkan.cmake`).
  volk resolves all Vulkan function pointers by dynamically loading
  `vulkan-1.dll` at runtime, so there's nothing to link against or copy next
  to the .exe: any machine with a Vulkan-capable GPU driver installed already
  has `vulkan-1.dll` on its normal DLL search path. These are gitignored too,
  same as SDL3.
- The **Vulkan Memory Allocator (VMA)** header
  (https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) is
  fetched the same way, into `third_party/vma/vk_mem_alloc.h` (see
  `cmake/FetchVMA.cmake`), and gitignored like everything else above. Every
  GPU allocation in the engine (`RenderTexture` today) goes through the
  `vma` target via `VulkanAllocator` (`src/Renderer/Vulkan/VulkanAllocator.h/.cpp`)
  — see Rendering above and Status below.
- Dear ImGui (core + its SDL3/Vulkan backends) is fetched the same way, into
  `third_party/imgui/` (see `cmake/FetchImGui.cmake`), but **only** when
  `GTE_ENABLE_EDITOR` is `ON` (the default) — a build configured with
  `-DGTE_ENABLE_EDITOR=OFF` never touches the network for this and never
  compiles a single ImGui source file.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
build\Debug\GreatTamanaEngine.exe
```

(swap the `-G` generator for whatever matches your installed toolchain, e.g.
`-G Ninja` or `-G "MinGW Makefiles"`)

## Status

Early foundation stage, but past the basic-scaffolding phase for several
pieces:

- Window/Renderer/Game scaffolding is in place, and event handling flows
  through `EventTranslator`/`InputState` as described above instead of raw
  SDL events reaching `Game` directly.
- `Renderer` owns a real Vulkan pipeline (instance/device/swapchain/command
  buffers, using dynamic rendering) instead of SDL's `SDL_Renderer`, including
  off-screen rendering into a `RenderTexture` for Editor panels.
- The Editor module is wired up end-to-end: Dear ImGui's SDL3 + Vulkan
  backends are integrated behind `IEditorLayer`, with a working "Game" panel
  that displays Game's camera output via a `RenderTexture`. Toggling
  `GTE_ENABLE_EDITOR` fully includes/excludes it, down to CMake never
  fetching or compiling ImGui at all when it's off.
- GPU memory allocation goes through **VMA** (Vulkan Memory Allocator) via
  the new `VulkanAllocator` RAII wrapper (`src/Renderer/Vulkan/`) — `Renderer`
  owns a single `VmaAllocator`, and `RenderTexture` now creates its `VkImage`
  through `vmaCreateImage`/`vmaDestroyImage` instead of the manual
  `FindMemoryType()` + `vkAllocateMemory`/`vkBindImageMemory`/`vkFreeMemory`
  dance it used to do by hand. Verified building cleanly both with
  `GTE_ENABLE_EDITOR` `ON` and `OFF`. Future GPU resource types (vertex/
  index/uniform/staging buffers) should follow the same pattern via
  `vmaCreateBuffer` once they're added.
