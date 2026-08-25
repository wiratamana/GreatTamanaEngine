# Testing

Every engine source file except `src/main.cpp` is compiled into a static
library, `gte_core` (see `CMakeLists.txt`) - both the real executable
(`GreatTamanaEngine`) and the unit test suite (`GreatTamanaEngineTests`,
below) link against it, so tests always exercise the exact same compiled
engine code the shipped `.exe` does.

`GreatTamanaEngineTests` (`tests/`) is a [GoogleTest](https://github.com/google/googletest)
suite, gated by the `GTE_BUILD_TESTS` CMake option (`ON` by default, same
"zero-touch when off" philosophy as `GTE_ENABLE_EDITOR`). GoogleTest itself
is not vendored either - it's fetched/built the same way as SDL3/Vulkan/VMA/
ImGui (see `cmake/FetchGTest.cmake`), staged into `third_party/googletest/`
and gitignored.

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

(or just run `build\Debug\GreatTamanaEngineTests.exe` directly)

Every test currently in the suite is deliberately "Tier 1": pure logic, with
no live Vulkan device, GPU, or SDL window/video subsystem required to run -
safe on any machine/CI runner, GPU or not:

- `Memory/GpuResourceHandleTests.cpp` / `Memory/GpuMemoryTrackerTests.cpp` -
  `GpuResourceHandle` value semantics and `GpuMemoryTracker`'s Track()/
  Untrack()/totals/snapshot bookkeeping, including generation-counted
  handle-reuse safety and (Editor builds only) debug names.
- `Memory/SdlMemoryTrackerTests.cpp` - `SdlMemoryTracker`'s byte-counting
  wrapper around `SDL_malloc`/`calloc`/`realloc`/`free`, exercised by calling
  those functions directly (no `SDL_Init()`/video subsystem needed - see
  AGENTS.md, "CPU Dependency Memory Tracking"). Always built (SDL is used
  regardless of `GTE_ENABLE_EDITOR`).
- `Assets/AssetTypesTests.cpp` - `Guid` value semantics/generation/string
  round-trip, `AssetFlags` bit ops, and `AssetTypeFromExtension()`.
- `Assets/GtaFileTests.cpp` - `WriteGtaFile()`/`ReadGtaHeader()`/
  `ReadGtaFile()` round-tripping the `*.gta` binary format's 64-byte header +
  metadata/payload sections.
- `Assets/AssetDatabaseTests.cpp` - `AssetDatabase`'s directory scan
  (`RefreshFromDirectory()`), import (`ImportAsset()`/`ImportRawFile()`), and
  `Guid`/path lookups.
- `Assets/Ktx2EncoderTests.cpp` / `Assets/Ktx2DecoderTests.cpp` -
  `EncodeImageBytesToKtx2()`/`EncodeImageFileToKtx2()` and
  `DecodeKtx2ToRgba8()` round-tripping a decoded source image through an
  in-memory KTX2 container (via the statically-linked libktx) and back,
  pixel-exact, plus malformed/empty input rejection - no GPU device
  involved.
- `Assets/AssetImporterTests.cpp` - `IsImportableAsKtx2Texture()`/
  `IsImportableAsMeshAsset()`'s extension gating, and `ImportAssetFile()`'s
  PNG/JPG -> KTX2 -> `*.gta` and `.pmx` -> Mesh -> `*.gta` conversions vs.
  their respective plain-copy-fallback behavior on a corrupt source file.
- `Assets/PmxLoaderTests.cpp` - `LoadPmxModel()`'s extraction of vertex
  positions/normals/UVs/triangle indices from a MikuMikuDance `.pmx` model
  file, against a hand-built minimal binary fixture (same "construct the
  exact binary format by hand" approach as `GtaFileTests.cpp`), plus one
  machine-gated smoke test against a real, non-vendored MMD model
  (`GTEST_SKIP()`s everywhere that model isn't present on disk).
- `Assets/MeshFileTests.cpp` - `EncodeMeshDataToBytes()`/
  `DecodeMeshDataFromBytes()` round-tripping the `*.gta` `AssetType::Mesh`
  payload's binary layout, plus malformed/truncated/empty input rejection.
- `Input/InputStateTests.cpp` - `InputState`'s held/just-pressed/
  just-released/delta semantics, fed with hand-built `gte::Event` values.
- `Application/EventTranslatorTests.cpp` - the `SDL_Event` -> `gte::Event`
  mapping, using hand-built `SDL_Event` values (no `SDL_Init()` needed).
- `Renderer/VertexTests.cpp` - `Vertex`'s Vulkan binding/attribute
  description metadata.
- `Renderer/ResourcePoolTests.cpp` - the generic `ResourcePool<T, HandleT>`
  slot-map's insert/remove/lookup and generation-guard semantics (backing
  `MeshHandle`/`PipelineHandle` - see `README.md`, "Entity-Component-System").
- `Math/Vec2Tests.cpp` / `Math/Vec3Tests.cpp` - `Vec2`/`Vec3` arithmetic and
  geometric ops (dot/cross/length/normalize/lerp), using exact hand-computed
  values.
- `Math/Mat4Tests.cpp` - `Mat4` multiply/transpose/inverse/`LookAtLH`/
  `PerspectiveFovLH_ZO`, using hand-verified exact values.
- `Math/QuatTests.cpp` - `Quat` multiply/slerp/nlerp/axis-angle/matrix and
  Euler round-trip conversions.
- `ECS/EntityManagerTests.cpp` - `EntityManager`'s Create()/Destroy()/
  IsAlive() handle allocation/recycling/generation semantics.
- `ECS/ComponentStorageTests.cpp` - `ComponentStorage<T>`'s sparse-set
  Add()/Remove()/Has()/Get() and swap-with-last removal, fed with
  hand-built `Entity` values.
- `ECS/RegistryTests.cpp` - `Registry` gluing `EntityManager` +
  `ComponentStorage<T>` together: multi-component-type entities,
  DestroyEntity() clearing every pool, and `Transform`.
- `ECS/TransformHierarchyTests.cpp` - `ECS/TransformHierarchy.h`'s
  `ComputeWorldMatrix()`/`ComputeWorldTransform()` (parent-chain composition,
  including a dead/dangling parent falling back gracefully), `IsDescendantOf()`
  (cycle detection), `SetParent()` (cycle rejection, missing-`Transform`
  rejection, and both the world-position-preserving and as-authored
  reparenting behaviors), and `GetChildren()`/`SetSiblingIndex()`/
  `MoveToLastSibling()` (sibling ordering/reordering) - all pure functions
  needing nothing but a `Registry`.
- `ECS/CameraTests.cpp` - `Camera`'s pure math helpers, `ProjectionMatrix()`/
  `ViewMatrix()`, checked directly against `Mat4::PerspectiveFovLH_ZO()`/
  `LookAtLH()`.
- `Game/RenderSystemTests.cpp` - `RenderSystem::CollectRenderables()` (the
  pure ECS -> `DrawCommand` step: every entity with a `MeshRenderer` becomes
  one draw command, using `ECS/TransformHierarchy.h`'s `ComputeWorldMatrix()`
  - its `Transform`'s world matrix, composed up its parent chain if any, if
  present) and `RenderSystem::ResolveActiveCameraViewProjection()` (the pure
  ECS -> camera view-projection step: the first entity with an active
  `Camera` becomes a combined view-projection matrix resolved from that
  camera's own full world transform - so a parented `Camera` follows its
  parent too - `Mat4::Identity()` if none exists) - both
  need nothing but a `Registry`, no live Renderer/GPU device at all.
- `Game/EntityInstantiatorTests.cpp` - `EntityInstantiator.h`'s
  `Instantiate()` (`src/Game/Instantiation/`): hand-built `EntityBlueprint`
  values in, asserting the resulting entity/component (`Transform`/
  `MeshRenderer`/`Name`/`MeshAssetSource`) and parent-child structure - no
  Renderer/GPU device involved.
- `Game/MeshVertexPackingTests.cpp` - `PackMeshVertices()`/`PackMeshVertexUvs()`
  (`src/Game/Instantiation/MeshVertexPacking.h`): exact field-copy
  correctness, plus the missing-normals/missing-uvs fallback behavior - the
  two pure functions that replaced four hand-copied vertex-packing loops
  across `Game.cpp`.
- `Game/MeshMaterialPartitionerTests.cpp` - `PartitionMeshMaterials()`
  (`src/Game/Instantiation/MeshMaterialPartitioner.h`): materials summing
  exactly to the mesh's index count, materials summing to LESS (leftover
  bucket), a corrupt file whose materials over-claim past the index count
  (clamping), and zero materials at all (everything falls into one leftover
  bucket).
- `Game/SkeletalRigCacheTests.cpp` - `SkeletalRigCache`'s
  (`src/Game/Animation/SkeletalRigCache.h`) Register()/TryGet() register/
  lookup/miss behavior over plain `SkinnedMeshData`.
- `Game/AnimationClipCacheTests.cpp` - `AnimationClipCache`'s
  (`src/Game/Animation/AnimationClipCache.h`) `GetOrLoad()`/`TryGet()`
  against a real temp-directory `*.gta` `AssetType::Animation` file (same
  convention as `Assets/AssetDatabaseTests.cpp`), including the wrong-
  asset-type and missing-file failure cases.
- `Game/ResolvedAnimationBindingCacheTests.cpp` - `ResolvedAnimationBindingCache`'s
  (`src/Game/Animation/ResolvedAnimationBindingCache.h`) cache hit/miss
  behavior with the new `AnimationBindingKey` struct key (replacing the old
  hand-concatenated `meshPath + '\x1F' + animationPath` string key),
  including a regression case proving two different mesh/animation pairs
  can never collide the way a naive string join could.
- `Animation/BoneChainResolverTests.cpp` - the shared, generic "walk a
  per-bone single-index chain, cycle-safe" primitive
  (`src/Animation/BoneChainResolver.h`) `SkeletonPose`/`AppendBoneSolver`/
  `IkSolver` all build on: `ResolveBoneChain()`'s whole-skeleton memoized
  walk (parent-chain accumulation, each node resolved exactly once, a
  cyclic chain terminating with a root-value fallback instead of hanging)
  and `ResolveSingleBoneChain()`'s per-call, deliberately non-memoized
  single-bone query (reflecting an external state change between two
  successive calls, out-of-range/cyclic input degrading gracefully) - pure
  generic logic, with no bone-specific behavior of its own.
- `Animation/BonePoseMathTests.cpp` - `ComputeBoneLocalMatrix()`'s
  (`src/Animation/BonePoseMath.h`) shared bind-relative local-transform
  formula, the one both `SkeletonPose.cpp` and `IkSolver.cpp` build their
  own world-matrix walk on top of: a root bone's local matrix is its own
  bind position plus its offset, a child bone's is relative to its
  parent's bind position, and a self-referencing/out-of-range parent index
  is treated as "no parent" rather than misbehaving.
- `Animation/SkeletonPoseTests.cpp` - `ComputeSkinningMatrices()`'s
  (`src/Animation/SkeletonPose.h`) forward-kinematics bone-pose evaluation:
  all-identity offsets produce identity matrices, a rotated parent correctly
  swings a child bone, a translation offset moves a bone relative to its own
  bind pose, and a malformed/cyclic parent chain terminates and produces
  finite (non-NaN/Inf) matrices rather than hanging - no ECS/GPU/Renderer
  involved.
- `Animation/IkSolverTests.cpp` - `SolveIkChains()`'s
  (`src/Animation/IkSolver.h`) Cyclic-Coordinate-Descent IK solve: a
  non-IK bone's offsets are left untouched, moving an IK target bone
  genuinely bends a 2-bone thigh/knee chain and the effector converges onto
  the target (checked against `ComputeSkinningMatrices()`'s own resolved
  world positions), a configured per-link angle limit (e.g. a knee
  restricted to one axis) is respected, and malformed/out-of-range IK data
  (bad target/link indices) doesn't crash or hang.
- `Animation/AppendBoneSolverTests.cpp` - `ApplyAppendInheritance()`'s
  (`src/Animation/AppendBoneSolver.h`) PMX append/grant bone-rotation
  inheritance: a non-append bone is left untouched, a full-weight (1.0)
  append-rotate bone ends up with exactly its source bone's rotation (the
  real-world "D-bone" rig pattern this feature exists for - see the file's
  own header comment), a partial weight blends strictly between identity
  and the source rotation, append-translate adds a weighted-scaled source
  translation, a negative weight produces an inverse-like rotation (a
  shoulder-cancel bone), a cascading append chain (A -> B appends from A ->
  C appends from B's own already-appended total) resolves in dependency
  order rather than a blind flat pass, and malformed/self-referencing
  append data doesn't crash or hang.
- `Animation/MotionSamplerTests.cpp` - `GroupAndSortBoneTracksByName()`/
  `ResolveBoneTracksToSkeleton()`/`SampleBoneTrack()`/`SampleAnimationPose()`
  (`src/Animation/MotionSampler.h`): per-bone frame sorting, the bone-NAME
  resolution between a motion and a skeleton that tolerates a mismatch in
  EITHER direction (an unmatched skeleton bone stays at bind pose, an
  unmatched motion track is simply dropped - the "bones/weights in the
  animation file and the model file don't necessarily match" problem this
  module exists to handle), and keyframe interpolation/clamping at the
  track's edges.
- `Animation/AnimationPoseEvaluatorTests.cpp` - `EvaluateAnimatedSkinningPose()`
  (`src/Animation/AnimationPoseEvaluator.h`), the one function that composes
  `SampleAnimationPose()` -> `SolveIkChains()` -> `ApplyAppendInheritance()` ->
  `ComputeSkinningMatrices()` in that exact, correctness-critical fixed order
  (previously reproduced by hand inside `Game::UpdateSkeletalAnimators()`,
  `src/Game/Game.cpp`, the only call site): matches calling the four steps
  manually in the documented order, a genuine ORDERING regression test (an
  appended "D-bone" must inherit its source's IK-SOLVED rotation, not its
  raw bind-pose one - this fails if a future edit ever reorders IK solving
  and append inheritance), and a skeleton with neither IK nor append bones
  still produces plain forward kinematics.
- `Animation/VertexSkinningTests.cpp` - `SkinVertices()`
  (`src/Animation/VertexSkinning.h`)'s CPU per-vertex bone blending:
  no-skin-weights-at-all leaves vertices at bind pose, BDEF1/BDEF2-shaped
  weights apply/blend the expected bone transforms, unused influence slots
  are ignored regardless of weight type, and no-valid-influence-at-all
  degrades to the bind pose rather than collapsing to the origin.
- `Editor/EditorCameraTests.cpp` - `EditorCamera`'s pure pan/dolly/rotate
  math and pitch clamping (`src/Editor/EditorCamera.h`) - no ImGui/SDL/
  Vulkan involved despite living under `src/Editor/`. Only built when
  `GTE_ENABLE_EDITOR` is `ON` (see `tests/CMakeLists.txt`), since
  `EditorCamera` itself is only compiled into `gte_core` then - same
  "zero-touch when off" rule as the Editor module itself.
- `Editor/MemoryPanelDataTests.cpp` - the Editor "Memory" panel's pure
  data-shaping logic (`BuildMemoryRows()`/`FormatBytes()`/`ToString()`,
  `src/Editor/MemoryPanelData.h`) - sorting/naming/byte-formatting only, no
  ImGui/Renderer/live GPU device involved. Only built when `GTE_ENABLE_EDITOR`
  is `ON`, same reason as `Editor/EditorCameraTests.cpp` above. Also covers
  `BuildHeapBudgetRows()` (reshapes `VmaBudget` entries into plain rows for
  the panel's "GPU Heap Budgets" section - no live `VmaAllocator` needed) and
  `FormatBlockSummary()` (the "VMA Allocated" column's block/sub-allocation
  count wording, including singular vs. plural phrasing).
- `Editor/ImGuiMemoryTrackerTests.cpp` - `ImGuiMemoryTracker`'s byte-counting
  wrapper around Dear ImGui's own `MemAlloc()`/`MemFree()`, exercised by
  calling those functions directly (no live `ImGuiContext`/window needed -
  see AGENTS.md, "CPU Dependency Memory Tracking"). Only built when
  `GTE_ENABLE_EDITOR` is `ON`, same reason as `Editor/EditorCameraTests.cpp`
  above.

Testing `Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory` properly
needs a real `VkDevice`+`VmaAllocator` (and, as `VulkanDevice` is written
today, a real `VkSurfaceKHR` to query present support) - a "Tier 2" of
GPU-backed integration tests, most likely via a headless `VK_EXT_headless_surface`-based
fixture that `GTEST_SKIP()`s cleanly on a GPU-less machine, is a documented
follow-up (see the comment in `tests/CMakeLists.txt`) rather than
implemented yet. `src/Editor/AssetPreviewMesh.cpp`/`AssetPreviewTexture.cpp`
(the Inspector's live texture/3D-mesh previews) fall into this same
untested-for-now Tier 2 bucket, for the same reason - both need a real
`VkDevice`/`ImGuiContext` to render anything at all, so they're presently
only verified manually (see `README.md`, "Editor / Debug UI").

See `README.md` for the overall architecture and `BUILDING.md` for how to
build the engine itself.
