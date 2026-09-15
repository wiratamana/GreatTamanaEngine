# Entity-Component-System (ECS)

_Part of [GreatTamanaEngine](../../AGENTS.md)'s contributor conventions. See
[docs/README.md](../README.md) for the full documentation index._

The engine's Scene/World data model lives under `src/ECS/`: `Entity`
(`src/ECS/Entity.h`), `EntityManager` (`src/ECS/EntityManager.h/.cpp`),
`ComponentStorage<T>` (`src/ECS/ComponentStorage.h`), and `Registry`
(`src/ECS/Registry.h`), which owns one of each. This was deliberately rolled
by hand (not via a third-party library like EnTT) so the engine keeps
ownership of its core gameplay data model, the same way its math library
(`src/Math/`) was written from scratch rather than depending on GLM (see
`MathTypes.h`). Follow these rules whenever touching entity/component
lifetime code:

- **Identify entities by handle, never by pointer or string.** `Entity` is a
  cheap 8-byte POD (index + generation), generated automatically by
  `EntityManager::Create()` - calling code never invents/assigns its own id.
  This is the exact same shape and rationale as `GpuResourceHandle` (see
  [GPU Resource Memory Tracking](gpu-resource-memory-tracking.md) above): cheap to copy/store/compare by the
  thousands, and the `generation` field guards against a stale `Entity`
  silently referring to a different entity that was later created in the
  same (reused) slot - `EntityManager::Create()`/`Destroy()` use the exact
  same slot + free-list + generation-bump pattern as
  `GpuMemoryTracker::Track()`/`Untrack()`, on purpose, so there is only one
  such pattern in the codebase to understand, not two subtly different ones.
- **Components are plain data, never GPU/SDL-resource-owning types, and
  never carry virtual behavior of their own.** `Transform`
  (`src/ECS/Components/Transform.h`) is the pattern to copy: fields only,
  plus at most small pure-math helper methods (`LocalToWorldMatrix()`). A
  component that needs a live GPU resource - `MeshRenderer`
  (`src/ECS/Components/MeshRenderer.h`) is the first one - must reference it
  by handle/value data (`MeshHandle`/`PipelineHandle`,
  `src/Renderer/MeshHandle.h`/`PipelineHandle.h`), never by embedding a
  `Buffer`/`RenderTexture`/`Mesh`/`Pipeline`/raw Vulkan handle directly - the
  RAII-owning object stays behind a `ResourcePool<T, HandleT>`
  (`src/Renderer/ResourcePool.h`, owned by `RenderSystem` - see below),
  exactly as GPU resources are already addressed by `GpuResourceHandle`
  rather than a raw pointer.
- **A component that references ANOTHER entity (e.g. `Transform::parent`)
  stays plain data too - the logic that actually WALKS that reference lives
  in a separate free-function module, never on the component itself.**
  `Transform::parent` (an `Entity`, `kInvalidEntity` by default) plus
  `Transform::siblingIndex` are exactly this: plain fields, no different in
  kind from `position`/`rotation`/`scale` above. Resolving a full WORLD
  transform by walking the parent chain, cycle-safe reparenting, and sibling
  reordering all live in `src/ECS/TransformHierarchy.h/.cpp` instead
  (`ComputeWorldMatrix()`/`ComputeWorldTransform()`, `SetParent()`,
  `GetChildren()`/`SetSiblingIndex()`) - free functions that take a
  `Registry&` plus plain `Entity` values, same shape as `RenderSystem`'s own
  ECS-bridging functions below, just bridging ECS-to-ECS instead of
  ECS-to-Renderer. This keeps `Transform` itself trivially copyable/
  Tier-1-testable-by-construction while still allowing genuinely non-trivial
  hierarchy logic (cycle detection, world-position-preserving reparenting) to
  exist somewhere sensible - never add a Registry-dependent method to
  `Transform` (or any other component) directly, follow this same
  free-function pattern instead.
