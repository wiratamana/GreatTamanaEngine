# Editor
> How the editor is built: host, panels, bridges and the rules for adding UI. For anyone who changes `src/Editor/`.
> Verified against code: 2026-11-01 (after window context menu fix)

## TL;DR
- There is one executable: `GreatTamanaEditor.exe`. `EditorHost` owns the main loop.
- `gte_core` has no ImGui and no SDL. Only `src/Editor/` includes ImGui. `Core` sees the editor through `IEditorLayer`.
- Every window is an `EditorWindow`. Windows share one plain struct, `EditorContext`, through `EditorFrame`.
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
1. Dockspace and menu bar, then the New/Open Project and Create Asset dialogs.
2. View descriptors.
3. Every registered window, in registration order: Hierarchy, Inspector, Scene, Game, Memory, Profiler, Render Graph, Render Features, Jobs, Log, Frame Debugger, Project, Bone Viewer, Cluster Viewer, then project windows, then the 8 render feature windows.
4. Scene camera adapter.

## Panels
Every window below derives from `EditorWindow`. Open any of them from the Window menu. Closing a window keeps its place in `imgui.ini`.

| Window | Default place | Purpose |
|---|---|---|
| Hierarchy | Left | Entity tree. Drag to reparent or reorder. Right-click to create objects |
| Inspector | Right | Edit components. Asset metadata, previews, cluster tools, "Open Bone Viewer" |
| Scene | Center tab | Own render target, fly camera with Persp / Iso, view gizmo, transform gizmo, camera frustum overlay |
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
| Window | Every registered window, one flat list sorted by name. A click opens it, raises it and centers it if it floats |
| Project | New Project..., Open Project..., Compile `<Name>` |
| Hierarchy right-click | Create 3D Object, Create Directional Light, Create Instance Grid |

### Default layout
- Hierarchy 20% left, Inspector 28% right, Scene and Game tabs in the center, a bottom strip of 25%.
- The layout is built only on a first run (no dock space in `imgui.ini`). After that `imgui.ini` and the user decide.

### Controls and drag-drop
| Where | Input | Action |
|---|---|---|
| Scene | Middle mouse drag | Pan. The scene follows the cursor at any zoom |
| Scene | Mouse wheel | Zoom to the pivot. Fast scrolling accelerates |
| Scene | Right mouse drag | Look around (Perspective) or orbit the pivot (Isometric) |
| Scene | Click a gizmo head (top-right) | Look along that axis: Right, Left, Top, Bottom, Back, Front |
| Scene | Click `Persp` / `Iso` under the gizmo | Switch Perspective and Isometric |
| Scene | Left click on a camera frustum | Select that camera entity. Meshes cannot be picked by click |
| Project panel | Drag a file out | Payload `GTE_PROJECT_ASSET_PATH` (absolute path) |
| Hierarchy, Scene | Drop a `*.gta` mesh | Spawn it. On an entity row it becomes a child |
| Hierarchy | Drag a row | Payload `GTE_HIERARCHY_ENTITY`. Middle of a row = reparent, top or bottom edge = reorder |
| Project panel | Drop a file from Explorer | Import it into the folder. Textures and meshes become `.gta`. Name clashes get `name (1)` |

- The pivot is the point `distance` in front of the camera. Head clicks and Isometric orbit turn around it.
- Isometric is a plain orthographic projection. Features that need a point eye (Atmosphere, Gtao, Ssgi, VolumetricLight) are not valid there.
- The grid has no fixed fade distance. Each line level fades by its spacing in pixels.

## Windows
- Derive from `EditorWindow`, give a name, a default dock place and a default open state, write `OnGui()`. The base class draws the title bar and the close button.
- Hooks: `OnFrame()` every frame (even closed), `OnGui()` when the content is visible, `OnHidden()` when closed or the tab is not selected.
- Floating window: X in the title bar. Docked window: no X; right-click the tab, then "Close Tab". No window can collapse. Every window can dock.
- `Show()` opens, raises and (floating) centers. The Window menu, `GET /activate_tab` and `GET /panel_visible` use it.
- A window's own right-click menu opens only below the title bar, so the title bar always gives Close Window.
- Known limits: right-click on a background tab only selects it. A window registered later by hot reload floats until you dock it. A window that is closed and shown again returns to the same dock node but at the end of its tab strip.
- `TabItemFlagsOverrideSet` is an experimental ImGui field, so a new window can show an X on its tab for one frame.

## Key files
| File (`src/Editor/...`) | Role |
|---|---|
| `EditorHost.cpp` | Main loop, bridge draining, captures, startup |
| `ImGuiEditorLayer.cpp` | ImGui context, two view textures, `BuildUI()` |
| `EditorWindow.h/.cpp` | Base class of every window: open state, title bar, close, Window menu hooks |
| `EditorFrame.h` | Per-frame input handed to every window |
| `EditorContext.h` | Shared plain state for all panels |
| `Selection.h` | The only gate for selection changes |
| `DockLayout.cpp` | Menu bar, dockspace, default layout |
| `Panels/*Panel.cpp` | One file per panel |
| `*Data.cpp`, `Panels/HierarchyRows.*`, `EditorCamera.*` | Pure logic, no ImGui |
| `src/Features/<Name>/Editor/` | Settings panel of one render feature (not in `src/Editor/`) |
| `src/Core/EditorCapabilities.h` | Interfaces that `Core` and HTTP use to reach editor features |
| `src/Core/EditorPanelRegistry.h` | Name -> window pointer of every window |

## Rules
1. Only `src/Editor/` includes ImGui. `Core` talks to the editor through `IEditorLayer` only.
2. Every window derives from `EditorWindow` and draws in `OnGui()`. It never calls `ImGui::Begin` or `End` itself.
3. `EditorContext` is plain data. Add a field only when two places need it.
4. Change selection only through `Selection`. A panel never writes the selection fields.
5. A window with its own render target uses `IsVisible()` as its visibility flag. `ImGuiEditorLayer` returns `nullptr` from `PrimaryViewTarget()` or `SecondaryViewTarget()` for a hidden or closed view, so that view is not rendered. The exception is the Game view while the Frame Debugger is enabled and open.
6. Keep logic that needs no ImGui in a separate file. Then it can be unit tested.
7. Never write editor or ECS state from the network thread. Use a bridge.
8. Use `ScopedUniqueId` for ImGui IDs in loops. See [ImGui ID uniqueness](../conventions/imgui-id-uniqueness.md).
9. Use the engine's handles for GPU objects. Only the ImGui backend setup in `ImGuiEditorLayer.cpp` may touch Vulkan.
10. A scene opener builds the new scene first. It deletes the old roots only on success.

## Project and feature windows
- A project assembly derives from `EditorWindow` and calls `gte::RegisterEditorWindow(window)` in its entry point. See [Project assembly](../guides/project-assembly.md).
- A render feature derives from `EditorWindow` in `Features/<Name>/Editor/` and registers a factory with `GTE_REGISTER_BUILTIN_FEATURE_EDITOR_PANEL("Name", &Factory)`.
- Registration order is draw order and default tab order. All windows with `DefaultDock` Bottom tab into the bottom strip.

## Gotchas
- `WantsCaptureMouse` and `WantsCaptureKeyboard` stop clicks in ImGui from reaching gameplay. They come from `ImGuiIO`.
- `/get_window?title=...` captures floating (undocked) windows only. For docked panels use `/get_swapchain`.
- The Frame Debugger pauses the game when you enable it. Disabling it resumes, unless you had paused first.
- The Game view is `Primary` and the Scene view is `Secondary` in renderer code. The Render Graph window is open on start.
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
