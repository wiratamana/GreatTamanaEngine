#pragma once

#include "../ECS/Entity.h"

#include <algorithm>
#include <string>
#include <vector>

namespace gte {

// Which "thing" the Inspector should currently display - an ECS entity
// (Hierarchy selection, Selection::SelectedEntity()) or a Project-panel
// file/folder (Project selection, Selection::SelectedAsset*() below) -
// whichever the user picked most recently. The underlying entity/asset
// fields are never cleared by picking the other (see SelectEntity()/
// SelectAsset() below), but Kind() is the SINGLE source of truth for which
// one is currently "active" - both InspectorPanel (which one to show) AND
// every panel's own highlight (HierarchyPanel's entity row, ProjectPanel's
// asset row - see IsEntitySelected()/IsAssetSelected() below) gate on this
// exact same value, so exactly one thing is ever visibly selected across
// the whole Editor at a time, never both at once. Only ever becomes Asset
// when GTE_ENABLE_PROJECT_PANEL is ON (see Panels/ProjectPanel.cpp) -
// otherwise nothing ever sets it to anything but None/Entity. Also becomes
// ModelPart when a bone/rigid-body/joint is selected inside the Bone Viewer
// window (BoneViewerWindow.h) - only ever reachable the same way Asset is,
// when GTE_ENABLE_PROJECT_PANEL is ON, since that window is only ever
// compiled then. A free enum (not nested in Selection), same convention as
// GizmoOperation (TransformGizmo.h), so every panel can write
// `InspectorSelectionKind::Entity` unqualified.
enum class InspectorSelectionKind {
    None,
    Entity,
    Asset,
    ModelPart,
};

// Which of the three categories a ModelPart selection (see
// InspectorSelectionKind::ModelPart above) refers to - the Bone Viewer's
// own "Bones / Rigid Bodies / Joints" dropdown (BoneViewerWindow.h,
// PHASE3_BONE_VIEWER_VIEW_MODE_DROPDOWN_AND_GIZMO.md) picks exactly one of
// these to display/select from at a time. A free enum (not nested in
// Selection), same convention as GizmoOperation (TransformGizmo.h) and
// InspectorSelectionKind above, so every panel can write
// `ModelPartKind::Bone` unqualified.
enum class ModelPartKind {
    Bone,
    RigidBody,
    Joint,
    Verlet, // task_manager/verlet-integration-5 - a physics-simulated
            // ("jiggle") bone chain joint, drawn/selected in the Bone
            // Viewer's "Verlet" mode - see BoneViewerWindow.h. partIndex
            // for this kind is the joint's own SKELETON BONE INDEX (the
            // same index space Bone mode already uses), NOT a freshly
            // flattened per-chain joint counter - see
            // Physics/DynamicChainDefinition.h's
            // FindDynamicChainJointByBoneIndex() for how a caller turns
            // this back into "which chain, which position in it."
};

// The single gate-keeper for every Hierarchy-entity / Project-asset /
// Bone-Viewer-model-part selection in the Editor - EditorContext holds
// exactly one of these (EditorContext::selection) and every panel that used
// to write ctx.selectedEntity/ctx.inspectorSelectionKind/ctx.selectedAsset*
// directly (HierarchyPanel, ProjectPanel) now goes through SelectEntity()/
// SelectAsset()/ClearAssetIfPath() below instead - nothing outside this
// class ever assigns those fields. Just as importantly, no panel keeps its
// own local "am I highlighted" state either (e.g. a `m_selectedRelativePath`
// member) - every highlight check (HierarchyPanel's entity row,
// ProjectPanel's asset row, BoneViewerWindow's tree row/gizmo dot) reads
// back through IsEntitySelected()/IsAssetSelected()/IsModelPartSelected()
// below, all three of which are gated on Kind(), so selecting one of these
// always visibly clears whatever was highlighted elsewhere - there is never
// a moment where two different things appear selected in two different
// panels at once. Plain data plus small pure mutators, the same "plain
// data, no virtual behavior" philosophy AGENTS.md already applies to ECS
// components (see ECS/Components/Transform.h); the point of centralizing
// this is purely to have ONE choke point for "the selection changed", not
// to add behavior of its own - a future Command-pattern implementation
// (e.g. SelectEntityCommand/SelectAssetCommand, for Hierarchy/Project
// selection to become undo-able) calls these exact same methods rather than
// reinventing its own selection-writing path. This class was extended once
// already, from Entity/Asset to also cover ModelPart (see
// task_manager/verlet-integration-2/PHASE1_SELECTION_MODEL_PART_FOUNDATION.md)
// - any future selectable "thing" should extend it the same way rather than
// adding a new ad hoc field elsewhere. Extended a second time in
// task_manager/verlet-integration-4/PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md
// to let the ModelPart selection hold MANY indices at once (a
// std::vector<int> instead of one int) - SelectModelPart()/
// SelectedModelPartIndex() stayed as single-element convenience wrappers so
// every pre-existing single-selection call site kept compiling unchanged.
class Selection {
public:
    // Makes `entity` the Hierarchy selection and the current Inspector
    // source (Kind() becomes Entity) - the Project/asset selection FIELDS
    // are left untouched (SelectedAssetAbsolutePath()/RelativePath() still
    // return whatever was last picked in Project), but since Kind() is now
    // Entity, IsAssetSelected()/HasAssetSelection() below immediately
    // report nothing selected - ProjectPanel's own row highlight and its
    // "Delete Selected" menu item both go blank/disabled the instant this
    // is called, exactly like Unity: picking an entity never leaves a
    // second thing looking selected elsewhere.
    void SelectEntity(Entity entity);

