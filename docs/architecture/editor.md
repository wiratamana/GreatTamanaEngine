# Editor
> How the editor is built: host, panels, bridges and the rules for adding UI. For anyone who changes `src/Editor/`.
> Verified against code: 2026-10-31

## TL;DR
- There is one executable: `GreatTamanaEditor.exe`. `EditorHost` owns the main loop.
- `gte_core` has no ImGui and no SDL. Only `src/Editor/` includes ImGui. `Core` sees the editor through `IEditorLayer`.
- Panels are free functions or small classes. They share one plain struct, `EditorContext`.
- The HTTP server never touches editor state. It hands work to the main thread through a bridge.
- Pure logic lives in `*Data.cpp` files without ImGui. Those files have unit tests in `tests/Editor/`.

## Diagram
```text
main.cpp
  └─ EditorHost                     composition root, owns Run()
       ├─ SdlContext -> Window -> Renderer
       ├─ Core (gte_core)           engine, no ImGui
       ├─ ImGuiEditorLayer          implements IEditorLayer, draws all panels
       ├─ Bridges                   one pending request each
       ├─ Capabilities              ISceneIO, ILogQuery, IHotReloadDebug,
       │                            IProjectLifecycle, IAssetScaffolding
       └─ NetworkServer             127.0.0.1:8080, started at the end of the constructor

HTTP thread:  request -> bridge.SubmitAndWait() -> waits -------+
Main thread:  Run() peeks the bridge, runs it, fulfils it ------+
```

`NullEditorLayer` is a no-op `IEditorLayer`. Hosts without an editor, such as the CI probes, use it. `Game` never knows the editor exists.

## How it works
Frame order in `EditorHost::Run()`:
1. `FrameProfiler::BeginFrame()`.
2. Poll SDL events. The editor sees each raw event first. Then `EventTranslator` makes an `Event`.
3. If the editor does not want the mouse or keyboard, the event goes to `InputState` and `Game::OnEvent`.
4. Run at most one engine command from HTTP.
5. Run a pending hot reload. It blocks the main thread until it is done.
6. `Core::Update()`. It honors Pause and Step.
7. `NewFrame()`. Then run at most one command each: UI, feature debug, frame debugger, render graph control, asset import, project load.
8. `Core::BuildFrame()`. Then host-level captures of the Game view.
9. `BuildUI()`, then `Core::Present()`, then window and texture captures, then `FrameProfiler::EndFrame()`.

`Quit` and `WindowResized` are not mouse or keyboard events. The capture check never blocks them.

### `BuildUI()` order
1. Dockspace and menu bar, then the New/Open Project and Create Asset windows.
2. View descriptors, then Hierarchy, Inspector, Scene, Game.
3. Memory, Profiler, Render Graph, Render Features, Jobs, Log, Frame Debugger.
4. Project, Bone Viewer, Cluster Viewer.
5. Every registered extra panel.

## Panels
| Window | Default place | Purpose |
|---|---|---|
| Hierarchy | Left | Entity tree. Drag to reparent or reorder. Right-click to create objects |
| Inspector | Right | Edit components. Asset metadata, previews, cluster tools, "Open Bone Viewer" |
| Scene | Center tab | Own render target, orbit camera, transform gizmo, camera frustum overlay |
| Game | Center tab | Shows the Game view. A hidden tab is not rendered, unless the Frame Debugger is enabled |
| Memory | Bottom | SDL and ImGui CPU bytes, tracked GPU resources, VMA heap budgets |
| Profiler | Bottom | Plain summary, CPU/GPU frame graph (tick a row to plot it), table with calls, total, self and % per name, Capture and Pause |
| Jobs | Bottom | Worker timeline. CPU/GPU skinning toggle |
| Log | Bottom | Filterable, colored log |
| Project | Bottom | File browser for `<exe dir>/Project/`. Drag-in import. Right-click Create |
| Render Graph | Floating, open | Pass tree, canvas, table, GPU timeline, pass toggles, DOT export |
| Render Features | Floating, closed | Feature priority and enable |
| Frame Debugger | Floating, closed | One frozen Game-view frame. See [Frame debugger](../debugging/frame-debugger.md) |
| Bone Viewer, Cluster Viewer | Floating | Skeleton and mesh-cluster debug views |
| Create New Project, Open Project, Create New ... | Floating windows | Project workflow. See [Project assembly](../guides/project-assembly.md) |
| Atmosphere, Culling, Gtao, SceneLighting, Shadow, Ssgi, ToneMapping, VolumetricLight | Bottom tabs | One settings panel per built-in render feature. `Culling` is `GpuDrivenScene` |

- The Project panel and Bone Viewer exist only when CMake option `GTE_ENABLE_PROJECT_PANEL` is `ON` (default).
- The Project panel scans its folder only while visible, on a job-system worker (`ProjectScanRunner`), and keeps no `AssetDatabase`.
- The Memory panel reads data that `MemoryPanelData` shapes. The Profiler panel reads [Profiling](../debugging/profiling.md) data.
- The Profiler panel is split in three layers. Plain text and descriptions: `src/Application/ProfilerScopeCatalog`. Pure view models without ImGui: `ProfilerTableModel` and `ProfilerGraphModel` in `src/Editor`. Drawing: `ProfilerPanel`, `ProfilerGraphWidget` and `ProfilerTableWidget` in `src/Editor/Panels`. Tree rows are placed with `SetCursorPosX`, never `ImGui::Indent(0)` (zero means the default indent).
- A toolbar under the menu has Pause, Resume and Step.

