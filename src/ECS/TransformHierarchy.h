#pragma once

#include "Entity.h"
#include "Math/Mat4.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gte {

class Registry;
struct Transform;

// Hierarchy operations on Transform::parent / Transform::siblingIndex. Registry-only, no GPU.

// World matrix: local matrices composed up the parent chain. Identity without a Transform.
// Depth-capped so a malformed (cyclic) chain cannot hang.
Mat4 ComputeWorldMatrix(Registry& registry, Entity entity);

// World-space position/rotation/scale of `entity`. Identity Transform without a Transform component.
// The returned parent/siblingIndex are left at their defaults.
Transform ComputeWorldTransform(Registry& registry, Entity entity);

// True if `potentialAncestor` lies on the parent chain above `entity`. Depth-capped.
bool IsDescendantOf(Registry& registry, Entity entity, Entity potentialAncestor);

// The parent that counts: `transform.parent` when that entity is alive and has a Transform,
// otherwise kInvalidEntity (the entity is a root).
Entity EffectiveParent(const Registry& registry, const Transform& transform) noexcept;

// Total order of siblings: siblingIndex, then entity index, then entity generation.
bool SiblingLess(std::uint32_t siblingA, Entity a, std::uint32_t siblingB, Entity b) noexcept;

// Children of `parent` in SiblingLess order. kInvalidEntity lists the roots.
// O(N) per call: never call it in a loop over many entities (use HierarchySnapshot).
std::vector<Entity> GetChildren(Registry& registry, Entity parent);

// Appends `child` as the last child of `newParent` (kInvalidEntity = root). Fails (false, child untouched) when
// either has no Transform or the move makes a cycle. worldPositionStays keeps the world transform.
bool SetParent(Registry& registry, Entity child, Entity newParent, bool worldPositionStays = true);

// Moves `entity` to position `desiredIndex` among its other siblings (clamped) and renumbers the group 0..N-1.
// The parent is not changed. False if `entity` has no Transform.
bool SetSiblingIndex(Registry& registry, Entity entity, std::uint32_t desiredIndex);

// Makes `entity` the last sibling under its effective parent. No-op without a Transform.
void MoveToLastSibling(Registry& registry, Entity entity);

// Destroys `entity` and all its descendants, children before parents. No-op on a dead entity.
void DestroyEntityAndDescendants(Registry& registry, Entity entity);

// Destroys every root and all descendants in one O(N) pass. Dead roots and overlapping roots are fine.
void DestroyEntitiesAndDescendants(Registry& registry, std::span<const Entity> roots);

struct HierarchyAssignment {
    Entity child;
    Entity parent;                  // kInvalidEntity = root
    std::uint32_t siblingIndex = 0; // saved order key inside the new parent
};

// Applies many parent links at once, keeping local transforms. Invalid links (no Transform, dead parent, cycle)
// make the child a root. Each parent's assigned children are renumbered 0..k-1 by (siblingIndex, input order).
void AssignHierarchy(Registry& registry, std::span<const HierarchyAssignment> assignments);

} // namespace gte
