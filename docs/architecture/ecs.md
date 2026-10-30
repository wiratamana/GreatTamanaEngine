# Entity-Component-System
> The world data model under `src/ECS/`: entities, components, hierarchy and reflection. For anyone adding components or systems.
> Verified against code: 2026-10-30

## TL;DR
- The ECS is hand-written (no EnTT). Storage is a sparse set per component type.
- An `Entity` is an 8-byte handle: `index` + `generation`. Never a pointer or a string.
- Components are plain data. Logic lives in free functions and in systems.
- `Transform` is local (parent-relative). Use `TransformHierarchy.h` for world matrices and reparenting.
- `Registry` is not thread-safe. Only the main thread mutates it.

## Diagram
```text
Registry
 ├─ EntityManager            slots + free list + generation bump
 └─ pools[typeId]            one ComponentStorage<T> per component type
        sparse[entity.index] ──► dense[]  (components)  +  entities[]

Free functions on Registry&           Systems (src/Game/)
 ├─ TransformHierarchy.h              ├─ RenderSystem            ECS ↔ Renderer
 ├─ EntityQuery.h                     ├─ MeshInstantiationSystem ECS ↔ Renderer
 └─ HierarchySnapshot (cached)        ├─ AnimationSystem         ECS ↔ Renderer
                                      └─ PhysicsSystem           ECS only
```

## How it works

### Entities
- `EntityManager::Create()` reuses a free slot and bumps its `generation`. `generation == 0` means invalid.
- `kInvalidEntity` is the invalid handle. `Registry::IsAlive(e)` rejects stale handles.
- `EntityMarkSet` is an O(1) set of entities. `Clear()` only bumps an epoch. Used for per-frame claims and exclusions.

### Components
| Component | Holds | Saved in scene |
|---|---|---|
| `Transform` | local `position`, `rotation`, `scale`, `parent`, `siblingIndex` | `position`, `rotation`, `scale` only |
| `Name` | display string `value` | ✅ |
| `Camera` | `fovYDegrees` 60, `nearZ` 0.1, `farZ` 1000, `active` | ✅ |
| `DirectionalLight` | `color`, `illuminanceLux` 100000, `active` | ✅ |
| `PrimitiveSource` | `type` (cube, sphere, ...) | ✅ |
| `MeshRenderer` | `MeshHandle`, `PipelineHandle`, `MaterialTextureHandle` | ❌ session-only GPU handles |
| `MeshAssetSource` | `gtaPath` | ❌ saved as `asset_guid` |
| `SkeletalAnimator` | mesh and clip path, `frame`, `speed`, `playing`, `loop` | ❌ |
| `ResolvedAnimationPose` | per-frame bone offsets | ❌ derived |
| `DynamicChainRig` | physics chain state | ❌ simulation state |

Files are in `src/ECS/Components/`. Details of saving: [Scene serialization](scene-serialization.md).

### Hierarchy
- `ComputeWorldMatrix(registry, e)` walks up the parents: `parentWorld * local`.
- `SetParent(child, parent, worldPositionStays = true)` returns `false` on a cycle or a missing `Transform`.
- `GetChildren()` scans all transforms. In loops, use `HierarchySnapshot`. It rebuilds only when the hierarchy changes.
- `DestroyEntityAndDescendants()` removes a whole subtree.
- `AssignHierarchy()` sets many parents at once. Scene load uses it.

### Names
- `FindEntityByName` returns the first match in dense order. That order is not stable.
- `MakeUniqueEntityName` gives duplicates a suffix: `Name (1)`, `Name (2)`.

### Component type ids
- `Registry` finds a pool by `detail::ComponentTypeId<T>()`.
- The id comes from `typeid(T).name()` through one process-wide table (`ResolveComponentTypeIdByName`, in `Registry.cpp`).
- One table means the exe and every project DLL agree on the id of the same type.

