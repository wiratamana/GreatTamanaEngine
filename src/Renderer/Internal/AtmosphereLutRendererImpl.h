#pragma once

// Internal state and pipeline definitions for AtmosphereLutRenderer.

#include "../../Features/Atmosphere/AtmosphereLutRenderer.h"
#include "../../Features/Atmosphere/AtmosphereTypes.h"
#include "../Compute/ComputeDescriptorSet.h"
#include "../Compute/ComputePipeline.h"
#include "../GraphicsHandleRegistry.h"
#include "../RenderTexture.h"
#include "../VolumeTexture.h"
#include "../Buffer.h"
#include "../Pipeline.h"
#include <volk.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace gte {

class Renderer;

namespace rg {
class RenderGraph;
} // namespace rg

struct AtmosphereLutRenderer::Impl {
    ~Impl();

    VkDevice device = VK_NULL_HANDLE;

    DescriptorSetLayoutHandle transmittanceLutDescriptorSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    std::optional<ComputePipeline> transmittanceLutPipeline;
    std::optional<Buffer> atmosphereParametersBuffer; // Shared by all LUT passes
    rg::PersistentTextureCacheToken transmittanceLutToken;

    DescriptorSetLayoutHandle multiScatteringLutDescriptorSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    std::optional<ComputePipeline> multiScatteringLutPipeline;
    rg::PersistentTextureCacheToken multiScatteringLutToken;

    struct SkyViewLutViewState {
        std::optional<Buffer> frameUniformsBuffer;
        rg::PersistentTextureCacheToken token;
    };
    DescriptorSetLayoutHandle skyViewLutDescriptorSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    std::optional<ComputePipeline> skyViewLutPipeline;
    std::unordered_map<std::string, SkyViewLutViewState> skyViewLutViewStates;

    struct AerialPerspectiveVolumeViewState {
        std::optional<Buffer> frameUniformsBuffer;
        std::optional<VolumeTexture> output;
        CachedSamplerHandle volumeSamplerHandle; // Trilinear volume sampler
    };
    DescriptorSetLayoutHandle aerialPerspectiveVolumeDescriptorSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    std::optional<ComputePipeline> aerialPerspectiveVolumePipeline;
    std::unordered_map<std::string, AerialPerspectiveVolumeViewState> aerialPerspectiveVolumeViewStates;

    struct AerialPerspectiveCompositeViewState {
        std::optional<RenderTexture> output;
    };
    DescriptorSetLayoutHandle aerialPerspectiveCompositeDescriptorSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    std::optional<ComputePipeline> aerialPerspectiveCompositePipeline;
    std::unordered_map<std::string, AerialPerspectiveCompositeViewState> aerialPerspectiveCompositeViewStates;

    // Sky background graphics pass resources
    ComputeDescriptorSet skyBackgroundDescriptorSet;
    std::unique_ptr<Pipeline> skyBackgroundPipeline;
    DescriptorSetLayoutHandle skyBackgroundSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    Renderer* ownerRenderer = nullptr; // Used to clean up descriptor set layout

    void EnsureTransmittanceLutInitialized(Renderer& renderer, const AtmosphereParametersGpu& params);
    void EnsureMultiScatteringLutInitialized(Renderer& renderer);
    void EnsureSkyViewLutInitialized(Renderer& renderer);
    SkyViewLutViewState& EnsureSkyViewLutViewInitialized(Renderer& renderer, const char* outputTextureName);
    void EnsureAerialPerspectiveVolumeInitialized(Renderer& renderer);
    AerialPerspectiveVolumeViewState& EnsureAerialPerspectiveVolumeViewInitialized(
        Renderer& renderer, const char* outputVolumeName);
    void EnsureAerialPerspectiveCompositeInitialized(Renderer& renderer);
    AerialPerspectiveCompositeViewState& EnsureAerialPerspectiveCompositeViewInitialized(
        Renderer& renderer, const char* outputTextureName, Extent2D extent);
    void EnsureSkyBackgroundPipelineInitialized(Renderer& renderer);

    // Full-screen pass for sky background
    void DrawSkyBackgroundPass(Renderer& renderer, rg::CommandBuffer& cmd, const Mat4& viewProjection,
        VkImageView skyViewLutView, VkSampler skyViewLutSampler, float eyeHeightKm, float planetRadiusKm,
        float skyExposure);

    // Render graph pass builders
    rg::TextureHandle AddTransmittanceLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, rg::RenderPassToggleRegistry* toggleRegistry);
    rg::TextureHandle AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle,
        rg::RenderPassToggleRegistry* toggleRegistry);
    rg::TextureHandle AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
        const char* outputTextureName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry);
    rg::VolumeTextureHandle AddAerialPerspectiveVolumePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
        rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
        const char* outputVolumeName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry);
    void DrawSkyBackground(Renderer& renderer, rg::CommandBuffer& cmd, const rg::RenderGraph& renderGraph,
        const Mat4& viewProjection, const AtmosphereParametersGpu& params,
        const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure);
    rg::TextureHandle AddAerialPerspectiveCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        rg::TextureHandle sourceColorHandle, SamplerHandle sourceColorSampler, rg::TextureHandle sourceDepthHandle,
        ImageViewHandle sourceDepthView, SamplerHandle sourceDepthSampler,
        rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
        const Mat4& invViewProjection, Vec3 cameraWorldPosition, float aerialPerspectiveStrength, float maxDistanceKm,
        float depthExponent, Extent2D extent, const char* outputTextureName, rg::ViewScope viewScope,
        rg::RenderPassToggleRegistry* toggleRegistry);
    RenderTexture* CompositedOutput(const char* outputTextureName) noexcept;
    void FinalizeAerialPerspectiveCompositeForSampling(rg::CommandBuffer& cmd, const char* outputTextureName);
    int AerialPerspectiveVolumeDepth(const char* aerialPerspectiveVolumeName) const noexcept;
};

} // namespace gte
