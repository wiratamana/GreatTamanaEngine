# Entity-Component-System (ECS)

_Part of [GreatTamanaEngine](../../README.md)'s architecture docs. See
[docs/README.md](../README.md) for the full documentation index._

The engine's Scene/World data model lives under `src/ECS/`: `Entity`
(cheap, generational index+id, never a pointer/string), `EntityManager`
(id allocation/recycling), `ComponentStorage<T>` (a sparse-set pool per
component type), and `Registry` (owns one of each). Rolled by hand rather
than via a third-party library (EnTT), the same "own the core data model"
choice as `src/Math/` not depending on GLM. `Transform`
(`ECS/Components/Transform.h`), `MeshRenderer`
(`ECS/Components/MeshRenderer.h`), `Camera` (`ECS/Components/Camera.h`), and
`Name` (`ECS/Components/Name.h` — a single optional `std::string value`,
used purely as a display label wherever one exists) are the four components
that exist today —
all plain data, no behavior beyond small pure-math helpers, no GPU/SDL
ownership of their own.
`Transform` now carries a real parent/child relationship, Unity's own
`Transform.parent`/`GetSiblingIndex()` shape: a `parent` `Entity` field
(`kInvalidEntity` — the default — means "world space, no parent", exactly
matching every `Transform`'s behavior before this existed) and a
`siblingIndex` used purely for display/ordering (which order "Hierarchy"
lists siblings in). `position`/`rotation`/`scale` are always PARENT-RELATIVE
now (Unity's own `localPosition`/`localRotation`/`localScale` semantics) —
`Transform::LocalToWorldMatrix()` itself only ever composes this one
entity's own local T/R/S (components stay plain data with no `Registry`
dependency of their own, per this file's own philosophy above); actually
resolving a full WORLD transform by walking the parent chain, cycle-safe
reparenting, and sibling reordering all live in a new sibling module,
`src/ECS/TransformHierarchy.h/.cpp` — `ComputeWorldMatrix()`/
`ComputeWorldTransform()` (walk parent -> parent -> ... -> root, composing
`parentWorld * local` at each level), `IsDescendantOf()` (cycle detection),
`GetChildren()` (the direct-child list a Hierarchy tree needs, sorted by
`siblingIndex`, dangling-parent-safe), `SetParent()` (cycle-safe attach/
detach with an optional world-position-preserving conversion — Unity's own
`Transform.SetParent(parent, worldPositionStays)` default), and
`SetSiblingIndex()`/`MoveToLastSibling()` (reordering). All pure functions
needing nothing but a `Registry` (no Renderer/GPU/ImGui), so this whole
module is Tier-1-testable exactly like the rest of ECS (see `TESTING.md`).
`MeshRenderer` references a mesh/pipeline purely by handle
(`MeshHandle`/`PipelineHandle`, `src/Renderer/MeshHandle.h`/
`PipelineHandle.h`) — the exact same cheap, generational, index+generation
shape as `Entity` and `GpuResourceHandle`, minted by a generic
`ResourcePool<T, HandleT>` (`src/Renderer/ResourcePool.h`) rather than ever
embedding a live `Mesh`/`Pipeline` in a component. `Camera` is
perspective-only for now (`fovYDegrees`/`nearZ`/`farZ`/`active`), with two
pure-math helpers — `ProjectionMatrix(aspect)` (via
`Mat4::PerspectiveFovLH_ZO`) and the static `ViewMatrix(transform)` (via
`Mat4::LookAtLH`, looking down the `Transform`'s rotated `Vec3::Forward()`)
— rather than a bespoke eye/target/up triple, so a camera entity is edited
exactly like any other (Transform in the Inspector, same as everything
else) — including following a parent, since `RenderSystem` resolves its
full WORLD transform (below) before building its view matrix.

`RenderSystem` (`src/Game/RenderSystem.h/.cpp`) was the first, and remains
the primary, piece of the engine allowed to depend on both the ECS world and
`Renderer` — the same "only one layer crosses this boundary" rule this
engine already applies to SDL (only `Application` touches it directly).
`Renderer` itself never depends on ECS in any way: `Submit()` takes plain
`Mat4`s, never an `Entity`/`Registry`. `RenderSystem::CollectRenderables()`
(every entity with a `MeshRenderer` becomes one `DrawCommand`, using
`TransformHierarchy.h`'s `ComputeWorldMatrix()` — its `Transform`'s local
matrix composed all the way up its parent chain, if any) and
`RenderSystem::ResolveActiveCameraViewProjection()` (the first entity with
an active `Camera` becomes a combined view-projection matrix, resolved from
that camera entity's own full WORLD transform the same way,
`Mat4::Identity()` if no active `Camera` exists at all) are both pure
functions that need nothing but a `Registry` — no live Renderer/GPU device —
so both are unit-tested exactly like the rest of ECS (see `TESTING.md`).
`RenderSystem::Draw()` is the one non-pure step that resolves DrawCommand
handles against its own `ResourcePool<Mesh, MeshHandle>`/
`ResourcePool<Pipeline, PipelineHandle>` and calls `Renderer::Submit()` with
both the per-object model matrix and the resolved view-projection matrix.
Two more systems now share this same "allowed to touch both ECS and
Renderer" seam alongside it — `MeshInstantiationSystem`
(`src/Game/Instantiation/MeshInstantiationSystem.h/.cpp`, entity/mesh
spawning) and `AnimationSystem` (`src/Game/Animation/AnimationSystem.h/.cpp`,
skeletal animation playback) — see [the Changelog](../CHANGELOG.md) for the full rundown of
that refactor and AGENTS.md's "Entity-Component-System" section for the
architectural rule itself. `Game` no longer holds a hardcoded `Pipeline`/
`Mesh` pair, or any instantiation/animation logic of its own, at all — it
owns a `Registry` plus these three systems and just forwards to them.

---

This file describes the ENGINE-side ECS architecture/layering (data model,
component shapes, the `RenderSystem`/ECS boundary rule). See
[docs/conventions/ecs.md](../conventions/ecs.md) for the `AGENTS.md`-side
coding CONVENTIONS to follow when writing new components/systems.
