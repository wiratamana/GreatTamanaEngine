# Building
> How to build and run the engine on Windows. For anyone who just cloned the repo.
> Verified against code: 2026-10-30

## TL;DR
- Windows only. Toolchain is **MinGW-w64 (g++) + Ninja**. Nothing else is supported.
- Four commands: configure, build, run, test.
- First configure downloads all dependencies. You need internet and `git`.
- `glslc` is the one tool you must install yourself.

## Requirements
| Tool | Version | Note |
|---|---|---|
| CMake | 3.19 or newer | |
| MinGW-w64 `g++` | with C++20 | On `PATH`. The link step uses GNU `ld` flags, so MSVC does not work |
| Ninja | any recent | On `PATH`. It is a single-config generator |
| `glslc` | any recent | On `PATH`. Not downloaded. Install the Vulkan SDK or run `scoop install shaderc` |
| `git` | any recent | Fetches the Sponza test model. Nothing else needs it |
| Vulkan GPU driver | 1.3 | Needed to run. No Vulkan SDK is needed to build |

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
CMake fetches everything at configure time. Later configures reuse the files. These folders are gitignored: `include/`, `lib/`, `third_party/`, `_reference/`, `build/`.

| Dependency | Source | Pin |
|---|---|---|
| SDL3 | GitHub release (prebuilt) | `latest` |
| Vulkan-Headers, volk | GitHub zip | `latest` |
| VMA, cpp-httplib, nlohmann/json, GoogleTest | GitHub | `latest` |
| Dear ImGui | GitHub zip | branch `docking` (moves) |
| ImGuizmo, stb, MikkTSpace, meshoptimizer | GitHub | fixed commit |
| KTX-Software | built from source | `v4.4.2` |
| cgltf | GitHub | `v1.15` |
| saba (PMX/VMD) and glm | GitHub zip subset | fixed commit and `1.0.1` |
| SPIRV-Reflect | vendored zip name in CMake | fixed |
| Sponza model | sparse `git` clone of `glTF-Sample-Assets` | `main` |

- Override a pin: `-D<NAME>_RELEASE_TAG=<tag>`, for example `-DKTX_RELEASE_TAG=v4.4.2`.
- Force a fresh download: `-D<NAME>_FORCE_REDOWNLOAD=ON`.
- Many pins are `latest`. Two clones on different days can build different code.

## Troubleshooting
| Symptom | Cause | Fix |
|---|---|---|
| Configure stops, `glslc` not found | `glslc` not on `PATH` | Install it, open a new shell |
| Linker rejects `-Wl,--whole-archive` | Not using MinGW-w64 | Use MinGW-w64 and Ninja |
| Sponza missing, only a warning | No `git` or no network | Ignore it, or set `-DGTE_FETCH_SPONZA=OFF` |
| Fetch fails | No internet or a proxy | Fix the network, configure again |
| Editor starts, no HTTP | Port 8080 is taken | Close the other editor instance |

## Related
- [Getting started](docs/getting-started.md)
- [Testing](TESTING.md)
- [Project assembly](docs/guides/project-assembly.md)
- [Documentation index](docs/README.md)
