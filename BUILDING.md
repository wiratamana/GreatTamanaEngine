# Building

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
  GPU allocation in the engine (`RenderTexture`/`Buffer`) goes through the
  `vma` target via `VulkanAllocator` (`src/Renderer/Vulkan/VulkanAllocator.h/.cpp`)
  — see Rendering (in `README.md`) and Status (in `README.md`) for details.
- Dear ImGui (core + its SDL3/Vulkan backends) is fetched the same way, into
  `third_party/imgui/` (see `cmake/FetchImGui.cmake`), but **only** when
  `GTE_ENABLE_EDITOR` is `ON` (the default) — a build configured with
  `-DGTE_ENABLE_EDITOR=OFF` never touches the network for this and never
  compiles a single ImGui source file.
- The Editor's Unity-style "Project" panel (a live file browser rooted at a
  "Project" folder created next to the built `.exe`, plus drag-and-drop
  import from Windows Explorer — see `README.md`, "Editor / Debug UI") is
  gated by its OWN switch, `GTE_ENABLE_PROJECT_PANEL` (`ON` by default),
  separate from `GTE_ENABLE_EDITOR` — pass `-DGTE_ENABLE_PROJECT_PANEL=OFF`
  to build the rest of the Editor without it. Only meaningful when
  `GTE_ENABLE_EDITOR` is also `ON`.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
build\Debug\GreatTamanaEngine.exe
```

(swap the `-G` generator for whatever matches your installed toolchain, e.g.
`-G Ninja` or `-G "MinGW Makefiles"`)

See `README.md` for the overall architecture and `TESTING.md` for how to
build and run the test suite.
