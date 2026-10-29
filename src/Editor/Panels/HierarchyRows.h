#pragma once

#include "../../ECS/Entity.h"
#include "../../ECS/HierarchySnapshot.h"
#include "../HierarchyActionRequest.h"
#include "../HierarchyStateReport.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_set>
#include <vector>

namespace gte {

class Registry;
class Selection;

// One visible line of the Hierarchy panel.
struct HierarchyRow {
    Entity entity;
    Entity parent;                     // effective parent; kInvalidEntity for a root
    std::uint32_t depth = 0;
    std::uint32_t siblingPosition = 0; // index in the parent's child list
    bool hasChildren = false;
    bool expanded = true;              // meaningful only when hasChildren
};

// Hierarchy panel state that outlives one frame.
struct HierarchyViewState {
    HierarchySnapshot snapshot;
    std::unordered_set<Entity> collapsed; // closed nodes; every other node is open
    std::uint64_t collapsedRevision = 0;  // bumped by every change of `collapsed`
    std::vector<HierarchyRow> rows;
    bool rowsFlattened = false;           // `rows` was built from the revisions below
    std::uint64_t flattenedSnapshotRevision = 0;
    std::uint64_t flattenedCollapsedRevision = 0;
    std::size_t flattenedCollapsedSize = 0;
    std::uint32_t hiddenFrames = 0;
    bool visible = false;                 // the panel was drawn on the last frame
    double lastBuildMs = 0.0;
    double lastFlattenMs = 0.0;           // 0 when the rows were reused
    std::uint32_t lastDrawnRows = 0;
};

// Frames in a row the panel may stay hidden before it frees its buffers.
inline constexpr std::uint32_t kHierarchyHiddenFramesBeforeRelease = 600;

// Label shown for an entity: its Name, else "Entity <index>"; cameras get a " (Camera)" suffix.
std::string BuildEntityLabel(const Registry& registry, Entity entity);

// Depth-first rows in sibling order. A collapsed node keeps its row and hides its descendants.
void FlattenHierarchyRows(
    const HierarchySnapshot& snapshot, const std::unordered_set<Entity>& collapsed, std::vector<HierarchyRow>& out);

// Removes every dead entity from `collapsed`. Returns true when something was removed.
bool PruneCollapsedEntities(const Registry& registry, std::unordered_set<Entity>& collapsed);

// Prunes, refreshes the snapshot and re-flattens the rows only when the hierarchy or the collapsed set changed.
// Records the build and flatten times.
void UpdateHierarchyRows(Registry& registry, HierarchyViewState& state);

// Frame bookkeeping: a visible frame resets the hidden counter; a long hidden run frees the buffers.
void NoteHierarchyVisible(HierarchyViewState& state) noexcept;
void NoteHierarchyHidden(HierarchyViewState& state);

// Index to pass to SetSiblingIndex so the dragged row lands before or after the target.
// `draggedPosition` is the dragged row's position in the target's group when it already lives there.
std::uint32_t ComputeSiblingInsertIndex(
    std::uint32_t targetPosition, std::optional<std::uint32_t> draggedPosition, bool insertAfter) noexcept;

// One drop recorded while drawing; applied after the row loop.
struct PendingHierarchyAction {
    enum class Kind { None, Reparent, Reorder, SpawnAsset };

    Kind kind = Kind::None;
    Entity dragged;
    Entity target;
    Entity targetParent;                // Reorder: effective parent of the target row
    std::uint32_t targetPosition = 0;   // Reorder: sibling position of the target row
    bool insertAfter = false;           // Reorder
    std::string assetPath;              // SpawnAsset
};

// Applies a Reparent or Reorder action. `snapshot` must still match the registry.
// A reorder inside one parent leaves the transform untouched. Returns false when nothing changed.
bool ExecuteHierarchyAction(Registry& registry, const HierarchySnapshot& snapshot, const PendingHierarchyAction& action);

// Applies a recorded drop against the current snapshot; the one entry point of the panel and POST /hierarchy_action.
bool ApplyHierarchyAction(Registry& registry, HierarchyViewState& state, const PendingHierarchyAction& action);

// Flips the open state of a row that has children; the one writer of `collapsed` for arrow clicks.
void ToggleHierarchyCollapsed(HierarchyViewState& state, const HierarchyRow& row);

// Result of turning a POST /hierarchy_action request into the action the panel would have recorded.
struct HierarchyActionPlan {
    bool ok = false;
    std::string error;
    PendingHierarchyAction action; // Reparent / Reorder
    HierarchyRow row;              // Toggle: the target row
};

// Row data (parent, siblingPosition) comes from `rows`, exactly as a real drop records it.
HierarchyActionPlan PlanHierarchyAction(
    const Registry& registry, std::span<const HierarchyRow> rows, const HierarchyActionCommand& request);

// Rows of the report for `rows[begin, end)` (clamped).
std::vector<HierarchyStateRow> BuildHierarchyStateRows(const Registry& registry, const HierarchyViewState& state,
    const Selection& selection, std::uint64_t begin, std::uint64_t end);

// Rows shown on each side of the target in a POST /hierarchy_action reply.
inline constexpr std::uint32_t kHierarchyActionContextRows = 5;

// Refreshes the rows, plans and applies the request, refreshes again and reports the rows around the target.
HierarchyActionReport RunHierarchyAction(
    Registry& registry, HierarchyViewState& state, const Selection& selection, const HierarchyActionCommand& request);

// Report of the rows drawn on the last frame, `count` rows from `first` (clamped to the row list).
HierarchyStateReport BuildHierarchyStateReport(const Registry& registry, const HierarchyViewState& state,
    const Selection& selection, std::uint64_t first, std::uint64_t count);

} // namespace gte
