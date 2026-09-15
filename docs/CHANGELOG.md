# Changelog

Reverse-chronological history of every feature campaign landed in this
engine. For the short, current-state summary, see the main
[README.md](../README.md)'s own "Status" section.

Early foundation stage, but past the basic-scaffolding phase for several
pieces:

- Window/Renderer/Game scaffolding is in place, and event handling flows
  through `EventTranslator`/`InputState` as described above instead of raw
  SDL events reaching `Game` directly.
- `Renderer` owns a real Vulkan pipeline (instance/device/swapchain/command
  buffers, using dynamic rendering) instead of SDL's `SDL_Renderer`, including
  off-screen rendering into a `RenderTexture` for Editor panels.
- The Editor module is wired up end-to-end: Dear ImGui (docking branch)'s
  SDL3 + Vulkan backends are integrated behind `IEditorLayer`, with a full
  Unity-style docked layout — top menu bar (`File > Exit`), "Hierarchy"
  (left), "Inspector" (right), and "Scene"/"Game" tabbed in the center, all
  freely rearrangeable/splittable via ImGui docking. "Game" and "Scene" each
  display Game's camera output via their OWN `RenderTexture` now (each
  tracking its own panel's size/aspect ratio independently), and each is
  only actually rendered into when its own panel is visible — tabbed
  together, only the active one costs any GPU time; split apart, both do;
  "Hierarchy"/"Inspector" list and edit entities/components straight from
  Game's ECS world via `Game::GetRegistry()`. Toggling `GTE_ENABLE_EDITOR`
  fully includes/excludes the whole module, down to CMake never fetching or
  compiling ImGui at all when it's off.
- "Scene" now has its own independently-orbitable Editor-only camera
  (`EditorCamera`, `src/Editor/EditorCamera.h`) with Unity-style
  middle-drag pan / wheel dolly / right-drag look controls, wired through
  a new `IEditorLayer::SceneViewProjection()` and
  `Game::Render()`'s `viewProjectionOverride` parameter — "Game" is
  unaffected and still renders through the ECS's own active `Camera`
  component. Fully unit-tested (pan/dolly/rotate math, pitch clamping,
  `ViewProjection()`) despite living under `src/Editor/`, since it has no
  ImGui/SDL/Vulkan dependency at all — see `tests/Editor/EditorCameraTests.cpp`.
- "Scene" also now has a Unity-style translate/rotate/scale **transform
  gizmo** via **ImGuizmo** (`src/Editor/TransformGizmo.h/.cpp`,
  `third_party/imguizmo/` — fetched the same way as Dear ImGui itself, see
  `cmake/FetchImGuizmo.cmake`) for whichever entity is currently selected in
  "Hierarchy", plus a top-left Move/Rotate/Scale switcher overlay
  (`EditorContext::gizmoOperation`). `ManipulateTransformGizmo()` writes the
  dragged result straight back into that entity's `Transform`, decomposed by
  hand rather than via `ImGuizmo::DecomposeMatrixToComponents()` — the
  manipulated matrix's translation/scale are read straight off its own
  columns, and rotation goes through `Quat::FromMat4()` on the (unscaled)
  rotation columns, sidestepping any Euler-angle-order mismatch between this
  engine's own convention and ImGuizmo's that would otherwise visibly fight
  the mouse mid-drag. Click-to-select via ray casting + a Scene-view outline
  highlight for the picked entity is a deliberately deferred follow-up — see
  `TODO.md` ("Editor / Debug UI"); selection today is manual, via
  "Hierarchy" only.
- **Transform now supports a real parent/child hierarchy**, Unity's own
  `Transform.parent`/`GetSiblingIndex()` shape (`ECS/Components/Transform.h`'s
  `parent`/`siblingIndex` fields, `ECS/TransformHierarchy.h/.cpp`'s
  `ComputeWorldMatrix()`/`SetParent()`/`GetChildren()`/`SetSiblingIndex()` -
  fully Tier-1-tested, see `tests/ECS/TransformHierarchyTests.cpp`). A
  parented entity's `RenderSystem`-resolved world transform - and a parented
  `Camera`'s view matrix - now genuinely follow their parent, closing the
  "Transform is flat today" gap `TODO.md` used to call out. "Hierarchy" lists
  every entity as a real indented tree instead of flat, with Unity-style
  drag-and-drop: drop onto another row's middle band to attach as its child
  (world position preserved), onto its top/bottom band to reorder as a
  sibling, or onto empty panel space to detach back to the scene root - see
  [Editor / Debug UI](architecture/editor-debug-ui.md). The Scene-view transform gizmo (below) and
  Inspector's "Parent"/"Unparent" control were both updated to match.
- The Scene-view transform gizmo now manipulates in WORLD space and converts
  back to the selected entity's own LOCAL `Transform` fields via its
  parent's resolved world matrix (`ComputeWorldMatrix()` above), so it stays
  correctly aligned for a parented entity - previously ImGuizmo's `LOCAL`
  space was always identical to `WORLD`, since no hierarchy existed yet.
