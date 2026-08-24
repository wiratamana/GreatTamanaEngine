# TODO / Roadmap

Backlog of known limitations, deliberately deferred follow-ups, and
longer-term ideas for GreatTamanaEngine - moved out of `README.md` to keep
that file focused on describing the architecture/status *as it exists
today*, rather than growing indefinitely with everything that *might* come
next. Nothing here is a forgotten bug; each item below was consciously
deferred (see the reasoning under each one) rather than overlooked.

Organized into three buckets: things already called out as deliberate
follow-ups in the codebase's own comments (Editor / Testing), fresh
follow-ups from the Memory Profiler work, and broader engine-roadmap ideas
that haven't been started yet at all.

## Editor / Debug UI

- **Per-entity Hierarchy context menu (Delete/Rename/Duplicate).**
  "Hierarchy" now has a right-click "Create 3D Object" menu
  (`Game::CreatePrimitiveEntity()`, see `README.md`) via
  `ImGui::BeginPopupContextWindow()`, but it opens the same way whether the
  right-click landed on empty space or an existing entity row - there is no
  separate per-entity menu yet (Unity's own right-click-on-a-GameObject
  menu has "Rename"/"Duplicate"/"Delete" alongside its own "3D Object"
  submenu). "Rename" also needs a Name component first (entities are
  identified purely by numeric index today - see
  `ECS/Components/Transform.h`) - a small, self-contained, likely-useful
  addition on its own (also helpful for a future scene file's
  readability). Deliberately deferred rather than folded into the initial
  "Create 3D Object" work - this engine has no way to destroy an entity's
  GPU-side mesh/pipeline registration mid-session yet either
  (`RenderSystem`'s `ResourcePool<Mesh, MeshHandle>`/
  `ResourcePool<Pipeline, PipelineHandle>` support `Remove()`, but nothing
  calls it today), so "Delete" needs that reasoned through first.
- **Click-to-select via ray casting + a Scene-view outline highlight.**
  Today, picking an entity by clicking directly on it inside "Scene" is not
  implemented - selection only happens via "Hierarchy" (see
  `Selection::SelectEntity()`, `src/Editor/Selection.h`). Adding it needs a real ray/triangle
  intersection test against each `MeshRenderer`'s actual mesh data (i.e. this
  engine's first real collider/picking system, since there is no collision/
  physics layer at all yet), plus a Scene-view-only outline post-process
  shader pass to actually highlight whatever gets picked. Deliberately
  deferred as its own follow-up rather than folded into the transform-gizmo
  work (see `README.md`, "Editor / Debug UI") - meaningfully heavier
  (mesh-level intersection math, a new picking/collider abstraction, and a
  new render pass) and orthogonal to it: the gizmo already works fine driven
  purely by a "Hierarchy" selection in the meantime.
- **Command pattern (undo/redo) for Editor edits.** `Selection`
  (`src/Editor/Selection.h`) is now the single gate-keeper for every
  Hierarchy-entity/Project-asset selection change (`SelectEntity()`/
  `SelectAsset()`/`ClearAssetIfPath()`) - deliberately centralized so a
  future `Command`/`ICommand` abstraction (`SelectEntityCommand`, then
  presumably `MoveEntityCommand`/`CreateEntityCommand`/`DeleteEntityCommand`,
  ...) has exactly one choke point to route selection changes through for
  undo/redo, rather than several panels each writing selection state
  directly. Not implemented yet - `Selection` itself is deliberately just
  the gate-keeper, with no undo/redo history of its own.
- **Long-term: replace ImGuizmo with a homegrown transform gizmo.**
  ImGuizmo (`third_party/imguizmo/`, `cmake/FetchImGuizmo.cmake`) currently
  backs the Scene-view translate/rotate/scale gizmo, and works correctly
  today - `IMGUIZMO_RELEASE_TAG` is pinned to a specific commit SHA (not the
  `master` branch) specifically so this stays true: a moving branch could
  silently change ImGuizmo's public API/behavior underneath this engine on a
  future fetch (a real, separate risk from - and compounding - the two
  hand-diagnosed upstream bugs this integration already had to work around: a
  Vulkan-vs-OpenGL clip-space Y convention mismatch, fixed via
  `EditorCamera::GizmoProjection()`; and HandleScale()/HandleRotation()
  freezing an in-progress drag the instant the cursor left the Scene panel,
  fixed via a source patch applied by `_imguizmo_apply_gte_patches()` in
  `cmake/FetchImGuizmo.cmake`). Pinning removes the "silently changes
  underneath us" risk, but not the underlying reason it came up in the first
  place: depending on a second library with its own coordinate/interaction
  conventions to reconcile with, for something this engine's own
  `Math`/`Renderer` already has every primitive needed for (screen-space
  projection, ray-plane intersection, `ImDrawList` rendering via Dear ImGui,
  which stays either way). Rolling a homegrown gizmo (translate first, then
  scale, then rotate - roughly the increasing order of implementation
  difficulty) would fit the same "own the core data model" philosophy
  already applied to `src/Math/` (no GLM) and `src/ECS/` (no EnTT), and
  permanently remove this entire class of integration bug rather than
  continuing to patch around it. Deliberately NOT undertaken now - real,
  multi-day effort better spent on higher-priority engine work at this early
  a stage.

