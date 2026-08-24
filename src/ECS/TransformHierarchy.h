#pragma once

#include "Entity.h"
#include "Math/Mat4.h"

#include <cstdint>
#include <vector>

namespace gte {

class Registry;
struct Transform;

// Free functions implementing Transform (Components/Transform.h)'s
// parent/child hierarchy - deliberately NOT methods on Transform itself
// (a component must stay plain data with no Registry dependency of its own -
// see AGENTS.md, "Entity-Component-System"), the same "only code that
// already depends on both the ECS world and something else may bridge them"
// boundary RenderSystem already follows for Renderer. Every function here
// only ever needs a Registry - no Renderer, no live GPU device - so this
// whole module is genuinely Tier-1-testable (see
// tests/ECS/TransformHierarchyTests.cpp).
//
// Unity-equivalent surface: Transform.parent (get/set via SetParent()),
// Transform.GetWorldToLocalMatrix()-adjacent LocalToWorldMatrix()
// (ComputeWorldMatrix() here), and Transform.GetSiblingIndex()/
// SetSiblingIndex()/GetChild()/childCount (GetChildren()/SetSiblingIndex()
// here).

// Recursively composes parentWorld * localTransform all the way up to a
// root (an entity with Transform::parent == kInvalidEntity, or whose parent
// entity is no longer alive/has no Transform of its own) - the actual
// Unity-"Transform.localToWorldMatrix" equivalent now that parenting
// exists (Transform::LocalToWorldMatrix() itself only ever composes ONE
// entity's own local T/R/S - see that method's own doc comment). An entity
// with no Transform at all resolves to Mat4::Identity(), matching
// RenderSystem::CollectRenderables()'s pre-existing "no Transform ->
// Identity" fallback. Depth-capped defensively against a malformed parent
// chain that somehow loops back on itself (should never happen if every
// reparent goes through SetParent() below, which itself rejects cycles) -
// falls back to treating the offending entity as a root rather than hanging
// forever.
Mat4 ComputeWorldMatrix(Registry& registry, Entity entity);

// The world-space equivalent of `entity`'s Transform - same
// position/rotation/scale fields, but fully resolved through its whole
// parent chain via ComputeWorldMatrix() above (decomposed back into
// position/rotation/scale as ComputeWorldMatrix()'s own inverse operation -
// same column-based decomposition TransformGizmo.cpp already uses). Returns
// a default (identity) Transform for an entity with no Transform component
// at all - matching Camera::ViewMatrix()'s pre-existing "no Transform ->
// identity" fallback (see RenderSystem::ResolveActiveCameraViewProjection()).
// The returned Transform's own `parent`/`siblingIndex` fields are always
// left at their defaults (kInvalidEntity/0) - this is a plain
// position/rotation/scale snapshot, not a real hierarchy node.
Transform ComputeWorldTransform(Registry& registry, Entity entity);

// True if walking UP `entity`'s own parent chain (Transform::parent, then
// ITS parent, ...) ever reaches `potentialAncestor` - i.e. `entity` is a
// (possibly indirect) child of `potentialAncestor`. Used by SetParent()
// below to reject a reparent that would create a cycle (attaching an entity
// as a child of one of its own descendants) before it happens. Depth-capped
// the same way ComputeWorldMatrix() is, for the same defensive reason.
bool IsDescendantOf(Registry& registry, Entity entity, Entity potentialAncestor);

// Every entity whose Transform::parent == `parent` (pass kInvalidEntity for
// the top-level/root entity list - see Panels/HierarchyPanel.cpp), sorted by
// Transform::siblingIndex (ties broken by ComponentStorage<Transform>'s own
// dense-iteration order, i.e. creation order until a Remove() reshuffles it -
// see ComponentStorage.h). An entity whose OWN parent field points at an
// entity that is no longer alive (e.g. that parent was destroyed without
// ever explicitly detaching its children first) is treated as if
// parent == kInvalidEntity instead - i.e. it "falls back to the root list" -
// exactly the same defensive fallback ComputeWorldMatrix() already applies,
// so a dangling parent reference never makes an entity silently vanish from
// "Hierarchy" entirely.
std::vector<Entity> GetChildren(Registry& registry, Entity parent);

// Reparents `child` under `newParent` (kInvalidEntity to detach it to the
// scene root), appending it as the new LAST sibling under that parent (see
// MoveToLastSibling() below) - the attach/detach half of "Hierarchy" drag-
// and-drop (see Panels/HierarchyPanel.cpp). Fails outright (returns false,
// leaving `child` completely untouched) when: child == newParent, `child`
// has no Transform component, `newParent` is non-invalid but has no
// Transform component, or `newParent` is `child` itself or one of `child`'s
// own descendants (IsDescendantOf() above) - reparenting onto your own
// descendant would create a cycle ComputeWorldMatrix() could never
// terminate correctly otherwise.
//
// When `worldPositionStays` is true (the default - matches Unity's own
// Transform.SetParent(Transform, bool) default), `child`'s local
// position/rotation/scale are recomputed so its WORLD transform is
// unchanged by the reparent - i.e. dragging an entity under a new parent in
// "Hierarchy" doesn't visually move it in "Scene"/"Game". Pass false to
// keep the local transform fields exactly as authored instead - the entity
// then jumps to wherever that local transform happens to resolve to under
// its new parent (Unity's Transform.SetParent(Transform) one-argument
// overload's behavior, minus the world-position-preserving step).
bool SetParent(Registry& registry, Entity child, Entity newParent, bool worldPositionStays = true);

// Moves `entity` to sibling index `desiredIndex` (0 = first) among the
// OTHER entities that currently share its own Transform::parent - the
// reorder half of "Hierarchy" drag-and-drop. Every entity in that sibling
// group (including `entity` itself) has its own siblingIndex renumbered
// densely (0..N-1) in the process - `desiredIndex` is clamped to
// [0, siblingCount] first, so passing an out-of-range value is equivalent
// to moving `entity` to the end. `entity`'s own Transform::parent is never
// touched by this function - see SetParent() above to also change parent.
// No-op (returns false) if `entity` has no Transform component.
bool SetSiblingIndex(Registry& registry, Entity entity, std::uint32_t desiredIndex);

// Appends `entity` as the last sibling under its CURRENT Transform::parent -
// convenience used internally by SetParent() (a freshly-reparented entity
// always lands at the end of its new parent's child list, exactly like a
// freshly-instantiated Unity GameObject does), and equally useful standalone
// (e.g. "move to bottom" without changing parent). No-op if `entity` has no
// Transform component.
void MoveToLastSibling(Registry& registry, Entity entity);

} // namespace gte
