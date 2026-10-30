# Architecture overview
> How the engine starts, runs one frame, and splits work across layers and threads. Read this first.
> Verified against code: 2026-10-30

## TL;DR
- Two static libraries: `gte_core` (no SDL, no ImGui) and `gte_editor` (SDL3, ImGui). The program is `GreatTamanaEditor`.
- `EditorHost` is the host. It owns the window, `Core`, the editor UI and the HTTP server.
- `Core` is the engine facade. Its frame calls are `Update()`, `BuildFrame()` and `Present()`.
- All GPU and ECS work runs on the main thread. HTTP threads and job workers never touch it directly.
- Only `src/Renderer/` names Vulkan. See [Rendering](rendering.md).

## Diagram
```text
main.cpp
  └─ EditorHost          gte_editor: SDL, Window, ImGui, HTTP server, bridges
       │                 init order: SdlContext > Window > Core > EditorLayer > NetworkServer
       └─ Core           gte_core: no SDL, no ImGui
            ├─ Renderer ── RenderGraph ── offscreen pipeline + present pipeline
            ├─ Game ─────── Registry (ECS)
            │                ├─ RenderSystem             ECS to Renderer
            │                ├─ MeshInstantiationSystem  spawn primitives and *.gta
            │                ├─ AnimationSystem          poses, skinning
            │                └─ PhysicsSystem            Verlet bone chains, no Renderer
            ├─ EngineContext { Time }
            ├─ RenderFeatureCompositor   built-in and project render features
            └─ ProjectAssemblyHost       loads <Project>_Game.dll and <Project>_Editor.dll
```

## How it works

### Startup
1. `main` installs the log sink first. Vulkan validation output is then captured too.
2. `main` reads the command line (table below). `--reimport` runs headless and exits.
3. `EditorHost` builds, in order: `SdlContext`, `Window`, `Core`, the editor layer, then `NetworkServer`.
4. `SdlContext` installs the SDL memory tracker, then calls `SDL_Init`.
5. `Core` builds `Renderer`, `RenderGraph`, `Game` and the render pipelines.
6. The host sweeps stale temp files, registers panel names, loads project assemblies, then creates built-in feature modules.
7. Last step: `NetworkServer::Start(8080)`. It binds loopback only. A bind failure is logged, not fatal.

| Flag | Effect |
|---|---|
| `--reimport <source> <dest.gta>` | Headless import. No window. Exit code 0 or 1. |
| `--vulkan-validation` | Vulkan validation on, in any build type. |
| `--no-vsync` | Uncapped present. For benchmarks only. |

