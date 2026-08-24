#include "TransformHierarchy.h"

#include "Components/Transform.h"
#include "Registry.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"
#include "Math/Vec4.h"

#include <algorithm>

namespace gte {

namespace {

// Defensive cap on how many parent-chain hops any of the walks below will
// ever follow - every entity is created fresh via Registry::CreateEntity()
// and this engine has nowhere near a million live entities today, so
// hitting this in practice would only ever mean a malformed parent chain
// (which SetParent()'s own cycle rejection should already prevent) rather
// than a legitimately deep scene graph.
constexpr int kMaxHierarchyDepth = 1'000'000;

// Same column-based decomposition TransformGizmo.cpp's ManipulateTransformGizmo()
// already uses to turn a manipulated 4x4 matrix back into position/rotation/
// scale (see that file's own comment for the full "why not
// ImGuizmo::DecomposeMatrixToComponents()" reasoning) - duplicated here
// rather than shared, since TransformGizmo.cpp deliberately has no
// Registry/ECS dependency at all and this module deliberately has no ImGui/
// ImGuizmo dependency; both independently need "Mat4 -> position/rotation/
// scale" and arrive at it the exact same handedness-agnostic way (translation
// is column 3 verbatim; columns 0/1/2 are the rotation's right/up/forward
// basis scaled by scale.x/y/z respectively, so dividing each column by its
// own length recovers the pure rotation basis for Quat::FromMat4()).
void DecomposeMatrix(const Mat4& m, Vec3& outPosition, Quat& outRotation, Vec3& outScale)
{
    const Vec3 col0{ m(0, 0), m(1, 0), m(2, 0) };
    const Vec3 col1{ m(0, 1), m(1, 1), m(2, 1) };
    const Vec3 col2{ m(0, 2), m(1, 2), m(2, 2) };

    outScale = Vec3{ Length(col0), Length(col1), Length(col2) };

    Mat4 rotationOnly = Mat4::Identity();
    rotationOnly.columns[0] = Vec4(outScale.x > kEpsilon ? col0 / outScale.x : Vec3::Right(), 0.0f);
    rotationOnly.columns[1] = Vec4(outScale.y > kEpsilon ? col1 / outScale.y : Vec3::Up(), 0.0f);
    rotationOnly.columns[2] = Vec4(outScale.z > kEpsilon ? col2 / outScale.z : Vec3::Forward(), 0.0f);

    outPosition = Vec3{ m(0, 3), m(1, 3), m(2, 3) };
    outRotation = Quat::FromMat4(rotationOnly);
}

} // namespace

Mat4 ComputeWorldMatrix(Registry& registry, Entity entity)
{
    const Transform* transform = registry.TryGetComponent<Transform>(entity);
    if (transform == nullptr) {
        return Mat4::Identity();
    }

    const Mat4 local = transform->LocalToWorldMatrix();

    const Entity parent = transform->parent;
    if (!parent.IsValid() || !registry.IsAlive(parent)) {
        return local;
    }

    Mat4 world = local;
    Entity current = parent;
    int depth = 0;
    // Iteratively walk UP the chain, accumulating parent * ... * local from
    // the innermost (immediate parent) outward - equivalent to (and cheaper
    // than) naive recursion computing the full ancestor chain per call.
    // Built by repeatedly left-multiplying by each ancestor's own LOCAL
    // matrix, which is exactly parentWorld * childWorld composed
    // incrementally: after this loop, `world` == root.local * ... *
    // grandparent.local * parent.local * entity.local, matching
    // ComputeWorldMatrix()'s own recursive definition.
    while (current.IsValid() && depth < kMaxHierarchyDepth) {
        const Transform* currentTransform = registry.TryGetComponent<Transform>(current);
        if (currentTransform == nullptr) {
            break;
        }
        world = currentTransform->LocalToWorldMatrix() * world;

        const Entity next = currentTransform->parent;
        if (!next.IsValid() || !registry.IsAlive(next)) {
            break;
        }
        current = next;
        ++depth;
    }

    return world;
}

Transform ComputeWorldTransform(Registry& registry, Entity entity)
{
    Transform result; // Identity default - matches "no Transform" fallback.
    if (registry.TryGetComponent<Transform>(entity) == nullptr) {
        return result;
    }

    const Mat4 world = ComputeWorldMatrix(registry, entity);
    DecomposeMatrix(world, result.position, result.rotation, result.scale);
    return result;
}

bool IsDescendantOf(Registry& registry, Entity entity, Entity potentialAncestor)
{
    if (!potentialAncestor.IsValid()) {
        return false;
    }

    Entity current = entity;
    int depth = 0;
    while (depth < kMaxHierarchyDepth) {
        const Transform* transform = registry.TryGetComponent<Transform>(current);
        if (transform == nullptr || !transform->parent.IsValid()) {
            return false;
        }
        if (transform->parent == potentialAncestor) {
            return true;
        }
        current = transform->parent;
        ++depth;
    }
    return false;
}

std::vector<Entity> GetChildren(Registry& registry, Entity parent)
{
    std::vector<Entity> result;

    ComponentStorage<Transform>& storage = registry.Storage<Transform>();
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        const Entity candidate = storage.EntityAt(i);
        const Transform& transform = storage.ComponentAt(i);

        Entity effectiveParent = transform.parent;
        if (effectiveParent.IsValid() && !registry.IsAlive(effectiveParent)) {
            // A dangling reference to a since-destroyed parent - treat as a
            // root rather than letting this entity silently vanish from
            // every GetChildren() call (see this function's own doc
            // comment).
            effectiveParent = kInvalidEntity;
        }

        if (effectiveParent == parent) {
            result.push_back(candidate);
        }
    }