- **`ComponentStorage<T>` is a sparse set, addressed by `Entity::index`
  directly - never a hash lookup.** Adding/removing/querying a component is
  O(1) array indexing (`m_sparse`/`m_dense`), and `Remove()` uses
  swap-with-last to keep the dense array packed for cache-friendly
  iteration - dense iteration order is therefore NOT stable across a
  `Remove()` call, never rely on it. `Registry` picks each component type's
  numeric id via a per-type function-local static counter
  (`detail::ComponentTypeId<T>()`), not `std::type_index`/RTTI, so
  `Registry::Storage<T>()`/`AddComponent<T>()`/etc. stay a plain array
  lookup rather than a hash on every call - the same "no hashing on the hot
  path" philosophy as `GpuMemoryTracker`'s handle-indexed slot array.
- **`Registry::DestroyEntity()` must remove the entity from EVERY pool it
  has ever touched, not just the ones a caller happens to think of.** This
  is why `Registry` keeps a homogeneous `std::vector<std::unique_ptr<IComponentPool>>`
  and calls `IComponentPool::Remove()` (the type-erased virtual, not the
  typed `ComponentStorage<T>::Remove()`) on every pool before destroying the
  entity itself - an entity is never left with a dangling/orphaned component
  in some pool this forgot about. Any new component-holding structure added
  later must go through this same `IComponentPool` path, not invent a
  separate destroy-time cleanup step.