    // Makes the given Project-panel entry the Project selection and the
    // current Inspector source (Kind() becomes Asset) - the Hierarchy/
    // entity selection FIELD is left untouched (SelectedEntity() still
    // returns whatever was last picked in Hierarchy), but since Kind() is
    // now Asset, IsEntitySelected() below immediately reports it not
    // selected - HierarchyPanel's own row highlight goes blank the instant
    // this is called, for the same reason as SelectEntity() above.
    // `absolutePath` is the real on-disk path (used by InspectorPanel to
    // gather metadata/attempt an image preview); `relativePath` is the same
    // entry's ProjectEntry::relativePath, purely for display; `isDirectory`
    // is whether the entry is a folder rather than a file. An empty
    // `relativePath` means the Project root itself.
    void SelectAsset(std::string absolutePath, std::string relativePath, bool isDirectory);

    // Clears the Project/asset selection fields ONLY if they currently
    // refer to `relativePath` exactly - a no-op otherwise. If the current
    // Inspector source (Kind()) is Asset at the moment this matches, it
    // also reverts to None (Inspector then shows nothing, rather than
    // stale metadata for something that no longer exists) - if Kind() is
    // Entity, it stays Entity (this never touches the entity selection).
    // Used by ProjectPanel::DeleteSelected() so deleting the item Project
    // currently has selected/highlighted (regardless of whether Inspector
    // happens to be showing it or an entity right now) can never leave a
    // stale asset path behind.
    void ClearAssetIfPath(const std::string& relativePath);

    // Makes "part `partIndex` of kind `partKind`, belonging to `owningEntity`'s
    // own model" the ENTIRE current Model-Part selection (replacing whatever
    // was selected before, exactly like clicking a single row/dot always
    // has) and the current Inspector source (Kind() becomes ModelPart) - a
    // thin single-element convenience wrapper over SelectModelParts() below
    // (`SelectModelParts(owningEntity, partKind, { partIndex })`), kept so
    // every pre-existing single-click call site
    // (BoneViewerWindow.cpp's RenderBoneTreeNode()/RenderFlatPartRow()/direct
    // viewport-dot click) keeps compiling and behaving exactly as before.
    // See SelectModelParts() below for the multi-object version (task_manager/
    // verlet-integration-4/PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md).
    void SelectModelPart(Entity owningEntity, ModelPartKind partKind, int partIndex);

