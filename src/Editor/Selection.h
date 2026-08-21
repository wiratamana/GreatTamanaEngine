#pragma once

#include "../ECS/Entity.h"

#include <string>

namespace gte {

// Which "thing" the Inspector should currently display - an ECS entity
// (Hierarchy selection, Selection::SelectedEntity()) or a Project-panel
// file/folder (Project selection, Selection::SelectedAsset*() below) -
// whichever the user picked most recently. The two selections are
// otherwise completely independent and neither is ever cleared by picking
// the other - this only tracks which one is currently "on top" for
// InspectorPanel to show, exactly like Unity: clicking an entity in
// Hierarchy while an asset was selected in Project doesn't forget the
// Project selection, it just stops being what Inspector displays (and vice
// versa). Only ever becomes Asset when GTE_ENABLE_PROJECT_PANEL is ON (see
// Panels/ProjectPanel.cpp) - otherwise nothing ever sets it to anything but
// None/Entity. A free enum (not nested in Selection), same convention as
// GizmoOperation (TransformGizmo.h), so every panel can write
// `InspectorSelectionKind::Entity` unqualified.
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
// class ever assigns those fields. Plain data plus small pure mutators, the
// same "plain data, no virtual behavior" philosophy AGENTS.md already
// applies to ECS components (see ECS/Components/Transform.h); the point of
// centralizing this is purely to have ONE choke point for "the selection
// changed", not to add behavior of its own - a future Command-pattern
// implementation (e.g. SelectEntityCommand/SelectAssetCommand, for
// Hierarchy/Project selection to become undo-able) calls these exact same
// methods rather than reinventing its own selection-writing path.
class Selection {
public:
    // Makes `entity` the Hierarchy selection and the current Inspector
    // source (Kind() becomes Entity) - the Project/asset selection fields
    // are left completely untouched (see the class comment above).
    void SelectEntity(Entity entity);

    // Makes the given Project-panel entry the Project selection and the
    // current Inspector source (Kind() becomes Asset) - the Hierarchy/
    // entity selection is left completely untouched. `absolutePath` is the
    // real on-disk path (used by InspectorPanel to gather metadata/attempt
    // an image preview); `relativePath` is the same entry's
    // ProjectEntry::relativePath, purely for display; `isDirectory` is
    // whether the entry is a folder rather than a file. An empty
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
    // Kind() is Entity (mirrors the Unity behavior described above: an
    // entity remains "selected" for highlighting purposes only while it's
    // also what Inspector is currently showing).
    bool IsEntitySelected(Entity entity) const;

    // True if `relativePath` is exactly the current Project selection -
    // deliberately NOT gated on Kind(), so ProjectPanel keeps highlighting
    // its own last-selected row even while an entity is currently "on top"
    // for Inspector (exactly like Unity's Project window keeps its own
    // highlight independent of what the Inspector is currently showing).
    bool IsAssetSelected(const std::string& relativePath) const;

private:
    InspectorSelectionKind m_kind = InspectorSelectionKind::None;

    Entity m_entity = kInvalidEntity;

    std::string m_assetAbsolutePath;
    std::string m_assetRelativePath;
    bool m_assetIsDirectory = false;
};

} // namespace gte
