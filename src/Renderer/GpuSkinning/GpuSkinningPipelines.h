#pragma once

#include "../RenderGraph/GpuBuffer.h"
#include "../RenderGraph/RenderGraphHandles.h"
#include "../RenderGraph/Shader.h"

#include <cstdint>

namespace gte {

namespace rg {
class RenderGraphBuilder;
} // namespace rg

// One skinning dispatch: the model's input buffers, the output group's buffer and the graph handle it was imported as.
struct GpuSkinningDispatch {
    const rg::GpuBuffer* bindPose = nullptr;
    const rg::GpuBuffer* skinWeights = nullptr;
    const rg::GpuBuffer* boneMatrices = nullptr;
    const rg::GpuBuffer* uv = nullptr; // Set only for the textured variant.
    const rg::GpuBuffer* output = nullptr;
    rg::BufferHandle outputHandle;
    std::uint32_t vertexCount = 0;
    std::uint64_t instance = 0; // Unique per output group; gives each dispatch its own descriptor set.
};

// The two skinning compute shaders (untextured and textured), built lazily on first use.
// Every dispatch shares one pass name; the instance key tells the groups apart.
class GpuSkinningPipelines {
public:
    GpuSkinningPipelines() = default;
    ~GpuSkinningPipelines() = default;

    GpuSkinningPipelines(const GpuSkinningPipelines&) = delete;
    GpuSkinningPipelines& operator=(const GpuSkinningPipelines&) = delete;
    GpuSkinningPipelines(GpuSkinningPipelines&&) = delete;
    GpuSkinningPipelines& operator=(GpuSkinningPipelines&&) = delete;

    // Declares the dispatch. False when nothing was declared (shader not ready or a missing buffer); the shader logs why.
    bool Declare(rg::RenderGraphBuilder& builder, const GpuSkinningDispatch& dispatch, rg::ViewScope scope,
        rg::RenderPassEvent event);

private:
    rg::Shader m_positionNormal{ "SkinVerticesPositionNormal" };
    rg::Shader m_positionNormalUv{ "SkinVerticesPositionNormalUv" };
};

// Must match `local_size_x` of both skinning shaders.
inline constexpr std::uint32_t kSkinningLocalSizeX = 256;

} // namespace gte
