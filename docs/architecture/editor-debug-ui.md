# Editor / Debug UI

_Part of [GreatTamanaEngine](../../README.md)'s architecture docs. See
[docs/README.md](../README.md) for the full documentation index._

An optional in-engine Editor module lives under `src/Editor/`, gated by the
`GTE_ENABLE_EDITOR` CMake option (`ON` by default). `Application` only ever
talks to the `IEditorLayer` interface (`src/Editor/EditorLayer.h`); exactly
one of two implementations gets compiled in, selected purely by which `.cpp`
CMake adds:

- **`ImGuiEditorLayer`** (real, `GTE_ENABLE_EDITOR=ON`) — owns the Dear ImGui
  context (fetched from ImGui's **docking** branch — see
  `cmake/FetchImGui.cmake` — with `ImGuiConfigFlags_DockingEnable` set) plus
  its SDL3 and Vulkan backends (routed through volk), and TWO
  `RenderTexture`s — one for "Game", one for "Scene" — that Game's camera
  renders into independently, each tracking its own panel's content-region
  size/aspect ratio (Unity's "Free Aspect" behavior). Lays out a Unity-style
  default arrangement the first time it runs (built once via the
  `DockBuilder` API, then left to the user/`imgui.ini` afterwards): a
  full-viewport `DockSpace` with a top menu bar (`File > Exit`, wired to
  `IEditorLayer::WantsExit()` so `Application::Run()` can end its main loop
  the same way closing the OS window does), **"Hierarchy"** docked left,
  **"Inspector"** docked right, and **"Scene"**/**"Game"** tabbed together in
  the remaining center — drag the "Scene" tab out to split it side-by-side
  with "Game" at any time, exactly like Unity. "Hierarchy" renders a real,
  indented parent/child TREE (via `ECS/TransformHierarchy.h`'s `GetChildren()`,
  walked recursively from the root entities down) of every entity that has a
  `Transform` (via `Game::GetRegistry()` — the Editor's only,
  read/write, view into Game's ECS world), tags one with "(Camera)" if it
  also has a `Camera` component, and lets you select one; "Inspector"
  shows/edits the selected entity's `Transform` (local position/rotation/
  scale, plus its current parent - if any - and a one-click "Unparent"
  button), `Camera` (active/field of view/near-far planes) if present, and
  displays its `MeshRenderer` handles read-only.
  **Hierarchy drag-and-drop attach/detach/reorder:** dragging one entity row
  onto another row's MIDDLE ~50% reparents it as that row's new last child
  (`ECS/TransformHierarchy.h`'s `SetParent()`, world position preserved so it
  never visually jumps in "Scene"/"Game" - Unity's own `SetParent()`
  default); dragging onto a row's TOP/BOTTOM ~25% instead REORDERS it as a
  sibling immediately before/after that row (`SetParent()` to the same
  parent, then `SetSiblingIndex()`); dragging onto empty panel space detaches
  it back to the scene root. `Transform`'s own `parent`/`siblingIndex` fields
  (see [Entity-Component-System](ecs.md)) are what a child's world transform is
  actually computed from - a parented mesh (or `Camera`) genuinely follows
  its parent's movement now, exactly like Unity.
  **Drag-and-drop instantiation:** a file dragged out of "Project" (see the
  Project panel below — `Panels/ProjectPanel.cpp`'s `BeginDragDropSource()`,
  payload = the file's absolute path,
  `EditorContext::kProjectAssetDragDropPayloadType`) can be dropped onto
  either "Hierarchy" (anywhere in the panel, or directly onto an entity row
  to spawn it as that entity's CHILD instead of at the scene root —
  `Panels/HierarchyPanel.cpp`) or directly onto the "Scene" viewport image
  (`Panels/ScenePanel.cpp`) to
  instantiate it, Unity's own "drag a model into the scene" convention — both
  drop targets just call `Game::CreateMeshEntityFromGtaFile()` and select the
  freshly spawned ROOT entity — for a multi-part model this selects the one
  entity that represents the whole thing, exactly like dropping onto an
  entity row spawns the whole model as that row's single CHILD (its own
  parts stay nested under their own model root, never spliced directly under
  the drop target as loose siblings). Dropping anything other than a valid
  `*.gta` `AssetType::Mesh` file is silently ignored (see
  [Asset Pipeline](asset-pipeline.md)).
  **Visibility-driven rendering:** `IEditorLayer::GameViewTarget()`/
  `SceneViewTarget()` each return `nullptr` (skipping that view's
  `Renderer::RenderOffscreen()` pass entirely) whenever `ImGui::Begin()`
  reported that panel wasn't actually visible last frame (an inactive dock
  tab hidden behind the other one) — while "Scene"/"Game" are tabbed
  together, only the active tab is ever rendered, at zero extra GPU cost for
  the hidden one; split them apart and both become visible/rendered
  simultaneously, each into its own `RenderTexture`.
  **Independent Scene camera:** "Game" still renders through whichever ECS
  entity currently has the active `Camera` component
  (`RenderSystem::ResolveActiveCameraViewProjection()`), but "Scene" now
  renders through its own independently-orbitable `EditorCamera`
  (`src/Editor/EditorCamera.h`) instead — Unity-style middle-mouse-drag pan
  (camera-local X/Y), mouse-wheel dolly (camera-local Z), and right-mouse-
  drag look (yaw around world up, pitch around camera-local right, clamped
  to ±89°), read from ImGui's mouse state in `Panels/ScenePanel.cpp` and fed
  into `EditorCamera::Update()` as plain values — `EditorCamera` itself has
  no ImGui/SDL/Vulkan dependency at all, so it is Tier-1-testable like the
  rest of the engine (see `tests/Editor/EditorCameraTests.cpp`).
  `Application::Run()` passes `IEditorLayer::SceneViewProjection()`'s result
  straight into `Game::Render()`'s `viewProjectionOverride` parameter for
  the Scene view specifically, bypassing ECS camera resolution for that
  view only — `Game` itself has no idea the Editor or `EditorCamera` exist
  either way.
  **Transform gizmo:** whichever entity is currently selected in "Hierarchy"
  gets a Unity-style translate/rotate/scale gizmo drawn directly over
  "Scene" (never "Game") via **ImGuizmo** (`third_party/imguizmo/`, fetched
  the same way as Dear ImGui itself — see `cmake/FetchImGuizmo.cmake`,
  wrapped by `src/Editor/TransformGizmo.h/.cpp`), plus a top-left
  Move/Rotate/Scale switcher overlay (`EditorContext::gizmoOperation`,
  `DrawGizmoOperationSwitcher()`). Always manipulates in ImGuizmo's `LOCAL`
  space; now that `Transform` carries a real parent/child relationship (see
  [Entity-Component-System](ecs.md)), `ManipulateTransformGizmo()` manipulates
  in WORLD space (`parentWorld * transform's own local matrix`, `parentWorld`
  resolved by the caller via `TransformHierarchy.h`'s `ComputeWorldMatrix()` —
  `Mat4::Identity()` for a root/unparented entity) and converts the result
  back into the selected entity's own LOCAL fields afterwards
  (`parentWorld`'s inverse times the manipulated world matrix) — for a root
  entity this is exactly the original "manipulate the local matrix directly"
  behavior, unchanged; for a parented entity, the gizmo now stays correctly
  aligned with wherever it's actually drawn instead of silently fighting the
  parent transform. Uses the Scene view's own `EditorCamera`
  (never the gameplay `Camera` entity's view/projection — see above).
  `ManipulateTransformGizmo()` decomposes the manipulated 4x4 matrix back
  into position/rotation/scale by hand rather than via
  `ImGuizmo::DecomposeMatrixToComponents()` — translation/scale are read
  straight off the matrix's own columns, and rotation goes through
  `Quat::FromMat4()` on the (unscaled) rotation columns, sidestepping any
  Euler-angle-order mismatch between this engine's own convention
  (`Quat::FromEulerDegrees()`) and ImGuizmo's, which would otherwise fight
  the mouse mid-drag. Left-click-to-select an entity by ray-casting into
  the Scene view (with a highlighted outline around the picked mesh) is a
  deliberately deferred follow-up — see `TODO.md` ("Editor / Debug UI");
  for now, selection is manual, via "Hierarchy" only.
  **Memory panel:** a Unity-Memory-Profiler-style **"Memory"** panel
  (`src/Editor/Panels/MemoryPanel.cpp`, docked full-width along the bottom —
  see `DockLayout.cpp`) shows exactly what's contributing to memory usage
  right now, across three sections: **"CPU (Engine Dependencies)"** — exact
  live byte/allocation totals for SDL and Dear ImGui specifically
  (`SdlMemoryTracker`/`ImGuiMemoryTracker`, below — each installs a
  byte-counting wrapper around that library's own allocator, so these are
  measured, not estimated); **"GPU (Tracked by Engine)"** — a header of
  aggregate totals (`Renderer::GetMemoryTotals()` — total bytes, buffer vs.
  texture bytes/count, device-local vs. host-visible vs. shared bytes)
  followed by a sortable table of every currently-live GPU resource
  (`Renderer::GetMemoryResources()`), biggest first, each row showing its
  debug name (if any — `Renderer::GetMemoryDebugName()`, Editor-only, empty/
  "(unnamed)" otherwise), type (Buffer/Texture), memory location, and size;
  and **"GPU Heap Budgets (Driver-Reported)"** — the REAL, driver-reported
  usage/budget for every Vulkan memory heap (`Renderer::GetVmaHeapBudgets()`,
  via `vmaGetHeapBudgets()`), fetched straight from VMA rather than tallied
  by this engine, letting you directly compare "what GpuMemoryTracker thinks
  is live" against "what the driver/Task Manager actually reports" for the
  same heap. Each heap row's "VMA Allocated" column shows not just a byte
  count but the full `VmaStatistics` story behind it — e.g. "64.00 MB across
  1 block (3 sub-allocations)" — since a `GpuMemoryTracker` total that looks
  much smaller than VMA's own block size isn't a tracking gap: VMA reserves
  whole `VkDeviceMemory` blocks up front (avoiding a slow, per-resource
  `vkAllocateMemory` call, and staying under `maxMemoryAllocationCount`) and
  sub-allocates individual resources out of them, so a block is often mostly
  unused headroom, not "missing" memory. The row-shaping logic itself
  (sorting, name resolution, human-readable byte formatting, heap-budget
  reshaping) lives in
  `src/Editor/MemoryPanelData.h/.cpp` as plain, ImGui-free functions
  (`BuildMemoryRows()`/`BuildHeapBudgetRows()`/`FormatBytes()`/`ToString()`),
  Tier-1-tested exactly like `EditorCamera` despite living under
  `src/Editor/` (see `tests/Editor/MemoryPanelDataTests.cpp`) — the panel
  itself (`Panels/MemoryPanel.cpp`) is a thin ImGui-table wrapper around
  them. The GPU section is the primitive the underlying `GpuMemoryTracker`
  (`src/Renderer/Memory/GpuMemoryTracker.h`) was already carrying every
  frame for every `Buffer`/`RenderTexture` this engine creates — no new
  bookkeeping was needed there, only a UI to surface it.
  **`SdlMemoryTracker`** (`src/Memory/SdlMemoryTracker.h`, class always
  compiled — SDL is used regardless of `GTE_ENABLE_EDITOR` — so it stays
  available/testable in every build config) and **`ImGuiMemoryTracker`**
  (`src/Editor/ImGuiMemoryTracker.h`, Editor-only) each install a
  byte-counting wrapper around their respective library's own allocator
  (`SDL_SetMemoryFunctions()`/`ImGui::SetAllocatorFunctions()`) — installed
  before that library's very first call
  (`Application::SdlContext`'s constructor, before `SDL_Init()`;
  `ImGuiEditorLayer`'s constructor, before `ImGui::CreateContext()`) since
  both APIs document that swapping allocators later risks a free() using a
  different allocator than whatever alloc() originally served that pointer.
  **Neither is actually installed/active in a release build**
  (`-DGTE_ENABLE_EDITOR=OFF`): `ImGuiEditorLayer` itself never compiles into
  that build (`NullEditorLayer` replaces it), and `Application::SdlContext`'s
  call to `SdlMemoryTracker::Install()` is explicitly wrapped in
  `#if GTE_ENABLE_EDITOR` for the same reason — a release build has no
  "Memory" panel to show these numbers and must not pay their real
  per-allocation tracking cost for nothing. Both are static/process-global
  (SDL's and ImGui's allocator callbacks carry no `this`-sized userdata to do
  otherwise) and Tier-1-tested despite touching a third-party library's own
  allocator directly — see `tests/Memory/SdlMemoryTrackerTests.cpp`/
  `tests/Editor/ImGuiMemoryTrackerTests.cpp` and AGENTS.md ("CPU Dependency
  Memory Tracking") for the full rationale, including why calling
  `SDL_malloc()`/`ImGui::MemAlloc()` directly in a test needs neither
  `SDL_Init()` nor a live `ImGuiContext`.
- **Profiler panel:** a Unity-Profiler-window-style **"Profiler"** panel
  (`src/Editor/Panels/ProfilerPanel.h/.cpp`, docked alongside "Memory" along
  the bottom - see `DockLayout.cpp`), reading exclusively from
  `Profiling::FrameProfiler`'s already-collected data (Phases 0-5/7 - see
  `PROFILER_STRATEGY_v2.md`/`PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md`),
  no new engine-level tracking added. Shows, live: a scrolling CPU
  frame-time graph over the last ~240 frames (current ms + FPS, visible-
  range min/max, via `Profiling::FrameGraphData.h`'s `BuildFrameGraphPoints()`/
  `ComputeCpuMillisecondsRange()`); a CPU-scope breakdown table, sorted
  biggest-total-first (`src/Editor/ProfilerPanelData.h`'s
  `BuildSortedCpuScopeRows()`), whose empty state honestly distinguishes
  "no scopes recorded yet this frame" from "CPU scope instrumentation is
  compiled out entirely" (`-DGTE_ENABLE_PROFILER=OFF` - see AGENTS.md,
  "Profiling") rather than looking identically blank either way; draw-call/
  triangle counts for all three named `GpuPass`es (Game View primary, Scene
  View/Present as de-emphasized context), reading "N/A" rather than a
  misleading "0" for any pass that didn't run this frame; current GPU
  memory totals plus a sparkline over the same window (`FrameGraphPoint`
  gained a `memory` field and `Profiling::FrameGraphData.h` gained
  `ComputeMemoryBytesRange()` for exactly this, in the Phase 7 session);
  and, as of Phase 4 (`PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md`,
  sub-phases 4A-4D - see `PHASE4A_COMPLETION_REPORT.md` through
  `PHASE4D_COMPLETION_REPORT.md`), a real **GPU Timing** line for all three
  named passes, showing genuine driver-measured milliseconds via real
  Vulkan timestamp queries (`src/Renderer/GpuTiming.h`/`GpuTimingService.h`,
  `Vulkan/VulkanQueryPool.h`) whenever that pass ran this frame, an honest
  "N/A" for a hidden/not-yet-warmed-up pass, and a permanent "Unsupported"
  on a device/build that can never produce this measurement - never a
  fabricated `0.00 ms` either way. Two independent controls:
  **Capture** (`Profiling::FrameProfiler::SetCaptureEnabled()` - the real
  on/off switch for data collection itself, which ALSO now genuinely gates
  the Phase 4 GPU timestamp query work, not just this panel's own display -
  see AGENTS.md, "Profiling") and **Pause** (a
  `ProfilerPanel`-local snapshot freeze that only affects what this ONE
  panel currently displays, leaving `FrameProfiler` collecting normally
  underneath - `ProfilerPanel` is a small stateful class, the second
  real-world instance of that pre-approved exception alongside
  `BoneViewerWindow` - see AGENTS.md, "Editor Module Structure"). A
  disabled, tooltipped "Export CSV" button is a deliberate stub pointing at
  Phase 6 (benchmark mode), which will own the real, shared CSV exporter -
  see `TODO.md`.
- **Render Graph panel:** a Unity-Profiler-window-style **"Render Graph"**
  panel (`src/Editor/Panels/RenderGraphPanel.h/.cpp`, docked alongside
  "Memory"/"Profiler" along the bottom — see `DockLayout.cpp`), added as
  Phase 8 of the Render Graph campaign (see [Rendering](rendering.md) and
  `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`) once the engine's real
  Game/Scene/Present passes were fully migrated onto
  `gte::rg::RenderGraph` (Phase 7). Shows, for each of the engine's two real
  per-frame `RenderGraph::Execute()` regimes ("Offscreen Regime (Game View +
  Scene View)" and "Pipelined Regime (Present)") independently: an ordered
  table of every SURVIVING pass (name, draw-call/triangle counts, GPU time -
  "N/A" today, since real GPU timestamp wiring for the graph is still a
  follow-up - and its declared reads/writes as resource-name chips), a
  de-emphasized CULLED-passes section (still visible, never hidden, with a
  hover tooltip explaining why a pass had no path to that call's final
  output), and a RESOURCES table showing every declared texture/buffer's
  imported-vs-transient kind and its computed lifetime as a
  "`<first-use pass>` -> `<last-use pass>`" string. Backed by a new pure,
  Tier-1-tested reshape,
  `src/Renderer/RenderGraph/RenderGraphSnapshot.h/.cpp`'s
  `BuildRenderGraphSnapshot()` (deliberately living alongside the rest of
  the Render Graph campaign under `src/Renderer/RenderGraph/`, not under
  `src/Editor/`, since it operates purely on `RenderGraph`'s own compiled-
  graph data) - the panel itself
  (`Panels/RenderGraphPanel.cpp`) is a thin ImGui-table wrapper around it,
  with only a **Pause** control (no "Capture" - building a snapshot costs
  nothing beyond copying already-computed small strings/vectors once per
  `Execute()` call, so there's no meaningful capture toggle to offer) that
  freezes only this ONE panel's own display, the same
  `ProfilerPanel`/`BoneViewerWindow`-style small-stateful-class exception
  (see AGENTS.md, "Editor Module Structure"). A disabled, tooltipped
  "Export DOT" button is a deliberate stub pointing at Phase 9, mirroring
  "Profiler"'s own "Export CSV" stub.
- **Project panel:** a Unity/Windows-Explorer-style **two-pane "Project"**
  panel (`src/Editor/Panels/ProjectPanel.h/.cpp`, docked alongside "Memory"
  along the bottom — see `DockLayout.cpp`), gated by its own
  `GTE_ENABLE_PROJECT_PANEL` switch (separate from `GTE_ENABLE_EDITOR` — see
  `BUILDING.md`), rooted at a real **"Project" folder created automatically
  next to the built `.exe`** (`SDL_GetBasePath()` + `"Project"`) if it
  doesn't already exist. **Left pane** — a folders-only tree of the whole
  Project (like Explorer's own left tree); clicking a folder both selects it
  and makes it the "open" folder. **Right pane** — the immediate files AND
  subfolders of whichever folder is open, behind a clickable breadcrumb;
  single-click selects an entry, double-clicking a subfolder navigates into
  it (same as Explorer). A draggable splitter sits between them. The tree is
  rebuilt from disk on a throttle (twice a second, or immediately after any
  operation below) rather than caching filesystem handles/pointers across
  frames, so anything deleted *externally* (Explorer, git, another process)
  while the Editor is running is simply gone from the next scan — never a
  dangling reference the Editor could crash on; if the currently *open*
  folder itself vanishes this way, it's walked back up to its nearest
  still-existing ancestor automatically. Right-click either pane for
  **Refresh**/**New Folder**/**Delete Selected**, or **drag a file (or
  folder) in from Windows Explorer**: dropping it directly onto a specific
  folder row (in EITHER pane) puts it inside that folder; dropping it
  anywhere else in the right pane puts it in the currently open folder
  (auto-renaming — `"name (1).ext"`, `"name (2).ext"`, ... — rather than
  clobbering an existing same-named item). The OS-level drop itself
  (`SDL_EVENT_DROP_FILE`, entirely separate from ImGui's own widget-to-widget
  drag-and-drop) is caught in `ImGuiEditorLayer::ProcessEvent()` and handed
  to `ProjectPanel::HandleExternalFileDrop()` with the drop's absolute
  desktop coordinates, resolved to an actual target folder by
  `ProjectPanelData::ResolveDropTarget()` against every folder row's own
  on-screen hit-box recorded while rendering the last visible frame. Every
  filesystem/geometry operation (`ScanProjectDirectory()`/
  `EnsureProjectRootExists()`/`ResolveDropTargetDirectory()`/
  `MakeUniqueDestinationPath()`/`FindEntryByRelativePath()`/
  `ParentRelativePath()`/`ResolveDropTarget()`, plus the
  `PathToUtf8()`/`Utf8ToPath()` UTF-8-safe path helpers so non-ASCII
  filenames display and round-trip correctly) lives in pure, ImGui-free
  `src/Editor/ProjectPanelData.h/.cpp`, Tier-1-tested (see
  `tests/Editor/ProjectPanelDataTests.cpp`) exactly like `MemoryPanelData`
  above — `Panels/ProjectPanel.cpp` itself (the one place holding
  cross-frame state: the cached tree, which folder is open vs. selected,
  both panes' rects/splitter position, a transient status message) is a
  thin class wrapper around them, never unit-tested directly, same division
  of labor as the "Memory" panel.
- **Inspector asset preview (texture + 3D mesh):** selecting a `*.gta` asset
  in "Project" makes "Inspector" show that file's real GTA-format metadata
  (GUID/`AssetType`/flags/payload size) in a scrollable region on top, a
  draggable splitter, then a Unity-style live preview pinned to the BOTTOM
  (`EditorContext::inspectorPreviewHeight`, shared by both preview kinds —
  see `Panels/InspectorPanel.cpp`'s `BuildAssetInspector()`). A
  `AssetType::Texture` asset (or any plain, not-yet-imported image file)
  gets `AssetPreviewTexture`'s contain-fit static image, decoded/uploaded
  once and cached until the selected path or its last-write-time changes
  (`src/Editor/AssetPreviewTexture.h/.cpp`). A `AssetType::Mesh` asset (the
  result of importing a `.pmx` — see [Asset Pipeline](asset-pipeline.md)) instead gets
  `AssetPreviewMesh`'s LIVE, auto-rotating 3D view
  (`src/Editor/AssetPreviewMesh.h/.cpp`) — re-rendered every call (the spin
  is driven directly off `ImGui::GetTime()`, no per-frame state to track),
  auto-framed to the mesh's own bounding sphere, lit with a small,
  self-contained shader pair (`Shaders/MeshPreview.vert/.frag` — a
  position+normal vertex layout and fixed-direction lambert shading,
  deliberately separate from the engine's shared position+color `Vertex`/
  `Pipeline`/`Renderer::CreateMesh()`/`Submit()`, which have no normal
  attribute or index-buffer support at all). `AssetPreviewMesh` builds its
  own `VkPipeline`/`VkPipelineLayout` directly and records its own indexed
  draw call via a `Renderer::RenderOffscreen()` `recordExtra` callback — the
  same "an external Vulkan-based rendering backend owned by the Editor
  module" pattern Dear ImGui's own backend already uses (see AGENTS.md,
  "Editor Module Structure") — rather than extending the shared pipeline.
  Only the uploaded GPU vertex/index buffers and bounding sphere are
  cached per selected asset; the `VkPipeline` itself is built once and
  reused across every mesh asset selected afterwards. Neither preview kind
  is treated as "should have worked but failed" for a `*.gta` wrapping
  something else (a future `Scene`/`Material`/... asset) — it just falls
  through to plain file metadata, exactly like a non-image/non-mesh
  extension always has.
- **Inspector animation metadata (no live preview):** selecting a
  `AssetType::Animation` asset (the result of importing a `.vmd` — see
  [Asset Pipeline](asset-pipeline.md)) shows a decoded metadata summary instead of a
  live viewer — a flat keyframe list has nothing to rasterize, so there is
  no bottom-pinned preview pane/splitter for this asset type at all, unlike
  the texture/mesh cases above. `BuildGtaAnimationMetadata()`
  (`Panels/InspectorPanel.cpp`) decodes the `*.gta`'s payload directly via
  `MotionFile.h`'s `DecodeMotionDataFromBytes()` (a plain CPU-side binary
  decode, no GPU involved) and shows: the VMD's own target model name (when
  set), the combined frame range across every populated track, per-track
  keyframe counts (bone/morph/camera/light/shadow/IK), the number of
  distinct bones/morphs actually driven, and a collapsible ("TreeNode",
  collapsed by default so a several-hundred-bone motion doesn't dominate the
  panel) scrollable list of every distinct bone/morph name — handy for
  eyeballing which rig a motion expects without leaving the Inspector. Falls
  back to header-only fields + a "Failed to decode motion data" notice if
  the payload is corrupt/truncated despite a valid `*.gta` header, same
  degrade-gracefully convention as the texture/mesh cases.
- **Bone Viewer (debug tool for imported MMD skeletons):** the Inspector's
  "Model" section — shown for any entity carrying a `MeshAssetSource`
  component, i.e. the ROOT of a model spawned via
  `Game::CreateMeshEntityFromGtaFile()` (see [Asset Pipeline](asset-pipeline.md)) — has an
  "Open Bone Viewer" button that opens a Unity-"Avatar configuration"-style
  floating debug window (`src/Editor/BoneViewerWindow.h/.cpp`), independent of
  the main docked layout (it can be dragged clean outside the main OS window
  too, like any other panel, since `ImGuiConfigFlags_ViewportsEnable` is
  already on). It reads the model's source `*.gta` straight off disk (the
  same `GtaFile.h`/`MeshFile.h`/`RigFile.h` path `AssetPreviewMesh` already
  uses — no dependency on `Game`'s own animation-runtime caches) and shows,
  side by side: a LEFT-hand bone hierarchy TREE, walked "starting from root"
  down through every bone's children (mirroring "Hierarchy"'s own
  `GetChildren()`-based entity tree), and a RIGHT-hand live 3D viewport
  rendering the model's original BIND POSE (its own small Vulkan pipeline,
  reusing `AssetPreviewMesh`'s `MeshPreview.vert/.frag` shader pair) through a
  user-orbitable camera (left-drag rotate, wheel dolly, middle-drag pan,
  "Reset View" auto-frames to the mesh's bounding sphere). Every skeleton
  bone is drawn as a small gizmo dot plus a line to its parent, projected
  through the exact view/projection matrix used to render the mesh so it
  always lines up pixel-perfectly; a bone's name is shown on hover, for any
  bone matching a live search filter (which also prunes the tree to just the
  matching branches, Unity-Hierarchy-search-style), or permanently via a
  "Show All Names" toggle. Selection is shared between the tree and the
  viewport — clicking a tree row (or a bone's dot directly in the 3D view)
  highlights it in both, and double-clicking a tree row re-centers the
  camera on that bone — exactly the tool needed to eyeball whether an
  imported model's bone hierarchy/naming actually looks right, and to figure
  out why an imported model + animation pairing doesn't line up (e.g. a
  renamed/missing bone `Animation/MotionSampler.h`'s
  `ResolveBoneTracksToSkeleton()` never finds a match for). Always shows the
  BIND pose, not whatever a live `SkeletalAnimator` might currently be
  posing the same entity into — a live posed-skeleton overlay is a natural,
  separate follow-up (see `TODO.md`). Gated behind `GTE_ENABLE_PROJECT_PANEL`
  like `AssetPreviewMesh` itself, purely because it reuses that same shader
  pair (only staged under that switch) — nothing about the feature itself is
  Project-panel-specific.
- **`NullEditorLayer`** (`GTE_ENABLE_EDITOR=OFF`) — every method is a no-op;
  `GameViewTarget()`/`SceneViewTarget()` always return `nullptr`, meaning
  "render straight to the swapchain, fullscreen". This is what makes
  `-DGTE_ENABLE_EDITOR=OFF` a genuine release/final-game build: no ImGui
  fetch, no ImGui sources compiled, no ImGui symbols linked at all — not
  just a runtime flag.

`Game` never depends on the Editor at all, in either direction — that's what
keeps turning the Editor off a zero-touch operation for gameplay code; the
Editor only ever *observes*/edits Game's ECS world through
`Game::GetRegistry()`, a public accessor Game exposes without knowing or
caring who calls it.