- The Editor now has a Unity-Memory-Profiler-style **"Memory"** panel
  (`src/Editor/Panels/MemoryPanel.cpp`, docked full-width along the bottom),
  now covering CPU AND GPU memory across three sections: **"CPU (Engine
  Dependencies)"** — exact, measured (not estimated) live byte/allocation
  totals for SDL and Dear ImGui specifically, via `SdlMemoryTracker`
  (`src/Memory/SdlMemoryTracker.h`, class always compiled for testability)
  and `ImGuiMemoryTracker` (`src/Editor/ImGuiMemoryTracker.h`, Editor-only),
  each installing a byte-counting wrapper around that library's own
  allocator (`SDL_SetMemoryFunctions()`/`ImGui::SetAllocatorFunctions()`)
  before its very first call — but neither actually installed/active in a
  release build (`-DGTE_ENABLE_EDITOR=OFF`): the install call site is
  explicitly `#if GTE_ENABLE_EDITOR`-gated (`SdlMemoryTracker`) or simply
  never compiled at all (`ImGuiMemoryTracker`, via `NullEditorLayer`
  replacing `ImGuiEditorLayer` entirely), so a shipped game pays zero
  per-allocation tracking overhead for a panel it doesn't have; **"GPU
  (Tracked by Engine)"** — aggregate totals
  (`Renderer::GetMemoryTotals()`) plus a sortable, biggest-first table of
  every currently-live GPU resource (`Renderer::GetMemoryResources()`) with
  its debug name/type/memory location/size (needed zero new bookkeeping —
  `GpuMemoryTracker`, see below, already carried all of this data every
  frame; only the debug-name forwarding, `Renderer::GetMemoryDebugName()`,
  was new here); and **"GPU Heap Budgets (Driver-Reported)"** — the REAL,
  driver-reported usage/budget for every Vulkan memory heap
  (`Renderer::GetVmaHeapBudgets()`, via VMA's `vmaGetHeapBudgets()`), the
  cross-check for whether the "Tracked by Engine" section plausibly accounts
  for everything a real GPU tool/Task Manager would report - each heap row
  also shows the `VmaStatistics` story behind its "VMA Allocated" bytes
  (`FormatBlockSummary()` - e.g. "64.00 MB across 1 block (3
  sub-allocations)"), since VMA reserves whole `VkDeviceMemory` blocks up
  front and sub-allocates resources out of them, so a much-smaller
  `GpuMemoryTracker` total is expected block-reservation headroom, not a
  tracking gap. All of the
  row-shaping logic (`BuildMemoryRows()`/`BuildHeapBudgetRows()`/
  `FormatBytes()`/`ToString()`, `src/Editor/MemoryPanelData.h/.cpp`) plus
  both CPU trackers are Tier-1-tested despite living under `src/Editor/`
  (`SdlMemoryTracker` lives outside it, in `src/Memory/`, and is tested the
  same way) - same as `EditorCamera` - see
  `tests/Editor/MemoryPanelDataTests.cpp`,
  `tests/Memory/SdlMemoryTrackerTests.cpp`, and
  `tests/Editor/ImGuiMemoryTrackerTests.cpp`.
- The Editor's **"Project"** panel is now a Unity/Windows-Explorer-style
  **two-pane** browser (`src/Editor/Panels/ProjectPanel.h/.cpp`, docked
  alongside "Memory" along the bottom), gated by its own
  `GTE_ENABLE_PROJECT_PANEL` switch (a build can disable just this panel
  independently of the rest of the Editor - see `BUILDING.md`): a
  folders-only tree on the left (click a folder to open it) and that
  folder's own files/subfolders on the right, behind a clickable breadcrumb
  (single-click selects, double-click a subfolder navigates into it), split
  by a draggable splitter. Rooted at a real **"Project" folder auto-created
  next to the built `.exe`**, rebuilt from disk on a throttle rather than
  caching filesystem handles across frames (so anything deleted
  *externally* while the Editor is running just quietly disappears from the
  next scan, and an open folder that vanishes is walked back up to its
  nearest still-existing ancestor - never a dangling reference to crash
  on), plus **drag-and-drop import**: drop a file/folder from Windows
  Explorer directly onto a specific folder row (in EITHER pane) to land it
  in that folder, or anywhere else in the right pane to land it in the
  currently open folder (auto-renaming to avoid clobbering an existing
  item) - caught via the raw `SDL_EVENT_DROP_FILE` OS event
  (`ImGuiEditorLayer::ProcessEvent()`), entirely separate from ImGui's own
  widget drag-and-drop, and resolved to a specific folder via
  `ProjectPanelData::ResolveDropTarget()` against every folder row's
  recorded on-screen hit-box. Right-click either pane for Refresh/New
  Folder/Delete Selected. All the actual filesystem/geometry logic
  (`ScanProjectDirectory()`/`EnsureProjectRootExists()`/
  `ResolveDropTargetDirectory()`/`MakeUniqueDestinationPath()`/
  `FindEntryByRelativePath()`/`ParentRelativePath()`/`ResolveDropTarget()`/
  `PathToUtf8()`/`Utf8ToPath()`) lives in pure, ImGui-free
  `src/Editor/ProjectPanelData.h/.cpp`, Tier-1-tested against a real temp
  directory - see `tests/Editor/ProjectPanelDataTests.cpp`.
- GPU memory allocation goes through **VMA** (Vulkan Memory Allocator) via
  the `VulkanAllocator` RAII wrapper (`src/Renderer/Vulkan/`) — `Renderer`
  owns a single `VmaAllocator`. `RenderTexture` creates its `VkImage` through
  `vmaCreateImage`/`vmaDestroyImage`, and `Buffer`
  (`src/Renderer/Buffer.h/.cpp`) creates `VkBuffer`s through
  `vmaCreateBuffer`/`vmaDestroyBuffer` — both replacing what used to be a
  manual `FindMemoryType()` + `vkAllocateMemory`/`vkBindMemory`/`vkFreeMemory`
  dance. `Renderer::CreateBuffer()`/`CreateDeviceLocalBuffer()` cover
  host-mapped (uniform/staging) and device-local-via-staging-upload
  (vertex/index) buffers respectively; `Renderer::ImmediateSubmit()` is the
  reusable one-shot command buffer helper behind the latter. Verified with a
  runtime smoke test (mapped-buffer round-trip + a full staging-buffer ->
  device-local-buffer copy) actually executing against a live Vulkan device,
  and building cleanly with both `GTE_ENABLE_EDITOR` `ON` and `OFF`.
- A from-scratch **Math library** (`src/Math/`: `Vec2`/`Vec3`/`Vec4`/`Mat4`/
  `Quat`) backs everything above and below — no GLM dependency. Fully
  unit-tested (multiply/transpose/inverse/`LookAtLH`/`PerspectiveFovLH_ZO`,
  `Quat` slerp/nlerp/axis-angle/Euler round-trips) against hand-verified
  exact values.
- A hand-rolled **Entity-Component-System** (`src/ECS/`: `Entity`/
  `EntityManager`/`ComponentStorage<T>`/`Registry`) is the engine's Scene/
  World data model — no third-party ECS library (EnTT), same "own the core
  data model" choice as Math. `Transform`, `MeshRenderer`, `Camera`, and
  `Name` (a single optional display-label string, `src/ECS/Components/Name.h`)
  are the four components that exist today. `Transform` now carries a real
  parent/child hierarchy (`parent`/`siblingIndex` fields, plus
  `ECS/TransformHierarchy.h/.cpp`'s `ComputeWorldMatrix()`/`SetParent()`/
  `GetChildren()`/`SetSiblingIndex()`) - a parented entity's world transform
  is composed all the way up its parent chain, cycle-safely, and "Hierarchy"
  exposes it as a real drag-and-drop tree (see [Editor / Debug UI](architecture/editor-debug-ui.md)).
  Fully unit-tested, including
  generation-guarded stale-handle safety.
- The ECS is wired all the way into actual rendering, not just present as
  inert data: `RenderSystem` (`src/Game/RenderSystem.h/.cpp`) is the one
  class allowed to depend on both the ECS world and `Renderer` — `Renderer`
  itself gained zero ECS awareness in the process. A generic
  `ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`) mints
  generational `MeshHandle`/`PipelineHandle` values a `MeshRenderer`
  component can safely hold instead of ever embedding a live GPU resource.
  `Pipeline` carries a push-constant `mat4 model` immediately followed by a
  `mat4 viewProj`, threaded through `Renderer::Submit()`/`FrameRecorder`
  down to `vkCmdPushConstants`, so each entity's `Transform` genuinely
  drives where it's drawn AND a real `Camera` entity genuinely drives how
  the whole scene is viewed (rather than vertices sitting directly in clip
  space). `Game` builds a small demo scene (three entities sharing one
  mesh/pipeline, positioned via `Transform` alone, plus one `Camera` entity
  sitting back along -Z looking at them) proving the whole ECS ->
  `RenderSystem` -> `Renderer` pipeline end to end — verified both by the
  test suite (`RenderSystem::CollectRenderables()`'s pure ECS ->
  draw-command logic, `RenderSystem::ResolveActiveCameraViewProjection()`'s
  pure ECS -> camera logic, and `Camera`'s own `ProjectionMatrix()`/
  `ViewMatrix()` math) and visually (three independently-positioned
  triangles on screen, seen through a real perspective camera, in both the
  "Game" and "Scene" panels' own separate `RenderTexture`s).
- The engine can now create real, non-flat 3D geometry instead of only the
  one hardcoded 2D-on-the-XY-plane triangle: `Vertex::position` grew from a
  `vec2` to a `vec3` (`src/Renderer/Vertex.h`, `Shaders/Triangle.vert`
  updated to match), and a new `PrimitiveMeshGenerator`
  (`src/Renderer/Primitives/PrimitiveMeshGenerator.h/.cpp`) generates
  Unity-equivalent built-in primitive shapes — Cube, Sphere, Capsule, Cone,
  Plane — as plain CPU-side vertex data, entirely independent of any GPU
  device/Renderer/ECS (Tier-1-tested against hand-derived geometric
  invariants — bounding box, distance from center/core segment — see
  `tests/Renderer/PrimitiveMeshGeneratorTests.cpp`). Each vertex's color
  bakes a simple fixed-direction "faux-lit" shade (flat per-face for
  Cube/Cone/Plane, smooth per-vertex for Sphere/Capsule) rather than a flat
  placeholder gray, so a freshly spawned shape actually reads as 3D despite
  the engine's one unlit vertex-color shader. `Game::CreatePrimitiveEntity()`
  (a RUNTIME API, not Editor-only — this engine's equivalent of Unity's
  `GameObject.CreatePrimitive()`) spawns a `Transform` + `MeshRenderer`
  entity from one of these shapes, reusing one shared `Pipeline` and one
  shared `Mesh` per shape across every instance, exactly like the existing
  demo triangles share theirs. The Editor's "Hierarchy" panel exposes this
  via a Unity-style right-click **"Create 3D Object"** menu that spawns and
  immediately selects the new entity — the first concrete way to build up a
  non-hardcoded scene in the Editor, and the planned way to exercise scene
  serialization (see `TODO.md`) once that lands.
- A unified binary asset container format, `*.gta` ("Great Tamana Asset" -
  see [Asset Pipeline](architecture/asset-pipeline.md)), plus an `AssetDatabase`
  (`src/Assets/AssetDatabase.h/.cpp`) tracking every one found under a
  directory tree by its embedded `Guid`. The Editor's "Project" panel
  drag-and-drop import now GATES on file type: dropping a PNG/JPEG/etc.
  decodes it and re-encodes it as an uncompressed KTX2 container (via the
  statically-linked KTX-Software library), wraps it as a `*.gta`
  (`AssetType::Texture`), and registers it immediately - every other file
  extension still imports as a plain, unmodified copy. The Editor's
  "Inspector" panel shows a live texture preview for a selected `*.gta`
  asset the same way it already does for a plain, not-yet-imported
  PNG/JPEG (`Assets/Ktx2Decoder.h/.cpp`'s `DecodeKtx2ToRgba8()`, the
  pixel-exact inverse of the encode step, feeds the exact same
  `Renderer::CreateTexture2D()` upload path `AssetPreviewTexture` already
  used) - a `*.gta` wrapping anything other than a texture just falls back
  to plain file metadata, with no spurious error message. Fully unit-tested
  (`*.gta` header/round-trip I/O, `AssetDatabase`'s scan/import/lookup
  behavior, and the PNG/JPG <-> KTX2 encode/decode steps themselves, all
  genuinely Tier 1 - no GPU device/ImGui/SDL involved) and verified
  building/passing its full test suite with `GTE_ENABLE_EDITOR` both `ON`
  and `OFF`.
- **MikuMikuDance (`.pmx`) model import**, the same "gate on file type"
  pipeline extended to a second asset kind: dropping a `.pmx` file now
  parses it via a curated, from-scratch-fetched subset of
  [benikabocha/saba](https://github.com/benikabocha/saba) (`PmxLoader.h/.cpp`,
  `cmake/FetchSaba.cmake` — no Bullet/skinning-runtime/viewer vendored, and
  its own spdlog dependency patched out), extracts per-vertex positions/
  normals/UVs plus triangle indices into a plain `MeshData`
  (`src/Assets/MeshData.h`), and wraps it as a `*.gta` (`AssetType::Mesh`)
  via a new `MeshFile.h/.cpp` binary format — the mesh equivalent of
  `Ktx2Encoder`. The Editor's "Inspector" panel shows a LIVE, auto-rotating
  3D preview for a selected Mesh asset (`AssetPreviewMesh.h/.cpp`, its own
  small position+normal Vulkan pipeline built directly in the Editor layer —
  `Shaders/MeshPreview.vert/.frag`), pinned to the bottom exactly like the
  existing texture viewer, above a metadata panel showing the mesh's real
  vertex/triangle counts. Verified end-to-end against a real, large MMD
  model (~30k vertices/~37k triangles) in addition to hand-built binary
  fixtures. Vertex-geometry import only for now — no bones/morphs/
  materials/textures/rigid bodies, no real skinning or VMD motion playback,
  and no GAMEPLAY consumption path yet (only the Editor's own Inspector
  preview renders it; nothing yet spawns a `MeshRenderer` entity from an
  imported Mesh asset) - see `TODO.md`.
- **PMX bone weights/skinning, bones, morphs, and rigid-body/joint physics
  import** — closes the "no bones/morphs/materials/rigid bodies" gap the
  previous entry called out. `PmxLoader::LoadPmxModel()` now also extracts:
  per-vertex skin weights covering all of BDEF1/BDEF2/BDEF4/SDEF/QDEF
  (bundled straight into `MeshData::skinWeights` — see `Assets/MeshData.h`),
  the full bone hierarchy including IK chains/limits and append/fixed-axis/
  local-axis bones (`Assets/SkeletonData.h`), all seven PMX morph kinds —
  Position/UV/Bone/Material/Group/Flip/Impulse (`Assets/MorphData.h`), and
  rigid bodies + joints (`Assets/PhysicsData.h`, DATA only — no Bullet or
  equivalent simulation backend is vendored). A new sibling binary format,
  `Assets/RigFile.h`'s `EncodeRigDataToBytes()`/`DecodeRigDataFromBytes()`,
  serializes all of that into the `*.gta`'s previously-always-empty
  METADATA section (`AssetImporter.cpp`), alongside the unchanged
  `MeshFile.h` geometry payload — so a boneless/riggless `.pmx` still
  imports exactly as before, and a rigged one now carries its full rig data
  along for free. Verified against the same real ~30k-vertex MMD model as
  the previous entry, which turned out to carry 387 bones, 63 morphs, 267
  rigid bodies, and 368 joints — all now correctly parsed end-to-end (see
  `tests/Assets/PmxLoaderTests.cpp`'s `PmxLoaderRealModelSmokeTest`), plus
  hand-built binary fixtures exercising every weight type/bone flag/morph
  kind/physics shape individually (`tests/Assets/RigFileTests.cpp` for the
  new binary format's own round-trip). Still import/data-extraction only —
  no GPU skinning, IK solving, morph blending, or physics simulation
  happens anywhere in this engine yet; see `TODO.md`.
- **MikuMikuDance (`.vmd`) motion import** — the model importer's companion:
  a dropped `.vmd` motion file now goes through the exact same "gate on file
  type, parse into an engine-native struct, wrap as `*.gta`" pipeline as
  `.pmx`, via a new `Assets/VmdLoader.h/.cpp` (wrapping
  [benikabocha/saba](https://github.com/benikabocha/saba)'s
  `Model/MMD/VMDFile.{h,cpp}` — newly added to the already-vendored curated
  saba subset, see `cmake/FetchSaba.cmake`) and a new `Assets/MotionData.h`/
  `Assets/MotionFile.h/.cpp`. Extracts every VMD track: bone keyframes
  (translation/rotation offset + raw bezier interpolation bytes, addressed
  by bone NAME rather than a model-specific index, matching how a `.vmd` is
  actually authored/reused across different models), morph keyframes, and
  the camera/light/shadow/IK-enable tracks a camera-work `.vmd` carries
  instead — wrapped as a new `AssetType::Animation` `*.gta`. Verified against
  the real motion file this integration was tested with (a 690-bone-keyframe
  character motion, `ChatanyaraKuushanku_bassui260717a.vmd`) via a machine-
  gated smoke test (`tests/Assets/VmdLoaderTests.cpp`'s
  `VmdLoaderRealMotionSmokeTest`), plus hand-built binary fixtures exercising
  every track (`BuildRichVmd()`) and `Assets/MotionFile.h`'s own encode/
  decode round-trip (`tests/Assets/MotionFileTests.cpp`). Import/data-
  extraction only, same as the model importer — no interpolation evaluation,
  keyframe playback, or wiring onto a model's own `SkeletonData`/`MorphData`
  by name happens anywhere in this engine yet; see `TODO.md`.
- **A dropped Mesh `*.gta` can now be instantiated AND actually rendered** —
  closes the "no gameplay consumption path" gap the PMX-import entry above
  used to call out. `Mesh` (`src/Renderer/Mesh.h`) gained a real, optional
  index buffer (a second, indexed constructor; the original non-indexed one
  is unchanged), `Pipeline` (`src/Renderer/Pipeline.h/.cpp`) gained a
  `VertexLayout` selector (`PositionColor` — the original `Vertex.h` — vs.
  `PositionNormal` — a new `MeshVertex.h` carrying a real per-vertex normal
  instead of a color), and `FrameRecorder` now issues `vkCmdDrawIndexed`
  whenever the submitted `Mesh` has one. `Game::CreateMeshEntityFromGtaFile()`
  (mirroring `CreatePrimitiveEntity()`) decodes a Mesh `*.gta`'s payload,
  uploads it once (cached per absolute path), and spawns a
  `Transform`+`MeshRenderer` entity for it, drawn through a shared,
  always-compiled "grey clay" pipeline (`Shaders/Mesh.vert/.frag` —
  fixed-direction lambert + ambient; no textures, since a Mesh asset carries
  no material data yet). The Editor wires this up as real drag-and-drop:
  dragging a file out of "Project" (`Panels/ProjectPanel.cpp`'s
  `BeginDragDropSource()`) onto either "Hierarchy" or directly onto the
  "Scene" viewport image (`Panels/HierarchyPanel.cpp`/`ScenePanel.cpp`'s
  `BeginDragDropTarget()`) instantiates and selects it, Unity's own "drag a
  model into the scene" convention. The spawned entity always renders in its
  ORIGINAL BIND POSE — no skinning/morph/IK evaluation runs yet (that
  remains explicitly deferred, see `TODO.md`). Verified against the real
  ~31k-vertex/~39k-triangle MMD model already used elsewhere in this
  session's testing, and the full test suite (342 tests) still passes.
- **A dropped Mesh `*.gta` can now render its ORIGINAL PMX MATERIALS/
  TEXTURES, not just flat "grey clay"** — closes the "no material/texture
  import" gap the two entries above used to call out. `PmxLoader.h`/
  `RigFile.h` now extract a `.pmx`'s material list + texture references into
  a new `MaterialData` (`src/Assets/MaterialData.h`), resolving every
  texture reference to an absolute file path (relative to the source
  `.pmx`'s own directory) once, at import time. `Game::EnsureMeshAsset()`
  (`src/Game/Game.cpp`) now splits a Mesh asset's index buffer into one
  submesh PER MATERIAL: a material with no resolvable diffuse texture is
  still merged into the single untextured "grey clay" submesh exactly as
  before, while a material that DOES have one gets its own submesh, decoded
  straight off disk (`Assets/ImageFileDecoder.h` — no `*.gta`-texture-asset
  wrapping needed for this) and uploaded as a `MaterialTexture`
  (`Renderer/MaterialTexture.h`) — a `Texture2D` bundled with a
  ready-to-bind `VkDescriptorSet`, built against the one shared
  `GpuResourceFactory::MaterialDescriptorSetLayout()` every textured
  `Pipeline` (`VertexLayout::PositionNormalUv`, `Shaders/
  TexturedMesh.vert/.frag`) is also built against, so the two are always
  binding-compatible. `MeshRenderer` gained an optional `TextureHandle`;
  `RenderSystem`/`Renderer::Submit()`/`FrameRecorder` thread the resolved
  `VkDescriptorSet` through to a `vkCmdBindDescriptorSets` call right before
  each textured draw. Verified against the same real ~31k-vertex Furina
  model (32 materials, 10 distinct textures resolved) via a headless
  `--reimport <source> <dest.gta>` CLI mode added to `main.cpp` for
  regenerating an existing `*.gta` without driving the Editor UI, and the
  full test suite (342 tests) still passes. Still NOT done: sphere-map/toon
  shading (parsed into `Material` but never sampled) — see `TODO.md`.
  **UPDATE (this session): a material's texture is no longer decoded
  straight off the ORIGINAL `.pmx` source folder — it's imported as its own
  `*.gta` `AssetType::Texture` asset, referenced purely by `Guid`.** This
  closed a real "referencing an asset from outside the Project" problem:
  `MaterialTextureRef::sourcePath` above used to be read directly by
  `Game::EnsureMaterialTexture()` at spawn time, meaning a Mesh `*.gta`
  would silently fail to render its textures the moment it was loaded on
  any machine other than the one it was imported on (or once the original
  `.pmx`'s own folder moved/was deleted). `AssetImporter.cpp`'s new
  `ImportPmxMaterialTextures()` now decodes/re-encodes every material
  texture as its own KTX2-wrapped `*.gta` (into a
  `"<meshFileStem>_Textures"` folder next to the Mesh `*.gta` itself) and
  fills in `MaterialTextureRef::guid`; `Game::EnsureMaterialTexture()`
  (`src/Game/Game.cpp`) now takes a `Guid` and resolves it through a fresh
  `AssetDatabase` scan of the mesh's own directory, decoding the resolved
  Texture `*.gta`'s KTX2 payload via `Ktx2Decoder.h` — the exact same
  Guid-based "asset manager resolves an id to wherever it actually lives"
  convention this engine's own `AssetDatabase` already established for
  every other `*.gta` asset, applied consistently here too. Verified
  end-to-end against the same real Furina model via the `--reimport` CLI
  (all 10 of its textures import as their own `*.gta` assets and resolve
  correctly by `Guid`), and the full test suite (363 tests) still passes.
- **A multi-part model now instantiates as a real parent/child hierarchy,
  not flat independent siblings, and both the model and its parts get real
  display names.** `Game::CreateMeshEntityFromGtaFile()` (`src/Game/Game.cpp`)
  now creates one plain, empty ROOT entity first (a `Transform` only — no
  `MeshRenderer`, so it never renders anything by itself), named after the
  source `*.gta` FILE itself (its own filename minus extension — e.g.
  "Furina.gta" spawns a root named "Furina"), then attaches every submesh
  "part" entity under it via `ECS/TransformHierarchy.h`'s `SetParent()` —
  moving/rotating/scaling the root now moves the whole model together,
  Unity's own "a multi-material import gets one root GameObject with a
  child per submesh" convention. A new plain-data `Name` component
  (`src/ECS/Components/Name.h`) is what carries a display name: each
  TEXTURED part is named after its own originating PMX `Material::name`
  (`MeshAssetPart::name`, threaded through from `RigFileData::materials`)
  whenever that material actually has a non-empty name, while the combined
  untextured "grey clay" part (which can merge more than one material) is
  left without a `Name` at all. "Hierarchy" (`Panels/HierarchyPanel.cpp`)
  and "Inspector" (`Panels/InspectorPanel.cpp`, which now also has an
  editable "Name" text field for ANY selected entity) both show a `Name`
  component's value in place of the usual synthesized "Entity %u" label
  whenever one is present, falling back to that same synthesized label
  otherwise. `CreateMeshEntityFromGtaFile()` now returns the ROOT entity
  (previously the first part) — the Hierarchy/Scene drag-and-drop targets
  (see [Editor / Debug UI](architecture/editor-debug-ui.md)) select exactly the one entity that
  represents the whole freshly instantiated model. Verified against the
  same real Furina model (32 materials) already used elsewhere in this
  README, and the full test suite (363 tests) still passes.
- **A spawned MMD model can now actually be ANIMATED from an imported `.vmd`
  motion — the first real animation runtime, not just import.** A new,
  always-compiled `src/Animation/` module provides the pure math this needs
  with zero ECS/GPU/Renderer dependency (fully Tier-1-tested — see
  `tests/Animation/`): `SkeletonPose.h`'s `ComputeSkinningMatrices()` (
  forward-kinematics-only bone-pose evaluation — walks each bone's parent
  chain, cycle-safely, producing a model-space skinning matrix per bone with
  the inverse bind pose already folded in), `MotionSampler.h` (linear/slerp
  keyframe sampling, plus the bone-NAME resolution described below), and
  `VertexSkinning.h`'s `SkinVertices()` (CPU-side per-vertex blending of up
  to 4 bone influences, treating SDEF/QDEF exactly like BDEF2/BDEF4 per
  those weight types' own documented equivalence). **The bone/weight
  mismatch problem** — a `.vmd` motion is authored independently of any one
  model's own bone numbering, so the same motion is routinely replayed
  against a model whose skeleton doesn't exactly match the one it was
  authored against — is handled by `MotionSampler.h`'s
  `ResolveBoneTracksToSkeleton()`, which matches every motion bone track
  against the target model's own `SkeletonData::bones` purely by NAME and
  tolerates a mismatch in EITHER direction: an unmatched skeleton bone
  simply stays at its authored bind pose for the whole clip, and an
  unmatched motion track is simply never applied to anything — never a
  failure, never a fuzzy-match guess. `Game::PlayAnimationOnEntity()`
  (`src/Game/Game.h/.cpp`) wires this onto a model spawned by
  `CreateMeshEntityFromGtaFile()` (now also tagged with a new
  `MeshAssetSource` component recording which `*.gta` it came from) via a
  new `SkeletalAnimator` component (`src/ECS/Components/SkeletalAnimator.h`)
  — `Game::UpdateSkeletalAnimators()` runs once per frame, advancing every
  live animator, resolving its pose, CPU-skinning its model's cached bind-
  pose vertex data, and re-uploading the result into that model's mesh
  parts' GPU vertex buffers via a new `Mesh::UpdateVertexData()`. This is
  possible because a RIGGED mesh's `Mesh` is now built via a new
  `Renderer::CreateSkinnedMesh()` (a host-visible, persistently-mapped
  vertex buffer, re-writable every frame — the index buffer stays static,
  since topology never changes as a mesh animates); a boneless/riggless
  mesh is completely unaffected, still built via the original, immutable
  `CreateMesh()`. The Editor's "Hierarchy" drag-and-drop now tries
  `PlayAnimationOnEntity()` first when a Project asset is dropped onto an
  existing entity row (falling back to the usual spawn-as-child behavior
  otherwise), so dropping an Animation `*.gta` straight onto an
  already-spawned rigged model plays it; "Inspector" gained a minimal
  "Skeletal Animator" section (play/pause, loop, speed, current frame).
  Deliberately FORWARD-KINEMATICS ONLY for this first pass — no IK solving,
  morph blending, physics simulation, or true bezier interpolation yet (see
  `TODO.md`). Verified against the real Furina model plus a real
  690-bone-keyframe `.vmd` motion, and the full test suite (377 tests)
  still passes.
  **UPDATE (this session): IK solving AND PMX append/grant bone
  inheritance are now both implemented — a real MMD dance motion's legs
  actually animate now, closing the two-part gap this bullet's own
  "FORWARD-KINEMATICS ONLY" caveat used to call out.** Two DISTINCT,
  additive fixes were needed, discovered in that order against this same
  real Furina model + `.vmd` motion:
  1. **IK solving** (`src/Animation/IkSolver.h/.cpp`'s `SolveIkChains()`) —
     a VMD dance motion never keyframes a leg's thigh/knee bones directly;
     it keyframes an invisible IK TARGET bone at the foot instead (MMD's
     own 左足ＩＫ/右足ＩＫ, already extracted into `SkeletonData::Bone::
     isIk`/`ikLinks`/`ikTargetBoneIndex`/`ikIterationCount`/
     `ikAngleLimitRadians` by `PmxLoader`/`RigFile`, but never evaluated
     anywhere before this). `SolveIkChains()` is a Cyclic-Coordinate-Descent
     (CCD) solver: for each IK bone, it iterates its own `ikLinks`
     (nearest-to-effector first, PMX's own storage order) up to
     `ikIterationCount` times, each step rotating a link bone (clamped to
     `ikAngleLimitRadians` per step) to swing its effector bone toward
     wherever the IK bone itself was animated to, then clamping a
     constrained link's (e.g. a knee limited to one axis) total
     bind-relative rotation to its own PMX angle limits when present. Called
     from `Game::UpdateSkeletalAnimators()` right after
     `SampleAnimationPose()` and before `ComputeSkinningMatrices()`.
     Deliberately NOT a bit-perfect reimplementation of MMD's own IK solver
     (no dedicated single-axis "solve on this plane only" fast path) — a
     pragmatic, visually-close approximation, same spirit as
     `MotionSampler.h`'s own linear/slerp interpolation standing in for true
     MMD bezier curves.
  2. **Append/grant bone inheritance** (`src/Animation/AppendBoneSolver.h/.cpp`'s
     `ApplyAppendInheritance()`) — even with IK solving correct, the legs
     STILL didn't visibly move, because this real-world model (like many
     higher-quality MMD rigs) skins its mesh to a SEPARATE, parallel
     "D-bone" chain (左足D/左ひざD/左足首D and the right-leg equivalents)
     that's supposed to inherit ("append"/"grant") its rotation from the
     corresponding main FK/IK bone via PMX's `Bone::appendRotate`/
     `appendBoneIndex`/`appendWeight` fields — also already extracted, also
     never evaluated anywhere before this. `ApplyAppendInheritance()`
     resolves every appended bone's rotation/translation from its source
     bone in dependency order (a cycle-safe recursive resolve, so a
     cascading append chain resolves correctly), blending rotation via
     `Slerp(Identity, source, weight)` (correct for negative weights too,
     e.g. a shoulder-cancel bone) and translation via a scaled add. Called
     right after `SolveIkChains()` (so an append source that's also an IK
     link carries its IK-solved rotation) and before
     `ComputeSkinningMatrices()`.
  Both were diagnosed with a standalone, throwaway diagnostic tool (not
  committed) built directly against the engine's own compiled
  `libgte_core.a` and run against the real Furina model + motion —
  confirming the IK solve alone already converges the ankle onto its target
  almost exactly, and that the append-inheritance fix alone moves ~1989
  left-leg-D-bone-weighted vertices by an average of 2.3–4.3 units across
  sampled frames while leaving unrelated geometry (8441 head vertices) at
  EXACTLY 0.0 delta — proof the fix is both real/large and correctly
  scoped. Fully unit-tested (`tests/Animation/IkSolverTests.cpp`,
  `tests/Animation/AppendBoneSolverTests.cpp`), full test suite (388 tests,
  1 pre-existing machine-gated smoke test skipped) passes. Still NOT done:
  morph blending, physics simulation, and true bezier interpolation (see
  `TODO.md`).

- **Skeletal animation pose resolution refactored into shared, reusable
  primitives — `Game.cpp`/`Game.h` got measurably smaller as a result.**
  `SkeletonPose.cpp`'s whole-skeleton world-matrix walk, `AppendBoneSolver.cpp`'s
  append/grant-source walk, and `IkSolver.cpp`'s per-iteration single-bone
  world-matrix query each used to hand-roll their own cycle-guarded
  ancestor-chain recursion (in three different styles) and their own copy of
  the bind-relative local-transform formula. Both are now pulled out into
  two new, generic, Tier-1-tested files: `Animation/BoneChainResolver.h`
  (`ResolveBoneChain()` — a memoized, whole-skeleton walk; `ResolveSingleBoneChain()`
  — a deliberately non-memoized, single-bone-at-a-time walk, since `IkSolver`'s
  CCD loop mutates the very pose being queried mid-solve) and
  `Animation/BonePoseMath.h`'s `ComputeBoneLocalMatrix()`. A new
  `Animation/AnimationPoseEvaluator.h`'s `EvaluateAnimatedSkinningPose()`
  also now owns the correctness-critical `SampleAnimationPose()` ->
  `SolveIkChains()` -> `ApplyAppendInheritance()` -> `ComputeSkinningMatrices()`
  ordering as one tested function, rather than that sequence being
  reproduced by hand inside `Game::UpdateSkeletalAnimators()` (previously
  the only call site) — `Game.cpp` now drops 3 direct `Animation/` includes
  and collapses that whole sequence into a single call, with nothing left
  to reorder incorrectly at a future second call site (e.g. the Bone
  Viewer's planned live-pose overlay — see `TODO.md`). Purely an internal
  refactor — every pre-existing `SkeletonPose`/`IkSolver`/`AppendBoneSolver`
  test passes unchanged, proving behavior wasn't altered — plus new
  dedicated tests for the extracted primitives and a genuine
  ordering-regression test for the new evaluator (see `TESTING.md`). Full
  test suite (400 tests, 1 pre-existing machine-gated smoke test skipped)
  passes.

- **A new "Bone Viewer" debug window helps diagnose imported-model/animation
  bone mismatches.** The Inspector's "Model" section (shown for any entity
  spawned via `Game::CreateMeshEntityFromGtaFile()`) now has an "Open Bone
  Viewer" button that opens a Unity-"Avatar configuration"-style floating
  window (`src/Editor/BoneViewerWindow.h/.cpp`, gated behind
  `GTE_ENABLE_PROJECT_PANEL` like `AssetPreviewMesh`) showing the model's
  BIND-POSE mesh in a user-orbitable 3D viewport next to a real, indented
  bone hierarchy tree ("starting from root", mirroring "Hierarchy"'s own
  entity tree) — every bone is drawn as a gizmo dot + a line to its parent,
  with its name shown on hover/search-match, and selection is shared between
  the tree and the viewport (click either one; double-click a tree row to
  re-center the camera on it). Reads straight from the source `*.gta` file
  (the same path `AssetPreviewMesh` already uses), independent of `Game`'s
  own animation-runtime caches. See [Editor / Debug UI](architecture/editor-debug-ui.md) for the full rundown.
- **`Game.cpp` cleaned back up into a thin composition root - the "god
  object" (ten distinct responsibilities: pipeline/mesh caches, `.gta`
  decode, material-texture resolution, entity spawning, animation clip
  loading, per-frame skinning) it had accumulated is now split into
  dedicated, single-responsibility, independently testable modules under
  two new folders.** `src/Game/Instantiation/` holds everything behind
  primitive/imported-mesh spawning: `EntityBlueprint.h` (a tiny, inert
  "what to spawn" data shape - one node for a primitive, a root+children
  tree for a multi-part model), `EntityInstantiator.h/.cpp` (the ONE shared
  function that turns a blueprint into live entities/components, used by
  both spawn paths instead of two hand-duplicated blocks),
  `MeshVertexPacking.h/.cpp` and `MeshMaterialPartitioner.h/.cpp` (the pure
  vertex-packing/material-index-range math that used to be copy-pasted
  inline, now shared with the animation re-upload path too), and the
  GPU-facing `PrimitiveGpuCatalog.h/.cpp`/`MaterialTextureGpuCache.h/.cpp`/
  `MeshAssetGpuCatalog.h/.cpp`, all orchestrated by
  `MeshInstantiationSystem.h/.cpp` (what `Game::CreatePrimitiveEntity()`/
  `CreateMeshEntityFromGtaFile()` now just forward into). `src/Game/Animation/`
  holds the animation side: `SkeletalRigCache.h`/`AnimationClipCache.h/.cpp`/
  `ResolvedAnimationBindingCache.h` (three small, explicitly-owned caches
  replacing `Game`'s old anonymous private members - including fixing a real
  fragility, a hand-concatenated `meshPath + '\x1F' + animationPath` string
  cache key, into a proper `AnimationBindingKey` struct with its own hash/
  equality, so two different mesh/animation pairs can never collide), all
  owned by `AnimationSystem.h/.cpp` (what `Game::PlayAnimationOnEntity()`/
  the per-frame skeletal-animator update now forward into). The previously
  IMPLICIT coupling between mesh loading and animation (mesh loading quietly
  wrote into a private `Game` member animation quietly read back out of) is
  now an explicit, visible-in-code hand-off:
  `Game::CreateMeshEntityFromGtaFile()` calls
  `MeshInstantiationSystem::TryGetSkinnedMeshData()` and, if the freshly
  spawned model is skinned, hands that data to
  `AnimationSystem::RegisterSkinnedMesh()` right there at the call site.
  `Game`'s three public methods (`CreatePrimitiveEntity()`,
  `CreateMeshEntityFromGtaFile()`, `PlayAnimationOnEntity()`) kept their
  exact pre-refactor signatures/behavior throughout, so no Editor call site
  (`Panels/HierarchyPanel.cpp`/`ScenePanel.cpp`) needed to change at all.
  Every newly-pure module shipped with its own Tier-1 tests in the same step
  it was introduced (`tests/Game/EntityInstantiatorTests.cpp`,
  `MeshVertexPackingTests.cpp`, `MeshMaterialPartitionerTests.cpp`,
  `SkeletalRigCacheTests.cpp`, `AnimationClipCacheTests.cpp`,
  `ResolvedAnimationBindingCacheTests.cpp`) - the GPU-touching catalogs
  themselves remain "Tier 2, no automated coverage yet", same accepted
  bucket as `Buffer`/`RenderTexture`/`Pipeline` (see `TESTING.md`). See
  AGENTS.md's "Entity-Component-System" section for the updated
  architectural rule naming `MeshInstantiationSystem`/`AnimationSystem`
  alongside `RenderSystem`. Full test suite (434 tests, 1 pre-existing
  machine-gated smoke test skipped) passes.

- **A new Editor "Profiler" panel makes the whole profiler data model
  (Phases 0-3/5) finally visible, closing out `PROFILER_STRATEGY_v2.md`'s
  original 8-phase plan except for Phase 4 (Vulkan GPU timestamp queries -
  since implemented, see the entry below) and Phase 6 (benchmark mode),
  both deliberately deferred at the time.** See [Editor / Debug UI](architecture/editor-debug-ui.md) for the full "Profiler panel:" rundown - live CPU
  frame-time graph, sorted CPU-scope table, per-pass draw-call/triangle
  counts, GPU memory totals + sparkline, an honest GPU-timing "N/A"
  placeholder (a permanent condition until Phase 4 landed - see below), and
  two genuinely independent Capture/Pause controls. No new
  engine-level tracking was needed - the one small, already-anticipated
  data-model extension this phase required (`FrameGraphPoint` gaining a
  `memory` field, `Profiling::FrameGraphData.h` gaining
  `ComputeMemoryBytesRange()`) was named as a predicted future addition
  back in the Phase 5 session's own status notes. Verified with a full
  clean build + `ctest` run in three configurations - default (502 tests,
  501 passing + 1 pre-existing machine-gated skip),
  `-DGTE_ENABLE_EDITOR=OFF` (clean build, zero ImGui linked), and
  `-DGTE_ENABLE_PROFILER=OFF` (clean build, 500 tests passing, confirming
  the CPU Scopes table's "instrumentation compiled out" wording actually
  differs from its ordinary "nothing recorded yet" empty state). See
  `PHASE7_EDITOR_PROFILER_PANEL_STRATEGY_v2.md` for the full session
  writeup.
- **Phase 4 (Vulkan GPU timestamp queries) is now fully implemented,
  across four independently-shippable sub-phases (4A-4D) - the Editor's
  "Profiler" panel's "GPU Timing" section (see above) now shows real,
  driver-measured milliseconds instead of a permanent "N/A".** See
  `PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md` for the full design
  reasoning and `PHASE4A_COMPLETION_REPORT.md`/
  `PHASE4B_COMPLETION_REPORT.md`/`PHASE4C_COMPLETION_REPORT.md`/
  `PHASE4D_COMPLETION_REPORT.md` for each sub-phase's own session writeup;
  summarized here: **4A** added device timestamp-capability probing
  (`VulkanDevice::TimestampCapability()`) plus Vulkan-header-free pure math
  (`src/Renderer/GpuTiming.h` - tick-delta -> millisecond conversion,
  wraparound-safe via `validBits` masking; query-slot indexing), with zero
  `VkQueryPool` created yet. **4B** added the actual RAII query-pool
  infrastructure - `Vulkan/VulkanQueryPool.h/.cpp` (a thin wrapper around
  one fixed-size `VK_QUERY_TYPE_TIMESTAMP` pool) and
  `GpuTimingService.h/.cpp` (owns that pool, every real
  `vkCmdResetQueryPool`/`vkCmdWriteTimestamp2`/`vkGetQueryPoolResults` call
  site, and a two-layer on/off gate - `GTE_ENABLE_PROFILER` at compile
  time, `SetCaptureEnabled()` at runtime, mirroring `ScopeTimer`'s own
  convention) - wired into `Renderer`'s ownership graph (shared via
  `std::shared_ptr`, same pattern as `GpuMemoryTracker`) but not yet called
  from anywhere. **4C** wired up the Game/Scene offscreen passes first
  (`RenderOffscreen()`'s already-fully-synchronous fence made this the
  easiest, safest half): `Renderer::RenderOffscreen()` gained a
  `std::optional<GpuTimingSlot>` parameter (`Offscreen0`/`Offscreen1` for
  Game/Scene, or `std::nullopt` for a call that has nothing to do with the
  Profiler's three named passes, e.g. `AssetPreviewMesh`'s Inspector mesh
  preview or `BoneViewerWindow`'s own viewport - closing a real
  slot-collision bug a naive design would have introduced), and
  `Application::Run()` started calling
  `Renderer::SetGpuTimingCaptureEnabled()` once per frame from the
  Editor's existing "Capture" checkbox. **4D** wired up the harder
  swapchain Present path, reading back a PAST frame's query result at
  exactly the point the existing two-frames-in-flight fence wait already
  proves it's safe (a per-slot "has this ever been written" warm-up flag,
  not a frame-count heuristic, correctly handles the first two frames of
  a session/of any capture-disabled or resize/minimize gap). No new GPU
  wait was added anywhere in any of the four sub-phases - every read
  piggybacks on synchronization the engine already performed for an
  unrelated, pre-existing reason. Verified at every sub-phase with a clean
  build, the full test suite (521 tests as of 4D, 1 pre-existing
  machine-gated smoke test skipped, no regressions across all four
  sub-phases), and a runtime smoke test against a real Vulkan device with
  validation layers enabled. See `AGENTS.md`'s "Profiling" section for the
  architectural rules this module follows.
- **The Render Graph campaign (Phases 1-8) is complete - the engine's real
  Game View/Scene View/Present passes are now declared, compiled, barrier-
  synthesized, and executed entirely through a genuine, declarative Render
  Graph (`gte::rg::RenderGraph`, `src/Renderer/RenderGraph/`), replacing the
  old hand-wired, three-hardcoded-pass `FrameRecorder` pipeline this section
  used to describe.** See `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the
  original nine-phase plan and `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md`
  for the full campaign-level summary (tying together all eight individual
  `RENDERGRAPH_PHASEn_COMPLETION_REPORT.md` writeups). In short: Phases 1-2
  built the graph's pure vocabulary and a declarative
  `AddPass()`/`CreateTexture()`/`ImportTexture()` builder API; Phase 3 is a
  pure dependency-resolving/culling/topological-sorting compiler; Phase 4
  pools/reuses real GPU resources across frames; Phase 5 synthesizes every
  barrier automatically (regression-matched field-for-field against the old
  hand-written ones); Phase 6 ties all of that into a real executor,
  `RenderGraph::Execute()`, called TWICE per frame (once for the
  synchronous offscreen Game+Scene regime, once for the pipelined swapchain
  Present regime); Phase 7 is the actual production cut-over -
  `Application::Run()` now drives every real frame through it, with zero
  observable behavior change by design; and Phase 8 (see [Editor / Debug UI](architecture/editor-debug-ui.md), "Render Graph panel") makes the whole thing observable via a
  new Editor panel. `Game::Render()`/`RenderSystem::Draw()`/
  `Renderer::Submit()` all kept their exact pre-campaign public signatures
  throughout - this was, and remains, a purely internal `Renderer`-layer
  architecture upgrade. Two genuine, explicitly-tracked follow-ups remain
  open (see the campaign completion report's own "What is genuinely proven
  vs. what remains open" section): real GPU timestamp-query timing for the
  graph's own passes (every pass's GPU-timing sample is honestly `Absent`
  today, never fabricated), and a real, shipped cross-pass texture READ to
  prove out the graph's own most novel capability end-to-end (the
  already-planned Scene-view outline-highlight post-process, see
  `TODO.md`, is the natural candidate).

- **GPU Vertex Skinning is now a second, switchable implementation of vertex
  skinning, alongside the existing CPU/Job-System path** - an eight-phase
  campaign (`task_manager/gpu_skinning/`,
  `GPU_SKINNING_PHASE0_MASTER_STRATEGY_v2.md`) added a compute-shader mirror
  of `Animation/VertexSkinning.cpp`'s CPU blend (`Shaders/
  SkinVerticesPositionNormal(Uv).comp`, `src/Renderer/GpuSkinning/`),
  per-model GPU buffer/descriptor-set/Mesh management
  (`GpuSkinningRigCache`), full render-graph synchronization (a new
  `ResourceAccess::VertexBufferRead`/`RenderGraphBuilder::ImportBuffer()`,
  closing a real write-after-write hazard two same-model instances could
  otherwise hit), and a genuine runtime switch
  (`AnimationSystem::SkinningMode`/`Game::SetSkinningMode()`) that swaps a
  model's `MeshRenderer` onto its GPU-skinned Mesh counterpart with no
  further per-frame CPU packing/upload cost beyond one bone-matrix buffer
  write. The Editor's "Jobs" panel now hosts the actual CPU/GPU toggle
  (`Panels/JobsPanel.cpp`'s "Skinning Mode" control) right next to the
  worker timeline it makes "SkinVertices" entries appear/disappear from,
  with a tooltip pointing at the "Render Graph" panel's own
  "SkinModel:..." pass timing for the other mode - neither panel needed a
  new "N/A"/fabricated-value state, since the absence of a row/segment
  already is the honest signal (see `AGENTS.md`, "Profiling"). A dedicated
  Editor-only validation tool (`src/Editor/GpuSkinningValidation.h/.cpp`,
  mirroring `ComputeBlurValidation`'s own proven pattern) numerically
  compares the GPU kernel's output against the CPU oracle per-vertex, since
  this repository has no live-`VkDevice`-requiring automated test
  infrastructure yet (see `TESTING.md`/`TODO.md`'s own "Tier 2" bucket).
  See `AGENTS.md`'s new "GPU Vertex Skinning" section for the load-bearing
  rules this feature depends on (the CPU path stays the permanent oracle;
  a "phantom" `VertexBufferRead` declaration that looks like dead code but
  isn't; `ComputeDescriptorSet::Rewrite()` called once per model, not every
  frame; a GPU-skinned model deliberately owns two separate `Mesh` objects
  at once). Full build/regression testing and a real, measured CPU-vs-GPU
  performance comparison are still outstanding - see `TODO.md`.

- **The hardcoded demo triangle scene is gone, replaced by a real (if
  deliberately minimal) scene Save/Load loop.** `Game::EnsureDemoSceneBuilt()`'s
  3 hardcoded triangle entities and their private `Pipeline`/`Mesh` (see
  earlier "Status" entries above, and the "Entity-Component-System"
  section's own now-historical description of them) are gone -
  `Game::EnsureDefaultCameraExists()` now creates only the one default
  `Camera` entity every fresh/just-loaded scene still needs. In their
  place, a new, always-compiled `src/Scene/` module (`SceneDocument.h`,
  `SceneTextFormat.h/.cpp`, `SceneBuilder.h/.cpp` - fully Tier-1-tested,
  zero ECS/Renderer/filesystem dependency) implements a small, hand-rolled,
  line-oriented TEXT file format (`*.gtscene` - deliberately not JSON, no
  JSON library is vendored in this engine) that can serialize/restore the
  two kinds of top-level scene object the Editor can currently create: a
  built-in primitive (`Game::CreatePrimitiveEntity()`, by which
  `PrimitiveType` it is - a new `PrimitiveSource` component) and an
  imported-asset instance (`Game::CreateMeshEntityFromGtaFile()`, by its
  stable `AssetDatabase` `Guid` - never a raw machine-local path, the same
  principle `MaterialTextureRef::guid` already established). A new
  `File > Save Scene` (Ctrl+S) / `File > Open Scene` (Ctrl+O) menu pair
  (`src/Editor/SceneIO.h/.cpp`, `DockLayout.cpp`) reads/writes one
  hardcoded path, `<Project folder>/TestScene.gtscene` - no file picker,
  no "Save As", no undo/redo yet; only each object's ROOT entity's
  Transform/Name round-trips (a multi-part asset's own child "submesh
  part" entities are always freshly re-derived from the asset itself on
  Load, never individually serialized), and Camera/physics/animation-
  runtime state is intentionally not persisted at all. See
  `task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md` for the
  full six-phase writeup.
- **The engine now has its first real networking feature: an embedded,
  loopback-only HTTP server.** `src/Network/` (`NetworkServer.h/.cpp`,
  built on the already-vendored cpp-httplib - see `cmake/FetchHttplib.cmake`)
  is auto-started by `Application`'s constructor (gated by a new
  `GTE_ENABLE_NETWORK` CMake option, default ON) on a dedicated background
  thread, so it never blocks the main frame loop. Binds to `127.0.0.1` only
  - `NetworkServer::Start(int port)` has no `host` parameter at all, so this
  is enforced by the type itself, never reachable from another machine. One
  endpoint exists today: `GET http://127.0.0.1:8080/http_hello_world`
  returns `hello world`. See `AGENTS.md`'s new "Networking" section for the
  thread-safety rule every future endpoint must follow (a route handler
  must be a pure function of its own request data - it must never touch
  ECS/Renderer/Game/AssetDatabase directly), and
  `task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md` for the full
  campaign writeup.
- **The engine's embedded HTTP server now has its first engine-state-
  touching endpoints: `GET /get_swapchain` and `GET /get_game_view`, both
  returning a live PNG screenshot.** (`network-impl-2` campaign.)
  `GET http://127.0.0.1:8080/get_swapchain` returns the literal, currently-
  presented OS-window swapchain image (in an Editor build, this is a
  screenshot of the whole Editor UI - dock panels, menu bar, chrome and all);
  `GET http://127.0.0.1:8080/get_game_view` returns just the Game's own
  off-screen 3D-scene `RenderTexture` ("Game view"), available only when a
  Game view actually exists and is currently visible this session (a `409`
  otherwise). Both accept an optional `?format=` query parameter (`png` -
  the default, raw bytes with `Content-Type: image/png` - or
  `base64`/`json`, a `{"width":...,"height":...,"format":"png",
  "data_base64":"..."}` JSON envelope), or honor an
  `Accept: application/json` request header when `?format=` is omitted. The
  engine's own window keeps rendering/updating at full frame rate,
  completely undisturbed by the request - a route handler never touches
  Vulkan/`Renderer`/`Game` directly; it only ever calls into
  `FrameCaptureBridge` (`src/Application/FrameCaptureBridge.h/.cpp`), the
  one reviewed, thread-safe bridge between the network's own background
  thread and the main thread's per-frame loop (returns HTTP `503` if a
  request of the same kind is already pending, `504` on a timeout, `409` if
  the main thread positively determines the target isn't available this
  frame). The swapchain capture is genuinely pipelined - a per-frame-in-
  flight readback buffer pair read back one "round" later
  (`SwapchainCaptureService`, `src/Renderer/SwapchainCaptureService.h/.cpp`,
  mirroring `GpuTimingService`'s own Present-timing pattern) - with **zero
  added GPU stall**, never a simplified wait-idle shortcut. A new
  `src/Encoding/` module (`Base64.h`/`PixelConversion.h`/`PngEncoder.h`,
  always-compiled, Vulkan-free, Tier-1-tested) provides the base64/BGRA→RGBA/
  PNG-encode primitives both routes share, built on a newly-vendored
  `stb_image_write.h` (the encode-side counterpart of the already-vendored
  `stb_image.h` decoder). No new CMake toggle was added - this feature is
  gated entirely by the existing `GTE_ENABLE_NETWORK` switch. See
  `task_manager/network-impl-2/PHASE0_MASTER_STRATEGY.md` for the full
  six-phase campaign writeup.

- **The engine's embedded HTTP server now has its first POST endpoints that
  MUTATE the ECS world, not just read pixels back.** (`network-impl-3`
  campaign, `task_manager/network-impl-3/PHASE0_MASTER_STRATEGY.md`.)
  `POST http://127.0.0.1:8080/instantiate_primitive` spawns one of the
  engine's 5 built-in primitive shapes (`cube`/`sphere`/`capsule`/`cone`/
  `plane`, case-insensitive) as a new, uniquely-named entity - Unity's own
  `GameObject.CreatePrimitive()` plus `transform.position =`/
  `transform.SetParent()`, reachable over HTTP - given a JSON body of
  `shape`/`name`/`world_position`/an optional `parent` (looked up by name; an
  unresolvable parent is a non-fatal warning, never a failure - the entity is
  still created, just left unparented). A repeated `name` auto-de-duplicates
  Unity-style (`"Cube"`, `"Cube (1)"`, ...). `POST
  http://127.0.0.1:8080/delete_entity` destroys a live entity (and every
  descendant of it) looked up by name. Both are built on a brand-new
  cross-thread bridge, `EngineCommandBridge`
  (`src/Application/EngineCommandBridge.h/.cpp` - contrast with
  `network-impl-2`'s read-only `FrameCaptureBridge`: this one carries a real
  request payload and a real success/failure mutation outcome, drained by
  `Application::Run()` once per frame, EARLY - right after SDL input polling,
  before `Game::Update()` - so a network-spawned/deleted entity is fully
  consistent for the rest of that same frame), and on the engine's first
  vendored JSON library (`nlohmann/json`, `cmake/FetchJson.cmake`) for real
  request-body parsing/response-building instead of hand-formatted strings.
  See `task_manager/network-impl-3/PHASE0_MASTER_STRATEGY.md` for the full
  six-phase campaign writeup.

- **Any render-graph texture can now be captured as a PNG by name over
  HTTP** (`network-impl-4` campaign,
  `task_manager/network-impl-4/PHASE0_MASTER_STRATEGY.md`) -
  `GET /get_texture?texture_name=<name>` captures whatever render-graph
  texture was registered under that exact name (every texture any pass
  declares via `RenderGraphBuilder::CreateTexture()`/`ImportTexture()`
  becomes capturable automatically, with zero opt-in), including a
  texture's own depth buffer (`&channel=depth`) - useful for debugging
  off-screen intermediate passes (e.g. future atmosphere-scattering LUTs)
  that never otherwise appear on screen. `GET /list_textures` lists every
  texture name registered so far this session, alongside its
  regime/format/extent/depth-availability and how many frames old its last
  update is (`frames_since_update`). See `AGENTS.md`'s "Networking" section
  ("Named Texture Capture") for the one narrow, deliberate exception this
  endpoint makes to this engine's usual "zero added GPU stall" networking
  rule, and
  `task_manager/network-impl-4/NETWORK_IMPL_4_CAMPAIGN_COMPLETION_REPORT.md`
  for the full six-phase campaign writeup.

- **The embedded HTTP server can now change an existing entity's transform,
  and spawn lights, over HTTP** (`network-impl-5` campaign,
  `task_manager/network-impl-5/PHASE0_MASTER_STRATEGY.md`) - two more
  `EngineCommandBridge`-backed POST endpoints, reusing the exact same
  cross-thread bridge/JSON machinery `network-impl-3` already built (no new
  vendored dependency, no new cross-thread mechanism). `POST
  /set_entity_trs` updates translation/rotation/scale on an existing,
  by-name entity, independently (any subset of the three), operating on its
  LOCAL (parent-relative) transform; every call's response always echoes the
  entity's full resulting transform (position, rotation as both Euler
  degrees and a raw quaternion, scale) plus which fields this call actually
  changed - including a call that changes nothing, which doubles as a
  lightweight "read the current transform" query. `POST /instantiate_light`
  spawns a new `DirectionalLight` entity (a `light_type` field future-proofs
  this for a later point/spot light), mirroring `/instantiate_primitive`'s
  own name/position/parent contract, plus color/illuminance/active fields
  mapping directly onto `DirectionalLight`'s own component fields - a
  network-spawned light with no explicit rotation gets the same
  "late-afternoon" default rotation the Editor's own "Create Directional
  Light" menu already uses. See `task_manager/network-impl-5/PHASE0_MASTER_STRATEGY.md`
  for the full five-phase campaign writeup.

