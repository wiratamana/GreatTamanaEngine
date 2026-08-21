#include "Selection.h"

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

void Selection::Clear()
{
    m_kind = InspectorSelectionKind::None;
    m_entity = kInvalidEntity;
    m_assetAbsolutePath.clear();
    m_assetRelativePath.clear();
    m_assetIsDirectory = false;
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

} // namespace gte