### One frame
`EditorHost::Run()` repeats these steps:
1. Start the profiler frame. Clear per-frame input flags.
2. Poll SDL events (see [Events](#events)).
3. Drain network commands: engine command, Hi-Z demo menu, project hot reload.
4. Call `Core::Update()`. It advances `Time`, then runs `Game::Update()`.
5. Stamp the log frame number. Start the ImGui frame.
6. Drain more commands: editor UI, feature debug, Frame Debugger, render graph control, asset import, project load.
7. `Renderer::BeginFrame()` clears last frame's queued draws.
8. `Core::BuildFrame()` records and submits the offscreen graph.
9. Fulfil a pending Game view capture.
10. Build all editor panels. Publish render graph metadata for `/render_graph` and `/list_textures`.
11. Check exit. Arm swapchain capture if requested.
12. `Core::Present()`, then floating ImGui windows, then window and texture captures.

Each bridge runs at most one command per frame. A command costs at least one frame of latency.

### Core::BuildFrame
1. Clear per-frame state: claimed entities, cull requests, cluster candidates, exclusion sets.
2. Fire armed faults. If the device is lost, stop. Rendering does not come back.
3. Pick the Primary (Game) and Secondary (Scene) targets. With no editor targets, `Core` makes its own.
4. Collect GPU skinning requests. Build the cluster candidate list.
5. Declare passes in provider order: `GpuSkinning`, `ClearViewTarget`, `PreOpaqueFeatures`, `RenderOpaque`, `OpaqueFeatures`, `RenderTransparent`, `PostOpaqueFeatures`, `PostTransparentFeatures`, `PluginRenderFeatures`.
6. Execute the graph, finalize view textures for sampling, submit, publish GPU timing.
7. An exception is logged once per distinct message. Present then clears the swapchain.

### Events
```text
SDL_Event > editor ProcessEvent (always first) > EventTranslator > gte::Event
         > if ImGui does not want it: InputState::Apply + Game::OnEvent
```
- `EventTranslator` is the only place that maps SDL events to `gte::Event`.
- It drops events from other windows (ImGui floating windows) and unknown mouse buttons.
- `Quit` is never filtered. `WindowResized` resizes the renderer and the Game view.

### Threads
| Thread | Runs | Rule |
|---|---|---|
| Main | `EditorHost::Run()`, `Core`, ECS, GPU, ImGui | Owns everything mutable. |
| HTTP pool | Route handlers in `src/Network/` | Hand work to the main thread through a bridge. Wait with a timeout. |
| Job workers | Pure batch work (math, skinning, cluster bounds) | No `Registry` writes, no GPU. See [Job system](../conventions/job-system.md). |

## Key files
| Path | Role |
|---|---|
| `src/main.cpp` | Entry point, flags |
| `src/Editor/EditorHost.cpp` | Host, frame loop, command draining |
| `src/Core/Core.cpp` | `Update`, `BuildFrame`, `Present`, pipeline providers |
| `src/Core/Time.*`, `EngineContext.h` | Frame clock |
| `src/Game/Game.cpp` | Simulation root, entity spawn and edit |
| `src/Application/` | Bridges, `EventTranslator`, command dispatch |
| `src/Window/`, `src/Input/`, `src/Event/` | Window, input state, event types |
| `src/ECS/`, `src/Scene/` | World data, scene file |
| `src/Renderer/` | The only Vulkan code |
| `src/Features/` | Built-in render features |
| `src/Network/` | HTTP server and routes |
| `src/Jobs/`, `src/Profiling/` | Worker pool, profiler |

## Rules
1. `gte_core` never includes SDL or ImGui. `Window`, `EventTranslator`, `SdlMemoryTracker` and the editor live in `gte_editor`.
2. `Core` talks to the host only through `IEditorLayer*` (nullable), `IHostServices` and capability interfaces.
3. Only `RenderSystem`, `MeshInstantiationSystem` and `AnimationSystem` know both ECS and `Renderer`. See [ECS](ecs.md).
4. Per-frame state is cleared at the start of `BuildFrame()`, so an early exit leaves nothing stale.
5. Network handlers never touch engine state. They submit a request to a bridge and wait. See [HTTP server](http-server.md).
6. Vulkan stays inside `src/Renderer/`. See [Vulkan boundary](../../AGENTS.md#vulkan-boundary-strict).

## Gotchas
- `Game::OnEvent` and the `InputState` argument of `Game::Update` are empty today. Nothing reads game input yet.
- `Core` ignores `IHostServices` for now.
- Hot reload blocks the main thread on purpose. The window freezes until it ends.
- `Core::Update` skips `Game::Update` when `InputFrame::inputState` is null. The host always sets it.
- After device loss, `Core` logs one line and stops rendering. Restart the editor.
- Pause and step are not exposed over HTTP. See [Time and playback pause](../conventions/time-and-playback-pause.md).

## Related
- [ECS](ecs.md) · [Math](math.md) · [Rendering](rendering.md) · [HTTP server](http-server.md)
- [Time and playback pause](../conventions/time-and-playback-pause.md) · [Job system](../conventions/job-system.md)
- [Project assembly](../guides/project-assembly.md) · [Editor](editor.md)
