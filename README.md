# GreatTamanaEngine
> A game engine and editor written from scratch in C++20 on Vulkan 1.3. For developers who build, extend or debug it.
> Verified against code: 2026-10-30

## What it is
- Windows only. SDL3 window, Dear ImGui docking editor, Vulkan 1.3 (dynamic rendering).
- Hand-written ECS, math, render graph, job system and profiler.
- GPU-driven mesh rendering with cluster culling and a Hi-Z pyramid.
- Render features: Atmosphere, Shadow, Gtao, Ssgi, VolumetricLight, SceneLighting, ToneMapping, GpuDrivenScene.
- Own asset container `*.gta`. Imports PNG/JPG, glTF, MMD (`.pmx`, `.vmd`) and STL. Scenes are JSON (`*.gtscene`).
- Per-developer game projects built as DLLs and hot reloaded.
- A loopback HTTP server on port 8080 for debugging, including image capture.

## Quick start
```cmd
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\GreatTamanaEditor.exe
```

You need MinGW-w64, Ninja, CMake and `glslc`. See [Building](BUILDING.md). Then follow [Getting started](docs/getting-started.md).

## Layers
```text
GreatTamanaEditor.exe            src/main.cpp
  EditorHost                     composition root and main loop (gte_editor)
    SDL window, ImGui panels     gte_editor
    Core                         gte_core: no SDL, no ImGui
      Game, ECS, Scene, Assets   runtime world and data
      Renderer + RenderGraph     the only code that names Vulkan
      Features (Feature Kit)     render features
      Network, Profiling, Jobs   HTTP server, profiler, workers
```

Only `src/Renderer/` may name Vulkan. Features use the Feature Kit. See [Overview](docs/architecture/overview.md).

## Repo map
| Folder | What is in it |
|---|---|
| `src/` | All engine and editor code. One folder per subsystem |
| `src/Renderer/` | Vulkan, render graph, GPU memory and timing |
| `src/Features/` | Built-in render features, one folder each |
| `src/Editor/` | Everything ImGui: panels, frame debugger, project windows |
| `src/Network/` | The HTTP debug server and its routes |
| `tests/` | GoogleTest suite. Mirrors `src/` |
| `tools/` | Guard checks (`ci/`) and benchmark scripts |
| `cmake/` | Dependency fetch, shader compile, project helpers |
| `docs/` | All documentation |
| `Projects/` | Your game projects. Gitignored |

Folders `build/`, `include/`, `lib/`, `third_party/` and `_reference/` are generated or downloaded. Do not edit them.

## Documentation
| I want to... | Read |
|---|---|
| Build and run | [Building](BUILDING.md), [Getting started](docs/getting-started.md) |
| Run or add tests | [Testing](TESTING.md) |
| Debug over HTTP | [HTTP cheat sheet](docs/debugging/http-cheat-sheet.md) |
| Understand the design | [Overview](docs/architecture/overview.md), [Rendering](docs/architecture/rendering.md) |
| Make a game project | [Project assembly](docs/guides/project-assembly.md) |
| Write a render feature | [Writing a render feature](docs/guides/writing-a-render-feature.md) |
| Follow the code rules | [Contributing](docs/conventions/contributing.md), [AGENTS.md](AGENTS.md#coding-guidelines) |
| Fix something broken | [Troubleshooting](docs/troubleshooting.md) |
| Find every doc | [Documentation index](docs/README.md) |
| See what is planned | [TODO](TODO.md) |

Older history is in git and in the [changelog](docs/CHANGELOG.md).

## Rules in one minute
- Only `src/Renderer/` names Vulkan. Ctest guard checks enforce it.
- Every engine type lives in `namespace gte`.
- Own resources with RAII. No raw `new` or `delete`.
- Make new debug features reachable over HTTP. See [AGENTS.md](AGENTS.md#development--coding-guidelines).
