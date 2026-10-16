#pragma once

// Offscreen preview of one position + normal mesh, shown in an editor panel as a UI texture.

#include "../Core/UiTextureHandle.h"
#include "../Math/Mat4.h"
#include "../Math/Vec3.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gte {

class Renderer;

// Vertex layout of the preview shader (24 bytes).
struct MeshPreviewVertex {
    float position[3];
    float normal[3];
};
static_assert(sizeof(MeshPreviewVertex) == 24, "layout differs from MeshPreview.vert");

// Preview vertices plus the bounding sphere (centre of the AABB, radius from it) used to frame a camera.
struct MeshPreviewData {
    std::vector<MeshPreviewVertex> vertices;
    Vec3 center = Vec3::Zero();
    float radius = 1.0f;
};

// Converts positions and normals (up when `normals` does not match `positions`) into preview vertices and bounds.
// Empty `positions` gives empty vertices. A degenerate radius becomes 1.
MeshPreviewData BuildMeshPreviewData(std::span<const Vec3> positions, std::span<const Vec3> normals);

// Owns the preview pipeline, one mesh and one render target. The Renderer must outlive it.
class MeshPreviewRenderer {
public:
    // Null (one error logged) when the pipeline or its helper resources cannot be built.
    static std::unique_ptr<MeshPreviewRenderer> TryCreate(Renderer& renderer);

    ~MeshPreviewRenderer();

    MeshPreviewRenderer(const MeshPreviewRenderer&) = delete;
    MeshPreviewRenderer& operator=(const MeshPreviewRenderer&) = delete;

    // Replaces the mesh. False, with no mesh left, for empty input or a failed upload.
    // Stalls the GPU when a mesh was set before: use it for rare, user-driven changes only.
    bool SetMesh(std::span<const MeshPreviewVertex> vertices, std::span<const std::uint32_t> indices);

    // Draws the mesh into the internal target, resized to at least 1x1, and returns the texture for ImGui::Image().
    // Invalid when no mesh is set or the target cannot be made.
    UiTextureHandle Render(const Mat4& model, const Mat4& viewProjection, int width, int height,
        const std::array<std::uint8_t, 4>& clearColorRgba);

    int Width() const noexcept;
    int Height() const noexcept;

private:
    struct Impl;
    // Lets std::make_unique call the constructor while only TryCreate can make the key.
    struct CreateKey {
        explicit CreateKey() = default;
    };

public:
    MeshPreviewRenderer(CreateKey, std::unique_ptr<Impl> impl) noexcept;

private:
    std::unique_ptr<Impl> m_impl;
};

} // namespace gte
