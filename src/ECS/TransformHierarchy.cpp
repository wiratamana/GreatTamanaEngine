#include "TransformHierarchy.h"

#include "Components/Transform.h"
#include "HierarchySnapshot.h"
#include "Registry.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"
#include "Math/Vec4.h"

#include <algorithm>
#include <limits>

namespace gte {

namespace {

// Cap on parent-chain hops, so a malformed chain cannot loop forever.
constexpr int kMaxHierarchyDepth = 1'000'000;

constexpr std::uint32_t kRootGroup = std::numeric_limits<std::uint32_t>::max();

// Column-based decomposition: translation is column 3, columns 0..2 are the rotation basis scaled per axis.
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

    // Walk up, left-multiplying each ancestor's local matrix.
    Mat4 world = local;
    Entity current = parent;
    int depth = 0;
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
    Transform result;
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

Entity EffectiveParent(const Registry& registry, const Transform& transform) noexcept
{
    const Entity parent = transform.parent;
    if (parent.IsValid() && registry.HasComponent<Transform>(parent)) {
        return parent;
    }
    return kInvalidEntity;
}

bool SiblingLess(std::uint32_t siblingA, Entity a, std::uint32_t siblingB, Entity b) noexcept
{
    if (siblingA != siblingB) {
        return siblingA < siblingB;
    }
    if (a.index != b.index) {
        return a.index < b.index;
    }
    return a.generation < b.generation;
}

std::vector<Entity> GetChildren(Registry& registry, Entity parent)
{
    struct Row {
        std::uint32_t sibling;
        Entity entity;
    };

    if (!parent.IsValid()) {
        parent = kInvalidEntity;
    }

    const Registry& constRegistry = registry;
    ComponentStorage<Transform>& storage = registry.Storage<Transform>();

    std::vector<Row> rows;
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        const Transform& transform = storage.ComponentAt(i);
        if (EffectiveParent(constRegistry, transform) == parent) {
            rows.push_back(Row{ transform.siblingIndex, storage.EntityAt(i) });
        }
    }

    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) noexcept {
        return SiblingLess(a.sibling, a.entity, b.sibling, b.entity);
    });

    std::vector<Entity> result;
    result.reserve(rows.size());
    for (const Row& row : rows) {
        result.push_back(row.entity);
    }
    return result;
}

void MoveToLastSibling(Registry& registry, Entity entity)
{
    Transform* transform = registry.TryGetComponent<Transform>(entity);
    if (transform == nullptr) {
        return;
    }

    const Registry& constRegistry = registry;
    const Entity parent = EffectiveParent(constRegistry, *transform);

    bool any = false;
    std::uint32_t maxIndex = 0;

    ComponentStorage<Transform>& storage = registry.Storage<Transform>();
    for (std::size_t i = 0; i < storage.Size(); ++i) {
        if (storage.EntityAt(i) == entity) {
            continue;
        }
        const Transform& candidate = storage.ComponentAt(i);
        if (EffectiveParent(constRegistry, candidate) == parent) {
            maxIndex = any ? std::max(maxIndex, candidate.siblingIndex) : candidate.siblingIndex;
            any = true;
        }
    }

    transform->siblingIndex = any ? maxIndex + 1 : 0;
}

bool SetSiblingIndex(Registry& registry, Entity entity, std::uint32_t desiredIndex)
{
    const Transform* transform = registry.TryGetComponent<Transform>(entity);
    if (transform == nullptr) {
        return false;
    }
    const Registry& constRegistry = registry;
    const Entity parent = EffectiveParent(constRegistry, *transform);

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
        if (IsDescendantOf(registry, newParent, child)) {
            return false;
        }
    }

    Mat4 worldBefore;
    if (worldPositionStays) {
        worldBefore = ComputeWorldMatrix(registry, child);
    }

    registry.TryGetComponent<Transform>(child)->parent = newParent;

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

void DestroyEntityAndDescendants(Registry& registry, Entity entity)
{
    DestroyEntitiesAndDescendants(registry, std::span<const Entity>(&entity, 1));
}

void DestroyEntitiesAndDescendants(Registry& registry, std::span<const Entity> roots)
{
    bool anyAlive = false;
    for (const Entity root : roots) {
        if (registry.IsAlive(root)) {
            anyAlive = true;
            break;
        }
    }
    if (!anyAlive) {
        return;
    }

    HierarchySnapshot snapshot;
    snapshot.Rebuild(registry);

    std::vector<std::uint8_t> collected; // indexed by entity index; live entities never share an index
    const auto collect = [&collected](Entity entity) {
        if (entity.index >= collected.size()) {
            collected.resize(std::max<std::size_t>(static_cast<std::size_t>(entity.index) + 1, collected.size() * 2), 0);
        }
        if (collected[entity.index] != 0) {
            return false;
        }
        collected[entity.index] = 1;
        return true;
    };

    // Iterative pre-order over every root subtree.
    std::vector<Entity> order;
    std::vector<Entity> stack;
    for (const Entity root : roots) {
        if (!registry.IsAlive(root) || !collect(root)) {
            continue;
        }
        stack.push_back(root);
        while (!stack.empty()) {
            const Entity entity = stack.back();
            stack.pop_back();
            order.push_back(entity);

            const std::span<const Entity> children = snapshot.Children(entity);
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if (collect(*it)) {
                    stack.push_back(*it);
                }
            }
        }
    }

    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        registry.DestroyEntity(*it);
    }
}

