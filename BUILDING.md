# Building
> How to build and run the engine on Windows. For anyone who just cloned the repo.
> Verified against code: 2026-11-02

## TL;DR
- Windows only. Toolchain is **MinGW-w64 (g++) + Ninja**. Nothing else is supported.
- Four commands: configure, build, run, test.
- First configure downloads all dependencies. You need internet. `git` is needed for Sponza and KTX (`bash`).
- No Vulkan SDK and no `glslc` install needed. A shader compiler is downloaded if you have none.

## Requirements
| Tool | Version | Note |
|---|---|---|
| CMake | 3.19 or newer | |
| MinGW-w64 `g++` | with C++20 | On `PATH`. The link step uses GNU `ld` flags, so MSVC does not work |
| Ninja | any recent | On `PATH`. It is a single-config generator |
| `git` (with `bash`) | any recent | KTX needs `bash` at configure time. Sponza test model is fetched with `git` |
| Vulkan GPU driver | 1.3 | Needed to run. Not needed to build. Volk loads `vulkan-1.dll` at runtime |

## Build
```cmd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\GreatTamanaEditor.exe
ctest --test-dir build --output-on-failure
```

- Build only the editor: `cmake --build build --target GreatTamanaEditor`.
- For a Debug build, set `-DCMAKE_BUILD_TYPE=Debug` at configure time. `--config Debug` does nothing with Ninja.
- A new folder under `Projects/` is picked up on the next build. See [Project assembly](docs/guides/project-assembly.md).

## Run flags
| Flag | Effect |
|---|---|
| `--no-vsync` | Uncapped present. Use it for benchmarks only |
| `--vulkan-validation` | Turns on Vulkan validation in any build type |
| `--reimport <source> <dest.gta>` | Headless re-import of one asset. Prints a message, then exits |

The editor starts an HTTP debug server on `127.0.0.1:8080`. See the [HTTP cheat sheet](docs/debugging/http-cheat-sheet.md).

## CMake options
Pass them as `-D<NAME>=ON|OFF` at configure time.

| Option | Default | Effect when OFF |
|---|---|---|
| `GTE_BUILD_TESTS` | ON | No GoogleTest fetch, no test exe, no ctest |
| `GTE_ENABLE_PROFILER` | ON | `GTE_PROFILE_SCOPE` compiles to nothing |
| `GTE_ENABLE_JOB_SYSTEM` | ON | `Schedule()` runs inline. No worker threads |
| `GTE_ENABLE_NETWORK` | ON | The HTTP server never starts |
| `GTE_ENABLE_PROJECT_PANEL` | ON | No Project panel, asset inspector, asset preview or Bone Viewer |
| `GTE_ENABLE_PROJECT_ASSEMBLIES` | ON | No `Projects/*/Libraries` discovery, no DLL loading. New Project refuses |
| `GTE_FETCH_SPONZA` | ON | No Sponza clone into `_reference/` |
| `GTE_CORE_STANDALONE_PROBE_ONLY` | OFF | **CI only.** Configures `gte_core` alone. Never set it by hand |

## What gets downloaded
CMake fetches everything at configure time into `include/`, `lib/` and `third_party/`. All are gitignored, with `_reference/` and `build/`. Later configures reuse the files.

Every dependency is pinned. Each pin is a cache variable `GTE_<NAME>_REF`.