### Menus
| Menu | Items |
|---|---|
| File | Save Scene (Ctrl+S), Open Scene (Ctrl+O), Open Sponza Scene, Open Hi-Z Demo Scene, Close Hi-Z Demo, Exit |
| Window | Frame Debugger, Render Graph, Render Features |
| Project | New Project..., Open Project..., Compile `<Name>` |
| Hierarchy right-click | Create 3D Object, Create Directional Light, Create Instance Grid |

### Default layout
- Hierarchy 20% left, Inspector 28% right, Scene and Game tabs in the center, a bottom strip of 25%.
- The layout is built once. It is built again only when the dockspace is missing or a registered panel was never docked.
- After that, `imgui.ini` and the user decide.

### Controls and drag-drop
| Where | Input | Action |
|---|---|---|
| Scene | Middle mouse drag | Pan |
| Scene | Mouse wheel | Dolly |
| Scene | Right mouse drag | Look |
| Scene | Left click on a camera frustum | Select that camera entity. Meshes cannot be picked by click |
| Project panel | Drag a file out | Payload `GTE_PROJECT_ASSET_PATH` (absolute path) |
| Hierarchy, Scene | Drop a `*.gta` mesh | Spawn it. On an entity row it becomes a child |
| Hierarchy | Drag a row | Payload `GTE_HIERARCHY_ENTITY`. Middle of a row = reparent, top or bottom edge = reorder |
| Project panel | Drop a file from Explorer | Import it into the folder. Textures and meshes become `.gta`. Name clashes get `name (1)` |

## Key files
| File (`src/Editor/...`) | Role |
|---|---|
| `EditorHost.cpp` | Main loop, bridge draining, captures, startup |
| `ImGuiEditorLayer.cpp` | ImGui context, two view textures, `BuildUI()` |
| `EditorContext.h` | Shared plain state for all panels |
| `Selection.h` | The only gate for selection changes |
| `DockLayout.cpp` | Menu bar, dockspace, default layout |
| `Panels/*Panel.cpp` | One file per panel |
| `*Data.cpp`, `Panels/HierarchyRows.*`, `EditorCamera.*` | Pure logic, no ImGui |
| `src/Features/<Name>/Editor/` | Settings panel of one render feature (not in `src/Editor/`) |
| `src/Core/EditorCapabilities.h` | Interfaces that `Core` and HTTP use to reach editor features |
| `src/Core/EditorPanelRegistry.h` | Names of all panels, built-in and extra |

## Rules
1. Only `src/Editor/` includes ImGui. `Core` talks to the editor through `IEditorLayer` only.
2. Panels are `BuildXPanel(ctx, ...)` functions, or small classes when they keep state (`ProfilerPanel`, `RenderGraphPanel`, `FrameDebuggerPanel`). There is no `IEditorPanel` base class.
3. `EditorContext` is plain data. Add a field only when two places need it.
4. Change selection only through `Selection`. A panel never writes the selection fields.
5. A panel with its own render target writes `ImGui::Begin()`'s return value to a visibility flag. `ImGuiEditorLayer` returns `nullptr` from `PrimaryViewTarget()` or `SecondaryViewTarget()` for a hidden view, so that view is not rendered. The exception is the Game view while the Frame Debugger is enabled and open.
6. Keep logic that needs no ImGui in a separate file. Then it can be unit tested.
7. Never write editor or ECS state from the network thread. Use a bridge.
8. Use `ScopedUniqueId` for ImGui IDs in loops. See [ImGui ID uniqueness](../conventions/imgui-id-uniqueness.md).
9. Use the engine's handles for GPU objects. Only the ImGui backend setup in `ImGuiEditorLayer.cpp` may touch Vulkan.
10. A scene opener builds the new scene first. It deletes the old roots only on success.

## Extra panels
- A panel is an `IEditorPanelModule_v1`: `GetPanelName()` and `BuildPanel(ctx)`.
- Register it with `EditorPanelRegistry::Instance().RegisterPluginPanel(name, module)`.
- Built-in features do it with `GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Name", &Factory)` in `Features/<Name>/Editor/`.
- Project assemblies do it from their `_Editor` DLL. See [Project assembly](../guides/project-assembly.md).
- `ImGuiEditorLayer` calls `ImGui::Begin(name)` for each one. The panel then draws its content.
- All extra panels are docked into the bottom strip.

## Gotchas
- `WantsCaptureMouse` and `WantsCaptureKeyboard` stop clicks in ImGui from reaching gameplay. They come from `ImGuiIO`.
- `/get_window?title=...` captures floating (undocked) windows only. For docked panels use `/get_swapchain`.
- The Frame Debugger pauses the game when you enable it. Disabling it resumes, unless you had paused first.
- The Game view is `Primary` and the Scene view is `Secondary` in renderer code. The Render Graph window is open on start.
- The extra-panel interface says a panel draws only through `ctx`. Project panels call real `ImGui::*` and ignore `ctx`, because they share the exe's ImGui.
- Hot reload has no menu item. It is HTTP only.
- `Projects/GteE2E*` folders are test leftovers. They are ignored by git.

## Related
- [Architecture overview](overview.md) - layers and frame loop of the whole engine.
- [HTTP server](http-server.md) - threads, bridges, timeouts.
- [HTTP routes](../debugging/http-routes.md) - `/get_window`, `/list_tabs`, `/hierarchy_state`, `/inspector_state`.
- [Profiling](../debugging/profiling.md) - what the Profiler and Jobs panels show.
- [Frame debugger](../debugging/frame-debugger.md) - the Frame Debugger window.
- [Project assembly](../guides/project-assembly.md) - panels and passes from a game project.
- [Writing a render feature](../guides/writing-a-render-feature.md) - how built-in feature panels fit in.
- [ImGui ID uniqueness](../conventions/imgui-id-uniqueness.md) - the ID rule.