- **The Editor's "Scene" panel now has a Unity-style procedural infinite
  ground grid** (`editor-enchancements-1` campaign,
  `task_manager/editor-enchancements-1/PHASE0_MASTER_STRATEGY.md`) - a
  ray-plane-intersection effect computed entirely in a fragment shader
  (`Shaders/SceneGrid.vert/.frag`), with **zero** mesh/texture/`*.gta` asset
  of its own: the vertex stage synthesizes a full-screen triangle purely from
  `gl_VertexIndex`, and the fragment stage casts each pixel's camera ray
  against the world's `Y = 0` plane. Shows two anti-aliased LOD levels
  (1-world-unit "minor" / 10-world-unit "major" lines, Unity's own default
  spacing, `fwidth()`-based coverage with the minor grid fading out as cells
  shrink toward sub-pixel size), Unity-style colored X/Z axis highlight lines
  through the world origin (red/blue), and a smooth distance fade so the
  horizon never shows a messy converging line-field. Correctly depth-TESTED
  (never depth-WRITTEN) against real scene geometry already drawn that frame -
  an opaque primitive sitting on the grid genuinely occludes the grid lines
  underneath it - by drawing as one more `vkCmdDraw` call
  (`src/Editor/SceneGridRenderer.h/.cpp`, a dedicated `VkPipeline` bypassing
  `Renderer::CreatePipeline()`/`Submit()` entirely, the same proven shape
  `AssetPreviewMesh`/`ComputeBlurValidation` already established) issued
  INSIDE the already-open `"SceneView"` RenderGraph pass's own
  `vkCmdBeginRendering` bracket, immediately after `Game::Render()`'s own
  draws finish - deliberately never a second, separate RenderGraph pass (see
  the campaign's own `PHASE0_MASTER_STRATEGY.md` for the write-after-write
  hazard a naive second-pass design would have introduced, given
  `RenderGraphBarrierPlanner`'s own barrier-elision rule for two
  same-resource attachment writes). Renders **only** in "Scene" - never
  "Game", and structurally absent at zero cost from a
  `-DGTE_ENABLE_EDITOR=OFF` release build, since `NullEditorLayer`'s
  `RenderSceneGrid()` override is a no-op and the shader itself is never even
  compiled in that configuration (its `gte_add_shader()` CMake registration
  lives inside the same `if(GTE_ENABLE_EDITOR)` block as everything else
  under `src/Editor/`). No visibility toggle (matches Unity, which has none
  either), never selectable/pickable, and never touches `Registry`/ECS in any
  way - it is pure Editor-side GPU output, by construction. The exact same
  ray-plane/grid-line/axis-line math also has a pure, Tier-1-tested CPU
  mirror, `src/Editor/SceneGridMath.h/.cpp`
  (`ComputeGridPlaneHit()`/`ComputeGridLineCoverage()`/
  `ComputeAxisLineCoverage()`, `tests/Editor/SceneGridMathTests.cpp`) - the
  "CPU math is the spec, GLSL mirrors it" discipline this engine already
  applies to GPU vertex skinning, verified in the campaign's own final phase
  against real spawned geometry (two cubes at the world origin and at
  `(5, 0, 0)`, confirming the axis lines/grid cells line up pixel-for-pixel
  with real scene content) and against grazing/straight-down/straight-up
  camera angles. See
  `task_manager/editor-enchancements-1/PHASE5_COMPLETION_REPORT.md` for the
  campaign's final polish/verification session and its full five-phase
  writeup.

