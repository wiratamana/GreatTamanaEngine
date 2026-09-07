#include "ModelRigCache.h"

#include "ProjectPanelData.h" // Utf8ToPath()
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()

namespace gte {

const RigFileData* ModelRigCache::GetOrLoad(const std::string& absoluteGtaPath)
{
    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(Utf8ToPath(absoluteGtaPath), timeEc);

    if (!absoluteGtaPath.empty() && absoluteGtaPath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        return m_cachedRig.has_value() ? &m_cachedRig.value() : nullptr;
    }

    m_cachedPath = absoluteGtaPath;
    m_cachedRig.reset();
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8ToPath(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return nullptr;
    }

    m_cachedRig = DecodeRigDataFromBytes(gta->metadata);
    // A well-formed but EMPTY metadata blob (a Mesh *.gta imported before
    // rig extraction existed) fails DecodeRigDataFromBytes() (bad
    // magic/too-short) rather than decoding to an empty RigFileData - map
    // that specific case to a valid, empty RigFileData instead of nullptr,
    // matching RigFile.h's own "a boneless/riggless mesh simply has an
    // empty metadata blob" documented convention (see
    // BoneViewerWindow::EnsureDataLoaded()'s own equivalent handling today).
    if (!m_cachedRig.has_value() && gta->metadata.empty()) {
        m_cachedRig = RigFileData{};
    }

    return m_cachedRig.has_value() ? &m_cachedRig.value() : nullptr;
}

} // namespace gte
