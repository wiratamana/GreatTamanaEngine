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
- `Input/InputStateTests.cpp` - `InputState`'s held/just-pressed/
  just-released/delta semantics, fed with hand-built `gte::Event` values.
- `Application/EventTranslatorTests.cpp` - the `SDL_Event` -> `gte::Event`
  mapping, using hand-built `SDL_Event` values (no `SDL_Init()` needed).
- `Renderer/VertexTests.cpp` - `Vertex`'s Vulkan binding/attribute
  description metadata.

Testing `Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory` properly
needs a real `VkDevice`+`VmaAllocator` (and, as `VulkanDevice` is written
today, a real `VkSurfaceKHR` to query present support) - a "Tier 2" of
GPU-backed integration tests, most likely via a headless `VK_EXT_headless_surface`-based
fixture that `GTEST_SKIP()`s cleanly on a GPU-less machine, is a documented
follow-up (see the comment in `tests/CMakeLists.txt`) rather than
implemented yet.

See `README.md` for the overall architecture and `BUILDING.md` for how to
build the engine itself.
