#include "AssetPreviewMesh.h"

#include "../Encoding/Utf8Path.h"
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/MeshFile.h" // DecodeMeshDataFromBytes()
#include "../Math/Quat.h"
#include "../Renderer/MeshPreviewRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace gte {

AssetPreviewMesh::AssetPreviewMesh() = default;

AssetPreviewMesh::~AssetPreviewMesh() = default;

void AssetPreviewMesh::Reset()
{
    m_preview.reset();
    m_previewBuildFailed = false;
    m_cachedPath.clear();
    m_cachedWriteTime = std::filesystem::file_time_type{};
    m_cachedIsValid = false;
    m_vertexCount = 0;
    m_indexCount = 0;
    m_clusterCount = 0;
}

bool AssetPreviewMesh::EnsureMeshUploaded(Renderer& renderer, const std::string& absolutePath)
{
    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(PathFromUtf8(absolutePath), timeEc);

    if (!absolutePath.empty() && absolutePath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        return true; // Unchanged since last call - whatever m_cachedIsValid says still holds.
    }

    if (!m_preview && !m_previewBuildFailed) {
        m_preview = MeshPreviewRenderer::TryCreate(renderer);
        m_previewBuildFailed = (m_preview == nullptr);
    }
    if (!m_preview) {
        return false;
    }

    m_cachedPath = absolutePath;
    m_cachedIsValid = false;
    m_vertexCount = 0;
    m_indexCount = 0;
    m_clusterCount = 0;
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absolutePath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        m_preview->SetMesh({}, {}); // Drops the previous asset's mesh.
        return true;
    }
    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        m_preview->SetMesh({}, {});
        return true;
    }

    const MeshPreviewData previewData = BuildMeshPreviewData(mesh->positions, mesh->normals);
    m_boundsCenter = previewData.center;
    m_boundsRadius = previewData.radius;

    if (!m_preview->SetMesh(previewData.vertices, mesh->indices)) {
        return true; // m_cachedIsValid stays false.
    }
    m_vertexCount = previewData.vertices.size();
    m_indexCount = mesh->indices.size();
    m_clusterCount = mesh->clusters.size();
    m_cachedIsValid = true;
    return true;
}

std::optional<AssetPreviewMesh::Preview> AssetPreviewMesh::Render(
    Renderer& renderer, const std::string& absolutePath, int viewportWidth, int viewportHeight)
{
    if (!EnsureMeshUploaded(renderer, absolutePath) || !m_cachedIsValid) {
        return std::nullopt;
    }

    const int width = std::max(1, viewportWidth);
    const int height = std::max(1, viewportHeight);

    // Spins around the mesh's own up axis; -m_boundsCenter is applied first so it spins about its visual middle.
    constexpr float kSpinDegreesPerSecond = 24.0f;
    const float angleDeg = std::fmod(static_cast<float>(ImGui::GetTime()) * kSpinDegreesPerSecond, 360.0f);
    const Quat spin = Quat::FromAxisAngle(Vec3::Up(), DegToRad(angleDeg));
    const Mat4 model = spin.ToMat4() * Mat4::Translation(-m_boundsCenter);

    const float radius = m_boundsRadius;
    const float fovYRadians = DegToRad(45.0f);
    // Distance that fits the bounding sphere in the vertical FOV, plus a margin.
    const float distance = (radius / std::tan(fovYRadians * 0.5f)) * 1.35f;
    const Vec3 eye(0.0f, radius * 0.35f, -distance);
    const Vec3 target(0.0f, radius * 0.05f, 0.0f);
    const Mat4 view = Mat4::LookAtLH(eye, target, Vec3::Up());
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const Mat4 proj =
        Mat4::PerspectiveFovLH_ZO(fovYRadians, aspect, std::max(0.01f, radius * 0.02f), distance + radius * 4.0f, /*flipY=*/true);

    const UiTextureHandle handle = m_preview->Render(model, proj * view, width, height, { 35, 35, 35, 255 });
    if (handle == UiTextureHandle::Invalid) {
        return std::nullopt;
    }

    Preview preview;
    preview.descriptor = handle;
    preview.width = m_preview->Width();
    preview.height = m_preview->Height();
    preview.vertexCount = m_vertexCount;
    preview.triangleCount = m_indexCount / 3;
    preview.clusterCount = m_clusterCount;
    return preview;
}

} // namespace gte
