#pragma once

#include "../ECS/Entity.h"

#include <string>

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
// otherwise nothing ever sets it to anything but None/Entity. A free enum
// (not nested in Selection), same convention as GizmoOperation
// (TransformGizmo.h), so every panel can write `InspectorSelectionKind::
// Entity` unqualified.
enum class InspectorSelectionKind {
    None,
    Entity,
    Asset,
};

// The single gate-keeper for every Hierarchy-entity / Project-asset
// selection in the Editor - EditorContext holds exactly one of these
// (EditorContext::selection) and every panel that used to write
// ctx.selectedEntity/ctx.inspectorSelectionKind/ctx.selectedAsset* directly
// (HierarchyPanel, ProjectPanel) now goes through SelectEntity()/
// SelectAsset()/ClearAssetIfPath() below instead - nothing outside this
// class ever assigns those fields. Just as importantly, no panel keeps its
// own local "am I highlighted" state either (e.g. a `m_selectedRelativePath`
// member) - every highlight check (HierarchyPanel's entity row,
// ProjectPanel's asset row) reads back through IsEntitySelected()/
// IsAssetSelected() below, both of which are gated on Kind(), so selecting
// an entity always visibly clears any highlighted Project row and vice
// versa - there is never a moment where two different things appear
// selected in two different panels at once. Plain data plus small pure
// mutators, the same "plain data, no virtual behavior" philosophy AGENTS.md
// already applies to ECS components (see ECS/Components/Transform.h); the
// point of centralizing this is purely to have ONE choke point for "the
// selection changed", not to add behavior of its own - a future
// Command-pattern implementation (e.g. SelectEntityCommand/
// SelectAssetCommand, for Hierarchy/Project selection to become undo-able)
// calls these exact same methods rather than reinventing its own
// selection-writing path.
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

    // Resets every field to its default (Kind() becomes None, no entity, no
    // asset) - not currently called by any panel, kept for a future
    // whole-selection reset (e.g. loading a new scene) rather than
    // reinventing one later.
    void Clear();

    InspectorSelectionKind Kind() const { return m_kind; }
    Entity SelectedEntity() const { return m_entity; }
    const std::string& SelectedAssetAbsolutePath() const { return m_assetAbsolutePath; }
    const std::string& SelectedAssetRelativePath() const { return m_assetRelativePath; }
    bool SelectedAssetIsDirectory() const { return m_assetIsDirectory; }

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

private:
    InspectorSelectionKind m_kind = InspectorSelectionKind::None;

    Entity m_entity = kInvalidEntity;

    std::string m_assetAbsolutePath;
    std::string m_assetRelativePath;
    bool m_assetIsDirectory = false;
};

} // namespace gte