## Memory Profiler

- **Vulkan `VkAllocationCallbacks` host-memory hook.** Every Vulkan
  `vkCreate*`/`vkDestroy*` call can take a custom allocator callback struct
  (`pfnAllocation`/`pfnReallocation`/`pfnFree`, plus internal-allocation
  notification callbacks), the spec-blessed way to see exactly how much HOST
  (CPU) memory the Vulkan driver spends per scope (`VK_SYSTEM_ALLOCATION_
  SCOPE_INSTANCE`/`DEVICE`/`COMMAND`/`OBJECT`/`CACHE`) - a third CPU bucket
  alongside `SdlMemoryTracker`/`ImGuiMemoryTracker` in the "Memory" panel.
  Deliberately NOT done yet: unlike the SDL/ImGui hooks (a single call each
  before that library's first use), this needs EVERY Vulkan object
  creation/destruction across the whole engine (`VulkanInstance`,
  `VulkanSurface`, `VulkanDevice`, `VulkanSwapchain`, `Buffer`,
  `RenderTexture`, `Pipeline`, command pools/buffers, sync objects, ImGui's
  own Vulkan backend, ...) to consistently pass the exact same
  `VkAllocationCallbacks*` (or consistently `nullptr`) - the Vulkan spec
  treats mixing allocators between create/destroy of the SAME object as
  undefined behavior - making this a real, multi-file refactor rather than a
  quick add. Also not a byte-perfect measurement even once done: driver/
  loader-internal bookkeeping and thread stacks aren't guaranteed to route
  through the callback on every implementation. Investigated live (see the
  "Memory" panel's "GPU Heap Budgets" section) and found that `Driver Usage`
  already matches `VMA Allocated` almost exactly with no large unexplained
  gap - i.e. there's no evidence yet of a real CPU-memory blind spot large
  enough to justify this effort; revisit if that ever changes (e.g. a future
  profiling session shows a process-wide CPU RAM number that
  `SdlMemoryTracker` + `ImGuiMemoryTracker` + the general engine heap can't
  account for).
- **`vmaBuildStatsString()` parser for real per-block detail.** The "GPU Heap
  Budgets" section's "VMA Allocated" column currently shows an aggregate
  block/sub-allocation summary per HEAP (`FormatBlockSummary()`,
  `src/Editor/MemoryPanelData.h`) - e.g. "64.00 MB across 2 blocks (3
  sub-allocations)" - but VMA's stable public statistics API
  (`vmaGetHeapBudgets()`/`VmaStatistics`) only reports heap-level totals, not
  which specific resource landed in which specific block. VMA does expose a
  JSON-formatted dump with real per-block/per-allocation detail via
  `vmaBuildStatsString()`, which would let the panel show something like
  "Block 0: GameView + SceneView (images)" / "Block 1: TriangleMesh
  (buffer)" directly, rather than requiring the Vulkan buffer/image
  `bufferImageGranularity` explanation to be inferred from the aggregate
  counts. Deliberately NOT done yet - parsing/rendering that JSON blob inside
  an ImGui panel is a meaningfully bigger addition than the current
  aggregate summary, and the aggregate view already answers "does
  GpuMemoryTracker account for everything the driver reports" without it.

## Testing

- **Tier 2 (GPU-backed) integration test fixture.** `Buffer`/`RenderTexture`/
  `Pipeline`/`GpuResourceFactory` all need a real `VkDevice`+`VmaAllocator`
  to construct at all, and today's `VulkanDevice::PickPhysicalDevice()`
  additionally requires a real `VkSurfaceKHR` (to query present support),
  which normally comes from an actual OS window (`Window`+`VulkanSurface`).
  A future `GpuTestFixture` could build a headless `VkSurfaceKHR` via
  `VK_EXT_headless_surface` (supported by most desktop drivers, and by
  software implementations like Mesa lavapipe/SwiftShader) instead of a real
  `Window`, then `GTEST_SKIP()` the whole fixture at runtime if instance/
  device creation fails - i.e. those tests would still PASS (skipped, not
  failed) on a GPU-less CI runner, and only actually exercise the GPU on a
  developer machine or a self-hosted runner that has one. Needs its own
  fixture/CMake wiring (`GTE_BUILD_GPU_TESTS` or similar) rather than being
  bolted onto Tier 1 - see `tests/CMakeLists.txt`. Explicitly NOT a blocker
  for any other work; the current development machine doesn't even support
  headless mode.

## Engine Roadmap (not yet started)

Broader, longer-horizon ideas for moving the engine past "tech demo with a
great editor" toward something that can hold a real scene/game - none of
these have any code written yet; listed here in roughly the order they'd
unblock the most follow-on work:

- **Scene serialization (save/load a scene to/from a file, e.g. JSON).**
  Right now the Editor lets you live-edit entities via the gizmo/Inspector,
  and can now also spawn new ones from scratch via "Hierarchy" -> "Create 3D
  Object" (`Game::CreatePrimitiveEntity()`, see `README.md`) - but there is
  still no way to persist or reload the result; `Game.cpp`'s own demo
  entities remain hardcoded in C++. Likely the single highest-leverage next
  engine-level feature: a pure `Registry` <-> text-format read/write, fitting
  this engine's existing Tier-1-testability doctrine, that turns every other
  item below into something you can actually author instead of hardcode.
  "Create 3D Object" is exactly the tool needed to build a non-trivial test
  scene to exercise save/modify/load against once this lands.
- **A minimal asset pipeline: real mesh loading + a real, shader-bindable
  texture from a *.gta.** The IMPORT half of both texture AND mesh handling
  now exists - dropping a PNG/JPG into "Project" decodes+re-encodes it as
  KTX2 and wraps it as a `*.gta` (`AssetType::Texture`), and dropping a
  MikuMikuDance `.pmx` model parses its vertex geometry and wraps it as a
  `*.gta` (`AssetType::Mesh`, see `src/Assets/PmxLoader.h`/`MeshFile.h`/
  `AssetImporter.h`) - both tracked by `AssetDatabase`. Both also now have a
  DISPLAY path good enough for the Editor's own "Inspector" panel
  (`Ktx2Decoder.h`'s `DecodeKtx2ToRgba8()` for textures,
  `AssetPreviewMesh.cpp`'s own small Vulkan pipeline for meshes) - but there
  is still NO GAMEPLAY consumption path for either: nothing yet lets a
  `MeshRenderer`/material reference a `*.gta` texture by `Guid` and have it
  bound to a shader descriptor (today's rendering is still unlit
  vertex-color only - see `Shaders/Triangle.vert/.frag` - no
  texture-sampling pipeline variant or descriptor-set plumbing exists), and
  nothing yet lets a `*.gta` Mesh asset be spawned as an actual
  `Transform`+`MeshRenderer` entity the way `Game::CreatePrimitiveEntity()`
  spawns a built-in primitive shape. The blocker for the latter is the same
  one called out below: `Mesh`/`GpuResourceFactory`/`Renderer::Submit()`/
  `FrameRecorder` only ever build/draw a plain, NON-INDEXED vertex buffer
  (see `Mesh.h`) - a real imported mesh (PMX or a future OBJ/glTF) needs
  indexed drawing for both memory and CPU-generation-time reasons, unlike
  `PrimitiveMeshGenerator`'s current duplicated-per-triangle vertices.
  `AssetPreviewMesh` (this session's Inspector 3D viewer) already proves
  indexed rendering works on this Vulkan backend, but deliberately as its
  OWN separate, Editor-only pipeline/vertex layout (position+normal, no
  color/UV) - it does NOT extend the shared `Mesh`/`Pipeline`/`Vertex`
  types, so none of this plumbing is reusable for gameplay as-is. Needs: (1)
  real index-buffer support added to the shared `Mesh`/`Renderer::CreateMesh()`/
  `Renderer::Submit()`/`FrameRecorder` path (`vkCmdDrawIndexed`, mirroring
  what `AssetPreviewMesh` already does bespoke); (2) a gameplay vertex
  format that actually carries normal/UV (today's shared `Vertex` is
  position+color only - see `Vertex.h`'s own comment anticipating this); (3)
  a basic textured/lit pipeline (descriptor set + sampler, following the
  same `Renderer::ColorFormat()`-style "single source of truth" discipline
  used elsewhere) a `MeshRenderer` can actually be drawn with; and (4) a
  "spawn an entity from an imported `*.gta` Mesh asset" entry point,
  `Game`-side, mirroring `CreatePrimitiveEntity()`. A future OBJ/glTF loader
  would produce the exact same `MeshData`/`*.gta` shape `PmxLoader` already
  does (see `src/Assets/MeshData.h`), so none of this work is PMX-specific.
- **Real MMD skinning/animation runtime (pose evaluation, morph blending,
  physics simulation, `.vmd` motion playback).** The DATA side of this is no
  longer a gap: `PmxLoader.h` now extracts per-vertex skin weights (all of
  BDEF1/BDEF2/BDEF4/SDEF/QDEF - `MeshData::skinWeights`), the full bone
  hierarchy including IK chains (`Assets/SkeletonData.h`), all seven morph
  kinds (`Assets/MorphData.h`), and rigid bodies/joints
  (`Assets/PhysicsData.h`), all round-tripped through `Assets/RigFile.h`
  into the `*.gta`'s metadata section - see README.md's own entry for the
  full rundown. What's still missing is the RUNTIME that actually DOES
  anything with that data: real bone-deformed rendering (a T-posed/A-posed
  import actually posing/animating) needs an IK solver (evaluating the
  `Bone::ikLinks`/`ikTargetBoneIndex`/`ikAngleLimitRadians` this session
  added) and a morph blender (applying `Morph::positionOffsets`/etc. at a
  runtime weight) - saba's own `PMXModel`/`MMDNode`/`MMDIkSolver`/`MMDMorph`
  layer does this and could be a reference, though it also expects its own
  `MMDPhysics` (rigid-body jiggle bones, physics-after-deform) wired in,
  which needs Bullet (NOT fetched today - deliberately out of scope, see
  `cmake/FetchSaba.cmake`'s header comment) to actually simulate the
  `RigidBody`/`Joint` data this session extracts. A `.vmd` motion file
  loader (`saba::VMDAnimation`/`VMDCameraAnimation`, also not vendored yet)
  would be needed to actually drive bones/morphs over time. A real
  skeletal-animation vertex format/shader (bone indices + weights, GPU
  skinning or CPU pre-skin, consuming `MeshData::skinWeights` directly)
  would also be needed on the rendering side - today's engine has no
  skinning concept anywhere in `Renderer`/`Pipeline`/`Vertex`. Still a
  large, multi-session effort even with the data-extraction half done.
- **PMX material/texture import.** `MeshData`/`MeshFile` carry no material
  data at all - a `.pmx`'s per-face material list (diffuse/specular/
  ambient color, a diffuse texture reference, optional sphere-map/toon
  shading - `saba::PMXMaterial`, read by `PMXFile` but discarded by
  `PmxLoader`) is dropped entirely on import today, so an imported model's
  actual surface appearance (skin/hair/cloth texturing) can't be
  reconstructed even once real gameplay rendering exists - only its bare
  grey-clay shape can (see `AssetPreviewMesh`'s fixed-color shading). Needs
  a `MaterialData`-shaped extension to `MeshData`/`MeshFile` (per-material
  index ranges into the index buffer, plus texture references resolved
  against the `.pmx`'s own sibling texture files/`AssetDatabase`), and ties
  into the same textured-pipeline work above.
- **Verify MMD's own coordinate/winding convention once wired into real
  gameplay rendering.** `PmxLoader.h` is explicit that it performs NO axis/
  winding remapping - MMD's own authoring convention may not exactly match
  this engine's left-handed/Y-up/Z-forward one (`src/Math/MathTypes.h`).
  `AssetPreviewMesh`'s Inspector viewer has sidestepped ever needing to know
  by disabling backface culling entirely (`VK_CULL_MODE_NONE` - see its own
  comment) and using a fixed, non-interactive camera, so a genuinely wrong
  winding/axis convention could still be silently masked today. This needs
  actually checking (and, if needed, correcting - most likely a one-time
  fix-up inside `PmxLoader.cpp` itself, not per-consumer) before any
  gameplay-facing rendering (culling enabled, arbitrary camera angles) is
  built on top of it.
- **Smarter fallback than a hardcoded up-vector for missing normals.**
  `AssetPreviewMesh::EnsureMeshUploaded()` substitutes `Vec3::Up()` for
  every vertex when a decoded `MeshData`'s `normals` array doesn't match its
  `positions` count (defensive - saba's own PMX parser always emits one
  normal per vertex today, so this doesn't trigger in practice yet, but a
  future mesh source might genuinely lack normals). Computing real face-
  weighted vertex normals from the index buffer's own winding would be a
  more correct fallback than a single fixed direction, whenever that
  actually comes up.
- **Transform parenting / hierarchy.** `Transform`
  (`src/ECS/Components/Transform.h`) is flat today - no parent/child
  relationship, which is why ImGuizmo's `LOCAL` space is currently identical
  to `WORLD` (see `README.md`). Adding a parent `Entity` reference (or
  index) with `LocalToWorldMatrix()` walking up the chain is relatively
  contained and unblocks real scene graphs.
- **Basic collision/physics primitives.** No collision/physics layer exists
  at all yet - a prerequisite for click-to-select raycasting (above) and for
  any real gameplay. Start minimal: AABB/sphere colliders + a simple
  broad-phase, rather than reaching for a full physics engine immediately.
- **Someday/maybe:** audio, and a gameplay scripting layer (today, all
  gameplay is hand-authored C++ entities in `Game.cpp`). Not sequenced yet -
  revisit once the items above land.
