#pragma once
#include "../../Core/Extent.h"
#include "../../Core/GraphicsHandles.h"
#include "../../Math/Mat4.h"
#include "../../Renderer/Buffer.h"
#include "../../Renderer/Compute/ComputeDescriptorSet.h"
#include "../../Renderer/Compute/ComputePipeline.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderTexture.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace gte {

class Renderer;

// Per-view inputs for the shadow mask pass.
struct ShadowMaskInputs {
    // Stable, static-storage-duration string: debug name, import name and this view's key into m_viewStates.
    const char* outputName = nullptr;
    Extent2D viewExtent;
    Mat4 invViewProjection;
    Mat4 lightViewProjection;
    float depthBias = 0.0f;
    std::uint32_t shadowMapResolution = 0;
};

// Reconstructs world position from view depth + the shadow map, writes a
// single-channel (r8) shadow mask. A plain compute dispatch wrapper.
class ShadowMaskRenderer {
public:
    // Both depth textures are resolved (view + sampler) from the render graph inside the pass.
    // Returns an invalid handle if `shadowMap` is invalid.
    rg::TextureHandle AddMaskPass(rg::RenderGraphBuilder& builder, Renderer& renderer, const ShadowMaskInputs& inputs,
        rg::TextureHandle shadowMap, rg::TextureHandle sceneDepth);

private:
    void EnsurePipelineBuilt(Renderer& renderer);

    // One entry per calling view (Game, Scene) - never shared. Each view's
    // own descriptor set is rewritten and bound entirely within that same
    // AddMaskPass() call, so two views recorded into the same frame's
    // command buffer never alias each other's bound resource at GPU-execution time.
    struct ViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<Buffer> paramsBuffer;
        std::optional<RenderTexture> output;
    };
    ViewState& EnsureViewState(Renderer& renderer, const char* outputName, Extent2D extent);

    std::optional<ComputePipeline> m_pipeline;
    std::unordered_map<std::string, ViewState> m_viewStates;
};

} // namespace gte