### Reflection
- `ComponentTypeRegistry::Instance()` holds a descriptor per saved type.
- Built-in descriptors: `Transform`, `Name`, `Camera`, `DirectionalLight`, `PrimitiveSource`.
- Register a type with `RegisterComponentType<T>("Name", { GTE_REFLECT_FIELD(T, f), ... })`.
- Descriptors are sorted by name, so scene JSON key order is stable.
- Project assemblies register their own types. They must unregister them before the DLL unloads.

## Key files
| File | Role |
|---|---|
| `src/ECS/Entity.h` | Handle type, `kInvalidEntity` |
| `src/ECS/EntityManager.*` | Slot allocation, `IsAlive` |
| `src/ECS/ComponentStorage.h` | Sparse set, `IComponentPool` |
| `src/ECS/Registry.*` | Owns manager and pools, type id table |
| `src/ECS/EntityMarkSet.h` | Epoch-based entity set |
| `src/ECS/TransformHierarchy.*` | World matrix, `SetParent`, siblings |
| `src/ECS/HierarchySnapshot.*` | Cached parent to children table, `Revision()` |
| `src/ECS/EntityQuery.*` | Find by name, unique names |
| `src/ECS/Reflection/` | Component descriptors for save and load |
| `src/Game/RenderSystem.*` | `CollectRenderables`, camera resolve, `Draw` |

## Rules
1. Refer to entities by `Entity`. Never invent an id. `EntityManager` makes them.
2. Components are plain data. No GPU or SDL ownership. No virtual functions.
3. A component that needs a GPU object holds a handle (`MeshHandle`, ...). The object sits in a `ResourcePool` owned by `RenderSystem`.
4. Code that walks entity references is a free function (`TransformHierarchy.h`). Never add a `Registry`-dependent method to a component.
5. Never write `Transform::parent` directly. Use `SetParent` or `AssignHierarchy`.
6. `Registry::DestroyEntity` removes the entity from every pool through `IComponentPool::Remove`. New storage must use `IComponentPool`.
7. `Camera` never stores an aspect ratio. Call `ProjectionMatrix(aspect)` per render target. The first active camera in storage order wins. No camera gives identity.
8. `Add<T>` overwrites in place. A reference into a pool dies on `Add` or `Remove`.
9. Dense iteration order changes after `Remove` (swap with last). Never rely on it.
10. Never mutate `Registry` from a job. See [Job system](../conventions/job-system.md).
11. Only `RenderSystem`, `MeshInstantiationSystem` and `AnimationSystem` may know both ECS and `Renderer`. `PhysicsSystem` must not touch `Renderer`, `Mesh` or `Pipeline`.
12. `Renderer::Submit` takes plain `Mat4`. Never `Entity` or `Registry`.
13. Keep `RenderSystem::CollectRenderables` and `ResolveActiveCameraViewProjection` pure (Registry only). Put GPU work in `Draw`.
14. Custom component types in a project DLL: call `Registry::ResetStoragePool<T>()` before the DLL unloads. The pool's vtable lives in the DLL.

## How to add a component
1. Add a struct in `src/ECS/Components/` with plain fields and defaults.
2. If it must be saved, register it in `BuiltinComponentReflection.cpp`. See [Scene serialization](scene-serialization.md).
3. Add a Tier-1 test under `tests/ECS/`. Hand-built entities are enough. No GPU needed.

## Gotchas
- `Transform::parent` and `siblingIndex` are not reflected. The scene file stores them per entity record.
- The parent walk stops at a depth of 1,000,000. A broken cycle gives a wrong but finite matrix, not a hang.
- `Game::EnsureDefaultCameraExists` adds a camera at (0, 0, -5) when none exists. It runs on every `Render()` and after scene load.
- `SetEntityTrs` (HTTP) sets local TRS. Rotation is Euler degrees.
- Two entities from the same `*.gta` share one GPU `Mesh`.

## Related
- [Architecture overview](overview.md) · [Math](math.md) · [Scene serialization](scene-serialization.md)
- [Animation and physics](animation-physics.md) · [Rendering](rendering.md)
- [Job system](../conventions/job-system.md) · [Project assembly](../guides/project-assembly.md)
