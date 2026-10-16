#include "AssetPreviewMesh.h"

#include "ProjectPanelData.h" // Utf8ToPath()
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
}

bool AssetPreviewMesh::EnsureMeshUploaded(Renderer& renderer, const std::string& absolutePath)
{
    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime =
        std::filesystem::last_write_time(Utf8ToPath(absolutePath), timeEc);

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
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(Utf8ToPath(absolutePath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        m_preview->SetMesh({}, {}); // Drops the previous asset's mesh.
        return true;
    }
    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        m_preview->SetMesh({}, {});
        return true;
    }

    std::vector<MeshPreviewVertex> vertices(mesh->positions.size());
    const bool hasNormals = mesh->normals.size() == mesh->positions.size();
    Vec3 minBounds = mesh->positions[0];
    Vec3 maxBounds = mesh->positions[0];
    for (std::size_t i = 0; i < mesh->positions.size(); ++i) {
        const Vec3& p = mesh->positions[i];
        const Vec3 n = hasNormals ? mesh->normals[i] : Vec3::Up();
        vertices[i] = MeshPreviewVertex{ { p.x, p.y, p.z }, { n.x, n.y, n.z } };
        minBounds = Vec3(std::min(minBounds.x, p.x), std::min(minBounds.y, p.y), std::min(minBounds.z, p.z));
        maxBounds = Vec3(std::max(maxBounds.x, p.x), std::max(maxBounds.y, p.y), std::max(maxBounds.z, p.z));
    }

    m_boundsCenter = (minBounds + maxBounds) * 0.5f;
    float radius = 0.0f;
    for (const Vec3& p : mesh->positions) {
        radius = std::max(radius, Length(p - m_boundsCenter));
    }
    m_boundsRadius = radius > kEpsilon ? radius : 1.0f;

    if (!m_preview->SetMesh(vertices, mesh->indices)) {
        return true; // m_cachedIsValid stays false.
    }
    m_vertexCount = vertices.size();
    m_indexCount = mesh->indices.size();
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
    return preview;
}

} // namespace gte