    std::stable_sort(result.begin(), result.end(), [&registry](Entity a, Entity b) {
        const Transform* ta = registry.TryGetComponent<Transform>(a);
        const Transform* tb = registry.TryGetComponent<Transform>(b);
        const std::uint32_t sa = ta != nullptr ? ta->siblingIndex : 0;
        const std::uint32_t sb = tb != nullptr ? tb->siblingIndex : 0;
        return sa < sb;
    });

    return result;
}

void MoveToLastSibling(Registry& registry, Entity entity)
{
    Transform* transform = registry.TryGetComponent<Transform>(entity);
    if (transform == nullptr) {
        return;
    }

    const Entity parent = transform->parent;

    bool any = false;
    std::uint32_t maxIndex = 0;

    ComponentStorage<Transform>& storage = registry.Storage<Transform>();
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        const Entity candidate = storage.EntityAt(i);
        if (candidate == entity) {
            continue;
        }
        const Transform& candidateTransform = storage.ComponentAt(i);
        if (candidateTransform.parent == parent) {
            maxIndex = any ? std::max(maxIndex, candidateTransform.siblingIndex) : candidateTransform.siblingIndex;
            any = true;
        }
    }

    // Re-fetch: the loop above only ever READS other entities' components
    // (ComponentStorage<Transform> is never resized by this function), so
    // `transform` is still the exact same valid pointer - re-fetched anyway
    // for clarity/defensiveness rather than relying on that invariant.
    Transform* target = registry.TryGetComponent<Transform>(entity);
    if (target != nullptr) {
        target->siblingIndex = any ? maxIndex + 1 : 0;
    }
}

bool SetSiblingIndex(Registry& registry, Entity entity, std::uint32_t desiredIndex)
{
    const Transform* transform = registry.TryGetComponent<Transform>(entity);
    if (transform == nullptr) {
        return false;
    }
    const Entity parent = transform->parent;

    std::vector<Entity> siblings = GetChildren(registry, parent);
    siblings.erase(std::remove(siblings.begin(), siblings.end(), entity), siblings.end());

    desiredIndex = std::min<std::uint32_t>(desiredIndex, static_cast<std::uint32_t>(siblings.size()));
    siblings.insert(siblings.begin() + static_cast<std::ptrdiff_t>(desiredIndex), entity);

    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (Transform* siblingTransform = registry.TryGetComponent<Transform>(siblings[i])) {
            siblingTransform->siblingIndex = static_cast<std::uint32_t>(i);
        }
    }
    return true;
}

bool SetParent(Registry& registry, Entity child, Entity newParent, bool worldPositionStays)
{
    if (child == newParent) {
        return false;
    }
    if (registry.TryGetComponent<Transform>(child) == nullptr) {
        return false;
    }
    if (newParent.IsValid()) {
        if (registry.TryGetComponent<Transform>(newParent) == nullptr) {
            return false;
        }
        // Reparenting `child` under one of its OWN descendants (or under
        // itself, already rejected above) would create a cycle
        // ComputeWorldMatrix() could never resolve correctly.
        if (IsDescendantOf(registry, newParent, child)) {
            return false;
        }
    }

    Mat4 worldBefore;
    if (worldPositionStays) {
        worldBefore = ComputeWorldMatrix(registry, child);
    }

    {
        Transform* childTransform = registry.TryGetComponent<Transform>(child);
        childTransform->parent = newParent;
    }

    if (worldPositionStays) {
        const Mat4 parentWorld = newParent.IsValid() ? ComputeWorldMatrix(registry, newParent) : Mat4::Identity();
        Mat4 parentInverse;
        if (!parentWorld.TryInverse(parentInverse)) {
            parentInverse = Mat4::Identity();
        }
        const Mat4 newLocal = parentInverse * worldBefore;

        Transform* childTransform = registry.TryGetComponent<Transform>(child);
        DecomposeMatrix(newLocal, childTransform->position, childTransform->rotation, childTransform->scale);
    }

    MoveToLastSibling(registry, child);
    return true;
}

} // namespace gte
