# Testing

Every engine source file except `src/main.cpp` is compiled into a static
library, `gte_core` (see `CMakeLists.txt`) - both the real executable
(`GreatTamanaEngine`) and the unit test suite (`GreatTamanaEngineTests`,
below) link against it, so tests always exercise the exact same compiled
engine code the shipped `.exe` does.

`GreatTamanaEngineTests` (`tests/`) is a [GoogleTest](https://github.com/google/googletest)
suite, gated by the `GTE_BUILD_TESTS` CMake option (`ON` by default, same
"zero-touch when off" philosophy as `GTE_ENABLE_EDITOR`). GoogleTest itself
is not vendored either - it's fetched/built the same way as SDL3/Vulkan/VMA/
ImGui (see `cmake/FetchGTest.cmake`), staged into `third_party/googletest/`
and gitignored.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

(or just run `build\Debug\GreatTamanaEngineTests.exe` directly)

Every test currently in the suite is deliberately "Tier 1": pure logic, with
no live Vulkan device, GPU, or SDL window/video subsystem required to run -
safe on any machine/CI runner, GPU or not:

- `Memory/GpuResourceHandleTests.cpp` / `Memory/GpuMemoryTrackerTests.cpp` -
  `GpuResourceHandle` value semantics and `GpuMemoryTracker`'s Track()/
  Untrack()/totals/snapshot bookkeeping, including generation-counted
  handle-reuse safety and (Editor builds only) debug names.
- `Memory/SdlMemoryTrackerTests.cpp` - `SdlMemoryTracker`'s byte-counting
  wrapper around `SDL_malloc`/`calloc`/`realloc`/`free`, exercised by calling
  those functions directly (no `SDL_Init()`/video subsystem needed - see
  AGENTS.md, "CPU Dependency Memory Tracking"). Always built (SDL is used
  regardless of `GTE_ENABLE_EDITOR`).
- `Input/InputStateTests.cpp` - `InputState`'s held/just-pressed/
  just-released/delta semantics, fed with hand-built `gte::Event` values.
- `Application/EventTranslatorTests.cpp` - the `SDL_Event` -> `gte::Event`
  mapping, using hand-built `SDL_Event` values (no `SDL_Init()` needed).
- `Renderer/VertexTests.cpp` - `Vertex`'s Vulkan binding/attribute
  description metadata.
- `Renderer/ResourcePoolTests.cpp` - the generic `ResourcePool<T, HandleT>`
  slot-map's insert/remove/lookup and generation-guard semantics (backing
  `MeshHandle`/`PipelineHandle` - see `README.md`, "Entity-Component-System").
- `Math/Vec2Tests.cpp` / `Math/Vec3Tests.cpp` - `Vec2`/`Vec3` arithmetic and
  geometric ops (dot/cross/length/normalize/lerp), using exact hand-computed
  values.
- `Math/Mat4Tests.cpp` - `Mat4` multiply/transpose/inverse/`LookAtLH`/
  `PerspectiveFovLH_ZO`, using hand-verified exact values.
- `Math/QuatTests.cpp` - `Quat` multiply/slerp/nlerp/axis-angle/matrix and
  Euler round-trip conversions.
- `ECS/EntityManagerTests.cpp` - `EntityManager`'s Create()/Destroy()/
  IsAlive() handle allocation/recycling/generation semantics.
- `ECS/ComponentStorageTests.cpp` - `ComponentStorage<T>`'s sparse-set
  Add()/Remove()/Has()/Get() and swap-with-last removal, fed with
  hand-built `Entity` values.
- `ECS/RegistryTests.cpp` - `Registry` gluing `EntityManager` +
  `ComponentStorage<T>` together: multi-component-type entities,
  DestroyEntity() clearing every pool, and `Transform`.
- `ECS/CameraTests.cpp` - `Camera`'s pure math helpers, `ProjectionMatrix()`/
  `ViewMatrix()`, checked directly against `Mat4::PerspectiveFovLH_ZO()`/
  `LookAtLH()`.
- `Game/RenderSystemTests.cpp` - `RenderSystem::CollectRenderables()` (the
  pure ECS -> `DrawCommand` step: every entity with a `MeshRenderer` becomes
  one draw command, using its `Transform`'s world matrix if present) and
  `RenderSystem::ResolveActiveCameraViewProjection()` (the pure ECS -> camera
  view-projection step: the first entity with an active `Camera` becomes a
  combined view-projection matrix, `Mat4::Identity()` if none exists) - both
  need nothing but a `Registry`, no live Renderer/GPU device at all.
- `Editor/EditorCameraTests.cpp` - `EditorCamera`'s pure pan/dolly/rotate
  math and pitch clamping (`src/Editor/EditorCamera.h`) - no ImGui/SDL/
  Vulkan involved despite living under `src/Editor/`. Only built when
  `GTE_ENABLE_EDITOR` is `ON` (see `tests/CMakeLists.txt`), since
  `EditorCamera` itself is only compiled into `gte_core` then - same
  "zero-touch when off" rule as the Editor module itself.
- `Editor/MemoryPanelDataTests.cpp` - the Editor "Memory" panel's pure
  data-shaping logic (`BuildMemoryRows()`/`FormatBytes()`/`ToString()`,
  `src/Editor/MemoryPanelData.h`) - sorting/naming/byte-formatting only, no
  ImGui/Renderer/live GPU device involved. Only built when `GTE_ENABLE_EDITOR`
  is `ON`, same reason as `Editor/EditorCameraTests.cpp` above. Also covers
  `BuildHeapBudgetRows()` (reshapes `VmaBudget` entries into plain rows for
  the panel's "GPU Heap Budgets" section - no live `VmaAllocator` needed) and
  `FormatBlockSummary()` (the "VMA Allocated" column's block/sub-allocation
  count wording, including singular vs. plural phrasing).
- `Editor/ImGuiMemoryTrackerTests.cpp` - `ImGuiMemoryTracker`'s byte-counting
  wrapper around Dear ImGui's own `MemAlloc()`/`MemFree()`, exercised by
  calling those functions directly (no live `ImGuiContext`/window needed -
  see AGENTS.md, "CPU Dependency Memory Tracking"). Only built when
  `GTE_ENABLE_EDITOR` is `ON`, same reason as `Editor/EditorCameraTests.cpp`
  above.

Testing `Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory` properly
needs a real `VkDevice`+`VmaAllocator` (and, as `VulkanDevice` is written
today, a real `VkSurfaceKHR` to query present support) - a "Tier 2" of
GPU-backed integration tests, most likely via a headless `VK_EXT_headless_surface`-based
fixture that `GTEST_SKIP()`s cleanly on a GPU-less machine, is a documented
follow-up (see the comment in `tests/CMakeLists.txt`) rather than
implemented yet.

See `README.md` for the overall architecture and `BUILDING.md` for how to
build the engine itself.