- **A `Registry`/`EntityManager`/`ComponentStorage<T>` is Tier-1-testable by
  construction, and must stay that way.** None of them touch a live
  `VkDevice`/`VmaAllocator`/SDL window - see `tests/ECS/` (`EntityManagerTests.cpp`,
  `ComponentStorageTests.cpp`, `RegistryTests.cpp`) for the pattern to copy
  when adding a new component type or Registry method: hand-built `Entity`
  values and plain component structs are enough, following the same
  Tier-1-testability rule already established
  [below](../../AGENTS.md#testability--regression-safety).
- **Only `RenderSystem` (`src/Game/RenderSystem.h/.cpp`), `MeshInstantiationSystem`
  (`src/Game/Instantiation/MeshInstantiationSystem.h/.cpp`), and `AnimationSystem`
  (`src/Game/Animation/AnimationSystem.h/.cpp`) are allowed to depend on both the ECS
  world (`Registry`/`Transform`/`MeshRenderer`) AND `Renderer`/`Mesh`/
  `Pipeline` - the same "only one layer crosses this boundary" rule this
  file already applies to SDL (see [Coding Guidelines](../../AGENTS.md#coding-guidelines), Clean Architecture:
  only `Application` touches SDL directly). This is not three unrelated
  exceptions: `Game` itself already crossed this boundary directly before
  its own instantiation/animation logic was extracted out of `Game.cpp`
  (see `GameInstantiationRefactorProposal.txt`) - `MeshInstantiationSystem`
  (spawning primitives/imported meshes, built on `PrimitiveGpuCatalog`/
  `MeshAssetGpuCatalog`/`EntityInstantiator`) and `AnimationSystem` (playing
  back skeletal animation, built on `SkeletalRigCache`/`AnimationClipCache`/
  `ResolvedAnimationBindingCache`) are `Game`'s own legitimate dual
  dependency decomposed into two named, focused sub-systems it owns, not a
  new architectural violation. `PhysicsSystem` (`src/Game/Physics/PhysicsSystem.h/.cpp`
  - the verlet-integration-1 campaign's dynamic-bone-chain physics
  orchestrator, see `task_manager/verlet-integration-1/PHASE3_PIPELINE_INTEGRATION_AND_FIXED_TIMESTEP.md`)
  is a FOURTH `Game`-layer orchestrator system, but is deliberately NOT part
  of this dual-dependency list - it depends on the ECS `Registry` (to
  read/write `DynamicChainRig`/`ResolvedAnimationPose`) but never on
  `Renderer`/`Mesh`/`Pipeline`, by design, so it stays independently
  testable and independently schedulable from `AnimationSystem`'s own
  GPU-touching half (`SkinAndUpload()`) - see that phase document's own v3
  Revision Notice for the full rationale. `Renderer` itself must never gain a
  dependency on ECS in either direction - `Renderer::Submit()` takes plain
  `Mat4`s, never an `Entity`/`Registry`.
  `RenderSystem::CollectRenderables(Registry&)` is the pure ECS -> plain-data
  (`DrawCommand`: `MeshHandle`/`PipelineHandle`/`Mat4`, no live Mesh/Pipeline/
  Renderer involved) step - keep it that way when extending it, and put any
  new Renderer-touching logic in `RenderSystem::Draw()` (or a sibling
  non-pure method) instead, so `CollectRenderables()` stays Tier-1-testable
  (see `tests/Game/RenderSystemTests.cpp`). The same "keep the pure part
  pure" discipline applies to the other two: `EntityInstantiator`/
  `MeshVertexPacking`/`MeshMaterialPartitioner` (used by
  `MeshInstantiationSystem`) and the three animation caches above (used by
  `AnimationSystem`) all need nothing but plain data/a `Registry` and are
  Tier-1-tested under `tests/Game/`, while the GPU-touching catalogs
  (`PrimitiveGpuCatalog`/`MaterialTextureGpuCache`/`MeshAssetGpuCatalog`)
  fall into the same "Tier 2, no automated coverage yet" bucket as
  `RenderSystem::Draw()` itself (see
  [Testability & Regression Safety](../../AGENTS.md#testability--regression-safety)
  below).
- **`Camera` (`src/ECS/Components/Camera.h`) never bakes an aspect ratio
  into itself.** `ProjectionMatrix(aspectWidthOverHeight)` always takes the
  aspect ratio as a parameter, resolved fresh by whoever is about to draw
  (`RenderSystem::ResolveActiveCameraViewProjection(Registry&,
  aspectWidthOverHeight)`), because the SAME `Camera` entity can legitimately
  render into multiple differently-sized/shaped targets in the same frame
  (the Editor's "Game" and "Scene" panels, each with their own
  `RenderTexture` - see [Editor Module Structure](editor-module-structure.md) below). Never cache a
  `Camera`'s resolved projection matrix keyed only by the component itself -
  always re-resolve it per render target/aspect ratio. `ViewMatrix()` is
  built from a plain `Transform` (via `Mat4::LookAtLH`, looking down its
  rotated `Vec3::Forward()`) rather than a bespoke eye/target/up API, so a
  camera entity is edited exactly like any other entity (Transform in the
  Inspector) - don't add a separate eye/target/up field set to `Camera`
  itself. `RenderSystem::ResolveActiveCameraViewProjection()` picks the
  FIRST entity (in `ComponentStorage<Camera>` order) with `active == true`
  and falls back to `Mat4::Identity()` if none exists - this is what
  preserves the engine's original "vertices already authored directly in
  clip space" behavior for a scene that hasn't added a `Camera` yet; don't
  change this fallback without checking `Shaders/Triangle.vert`'s
  `pc.viewProj * pc.model * ...` still makes sense for it. `Pipeline`'s one
  push constant range now carries a `model` `Mat4` immediately followed by a
  `viewProj` `Mat4` (128 bytes total - the guaranteed minimum
  `maxPushConstantsSize` on every conformant Vulkan implementation, see
  [Render Target Format Matching](render-target-format-matching.md) above for the same "match the GPU side
  exactly" philosophy applied here) - grow this only by moving to a uniform
  buffer/descriptor set instead of growing the push constant range further,
  since 128 bytes is the only size guaranteed to fit everywhere without a
  per-GPU limit check.

## Relationship to the engine's ECS architecture overview

This document covers the CONTRIBUTOR CONVENTIONS for touching ECS/entity/
component lifetime code (identify-by-handle, plain-data components,
free-function hierarchy walking, sparse-set storage, the `RenderSystem`/
`MeshInstantiationSystem`/`AnimationSystem` dual-dependency exception, etc.).
For the engine's high-level ARCHITECTURE overview of the Scene/World data
model (what the ECS is and how it fits into the six subsystems), see
[docs/architecture/ecs.md](../architecture/ecs.md).
