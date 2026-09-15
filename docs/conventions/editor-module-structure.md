# Editor Module Structure

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

`src/Editor/` is the Editor/Debug UI seam described in [Coding Guidelines](../../AGENTS.md#coding-guidelines)
(Clean Architecture) - the same boundary role `EventTranslator` plays for
SDL in the Application layer, but for ImGui. The boundary is the **folder,
compiled only under `GTE_ENABLE_EDITOR`** - not a single file. Only
`EditorLayer.h` (the pure `IEditorLayer` interface) and
`NullEditorLayer.cpp` (the release-build no-op implementation) must stay
completely free of ImGui/SDL/Vulkan-beyond-forward-declares; every other
file under `src/Editor/` is compiled exclusively when `GTE_ENABLE_EDITOR` is
ON (see `CMakeLists.txt`'s `target_sources(gte_core PRIVATE ...)` inside
that `if()` block) and is just as free to include ImGui/SDL headers
directly as `ImGuiEditorLayer.cpp` itself:

- **`ImGuiEditorLayer.cpp`** is the Editor's composition root, not a
  monolith holding every panel: it owns the ImGui context, the SDL3/Vulkan
  backend lifecycle, TWO `RenderTexture`s (`m_gameView`/`m_sceneView` - one
  per panel, never shared), and the shared `EditorContext` (below) -
  `BuildUI()` just calls out, in a fixed, deliberate order, to
  `DockLayout.cpp` and each `Panels/*.cpp` builder.
- **`EditorContext.h`** is a small plain-data struct (no behavior of its
  own, same philosophy as ECS components - see
  [Entity-Component-System](ecs.md) below) holding everything that needs to be shared across panels/frames:
  the Game-view/Scene-view ImGui descriptors, each panel's own desired
  render-texture extent (`desiredExtent`/`desiredSceneExtent`) and visibility
  flag (`gameViewVisible`/`sceneViewVisible`), the current Hierarchy/
  Inspector selection (`EditorContext::selection`, see `Selection.h` below),
  the exit-requested flag, and the dock-layout-ensured latch. Passed by
  reference into every panel/dock-layout function.
- **`Selection.h/.cpp`** is the single gate-keeper for every Hierarchy-entity
  / Project-asset selection change - `HierarchyPanel`/`ProjectPanel` never
  assign `EditorContext::selection`'s fields directly; they only ever call
  `Selection::SelectEntity()`/`SelectAsset()`/`ClearAssetIfPath()`, and every
  reader (`InspectorPanel`, `ScenePanel`'s gizmo) goes through its
  `Kind()`/`SelectedEntity()`/`SelectedAssetAbsolutePath()`/etc. accessors
  rather than reading a raw field. Deliberately pure logic with zero ImGui/
  SDL/Vulkan dependency (Tier-1-testable - see `tests/Editor/
  SelectionTests.cpp` - and
  [Testability & Regression Safety](../../AGENTS.md#testability--regression-safety) below), and
  deliberately just a plain gate-keeper with no history/undo of its own -
  this is what gives a future Command-pattern implementation (undo-able
  selection changes, then edits in general) exactly one choke point to route
  through, instead of several panels each writing selection state directly
  (see `TODO.md`, "Editor / Debug UI"). Any future selectable "thing" (e.g. a
  multi-select set) should extend this same class rather than adding a new
  ad hoc field to `EditorContext` directly.
- **`gameViewVisible`/`sceneViewVisible` are written from `ImGui::Begin()`'s
  own return value** (`Panels/GamePanel.cpp`/`ScenePanel.cpp`) - `false`
  whenever that panel is an inactive/hidden dock tab (or collapsed), not
  just "exists somewhere" - and read by
  `ImGuiEditorLayer::GameViewTarget()`/`SceneViewTarget()` at the START of
  the NEXT frame to return `nullptr` outright for a currently-invisible
  panel, which is what makes `Application::Run()` skip that view's
  `Renderer::RenderOffscreen()` pass entirely (real GPU savings, not just a
  cosmetic skip) whenever "Scene"/"Game" are tabbed together and only one is
  actually on screen. A future panel with its own `RenderTexture` should
  follow this exact same pattern rather than always rendering unconditionally.
- **`DockLayout.h/.cpp`** builds the top menu bar + full-viewport DockSpace
  and the one-shot default Unity-style layout (Hierarchy left, Inspector
  right, Scene/Game tabbed center) - see its own comments for why rebuilding
  that layout must stay strictly one-shot, never re-checked every frame,
  or the user could never drag a panel loose from the default arrangement.
- **`Panels/HierarchyPanel.*`, `InspectorPanel.*`, `ScenePanel.*`,
  `GamePanel.*`, `MemoryPanel.*`** are each a single free function (`BuildXPanel(...)`)
  taking `EditorContext&` (plus `Registry&` where a panel needs the ECS
  world) - not classes, and NOT implementations of any common
  `IEditorPanel` interface. There is deliberately no polymorphic
  panel list/registry here: the dock layout above already addresses each
  panel by its literal, hardcoded name, so nothing ever needs to iterate
  over "the panels" generically - `ImGuiEditorLayer::BuildUI()` calls each
  one explicitly, by name, in a fixed order. Don't introduce an
  `IEditorPanel` abstraction preemptively; only reach for one if a genuine,
  stated requirement for runtime-registered/plugin panels shows up later.
- A **future panel that genuinely needs its own persistent state across
  frames** (e.g. a Console's scrollback buffer) may become a small class
  instead of a free function - it still gets called explicitly by name from
  `ImGuiEditorLayer::BuildUI()`, exactly like the stateless ones, with no
  interface needed for it either. Two real precedents already exist:
  `BoneViewerWindow` (`BoneViewerWindow.h` - a floating debug window with
  its own GPU buffers/camera/selection state) and `ProfilerPanel`
  (`Panels/ProfilerPanel.h` - Phase 7,
  `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md`, holding its Pause control's
  frozen snapshot plus reusable `ImGui::PlotLines()` scratch buffers). Both
  are still called explicitly by name (`m_boneViewer.Build(...)`/
  `m_profilerPanel.Build(...)`), never through a shared interface.
- **Vulkan types (e.g. `EditorContext::gameViewDescriptor`,
  `VkExtent2D`) are fine to use directly anywhere in this folder** - this is
  not an architectural leak. `Renderer`'s own public API
  (`Renderer::GetVulkanContextInfo()`, `RenderTexture::Extent()`/`View()`/
  `Sampler()`) already hands out plain Vulkan handles on purpose, precisely
  so "an external Vulkan-based rendering backend... owned by the Editor
  module" (see `Renderer.h`) - i.e. Dear ImGui's own Vulkan backend - can
  use them directly; there is exactly one rendering backend in this engine
  and no plan to swap it, so wrapping these handles in a fake neutral type
  would add indirection with no real decoupling benefit. The boundary that
  actually matters and must stay intact is that `Renderer`'s *internal*
  RAII wrapper types (`VulkanInstance`, `VulkanDevice`, `VulkanSwapchain`,
  `VulkanAllocator`, `FramePresenter`, `FrameRecorder`, `GpuResourceFactory`
  - everything under `Renderer/Vulkan/` plus Renderer's private
  collaborators) never leak outside `Renderer`, and that `Game`/ECS never
  see a Vulkan type in either direction (see the [ECS](ecs.md) section's own `Renderer`-must-never-depend-on-ECS rule).
- **`IEditorLayer::WantsCaptureMouse()`/`WantsCaptureKeyboard()` gate every
  translated mouse/keyboard `Event` before it ever reaches
  `InputState`/`Game::OnEvent()`.** `Application::Run()` checks these
  (backed by `ImGuiIO::WantCaptureMouse`/`WantCaptureKeyboard` in
  `ImGuiEditorLayer`, always `false` in `NullEditorLayer`) so
  clicking/dragging/typing into the Editor's own ImGui panels never ALSO
  registers as gameplay input underneath them - the classic
  ImGui-in-a-game-engine "click-through" problem. This is deliberately NOT
  solved with a separate Editor-side event broadcaster/receiver system:
  Dear ImGui already does all the hard part itself (per-frame, internal
  topmost-window-wins hit-testing/focus/modal-exclusivity across every one
  of its own panels, via the one `ImGuiContext` `ProcessEvent()`/
  `NewFrame()` already feed) - there is nothing left for engine code to
  arbitrate between ImGui's own panels. The only genuinely missing piece
  was ImGui-vs-gameplay leakage, and two `bool` query methods (mirroring
  the existing `WantsExit()` pattern) are enough to close that gap; don't
  reintroduce a broadcaster/registry to solve a problem ImGui already owns.
  `Quit`/`WindowResized` events bypass this check entirely (see
  `Application::Run()`) - they aren't gameplay input in this sense, and
  Renderer/the Editor's own resize handling must always see them regardless
  of what the Editor UI currently wants captured.
