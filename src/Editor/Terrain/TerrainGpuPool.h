#pragma once

// GPU side of the streamed terrain: one mesh made of a fixed pool of node slots, one cluster per slot.
// The GPU-driven draw path culls the clusters (frustum, then Hi-Z) and draws the survivors with one indirect call.
// A slot is written (vertices) first and enabled (cluster record) second, so a node is never drawn half-written.

#include "../../Core/GraphicsHandles.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../Renderer/MaterialTextureHandle.h"
#include "../../Renderer/MeshHandle.h"
#include "../../Renderer/OffscreenFrameTicket.h"
#include "../../Renderer/PipelineHandle.h"
#include "../../Terrain/Gpu/TerrainClusterRecords.h"
#include "../../Terrain/Gpu/TerrainNodePacking.h"
#include "../../Terrain/Gpu/TerrainPoolLayout.h"
#include "../../Terrain/Gpu/TerrainRampImage.h"
#include "../../Terrain/Gpu/TerrainSlotAllocator.h"
#include "../../Terrain/TerrainSettings.h"
#include "../../Terrain/TerrainTypes.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

class Game;
class Renderer;

struct TerrainPoolParams {
    terrain::gpu::PoolLayout layout;
    terrain::gpu::BoxF worldBounds; // Constant union box of the cluster table (see terrain::gpu::PoolBounds).
    terrain::TerrainSettings settings;
    int rampSize = 128;
    int rampBorder = 4;
};

struct TerrainPoolStats {
    std::uint32_t slots = 0;
    std::uint32_t used = 0;     // Slots holding a node.
    std::uint32_t enabled = 0;  // Slots whose cluster is live.
    std::uint32_t retiring = 0;
    std::uint64_t vertexBytes = 0;
    std::uint64_t indexBytes = 0;
    std::uint64_t tableBytes = 0;
    std::uint64_t slotWrites = 0;       // Total node writes since the pool was built.
    std::uint64_t tableFlushes = 0;
    std::uint64_t tableReplacements = 0; // Overlap mode only.
};

class TerrainGpuPool {
public:
    TerrainGpuPool() = default;
    ~TerrainGpuPool();
    TerrainGpuPool(const TerrainGpuPool&) = delete;
    TerrainGpuPool& operator=(const TerrainGpuPool&) = delete;

    // Creates the mesh, pipeline and ramp material and registers them. Blocks while the buffers are filled.
    bool Build(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout,
        const TerrainPoolParams& params, std::string* error);

    // Releases everything after the GPU went idle. False (nothing released) when it did not.
    bool Reset();

    bool Built() const noexcept { return m_game != nullptr; }
    const terrain::gpu::PoolLayout& Layout() const noexcept { return m_params.layout; }

    // What the pool entity's MeshRenderer must hold.
    MeshRenderer MakeMeshRenderer() const noexcept;
    MaterialTextureHandle Texture() const noexcept { return m_texture; }

    // Writes the vertices of `node` into a free slot. nullopt when the pool is full or the write failed.
    std::optional<std::uint32_t> WriteNode(const terrain::NodeMesh& node, const terrain::gpu::SlotFrame& frame);

    // Makes a written slot visible to the cull, or hides it again. Both only change the CPU table copy until Flush().
    bool Enable(std::uint32_t slot);
    bool Disable(std::uint32_t slot);
    bool IsEnabled(std::uint32_t slot) const noexcept;

    // Hides the slot and hands it back; it is reused only when no frame in flight can still read it.
    void Release(std::uint32_t slot, const terrain::gpu::SlotFrame& frame);

    // Sends the changed cluster records to the GPU. Call once per frame after the writes. False on failure.
    bool Flush(const terrain::gpu::SlotFrame& frame);

    // Rebuilds the colour ramp for new material settings and swaps the material. The GPU is drained first.
    // The new handle is returned through Texture(); the caller must update the entity's MeshRenderer.
    bool ReplaceRamp(const terrain::TerrainSettings& settings, std::string* error);

    // World box of a node slot (valid while the slot holds a node).
    const terrain::gpu::BoxF& SlotBox(std::uint32_t slot) const noexcept { return m_slotBox[slot]; }

    TerrainPoolStats Stats() const noexcept;

private:
    bool UploadTableRanges();
    bool ReplaceWholeTable(std::string* error);

    Game* m_game = nullptr;
    ::gte::Renderer* m_renderer = nullptr;
    TerrainPoolParams m_params;
    MeshHandle m_mesh;
    PipelineHandle m_pipeline;
    MaterialTextureHandle m_texture;
    terrain::gpu::RampUvMap m_rampMap;

    std::optional<terrain::gpu::SlotAllocator> m_slots;
    std::optional<terrain::gpu::ClusterTableShadow> m_table;
    std::vector<terrain::gpu::BoxF> m_slotBox;
    std::vector<terrain::TerrainVertex> m_scratch;

    TerrainPoolStats m_stats;
};

} // namespace gte
