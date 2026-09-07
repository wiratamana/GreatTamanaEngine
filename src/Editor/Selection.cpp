#include "Selection.h"

#include <algorithm>

namespace gte {

void Selection::SelectEntity(Entity entity)
{
    m_entity = entity;
    m_kind = InspectorSelectionKind::Entity;
}

void Selection::SelectAsset(std::string absolutePath, std::string relativePath, bool isDirectory)
{
    m_assetAbsolutePath = std::move(absolutePath);
    m_assetRelativePath = std::move(relativePath);
    m_assetIsDirectory = isDirectory;
    m_kind = InspectorSelectionKind::Asset;
}

void Selection::ClearAssetIfPath(const std::string& relativePath)
{
    if (m_assetRelativePath != relativePath) {
        return;
    }

    m_assetAbsolutePath.clear();
    m_assetRelativePath.clear();
    m_assetIsDirectory = false;

    if (m_kind == InspectorSelectionKind::Asset) {
        m_kind = InspectorSelectionKind::None;
    }
}

void Selection::SelectModelPart(Entity owningEntity, ModelPartKind partKind, int partIndex)
{
    SelectModelParts(owningEntity, partKind, std::vector<int>{ partIndex });
}

void Selection::SelectModelParts(Entity owningEntity, ModelPartKind partKind, std::vector<int> indices)
{
    // Canonicalize the stored set (sorted + de-duplicated) - membership
    // checks in IsModelPartSelected() would be correct either way, but a
    // predictable, stable order is what lets any future/ Phase 4 UI that
    // ITERATES SelectedModelPartIndices() (e.g. an Inspector "N selected"
    // name list) show a consistent, non-jumbled order across frames.
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    m_modelPartEntity = owningEntity;
    m_modelPartKind = partKind;
    m_modelPartIndices = std::move(indices);

    if (m_modelPartIndices.empty()) {
        // Selecting an empty set is a genuine CLEAR, not a "ModelPart
        // selected with nothing in it" limbo state - mirrors
        // ClearModelPartIfEntity()'s own "Kind() reverts to None" behavior
        // exactly, and means IsModelPartSelected() never needs to special-
        // case an empty set separately from "Kind() isn't ModelPart at all."
        m_modelPartEntity = kInvalidEntity;
        m_modelPartKind = ModelPartKind::Bone;
        if (m_kind == InspectorSelectionKind::ModelPart) {
            m_kind = InspectorSelectionKind::None;
        }
        return;
    }

    m_kind = InspectorSelectionKind::ModelPart;
}

void Selection::ToggleModelPartInSelection(Entity owningEntity, ModelPartKind partKind, int index)
{
    if (m_kind != InspectorSelectionKind::ModelPart || m_modelPartEntity != owningEntity
        || m_modelPartKind != partKind) {
        // No existing COMPATIBLE selection to extend (nothing selected yet,
        // a different entity/model was selected, or a different partKind
        // e.g. Bone vs RigidBody) - a fresh Ctrl-click always starts a brand
        // new one-element selection instead of mixing incompatible
        // selections together, exactly like a plain click already does.
        SelectModelPart(owningEntity, partKind, index);
        return;
    }

    const auto it = std::find(m_modelPartIndices.begin(), m_modelPartIndices.end(), index);
    if (it != m_modelPartIndices.end()) {
        m_modelPartIndices.erase(it); // Already selected - Ctrl/Shift-clicking it again removes it.
    } else {
        m_modelPartIndices.push_back(index);
        std::sort(m_modelPartIndices.begin(), m_modelPartIndices.end());
    }

    if (m_modelPartIndices.empty()) {
        // The last remaining selected item was just toggled off - same
        // "empty set means a real clear" contract as SelectModelParts().
        m_modelPartEntity = kInvalidEntity;
        m_modelPartKind = ModelPartKind::Bone;
        m_kind = InspectorSelectionKind::None;
    }
}

void Selection::ClearModelPartIfEntity(Entity owningEntity)
{
    if (m_modelPartEntity != owningEntity) {
        return;
    }

    m_modelPartEntity = kInvalidEntity;
    m_modelPartKind = ModelPartKind::Bone;
    m_modelPartIndices.clear();

    if (m_kind == InspectorSelectionKind::ModelPart) {
        m_kind = InspectorSelectionKind::None;
    }
}

void Selection::Clear()
{
    m_kind = InspectorSelectionKind::None;
    m_entity = kInvalidEntity;
    m_assetAbsolutePath.clear();
    m_assetRelativePath.clear();
    m_assetIsDirectory = false;
    m_modelPartEntity = kInvalidEntity;
    m_modelPartKind = ModelPartKind::Bone;
    m_modelPartIndices.clear();
}

bool Selection::IsEntitySelected(Entity entity) const
{
    return m_kind == InspectorSelectionKind::Entity && m_entity == entity;
}

bool Selection::IsAssetSelected(const std::string& relativePath) const
{
    return m_kind == InspectorSelectionKind::Asset && m_assetRelativePath == relativePath;
}

bool Selection::HasAssetSelection() const
{
    return m_kind == InspectorSelectionKind::Asset && !m_assetRelativePath.empty();
}

bool Selection::IsModelPartSelected(Entity owningEntity, ModelPartKind partKind, int partIndex) const
{
    if (m_kind != InspectorSelectionKind::ModelPart || m_modelPartEntity != owningEntity
        || m_modelPartKind != partKind) {
        return false;
    }
    return std::find(m_modelPartIndices.begin(), m_modelPartIndices.end(), partIndex) != m_modelPartIndices.end();
}

} // namespace gte
