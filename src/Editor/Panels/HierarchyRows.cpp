#include "HierarchyRows.h"

#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/Name.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"
#include "../Selection.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <span>
#include <utility>

namespace gte {

namespace {

// One open level of the depth-first walk.
struct WalkFrame {
    std::span<const Entity> children;
    std::uint32_t next = 0;
    Entity parent;
};

double MillisecondsSince(std::chrono::steady_clock::time_point start) noexcept
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

std::string BuildEntityLabel(const Registry& registry, Entity entity)
{
    const bool isCamera = registry.HasComponent<Camera>(entity);
    if (const Name* name = registry.TryGetComponent<Name>(entity); name != nullptr && !name->value.empty()) {
        return isCamera ? (name->value + " (Camera)") : name->value;
    }

    char label[32];
    if (isCamera) {
        std::snprintf(label, sizeof(label), "Entity %u (Camera)", entity.index);
    } else {
        std::snprintf(label, sizeof(label), "Entity %u", entity.index);
    }
    return std::string(label);
}

void FlattenHierarchyRows(
    const HierarchySnapshot& snapshot, const std::unordered_set<Entity>& collapsed, std::vector<HierarchyRow>& out)
{
    out.clear();
    out.reserve(snapshot.Size());

    std::vector<WalkFrame> stack;
    stack.push_back({ snapshot.Children(kInvalidEntity), 0, kInvalidEntity });

    while (!stack.empty()) {
        WalkFrame& top = stack.back();
        if (top.next >= top.children.size()) {
            stack.pop_back();
            continue;
        }

        HierarchyRow row;
        row.siblingPosition = top.next;
        row.entity = top.children[top.next++];
        row.parent = top.parent;
        row.depth = static_cast<std::uint32_t>(stack.size() - 1);

        const std::span<const Entity> children = snapshot.Children(row.entity);
        row.hasChildren = !children.empty();
        row.expanded = !row.hasChildren || !collapsed.contains(row.entity);
        out.push_back(row);

        // `top` is dead after this push.
        if (row.hasChildren && row.expanded) {
            stack.push_back({ children, 0, row.entity });
        }
    }
}

void PruneCollapsedEntities(const Registry& registry, std::unordered_set<Entity>& collapsed)
{
    std::erase_if(collapsed, [&registry](Entity entity) { return !registry.IsAlive(entity); });
}

void UpdateHierarchyRows(Registry& registry, HierarchyViewState& state)
{
    PruneCollapsedEntities(registry, state.collapsed);

    const auto buildStart = std::chrono::steady_clock::now();
    state.snapshot.Rebuild(registry);
    state.lastBuildMs = MillisecondsSince(buildStart);

    const auto flattenStart = std::chrono::steady_clock::now();
    FlattenHierarchyRows(state.snapshot, state.collapsed, state.rows);
    state.lastFlattenMs = MillisecondsSince(flattenStart);
}

void NoteHierarchyVisible(HierarchyViewState& state) noexcept
{
    state.hiddenFrames = 0;
    state.visible = true;
}

void NoteHierarchyHidden(HierarchyViewState& state)
{
    state.visible = false;
    if (state.hiddenFrames < kHierarchyHiddenFramesBeforeRelease) {
        ++state.hiddenFrames;
        if (state.hiddenFrames == kHierarchyHiddenFramesBeforeRelease) {
            state.rows = std::vector<HierarchyRow>();
            state.snapshot = HierarchySnapshot();
        }
    }
}

std::uint32_t ComputeSiblingInsertIndex(
    std::uint32_t targetPosition, std::optional<std::uint32_t> draggedPosition, bool insertAfter) noexcept
{
    std::uint32_t base = targetPosition;
    if (draggedPosition.has_value() && *draggedPosition < targetPosition) {
        --base;
    }
    return base + (insertAfter ? 1u : 0u);
}

bool ExecuteHierarchyAction(Registry& registry, const HierarchySnapshot& snapshot, const PendingHierarchyAction& action)
{
    if (action.kind != PendingHierarchyAction::Kind::Reparent && action.kind != PendingHierarchyAction::Kind::Reorder) {
        return false;
    }
    if (action.dragged == action.target || !registry.IsAlive(action.dragged) || !registry.IsAlive(action.target)) {
        return false;
    }

    if (action.kind == PendingHierarchyAction::Kind::Reparent) {
        return SetParent(registry, action.dragged, action.target, /*worldPositionStays=*/true);
    }

    const Transform* draggedTransform = registry.TryGetComponent<Transform>(action.dragged);
    if (draggedTransform == nullptr) {
        return false;
    }

    std::optional<std::uint32_t> draggedPosition;
    if (EffectiveParent(registry, *draggedTransform) == action.targetParent) {
        const std::span<const Entity> siblings = snapshot.Children(action.targetParent);
        const auto found = std::find(siblings.begin(), siblings.end(), action.dragged);
        if (found != siblings.end()) {
            draggedPosition = static_cast<std::uint32_t>(found - siblings.begin());
        }
    } else if (!SetParent(registry, action.dragged, action.targetParent, /*worldPositionStays=*/true)) {
        return false;
    }

    return SetSiblingIndex(registry, action.dragged,
        ComputeSiblingInsertIndex(action.targetPosition, draggedPosition, action.insertAfter));
}

HierarchyStateReport BuildHierarchyStateReport(const Registry& registry, const HierarchyViewState& state,
    const Selection& selection, std::uint64_t first, std::uint64_t count)
{
    HierarchyStateReport report;
    report.visible = state.visible;
    report.entityCount = registry.AliveEntityCount();
    report.first = first;
    if (!state.visible) {
        return report;
    }

    report.rowCount = state.rows.size();
    report.collapsedCount = state.collapsed.size();
    report.buildMs = state.lastBuildMs;
    report.flattenMs = state.lastFlattenMs;
    report.drawnRows = state.lastDrawnRows;

    const std::uint64_t begin = std::min<std::uint64_t>(first, state.rows.size());
    const std::uint64_t end = begin + std::min<std::uint64_t>(count, state.rows.size() - begin);
    report.rows.reserve(static_cast<std::size_t>(end - begin));
    for (std::uint64_t i = begin; i < end; ++i) {
        const HierarchyRow& row = state.rows[static_cast<std::size_t>(i)];
        HierarchyStateRow out;
        out.entity = row.entity;
        out.parent = row.parent;
        out.depth = row.depth;
        out.siblingPosition = row.siblingPosition;
        out.label = registry.IsAlive(row.entity) ? BuildEntityLabel(registry, row.entity) : std::string("(destroyed)");
        out.hasChildren = row.hasChildren;
        out.expanded = row.expanded;
        out.selected = selection.IsEntitySelected(row.entity);
        report.rows.push_back(std::move(out));
    }
    return report;
}

} // namespace gte