void AssignHierarchy(Registry& registry, std::span<const HierarchyAssignment> assignments)
{
    ComponentStorage<Transform>& storage = registry.Storage<Transform>();
    const std::size_t transformCount = storage.Size();
    if (assignments.empty() || transformCount == 0) {
        return;
    }

    std::uint32_t maxIndex = 0;
    for (std::size_t i = 0; i < transformCount; ++i) {
        maxIndex = std::max(maxIndex, storage.EntityAt(i).index);
    }
    const std::size_t slotCount = static_cast<std::size_t>(maxIndex) + 1;

    // parentOf: the parent each slot will have; assignedAt: 1-based input position of its winning assignment.
    std::vector<Entity> parentOf(slotCount, kInvalidEntity);
    std::vector<std::uint32_t> assignedAt(slotCount, 0);

    const Registry& constRegistry = registry;
    for (std::size_t i = 0; i < transformCount; ++i) {
        parentOf[storage.EntityAt(i).index] = EffectiveParent(constRegistry, storage.ComponentAt(i));
    }
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        const HierarchyAssignment& assignment = assignments[i];
        if (!constRegistry.HasComponent<Transform>(assignment.child)) {
            continue;
        }
        Entity parent = assignment.parent;
        if (parent == assignment.child || !constRegistry.HasComponent<Transform>(parent)) {
            parent = kInvalidEntity;
        }
        parentOf[assignment.child.index] = parent;
        assignedAt[assignment.child.index] = static_cast<std::uint32_t>(i + 1);
    }

    // Cycle detection: one walk up from each assigned child with node states (1 = on the current
    // path, 2 = done). A cycle is cut at its latest assigned link.
    std::vector<std::uint8_t> state(slotCount, 0);
    std::vector<std::uint32_t> path;
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        const std::uint32_t start = assignments[i].child.index;
        if (start >= slotCount || assignedAt[start] != i + 1 || state[start] != 0) {
            continue;
        }

        path.clear();
        std::uint32_t current = start;
        for (;;) {
            state[current] = 1;
            path.push_back(current);

            const Entity next = parentOf[current];
            if (!next.IsValid() || state[next.index] == 2) {
                break;
            }
            if (state[next.index] == 1) {
                std::size_t cycleBegin = path.size();
                while (path[cycleBegin - 1] != next.index) {
                    --cycleBegin;
                }
                --cycleBegin;
                std::uint32_t victim = path.back();
                for (std::size_t k = cycleBegin; k < path.size(); ++k) {
                    if (assignedAt[path[k]] > assignedAt[victim]) {
                        victim = path[k];
                    }
                }
                parentOf[victim] = kInvalidEntity;
                break;
            }
            current = next.index;
        }
        for (const std::uint32_t slot : path) {
            state[slot] = 2;
        }
    }

    // Renumber each parent's assigned children densely by (savedIndex, input order).
    struct Row {
        std::uint32_t group;
        std::uint32_t saved;
        std::uint32_t order;
        Entity child;
    };
    std::vector<Row> rows;
    rows.reserve(assignments.size());
    for (std::size_t i = 0; i < assignments.size(); ++i) {
        const HierarchyAssignment& assignment = assignments[i];
        if (assignment.child.index >= slotCount || assignedAt[assignment.child.index] != i + 1) {
            continue;
        }
        const Entity parent = parentOf[assignment.child.index];
        rows.push_back(Row{ parent.IsValid() ? parent.index : kRootGroup, assignment.siblingIndex,
            static_cast<std::uint32_t>(i), assignment.child });
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) noexcept {
        if (a.group != b.group) {
            return a.group < b.group;
        }
        if (a.saved != b.saved) {
            return a.saved < b.saved;
        }
        return a.order < b.order;
    });

    std::uint32_t rank = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rank = (i > 0 && rows[i].group == rows[i - 1].group) ? rank + 1 : 0;
        if (Transform* transform = storage.TryGet(rows[i].child)) {
            transform->parent = parentOf[rows[i].child.index];
            transform->siblingIndex = rank;
        }
    }
}

} // namespace gte
