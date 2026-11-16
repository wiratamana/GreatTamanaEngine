#include "TerrainGpuPool.h"

#include "../../Core/Logging.h"
#include "../../Game/Game.h"
#include "../../Renderer/Culling/CullingTypes.h"
#include "../../Renderer/MaterialImageRole.h"
#include "../../Renderer/MaterialParams.h"
#include "../../Renderer/MaterialTexture.h"
#include "../../Renderer/Mesh.h"
#include "../../Renderer/Renderer.h"
#include "../../Terrain/TerrainTypes.h"

#include <algorithm>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <span>
#include <utility>

namespace gte {

namespace {

constexpr const char* kLogCategory = "TerrainGpuPool";
constexpr std::size_t kZeroChunkBytes = 4u * 1024u * 1024u;

AABB ToAabb(const terrain::gpu::BoxF& box) noexcept
{
    return AABB{ Vec3(box.min[0], box.min[1], box.min[2]), Vec3(box.max[0], box.max[1], box.max[2]) };
}

// Uploads the ramp image as a base colour material and returns it (not yet registered).
MaterialTexture MakeRampMaterial(Renderer& renderer, const terrain::gpu::RampImage& ramp)
{
    const MaterialImageUpload upload{ ramp.rgba.data(), ramp.size, ramp.size, MaterialImageRole::BaseColor, "TerrainRamp" };
    std::vector<std::shared_ptr<Texture2D>> created = renderer.CreateMaterialImages(std::span<const MaterialImageUpload>(&upload, 1));
    if (created.empty() || created[0] == nullptr) {
        throw std::runtime_error("the ramp image upload failed");
    }
    MaterialImages images;
    images[static_cast<std::size_t>(MaterialImageRole::BaseColor)] = std::move(created[0]);
    return renderer.CreateMaterialVariant(images, MakeMaterialParams(1.0f, 1.0f, 1.0f, 1.0f, 0.5f));
}

} // namespace

TerrainGpuPool::~TerrainGpuPool()
{
    // No retry is possible here: a busy GPU leaves the handles registered with the render system.
    static_cast<void>(Reset());
}

MeshRenderer TerrainGpuPool::MakeMeshRenderer() const noexcept
{
    MeshRenderer renderer;
    renderer.mesh = m_mesh;
    renderer.pipeline = m_pipeline;
    renderer.texture = m_texture;
    return renderer;
}

bool TerrainGpuPool::Reset()
{
    if (m_game == nullptr) {
        return true;
    }
    if (m_renderer != nullptr && m_renderer->DrainGpuForResourceRelease("terrain pool") != BlockOutcome::Completed) {
        return false;
    }
    RenderSystem& renderSystem = m_game->GetRenderSystem();
    renderSystem.ReleaseMesh(m_mesh);
    renderSystem.UnregisterTexture(m_texture);
    renderSystem.UnregisterPipeline(m_pipeline);
    m_mesh = MeshHandle{};
    m_texture = MaterialTextureHandle{};
    m_pipeline = PipelineHandle{};
    m_slots.reset();
    m_table.reset();
    m_slotBox.clear();
    m_scratch.clear();
    m_stats = TerrainPoolStats{};
    m_game = nullptr;
    m_renderer = nullptr;
    return true;
}

bool TerrainGpuPool::Build(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
    const TerrainPoolParams& params, std::string* error)
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
    const terrain::gpu::PoolLayout& layout = params.layout;
    if (layout.slotCount == 0 || layout.vertsPerSlot == 0 || layout.indicesPerSlot == 0) {
        return fail("the pool layout is empty");
    }