    // Replaces the ENTIRE current Model-Part selection with the exact set of
    // `indices` (de-duplicated and ascending-sorted internally - callers may
    // pass them in any order, with duplicates) and makes it the current
    // Inspector source (Kind() becomes ModelPart) - the multi-object
    // equivalent of SelectModelPart() above. Used by BoneViewerWindow's
    // "Select All (Group)"/"Select All (Branch)" toolbar buttons AND its own
    // genuine Shift-click contiguous range select (see task_manager/
    // verlet-integration-4/PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md)
    // to make MANY rigid bodies the current selection at once. An EMPTY
    // `indices` is treated as a genuine CLEAR of the Model-Part selection -
    // exactly like ClearModelPartIfEntity() (Kind() reverts to None if
    // ModelPart was currently on top) - "select all matching" that matched
    // nothing must never leave a stale "ModelPart selected, holding zero
    // parts" limbo state that IsModelPartSelected() would then have to
    // special-case separately.
    void SelectModelParts(Entity owningEntity, ModelPartKind partKind, std::vector<int> indices);

    // Adds `index` to the CURRENT Model-Part selection set if it is not
    // already present, or REMOVES it if it is - Ctrl-click's own "extend the
    // existing selection by exactly one item" semantics (Windows Explorer/
    // Unity style), as opposed to SelectModelPart()/SelectModelParts() above,
    // both of which always REPLACE the whole set. Also used by the Bone
    // tree's own Shift-click (BoneViewerWindow.cpp's RenderBoneTreeNode()) -
    // deliberately identical to its own Ctrl-click there, since a bone's raw
    // array index has no meaningful linear "range" the way a flat Rigid
    // Body/Joint row does (a genuine Shift-click range select for those two
    // is built on top of SelectModelParts() above instead - see PHASE3's own
    // FlatListRangeSelection.h). If the CURRENT selection does not already
    // belong to this exact `owningEntity`/`partKind` pair (Kind() is not
    // ModelPart yet, or it's a ModelPart selection for a DIFFERENT
    // entity/kind), this call first behaves exactly like
    // `SelectModelPart(owningEntity, partKind, index)` - a fresh Ctrl-click
    // on an unrelated part/entity/kind always starts a brand new
    // single-element selection rather than silently mixing incompatible
    // selections together. If, after toggling, the resulting set is empty
    // (the user Ctrl-clicked the last remaining selected item), this reverts
    // Kind() to None exactly like SelectModelParts({}) would.
    void ToggleModelPartInSelection(Entity owningEntity, ModelPartKind partKind, int index);

    // Clears the Model-Part selection fields ONLY if they currently refer to
    // `owningEntity` exactly (regardless of whatever partKind/partIndex they
    // currently hold) - a no-op otherwise. Mirrors ClearAssetIfPath()'s own
    // "clear only if it currently matches" shape exactly, just keyed on the
    // owning Entity rather than a path string (a bone/rigid-body/joint index is
    // meaningless without also knowing which entity's model it belongs to - see
    // SelectModelPart()'s own doc comment above). If the current Inspector
    // source (Kind()) is ModelPart at the moment this matches, it also reverts
    // to None (the Inspector then shows nothing, rather than stale info for a
    // part that may no longer mean the same thing against freshly reloaded
    // data) - if Kind() is Entity/Asset, it stays that way (this never touches
    // those fields). Used by BoneViewerWindow whenever `owningEntity`'s own
    // underlying model data genuinely reloads (a different file, or the same
    // file with a newer mtime) - a stale index from the PREVIOUS load means
    // nothing against the newly (re)loaded data, exactly the same reasoning
    // BoneViewerWindow's own (now-deleted) private `m_selectedBoneIndex` used
    // to reset to -1 for on every reload.
    void ClearModelPartIfEntity(Entity owningEntity);

    // Resets every field to its default (Kind() becomes None, no entity, no
    // asset, no model part) - not currently called by any panel, kept for a
    // future whole-selection reset (e.g. loading a new scene) rather than
    // reinventing one later.
    void Clear();

    InspectorSelectionKind Kind() const { return m_kind; }
    Entity SelectedEntity() const { return m_entity; }
    const std::string& SelectedAssetAbsolutePath() const { return m_assetAbsolutePath; }
    const std::string& SelectedAssetRelativePath() const { return m_assetRelativePath; }
    bool SelectedAssetIsDirectory() const { return m_assetIsDirectory; }

    Entity SelectedModelPartEntity() const { return m_modelPartEntity; }
    ModelPartKind SelectedModelPartKind() const { return m_modelPartKind; }

