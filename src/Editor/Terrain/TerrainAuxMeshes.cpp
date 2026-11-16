#include "TerrainAuxMeshes.h"

#include "../../Game/Game.h"
#include "../../Renderer/Culling/CullingTypes.h"
#include "../../Renderer/Mesh.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/Vertex.h"

#include <array>
#include <cstddef>
#include <exception>
#include <utility>
#include <vector>

namespace gte {

namespace {

static_assert(sizeof(terrain::gpu::PreviewVertex) == sizeof(Vertex));
static_assert(offsetof(terrain::gpu::PreviewVertex, normal) == offsetof(Vertex, normal));
static_assert(offsetof(terrain::gpu::PreviewVertex, color) == offsetof(Vertex, color));

constexpr float kWaterColor[3] = { 0.05f, 0.24f, 0.42f };

// Ocean quad at height 0, front face up, same winding as the terrain grid.
std::array<terrain::gpu::PreviewVertex, 4> WaterVertices(float half)
{
    std::array<terrain::gpu::PreviewVertex, 4> v{};
    const float xs[4] = { -half, half, -half, half };
    const float zs[4] = { -half, -half, half, half };
    for (int i = 0; i < 4; ++i) {
        v[static_cast<std::size_t>(i)] = { { xs[i], 0.0f, zs[i] }, { 0.0f, 1.0f, 0.0f }, { kWaterColor[0], kWaterColor[1], kWaterColor[2] } };
    }
    return v;
}

} // namespace

TerrainAuxMeshes::~TerrainAuxMeshes()
{
    static_cast<void>(Reset());
}

bool TerrainAuxMeshes::Reset()
{
    if (m_game == nullptr) {
        return true;
    }
    if (m_renderer != nullptr && m_renderer->DrainGpuForResourceRelease("terrain aux meshes") != BlockOutcome::Completed) {
        return false;
    }
    RenderSystem& renderSystem = m_game->GetRenderSystem();
    renderSystem.ReleaseMesh(m_preview);
    renderSystem.ReleaseMesh(m_water);
    renderSystem.UnregisterPipeline(m_pipeline);
    m_preview = MeshHandle{};
    m_water = MeshHandle{};
    m_pipeline = PipelineHandle{};
    m_previewVertices = 0;
    m_previewUse.Reset();
    m_scratch.clear();
    m_game = nullptr;
    m_renderer = nullptr;
    return true;
}

bool TerrainAuxMeshes::Build(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    int previewQuads, double waterHalfExtent, std::string* error)
{
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (!Reset()) {
        return fail("the GPU did not go idle");
    }
    if (previewQuads < 2 || previewQuads > 512) {
        return fail("preview size is out of range");
    }

    try {
        const std::uint32_t vertexCount = terrain::NodeVertexCount(previewQuads);
        const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * sizeof(terrain::gpu::PreviewVertex);
        Buffer vertexBuffer = renderer.CreateBuffer(vertexBytes, BufferUsage::VertexBuffer, BufferMemoryUsage::CpuToGpu,
            "TerrainPreview.Vertices");
        const std::vector<std::uint8_t> zeros(vertexBytes, 0);
        vertexBuffer.Upload(zeros.data(), zeros.size(), 0);
        const std::vector<std::uint32_t> indices = terrain::BuildSharedIndices(previewQuads);
        Buffer indexBuffer = renderer.CreateDeviceLocalBuffer(
            indices.data(), indices.size() * sizeof(std::uint32_t), BufferUsage::IndexBuffer, "TerrainPreview.Indices");
        Mesh previewMesh(std::move(vertexBuffer), vertexCount, std::move(indexBuffer), static_cast<std::uint32_t>(indices.size()));

        const float half = static_cast<float>(waterHalfExtent);
        const std::array<terrain::gpu::PreviewVertex, 4> water = WaterVertices(half);
        const std::array<std::uint32_t, 6> waterIndices = { 0, 1, 3, 3, 2, 0 };
        Mesh waterMesh = renderer.CreateMesh(water.data(), sizeof(water), 4, waterIndices.data(), sizeof(waterIndices), 6,
            "TerrainWater");
        waterMesh.SetLocalBounds(AABB{ Vec3(-half, -1.0f, -half), Vec3(half, 1.0f, half) });

        Pipeline pipeline = renderer.CreatePipeline("shaders/Triangle.vert.spv", "shaders/Triangle.frag.spv",
            VertexLayout::PositionColor, /*useMaterialTexture=*/false, "TerrainAux (Triangle PositionColor)",
            sceneServicesSetLayout);

        RenderSystem& renderSystem = game.GetRenderSystem();
        m_game = &game;
        m_renderer = &renderer;
        m_previewVertices = vertexCount;
        m_previewUse.Reset();
        m_scratch.assign(vertexCount, terrain::gpu::PreviewVertex{});
        m_preview = renderSystem.RegisterMesh(std::move(previewMesh));
        m_water = renderSystem.RegisterMesh(std::move(waterMesh));
        m_pipeline = renderSystem.RegisterPipeline(std::move(pipeline));
        return true;
    } catch (const std::exception& e) {
        Reset();
        return fail(std::string("preview meshes failed: ") + e.what());
    }
}

MeshRenderer TerrainAuxMeshes::PreviewRenderer() const noexcept
{
    MeshRenderer renderer;
    renderer.mesh = m_preview;
    renderer.pipeline = m_pipeline;
    return renderer;
}

MeshRenderer TerrainAuxMeshes::WaterRenderer() const noexcept
{
    MeshRenderer renderer;
    renderer.mesh = m_water;
    renderer.pipeline = m_pipeline;
    return renderer;
}

bool TerrainAuxMeshes::UploadPreview(const terrain::NodeMesh& node, const OffscreenFrameTicket& ticket)
{
    if (!Built() || node.vertices.size() != m_previewVertices) {
        return false;
    }
    Mesh* mesh = m_game->GetRenderSystem().TryGetMesh(m_preview);
    if (mesh == nullptr || !m_previewUse.Claim(ticket)) {
        return false;
    }
    try {
        terrain::gpu::PackPreviewVertices(node, m_scratch);
        mesh->WriteVertexRange(m_scratch.data(), m_scratch.size() * sizeof(terrain::gpu::PreviewVertex), 0);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

} // namespace gte