    try {
        // Host-visible buffers: slot writes are plain memory copies, no transfer pass and no barrier.
        Buffer vertexBuffer = renderer.CreateBuffer(
            layout.VertexBytes(), BufferUsage::VertexBuffer, BufferMemoryUsage::CpuToGpu, "TerrainPool.Vertices");
        Buffer indexBuffer = renderer.CreateBuffer(
            layout.IndexBytes(), BufferUsage::IndexBuffer, BufferMemoryUsage::CpuToGpu, "TerrainPool.Indices");

        // Free slots are never drawn by the GPU path, but a fallback draw must not read garbage.
        {
            const std::vector<std::uint8_t> zeros(kZeroChunkBytes, 0);
            for (std::uint64_t offset = 0; offset < layout.VertexBytes(); offset += zeros.size()) {
                const std::size_t count = static_cast<std::size_t>(std::min<std::uint64_t>(zeros.size(), layout.VertexBytes() - offset));
                vertexBuffer.Upload(zeros.data(), count, static_cast<std::size_t>(offset));
            }
        }
        {
            const std::vector<std::uint32_t> local = terrain::BuildSharedIndices(params.settings.chunkQuads);
            if (local.size() != layout.indicesPerSlot) {
                return fail("the node index pattern does not match the pool layout");
            }
            std::vector<std::uint32_t> slotIndices(layout.indicesPerSlot);
            for (std::uint32_t slot = 0; slot < layout.slotCount; ++slot) {
                terrain::gpu::WriteSlotIndices(local, slot * layout.vertsPerSlot, slotIndices);
                indexBuffer.Upload(slotIndices.data(), slotIndices.size() * sizeof(std::uint32_t),
                    static_cast<std::size_t>(layout.SlotIndexOffsetBytes(slot)));
            }
        }

        Mesh mesh(std::move(vertexBuffer), static_cast<std::uint32_t>(layout.TotalVertices()), std::move(indexBuffer),
            static_cast<std::uint32_t>(layout.TotalIndices()));
        const AABB poolBox = ToAabb(params.worldBounds);
        mesh.SetLocalBounds(poolBox);

        m_table.emplace(layout);
        rg::GpuBuffer table(renderer.Device(), static_cast<std::size_t>(layout.slotCount) * sizeof(MeshCluster),
            "TerrainClusterTable", rg::GpuBufferUse::Storage | rg::GpuBufferUse::HostWrite);
        const std::span<const MeshCluster> records = m_table->Records();
        if (!table.Upload(records.data(), records.size_bytes(), 0)) {
            m_table.reset();
            return fail("the cluster table upload failed");
        }
        m_table->TakeDirtyRanges();
        mesh.SetClusterTable(std::move(table), layout.slotCount, poolBox);

        const terrain::gpu::RampImage ramp = terrain::gpu::BuildPaddedRamp(params.settings, params.rampSize, params.rampBorder);
        MaterialTexture material = MakeRampMaterial(renderer, ramp);
        Pipeline pipeline = renderer.CreatePipeline("shaders/TexturedMesh.vert.spv", "shaders/TexturedMesh.frag.spv",
            VertexLayout::PositionNormalUv, /*useMaterialTexture=*/true, "TerrainPool (TexturedMesh PositionNormalUv)",
            sceneServicesSetLayout);

        RenderSystem& renderSystem = game.GetRenderSystem();
        m_game = &game;
        m_renderer = &renderer;
        m_params = params;
        m_rampMap = ramp.uvMap;
        m_slots.emplace(layout.slotCount);
        m_slotBox.assign(layout.slotCount, terrain::gpu::BoxF{});
        m_scratch.assign(layout.vertsPerSlot, terrain::TerrainVertex{});
        m_stats = TerrainPoolStats{};
        m_stats.slots = layout.slotCount;
        m_stats.vertexBytes = layout.VertexBytes();
        m_stats.indexBytes = layout.IndexBytes();
        m_stats.tableBytes = static_cast<std::uint64_t>(layout.slotCount) * sizeof(MeshCluster);
        m_mesh = renderSystem.RegisterMesh(std::move(mesh));
        m_texture = renderSystem.RegisterTexture(std::move(material));
        m_pipeline = renderSystem.RegisterPipeline(std::move(pipeline));
        return true;
    } catch (const std::exception& e) {
        m_table.reset();
        Reset();
        return fail(std::string("terrain pool failed: ") + e.what());
    }
}

std::optional<std::uint32_t> TerrainGpuPool::WriteNode(const terrain::NodeMesh& node, const terrain::gpu::SlotFrame& frame)
{
    if (!Built() || node.vertices.size() != m_params.layout.vertsPerSlot) {
        return std::nullopt;
    }
    const std::optional<std::uint32_t> slot = m_slots->Acquire(frame);
    if (!slot.has_value()) {
        return std::nullopt;
    }
    Mesh* mesh = m_game->GetRenderSystem().TryGetMesh(m_mesh);
    if (mesh == nullptr) {
        m_slots->Abandon(*slot);
        return std::nullopt;
    }
    try {
        const terrain::gpu::BoxF box = terrain::gpu::PackNodeVertices(node, m_rampMap, m_scratch);
        if (!terrain::gpu::IsUsableBox(box)) {
            m_slots->Abandon(*slot);
            return std::nullopt;
        }
        mesh->WriteVertexRange(m_scratch.data(), m_scratch.size() * sizeof(terrain::TerrainVertex),
            static_cast<std::size_t>(m_params.layout.SlotVertexOffsetBytes(*slot)));
        m_slotBox[*slot] = box;
    } catch (const std::exception& e) {
        GTE_LOG_WARNING(kLogCategory, std::string("node write failed: ") + e.what());
        m_slots->Abandon(*slot);
        return std::nullopt;
    }
    m_slots->MarkWritten(*slot);
    ++m_stats.slotWrites;
    return slot;
}

bool TerrainGpuPool::Enable(std::uint32_t slot)
{
    // Only a written slot has vertices; enabling anything else would draw stale data.
    if (!Built() || slot >= m_params.layout.slotCount || m_slots->State(slot) != terrain::gpu::SlotState::Written) {
        return false;
    }
    return m_table->SetLive(slot, m_slotBox[slot]);
}