| Dependency | Source | Variable (default) |
|---|---|---|
| SDL3 | GitHub release (prebuilt) | `GTE_SDL3_REF` (`release-3.4.14`) |
| Vulkan-Headers | GitHub zip | `GTE_VULKAN_HEADERS_REF` (`vulkan-sdk-1.4.357.0`) |
| volk | GitHub raw files | `GTE_VOLK_REF` (`1.4.350`) |
| VMA | GitHub raw file | `GTE_VMA_REF` (`v3.4.0`) |
| cpp-httplib, nlohmann/json | GitHub raw file | `GTE_HTTPLIB_REF` (`v0.54.1`), `GTE_JSON_REF` (`v3.12.0`) |
| stb, cgltf | GitHub raw file | `GTE_STB_REF` (commit), `GTE_CGLTF_REF` (`v1.15`) |
| SPIRV-Reflect | GitHub raw files | `GTE_SPIRV_REFLECT_REF` (`vulkan-sdk-1.4.363.0`) |
| MikkTSpace, meshoptimizer | GitHub raw files / zip | `GTE_MIKKTSPACE_REF`, `GTE_MESHOPTIMIZER_REF` (commits) |
| saba (PMX) and glm | GitHub zip subset | `GTE_SABA_REF` (commit), `GTE_GLM_REF` (`1.0.1`) |
| Dear ImGui | GitHub zip | `GTE_IMGUI_REF` (branch `docking`, it moves. Pin a commit if you need it fixed) |
| ImGuizmo | GitHub zip | `GTE_IMGUIZMO_REF` (commit) |
| KTX-Software | GitHub zip, built from source | `GTE_KTX_REF` (`v4.4.2`) |
| GoogleTest | GitHub zip | `GTE_GTEST_REF` (`v1.18.0`) |
| Sponza model | sparse `git` clone of `glTF-Sample-Assets` | `GTE_SPONZA_REF` (`main`), `GTE_SPONZA_REPO_URL` |

- Each staged copy has a `.gte_fetched_ref` stamp. If you change a `GTE_<NAME>_REF`, that dependency is fetched again on the next configure. Example: `-DGTE_KTX_REF=v4.4.2`.
- Fetch everything again: `-DGTE_FORCE_REDOWNLOAD=ON`.
- Download retries per file: `-DGTE_FETCH_RETRIES=<n>` (default 3).
- meshoptimizer files are checked against SHA-256 hashes in `cmake/MeshOptimizerHashes.cmake`. After you change `GTE_MESHOPTIMIZER_REF`, configure once with `-DGTE_MESHOPT_REGENERATE_HASHES=ON` to rewrite that file.
- KTX needs `bash` at configure time. Git for Windows or scoop `bash` is found automatically.

## Shader compiler
Shaders are compiled to SPIR-V at build time. The Vulkan SDK is not needed.
- CMake uses `glslc` if it finds one: `-DGLSLC_EXECUTABLE=...`, then `PATH`, then `VULKAN_SDK`, then `C:/VulkanSDK`.
- If there is none, CMake downloads glslang 16.6.0 into `build/_tools`. Options: `GTE_FETCH_GLSLANG` (default ON), `GTE_GLSLANG_URL`, `GTE_GLSLANG_SHA256`.
- With `-DGTE_FETCH_GLSLANG=OFF` and no compiler, configure fails at the first shader and tells you what to install.

## Cloud noise assets
The `GreatTamanaEditor` target bakes the cloud noise (`tools/cloud_noise_bake`) into `build/assets/clouds` (about 10 MB, 4 s). It runs only when an output is missing or the tool changed. It needs no network: the blue-noise source is in `tools/cloud_noise_bake/data`. Delete `build/assets/clouds` to force a re-bake.

## Troubleshooting
| Symptom | Cause | Fix |
|---|---|---|
| Configure stops, no shader compiler | No `glslc`, and `GTE_FETCH_GLSLANG=OFF` or the download failed | Allow the download, or install `glslc` (Vulkan SDK, `scoop install shaderc`) or pass `-DGLSLC_EXECUTABLE=...` |
| Configure stops, `bash` not found (KTX) | No `bash` on this PC | Install Git for Windows, then configure again |
| Linker rejects `-Wl,--whole-archive` | Not using MinGW-w64 | Use MinGW-w64 and Ninja |
| Sponza missing, only a warning | No `git` or no network | Ignore it, or set `-DGTE_FETCH_SPONZA=OFF` |
| Fetch fails | No internet or a proxy | Fix the network, configure again. Raise `-DGTE_FETCH_RETRIES` if it is flaky |
| A dependency looks broken or stale | Bad partial download | Configure with `-DGTE_FORCE_REDOWNLOAD=ON` |
| Editor starts, no HTTP | Port 8080 is taken | Close the other editor instance |

## Related
- [Getting started](docs/getting-started.md)
- [Testing](TESTING.md)
- [Project assembly](docs/guides/project-assembly.md)
- [Documentation index](docs/README.md)
