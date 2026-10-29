#pragma once

#include "../../ECS/Entity.h"
#include "../../ECS/HierarchySnapshot.h"
#include "../HierarchyStateReport.h"

#include <cstdint>
#include <optional>
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
    std::vector<HierarchyRow> rows;
    std::uint32_t hiddenFrames = 0;
    bool visible = false;                 // the panel was drawn on the last frame
    double lastBuildMs = 0.0;
    double lastFlattenMs = 0.0;
    std::uint32_t lastDrawnRows = 0;
};

// Frames in a row the panel may stay hidden before it frees its buffers.
inline constexpr std::uint32_t kHierarchyHiddenFramesBeforeRelease = 600;

// Label shown for an entity: its Name, else "Entity <index>"; cameras get a " (Camera)" suffix.
std::string BuildEntityLabel(const Registry& registry, Entity entity);

// Depth-first rows in sibling order. A collapsed node keeps its row and hides its descendants.
void FlattenHierarchyRows(
    const HierarchySnapshot& snapshot, const std::unordered_set<Entity>& collapsed, std::vector<HierarchyRow>& out);

// Removes every dead entity from `collapsed`.
void PruneCollapsedEntities(const Registry& registry, std::unordered_set<Entity>& collapsed);

// Prunes, rebuilds the snapshot and flattens the rows. Records the build and flatten times.
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

// Report of the rows drawn on the last frame, `count` rows from `first` (clamped to the row list).
HierarchyStateReport BuildHierarchyStateReport(const Registry& registry, const HierarchyViewState& state,
    const Selection& selection, std::uint64_t first, std::uint64_t count);

} // namespace gte