    // The lowest-numbered currently-selected Model-Part index, or -1 if
    // none is selected - a thin single-element convenience accessor over
    // SelectedModelPartIndices() below, kept so every pre-existing
    // single-selection reader (InspectorPanel.cpp's single-part property
    // sheet, before task_manager/verlet-integration-4/
    // PHASE4_INSPECTOR_MULTI_SELECTION_SUMMARY.md's own update) keeps
    // compiling and, for the single-selection case, returns byte-for-byte
    // the same value as before this phase (a one-element set's only
    // element IS its lowest element). NOTE: this is deliberately NOT "is
    // there exactly one selected" - a caller that needs to distinguish
    // "exactly one selected" from "several selected, this is merely the
    // lowest of them" (e.g. BoneViewerWindow.cpp's "Select All (...)"
    // button seed check - see PHASE3's own hasSeed fix, task_manager/
    // verlet-integration-4/PHASE0_MASTER_STRATEGY.md's Revision Notes,
    // finding #1) MUST also check SelectedModelPartIndices().size() == 1,
    // never rely on this accessor alone for that purpose.
    int SelectedModelPartIndex() const { return m_modelPartIndices.empty() ? -1 : m_modelPartIndices.front(); }

    // Every currently-selected Model-Part index, ascending-sorted, for
    // whichever (owningEntity, partKind) SelectedModelPartEntity()/
    // SelectedModelPartKind() currently report - empty if nothing is
    // selected. This is the SOURCE OF TRUTH for "how many/which" parts are
    // currently selected - SelectedModelPartIndex() above is only ever sugar
    // over this list's first (lowest) element.
    const std::vector<int>& SelectedModelPartIndices() const { return m_modelPartIndices; }

    // True if `entity` is exactly the current Hierarchy selection AND
    // Kind() is Entity - this is the ONLY thing HierarchyPanel's row
    // rendering may use to decide whether to highlight a row; it must never
    // keep its own separate "is this row selected" state.
    bool IsEntitySelected(Entity entity) const;

    // True if `relativePath` is exactly the current Project selection AND
    // Kind() is Asset - mirrors IsEntitySelected() above exactly, and for
    // the same reason: this is the ONLY thing ProjectPanel's row rendering
    // may use to decide whether to highlight a row. Gating on Kind() here
    // is what makes selecting an entity in Hierarchy immediately un-highlight
    // whatever was selected in Project (and vice versa via IsEntitySelected())
    // - ProjectPanel must never keep its own separate "is this row selected"
    // state (e.g. a local `m_selectedRelativePath`) to answer this instead.
    bool IsAssetSelected(const std::string& relativePath) const;

    // True if there is CURRENTLY a Project selection to act on - i.e. Kind()
    // is Asset and SelectedAssetRelativePath() is non-empty (an empty
    // relativePath means the Project root itself, which can never be
    // deleted). Used by ProjectPanel to enable/disable its "Delete
    // Selected" context-menu item - deliberately gated on Kind() exactly
    // like IsAssetSelected() above, so the menu item is disabled the moment
    // an entity becomes the active selection, matching the row highlight
    // disappearing at the same time (never "delete something that isn't
    // even highlighted anymore").
    bool HasAssetSelection() const;

    // True if `owningEntity`/`partKind`/`partIndex` are EXACTLY the current
    // Model-Part selection AND Kind() is ModelPart - this is the ONLY thing
    // BoneViewerWindow's tree-row/gizmo-dot rendering may use to decide whether
    // to highlight a row/dot; it must never keep its own separate "is this row
    // selected" state (see Selection.h's own class comment, and
    // PHASE0_MASTER_STRATEGY.md's Culprit C). Mirrors IsEntitySelected()/
    // IsAssetSelected() exactly, extended to three fields instead of one/two
    // since a bare index alone is ambiguous across both `partKind` and
    // `owningEntity`.
    bool IsModelPartSelected(Entity owningEntity, ModelPartKind partKind, int partIndex) const;

private:
    InspectorSelectionKind m_kind = InspectorSelectionKind::None;

    Entity m_entity = kInvalidEntity;

    std::string m_assetAbsolutePath;
    std::string m_assetRelativePath;
    bool m_assetIsDirectory = false;

    Entity m_modelPartEntity = kInvalidEntity;
    ModelPartKind m_modelPartKind = ModelPartKind::Bone;
    std::vector<int> m_modelPartIndices;
};

} // namespace gte
