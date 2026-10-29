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

bool PruneCollapsedEntities(const Registry& registry, std::unordered_set<Entity>& collapsed)
{
    return std::erase_if(collapsed, [&registry](Entity entity) { return !registry.IsAlive(entity); }) > 0;
}

void UpdateHierarchyRows(Registry& registry, HierarchyViewState& state)
{
    if (PruneCollapsedEntities(registry, state.collapsed)) {
        ++state.collapsedRevision;
    }

    const auto buildStart = std::chrono::steady_clock::now();
    state.snapshot.Rebuild(registry);
    state.lastBuildMs = MillisecondsSince(buildStart);

    // `collapsed.size()` also catches a direct write that skipped the revision.
    const bool current = state.rowsFlattened && state.flattenedSnapshotRevision == state.snapshot.Revision()
        && state.flattenedCollapsedRevision == state.collapsedRevision
        && state.flattenedCollapsedSize == state.collapsed.size()
        && (!state.rows.empty() || state.snapshot.Size() == 0);
    if (current) {
        state.lastFlattenMs = 0.0;
        return;
    }

    const auto flattenStart = std::chrono::steady_clock::now();
    FlattenHierarchyRows(state.snapshot, state.collapsed, state.rows);
    state.lastFlattenMs = MillisecondsSince(flattenStart);
    state.rowsFlattened = true;
    state.flattenedSnapshotRevision = state.snapshot.Revision();
    state.flattenedCollapsedRevision = state.collapsedRevision;
    state.flattenedCollapsedSize = state.collapsed.size();
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
            state.rowsFlattened = false;
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

bool ApplyHierarchyAction(Registry& registry, HierarchyViewState& state, const PendingHierarchyAction& action)
{
    return ExecuteHierarchyAction(registry, state.snapshot, action);
}

void ToggleHierarchyCollapsed(HierarchyViewState& state, const HierarchyRow& row)
{
    if (!row.hasChildren) {
        return;
    }
    if (row.expanded) {
        state.collapsed.insert(row.entity);
    } else {
        state.collapsed.erase(row.entity);
    }
    ++state.collapsedRevision;
}

namespace {

// True when `entity` is `root` or lies below it. Bounded by the entity count, so a broken cycle cannot hang.
bool IsInsideSubtree(const Registry& registry, Entity entity, Entity root)
{
    std::uint64_t budget = registry.AliveEntityCount();
    for (Entity current = entity; current.IsValid() && budget > 0; --budget) {
        if (current == root) {
            return true;
        }
        const Transform* transform = registry.TryGetComponent<Transform>(current);
        if (transform == nullptr) {
            return false;
        }
        current = EffectiveParent(registry, *transform);
    }
    return false;
}

} // namespace

HierarchyActionPlan PlanHierarchyAction(
    const Registry& registry, std::span<const HierarchyRow> rows, const HierarchyActionCommand& request)
{
    HierarchyActionPlan plan;
    const auto fail = [&plan](const char* message) {
        plan.error = message;
        return plan;
    };

    if (!request.target.IsValid() || !registry.IsAlive(request.target)) {
        return fail("target is not a live entity");
    }
    const auto targetRow = std::find_if(
        rows.begin(), rows.end(), [&request](const HierarchyRow& row) { return row.entity == request.target; });
    if (targetRow == rows.end()) {
        return fail("target is not a visible row (it may be hidden under a collapsed row)");
    }

    if (request.kind == HierarchyActionKind::Toggle) {
        if (!targetRow->hasChildren) {
            return fail("target row has no children to open or close");
        }
        plan.row = *targetRow;
        plan.ok = true;
        return plan;
    }

    if (!request.dragged.IsValid() || !registry.IsAlive(request.dragged)) {
        return fail("dragged is not a live entity");
    }
    if (request.dragged == request.target) {
        return fail("dragged and target are the same entity");
    }
    if (!registry.HasComponent<Transform>(request.dragged)) {
        return fail("dragged has no Transform");
    }

    const bool reorder = request.kind == HierarchyActionKind::Reorder;
    const Entity newParent = reorder ? targetRow->parent : request.target;
    if (IsInsideSubtree(registry, newParent, request.dragged)) {
        return fail("the new parent is inside the dragged entity's own subtree");
    }

    plan.action.kind = reorder ? PendingHierarchyAction::Kind::Reorder : PendingHierarchyAction::Kind::Reparent;
    plan.action.dragged = request.dragged;
    plan.action.target = request.target;
    if (reorder) {
        plan.action.targetParent = targetRow->parent;
        plan.action.targetPosition = targetRow->siblingPosition;
        plan.action.insertAfter = request.insertAfter;
    }
    plan.ok = true;
    return plan;
}

std::vector<HierarchyStateRow> BuildHierarchyStateRows(const Registry& registry, const HierarchyViewState& state,
    const Selection& selection, std::uint64_t begin, std::uint64_t end)
{
    end = std::min<std::uint64_t>(end, state.rows.size());
    begin = std::min(begin, end);

    std::vector<HierarchyStateRow> out;
    out.reserve(static_cast<std::size_t>(end - begin));
    for (std::uint64_t i = begin; i < end; ++i) {
        const HierarchyRow& row = state.rows[static_cast<std::size_t>(i)];
        HierarchyStateRow item;
        item.entity = row.entity;
        item.parent = row.parent;
        item.depth = row.depth;
        item.siblingPosition = row.siblingPosition;
        item.label = registry.IsAlive(row.entity) ? BuildEntityLabel(registry, row.entity) : std::string("(destroyed)");
        item.hasChildren = row.hasChildren;
        item.expanded = row.expanded;
        item.selected = selection.IsEntitySelected(row.entity);
        out.push_back(std::move(item));
    }
    return out;
}

HierarchyActionReport RunHierarchyAction(
    Registry& registry, HierarchyViewState& state, const Selection& selection, const HierarchyActionCommand& request)
{
    HierarchyActionReport report;
    report.kind = std::string(HierarchyActionKindName(request.kind));

    UpdateHierarchyRows(registry, state);
    const HierarchyActionPlan plan = PlanHierarchyAction(registry, state.rows, request);
    if (!plan.ok) {
        report.errorMessage = plan.error;
        return report;
    }

    if (request.kind == HierarchyActionKind::Toggle) {
        ToggleHierarchyCollapsed(state, plan.row);
        UpdateHierarchyRows(registry, state);
        report.changed = true;
    } else {
        report.changed = ApplyHierarchyAction(registry, state, plan.action);
        UpdateHierarchyRows(registry, state);
    }
    report.success = true;

    const auto find = [&state](Entity entity) {
        return std::find_if(state.rows.begin(), state.rows.end(),
            [entity](const HierarchyRow& row) { return row.entity == entity; });
    };
    auto focus = find(request.target);
    if (focus == state.rows.end() && request.kind != HierarchyActionKind::Toggle) {
        focus = find(request.dragged); // the target went under a collapsed row
    }
    const std::uint64_t context = kHierarchyActionContextRows;
    std::uint64_t begin = 0;
    std::uint64_t end = 2 * context + 1;
    if (focus != state.rows.end()) {
        const std::uint64_t at = static_cast<std::uint64_t>(focus - state.rows.begin());
        begin = at > context ? at - context : 0;
        end = at + context + 1;
    }
    report.firstRow = std::min<std::uint64_t>(begin, state.rows.size());
    report.rows = BuildHierarchyStateRows(registry, state, selection, begin, end);
    return report;
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
    report.rows = BuildHierarchyStateRows(registry, state, selection, begin, begin + count);
    return report;
}

} // namespace gte