bool TerrainGpuPool::Disable(std::uint32_t slot)
{
    return Built() && slot < m_params.layout.slotCount && m_table->SetInactive(slot);
}

bool TerrainGpuPool::IsEnabled(std::uint32_t slot) const noexcept
{
    return Built() && m_table->IsLive(slot);
}

void TerrainGpuPool::Release(std::uint32_t slot, const terrain::gpu::SlotFrame& frame)
{
    if (!Built() || slot >= m_params.layout.slotCount) {
        return;
    }
    // Hide first: the record changes before the slot can ever be handed out again.
    m_table->SetInactive(slot);
    m_slots->Release(slot, frame);
}

bool TerrainGpuPool::UploadTableRanges()
{
    Mesh* mesh = m_game->GetRenderSystem().TryGetMesh(m_mesh);
    rg::GpuBuffer* table = mesh != nullptr ? mesh->MutableClusterTable() : nullptr;
    if (table == nullptr) {
        return false;
    }
    const std::span<const MeshCluster> records = m_table->Records();
    bool ok = true;
    for (const terrain::gpu::SlotRange& range : m_table->TakeDirtyRanges()) {
        ok = table->Upload(records.data() + range.first, static_cast<std::size_t>(range.count) * sizeof(MeshCluster),
                 static_cast<std::size_t>(range.first) * sizeof(MeshCluster))
            && ok;
    }
    return ok;
}

bool TerrainGpuPool::ReplaceWholeTable(std::string* error)
{
    Mesh* mesh = m_game->GetRenderSystem().TryGetMesh(m_mesh);
    if (mesh == nullptr) {
        return false;
    }
    try {
        rg::GpuBuffer fresh(m_renderer->Device(), static_cast<std::size_t>(m_params.layout.slotCount) * sizeof(MeshCluster),
            "TerrainClusterTable", rg::GpuBufferUse::Storage | rg::GpuBufferUse::HostWrite);
        const std::span<const MeshCluster> records = m_table->Records();
        if (!fresh.Upload(records.data(), records.size_bytes(), 0)) {
            return false;
        }
        const std::optional<AABB> bounds = mesh->ClusterBounds();
        std::optional<rg::GpuBuffer> old = mesh->ReplaceClusterTable(std::move(fresh), m_params.layout.slotCount, bounds);
        m_table->TakeDirtyRanges();
        if (old.has_value()) {
            // A frame in flight may still read the old table.
            m_renderer->Retire(std::move(*old));
        }
        ++m_stats.tableReplacements;
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = e.what();
        }
        return false;
    }
}

bool TerrainGpuPool::Flush(const terrain::gpu::SlotFrame& frame)
{
    if (!Built()) {
        return false;
    }
    m_slots->Collect(frame);
    if (!m_table->AnyDirty()) {
        return true;
    }
    // A synchronous frame has nothing in flight, so the records are patched in place; otherwise the table is swapped.
    bool ok = false;
    std::string why;
    if (!frame.IsAsync()) {
        ok = UploadTableRanges();
    } else {
        ok = ReplaceWholeTable(&why);
    }
    if (!ok) {
        m_table->MarkAllDirty();
        GTE_LOG_WARNING(kLogCategory, "cluster table update failed" + (why.empty() ? std::string() : ": " + why));
        return false;
    }
    ++m_stats.tableFlushes;
    return true;
}

bool TerrainGpuPool::ReplaceRamp(const terrain::TerrainSettings& settings, std::string* error)
{
    if (!Built()) {
        return false;
    }
    if (m_renderer->DrainGpuForResourceRelease("terrain ramp") != BlockOutcome::Completed) {
        if (error != nullptr) {
            *error = "the GPU did not go idle";
        }
        return false;
    }
    try {
        const terrain::gpu::RampImage ramp = terrain::gpu::BuildPaddedRamp(settings, m_params.rampSize, m_params.rampBorder);
        RenderSystem& renderSystem = m_game->GetRenderSystem();
        const MaterialTextureHandle fresh = renderSystem.RegisterTexture(MakeRampMaterial(*m_renderer, ramp));
        renderSystem.UnregisterTexture(m_texture);
        m_texture = fresh;
        m_params.settings = settings;
        return true;
    } catch (const std::exception& e) {
        if (error != nullptr) {
            *error = e.what();
        }
        return false;
    }
}

TerrainPoolStats TerrainGpuPool::Stats() const noexcept
{
    TerrainPoolStats stats = m_stats;
    if (Built()) {
        stats.used = m_slots->UsedCount();
        stats.retiring = m_slots->RetiringCount();
        stats.enabled = m_table->LiveCount();
    }
    return stats;
}

} // namespace gte