- **The engine now has a physically-based, real-time Atmosphere Scattering +
  Aerial Perspective system** (`atmosphere-scattering-1` campaign, nine
  phases - `task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE0_MASTER_STRATEGY_v1.md`,
  `ATMOSPHERE_CAMPAIGN_COMPLETION_REPORT.md`) - the same class of technique
  as Sébastien Hillaire's *"A Scalable and Production Ready Sky and
  Atmosphere Rendering Technique"*, hand-ported (never vendored) from a
  cloned reference implementation, [hoffstadt/pl-sky](https://github.com/hoffstadt/pl-sky).
  A physically-plausible sky renders behind all scene geometry in both the
  Editor's "Game" and "Scene" views, and distant opaque geometry now
  progressively washes out/tints toward the sky's own color with distance
  (aerial perspective) - a real, visible change from the engine's previous
  "no atmosphere at all" look. **The permanent CPU oracle**:
  `src/Renderer/Atmosphere/AtmosphereMath.h/.cpp` (density profiles, optical
  depth, transmittance, phase functions - fully Tier-1-tested,
  `tests/Renderer/Atmosphere/AtmosphereMathTests.cpp`) is the from-scratch,
  hand-verified ground truth every GLSL shader in this campaign is checked
  against, never the other way around - see `AGENTS.md`'s new "Atmosphere
  Scattering" section for the full rule. **Four chained compute passes**
  build the technique's LUT (look-up texture) chain every frame: the
  Transmittance LUT (`Shaders/AtmosphereTransmittanceLut.comp`, 256x256, the
  only one of the four whose GPU output is now NUMERICALLY validated against
  the CPU oracle - see below), the Multi-Scattering LUT
  (`AtmosphereMultiScatteringLut.comp`, 64x64), the per-view Sky-View LUT
  (`AtmosphereSkyViewLut.comp`, 200x100, one per Game/Scene View), and the
  Aerial Perspective froxel volume (`AtmosphereAerialPerspectiveVolume.comp`,
  128x128x32) - the last of which required teaching the engine's Render
  Graph (`gte::rg::RenderGraph`) a genuine THIRD resource kind,
  `VolumeTexture`/`VolumeTextureHandle` (`src/Renderer/VolumeTexture.h`,
  `RenderGraphBuilder::ImportVolumeTexture()`/`KeepVolumeTextureOutput()`),
  alongside its existing 2D-texture/buffer vocabulary. Two new full-screen
  passes composite the result onto real content: a Sky Background pass
  (`AtmosphereSkyBackgroundRenderer.h/.cpp`, `Shaders/AtmosphereSkyBackground.vert/.frag`)
  draws the sky wherever nothing else was drawn (a depth-EQUAL-against-the-
  clear-value trick, never depth-written), and an Aerial Perspective
  Composite pass (`Shaders/AtmosphereAerialPerspectiveComposite.comp`) blends
  the froxel volume's transmittance/in-scattering onto the already-rendered
  scene color+depth - both wired into the real, permanent per-frame Game
  View/Scene View pass sequence (`src/Application/AtmospherePassSequence.h/.cpp`),
  never a parallel/throwaway code path. **Scene control**: a new
  `DirectionalLight` ECS component (`src/ECS/Components/DirectionalLight.h`)
  sits on a `Transform`-bearing "Sun" entity, selectable/rotatable in
  "Hierarchy"/"Inspector" exactly like any other entity (Hierarchy gained a
  "Create Directional Light" entry) - `src/Renderer/Atmosphere/
  DirectionalLightResolver.h`'s `ResolveActiveDirectionalLight()` picks the
  first active one (falling back to a fixed placeholder sun when none
  exists, so a scene with no Sun entity still renders a plausible sky), the
  same "first active wins" convention `Camera` already established. A new
  Editor **"Atmosphere" panel** (`src/Editor/Panels/AtmospherePanel.h/.cpp`,
  docked alongside "Memory"/"Profiler"/"Render Graph") exposes a short,
  deliberately curated `AtmosphereSettings` struct (ground albedo tint,
  aerial perspective strength, sky exposure - never every physical constant)
  plus, as of this campaign's final phase, an aerial-perspective-volume
  debug-slice slider and a live LUT-validation button (see below).
  **Phase 9 (validation/tooling/docs) closed out the campaign**: a new
  Editor-only tool, `src/Editor/AtmosphereTransmittanceLutValidation.h/.cpp`
  (mirroring `GpuSkinningValidation`'s own proven shape - self-contained,
  built on `Renderer::CaptureImagePixels()`, no RenderGraph dependency),
  reads back the REAL, currently-computed `"AtmosphereTransmittanceLut"`
  texture, decodes each texel's UV back into `(height, zenithAngle)` (a new
  `AtmosphereMath::TransmittanceLutUvToHeightZenith()` C++ port of the
  shader's own parameterization), and numerically compares every texel
  against the CPU oracle - surfaced as a "Validate Transmittance LUT" button
  in the "Atmosphere" panel. **The very first real run found every texel
  agreeing with the CPU oracle well within the documented 0.01 tolerance
  (UNORM8 quantization alone accounts for ~0.004 of that) - no shader bug was
  found, so nothing needed fixing.** The aerial-perspective volume also
  gained permanent debug visibility Phase 2 had deliberately deferred: a
  tiny new compute pass, `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()`
  (`Shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp`), mirrors one
  Z-slice of the Game View's own froxel volume into a real, registered 2D
  texture, `"AtmosphereAerialPerspectiveVolumeDebugSlice"` - automatically
  `GET /get_texture`/`GET /list_textures`-capturable with zero further
  networking changes, exactly like every other named texture this engine
  already exposes (`network-impl-4` campaign). Verified with a full clean
  build (`gte_core`/`GreatTamanaEngineTests`/`GreatTamanaEngine`, all three
  targets) and a full `ctest` regression pass (1205 tests, 1 pre-existing
  machine-gated smoke test skipped, zero regressions), plus a live runtime
  smoke pass confirming all four LUTs, the new debug-slice texture, the
  composited Game/Scene views, and `GET /list_textures` all report sane,
  live-updating data. Explicitly deferred (see `TODO.md`'s new "Atmosphere
  Scattering" section): scene serialization of `DirectionalLight`/
  `AtmosphereSettings`, volumetric clouds/god-rays, a general lighting
  system, day-night animation, and per-render-graph-pass GPU timing in
  general (a pre-existing, campaign-external gap, not unique to this
  feature). See `AGENTS.md`'s new "Atmosphere Scattering" section for every
  load-bearing rule future contributors must follow, and each phase's own
  `ATMOSPHERE_PHASEn_COMPLETION_REPORT.md` for the full nine-phase writeup.
- **The Aerial Perspective haze is now actually VISIBLE at this engine's real
  scene scale, its own tuning knobs are live Editor sliders instead of
  hardcoded shader constants, and its froxel volume has a genuinely useful
  live visual + numeric debugging path** (`atmosphere-scattering-2` campaign,
  six phases - `task_manager/atmosphere-scattering-2/PHASE0_MASTER_STRATEGY.md`,
  `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md`). The original
  `atmosphere-scattering-1` pipeline was confirmed logically correct
  end-to-end, but its fixed 10km froxel far-plane and real-Earth-scale
  scattering coefficients made the effect ~2-3 orders of magnitude too faint
  to see at the few-meters-to-few-hundred-meters distances this engine's real
  content actually lives at - a genuine SCALE MISMATCH, not a bug (see
  `AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md`, kept as permanent historical
  record). `aerialPerspectiveMaxDistanceKm`/`aerialPerspectiveDepthExponent`/
  `aerialPerspectiveSamplesPerSlice`/`aerialPerspectiveScatteringExaggeration`
  are now real `AtmosphereSettings` fields with live sliders in the "Atmosphere"
  panel (Phase 1), the max distance default shrank from 10km to **0.5km** and a
  new scattering-exaggeration multiplier shipped at **30.0x** (Phase 3) -
  applied strictly LOCALLY inside the aerial volume's own shader, never
  touching the shared `AtmosphereMath.h`/`AtmosphereCommon.glsl` oracle the
  Sky-View/Transmittance/Multi-Scattering LUTs also rely on - plus two smaller,
  independently-confirmed composite-pass precision fixes (half-texel Z-bias,
  first-slice fade-in blend, Phase 2). The existing `GET /get_texture`
  volume-preview raymarch (`network-impl-6`) gained a second, atmosphere-aware
  interpretation mode auto-selected by volume name (Phase 4), turning what used
  to render as a flat, uninformative dark box into a legible spatial gradient,
  and a new numeric CPU-readback inspection tool
  (`src/Editor/AtmosphereAerialPerspectiveLutInspection.h/.cpp`, an "Inspect
  Aerial Perspective LUT" button in the "Atmosphere" panel, Phase 5) reports
  live min/max/mean transmittance/in-scattering plus a "likely visible"
  heuristic. Verified with a full clean build, a full `ctest` regression pass,
  and a live runtime smoke test confirming the blueish haze is now clearly,
  smoothly visible on far-distance test geometry relative to near geometry -
  see `AERIAL_PERSPECTIVE_CAMPAIGN_COMPLETION_REPORT.md` for the full
  six-phase writeup and final verification snapshot.
- **The embedded HTTP server's `GET /get_texture`/`GET /list_textures` now
  understand live, GPU-resident 3D (volume) textures, not just 2D ones**
  (`network-impl-6` campaign,
  `task_manager/network-impl-6/PHASE0_MASTER_STRATEGY.md`) - the same class
  of capability Unity's Editor gives you when it draws a raymarched
  "smoke cloud"-style preview thumbnail for a `Texture3D` asset in the
  Inspector, except here the client is an LLM/AI agent talking to
  `GET /get_texture` over loopback HTTP, not a human looking at an Editor
  panel. A new, pure, Tier-1-tested data model,
  `gte::rg::RenderGraphDebugVolumeTextureRegistry`
  (`src/Renderer/RenderGraph/RenderGraphDebugVolumeTextureRegistry.h/.cpp`),
  mirrors the existing 2D `RenderGraphDebugTextureRegistry`
  (`network-impl-4` campaign) and is auto-populated by
  `gte::rg::RenderGraph::ExecuteCompiledGraph()` every frame, with zero
  opt-in from whichever pass declared the volume texture (today: the
  Atmosphere feature's own two aerial-perspective froxel volumes - see the
  Atmosphere Scattering entry immediately above - this campaign's own first
  real, verified consumer, but the mechanism works generically for any future
  `VolumeTextureHandle`). A requested `texture_name` that resolves to a
  volume now renders a fresh, on-demand, single-fixed-camera, front-to-back
  alpha-composite raymarch (`gte::VolumeTexturePreviewRenderer`, driven by a
  new compute shader, `Shaders/VolumeTexturePreview.comp`, and its own pure
  CPU camera/ray-box math oracle, `VolumeTexturePreviewMath.h` - the same
  "CPU oracle is right by definition" discipline the Atmosphere Scattering
  campaign's own `AtmosphereMath.h` already established) into a persistent
  256x256 RGBA8 thumbnail, then rejoins the exact same PNG-encode/
  `?format=`/`Accept:` negotiation path every existing 2D capture already
  uses - no new endpoint, no new query parameter, no new cross-thread bridge
  type. `GET /list_textures` entries now also carry a
  `"kind":"texture2d"|"texture3d"` field plus a `"depth"` field (a volume's
  Z/texel-count extent), so an LLM/AI agent caller can discover which
  `texture_name`s are volumes worth requesting with no prior knowledge of
  the engine's internal naming convention. Verified end-to-end against a
  live running engine (`GET /get_texture?texture_name=AtmosphereAerialPerspectiveVolume_GameView`
  returning a real, visually plausible non-cubic-box PNG thumbnail;
  `channel=depth` against a volume
  name correctly returning `409`; every pre-existing 2D-texture capture
  request behaving byte-for-byte unchanged) and a full clean build plus full
  `ctest` regression pass. See `AGENTS.md`'s "Named Texture Capture"/
  "Atmosphere Scattering" sections for every load-bearing rule this feature
  depends on, and each phase's own `PHASEn_COMPLETION_REPORT.md` for the
  full six-phase campaign writeup.
- **The Aerial Perspective volume's own HTTP preview thumbnail (above) had its
  camera framing corrected in a follow-up campaign, `atmosphere-scattering-3`**
  (`task_manager/atmosphere-scattering-3/`) - the generic volume-preview
  camera/proxy-box math is correct for an ordinary spatial volume, but it
  flattened this particular LUT's one meaningful (near/far) axis into an
  unreadably thin sliver, since its three axes aren't comparable units; a
  dedicated camera, and then a literal tapering frustum-shaped raymarch proxy
  (auto-selected purely by texture name, same convention as the
  color-interpretation fix above, with zero new HTTP parameter), now make the
  preview actually widen away from the camera with a legible near/far haze
  gradient - verified with a full clean build and full `ctest` regression
  pass. See `task_manager/atmosphere-scattering-3/CAMPAIGN_COMPLETION_REPORT.md`
  for the full five-phase writeup.
- **A follow-up campaign, `atmosphere-scattering-4`, fixed a confirmed bug
  where Aerial Perspective was double-applied to empty sky pixels** (no
  opaque geometry drawn into them that frame) — the Aerial Perspective
  Composite pass used to run its full haze blend unconditionally, re-fogging
  a sky pixel the Sky Background pass had already finished, correctly,
  earlier in the same frame (confirmed by toggling `aerialPerspectiveStrength`
  visibly changing the sky itself, which should never happen). The fix is a
  small early pass-through branch in
  `Shaders/AtmosphereAerialPerspectiveComposite.comp`, mirroring a new,
  dedicated, Tier-1-tested CPU oracle
  (`src/Renderer/Atmosphere/AtmosphereAerialPerspectiveCompositeMath.h/.cpp`)
  built and tested BEFORE the shader was touched — real opaque geometry
  (a mesh, the reference grid) still fogs progressively with distance exactly
  as before, completely unaffected. A new permanent Editor diagnostic, the
  "Validate Aerial Perspective Sky Purity" button in the "Atmosphere" panel
  (`src/Editor/AtmosphereAerialPerspectiveSkyPurityValidation.h/.cpp`),
  numerically re-confirms every sky pixel's post-composite color exactly
  matches its pre-composite color, on demand — catching a future regression
  of this exact bug class without relying on a human eyeballing a screenshot.
  Verified with a full clean build, a full `ctest` regression pass, and a
  live runtime smoke test. See
  `task_manager/atmosphere-scattering-4/CAMPAIGN_COMPLETION_REPORT.md` for
  the full four-phase writeup.

- **The embedded HTTP server can now bring a specific Editor panel/tab to
  the front on command, and list every known panel name** (`network-impl-7`
  campaign, `task_manager/network-impl-7/PHASE0_MASTER_STRATEGY.md`) -
  `GET /activate_tab?name=<PanelName>` makes that named tab
  (`"Hierarchy"`/`"Inspector"`/`"Scene"`/`"Game"`/`"Memory"`/`"Profiler"`/
  `"Render Graph"`/`"Jobs"`/`"Atmosphere"`/`"Project"`) the active/focused tab
  this same frame, exactly as if a human had clicked it, and
  `GET /list_tabs` reports every currently-known panel name so a caller
  never has to guess or hardcode the engine's internal naming convention.
  Built on a brand-new, dedicated cross-thread bridge,
  `EditorUiCommandBridge` (`src/Application/EditorUiCommandBridge.h/.cpp`),
  mirroring `FrameCaptureBridge`/`EngineCommandBridge`'s own narrow,
  single-purpose bridge precedent, and a shared
  `src/Editor/EditorPanelCatalog.h` panel-name catalog that `DockLayout.cpp`'s
  own default layout now reads from too, so the set of valid tab names can
  never drift out of sync between the two. Verified end-to-end with a real
  running engine (`GET /activate_tab?name=Profiler` followed by
  `GET /get_swapchain` visually confirming the "Profiler" tab genuinely came
  to the front) and a full `ctest` regression pass. See
  `task_manager/network-impl-7/CAMPAIGN_COMPLETION_REPORT.md` for the full
  five-phase campaign writeup.
