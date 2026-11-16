#pragma once

// Small CPU-drawn meshes next to the terrain pool: the live preview chunk (host-visible vertices, rewritten in place
// on every slider move, so there is no mesh swap and no GPU drain) and a flat ocean quad at sea level.

#include "../../Core/GraphicsHandles.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../Renderer/MeshHandle.h"
#include "../../Renderer/OffscreenFrameTicket.h"
#include "../../Renderer/OffscreenRingGuard.h"
#include "../../Renderer/PipelineHandle.h"
#include "../../Terrain/Gpu/TerrainNodePacking.h"
#include "../../Terrain/TerrainTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

class Game;
class Renderer;

class TerrainAuxMeshes {
public:
    TerrainAuxMeshes() = default;
    ~TerrainAuxMeshes();
    TerrainAuxMeshes(const TerrainAuxMeshes&) = delete;
    TerrainAuxMeshes& operator=(const TerrainAuxMeshes&) = delete;

    // `previewQuads` quads per side for the preview chunk (128 gives 129x129 vertices). The water quad covers
    // +-`waterHalfExtent` metres at height 0.
    bool Build(Game& game, Renderer& renderer, DescriptorSetLayoutHandle sceneServicesSetLayout, int previewQuads,
        double waterHalfExtent, std::string* error);

    // Releases both meshes and the pipeline after the GPU went idle. False when it did not.
    bool Reset();

    bool Built() const noexcept { return m_game != nullptr; }
    std::uint32_t PreviewVertexCount() const noexcept { return m_previewVertices; }

    MeshRenderer PreviewRenderer() const noexcept;
    MeshRenderer WaterRenderer() const noexcept;

    // Copies the node's vertices into the preview buffer. False when the node does not fit, or when a frame in
    // flight still reads the buffer (retry on a later frame).
    bool UploadPreview(const terrain::NodeMesh& node, const OffscreenFrameTicket& ticket);

    // Records that the frame of `ticket` draws the preview, so a later write waits for it.
    void NotePreviewRead(const OffscreenFrameTicket& ticket) noexcept { m_previewUse.Note(ticket); }

private:
    Game* m_game = nullptr;
    ::gte::Renderer* m_renderer = nullptr;
    MeshHandle m_preview;
    MeshHandle m_water;
    PipelineHandle m_pipeline;
    std::uint32_t m_previewVertices = 0;
    RingCopyUse m_previewUse;
    std::vector<terrain::gpu::PreviewVertex> m_scratch;
};

} // namespace gte
