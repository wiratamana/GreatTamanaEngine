#pragma once

// Renderer-internal only - never included by a Feature/Editor file. Holds
// every Vk*/ComputePipeline/ComputeDescriptorSet/RenderTexture field
// AtmosphereLutRenderer.h used to expose directly, plus the former
// AtmosphereSkyBackgroundRenderer's own state (folded in here - see
// EnsureSkyBackgroundPipelineInitialized()/DrawSkyBackgroundPass() below).
// Only AtmosphereLutRenderer.cpp and AtmosphereLutRendererImpl.cpp include
// this.

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
class RenderGraph; // DrawSkyBackground() looks up its own Sky-View LUT by name - see that method below.
} // namespace rg

struct AtmosphereLutRenderer::Impl {
    ~Impl();

    VkDevice device = VK_NULL_HANDLE;

    VkDescriptorSetLayout transmittanceLutDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> transmittanceLutPipeline;
    ComputeDescriptorSet transmittanceLutDescriptorSet;
    std::optional<Buffer> atmosphereParametersBuffer; // shared by every LUT pass below
    // Persistent cache owns the real RenderTexture, registered under the
    // exact literal name "AtmosphereTransmittanceLut" (no owner prefix) -
    // AtmosphereTransmittanceLutValidation.cpp reads it back by that same
    // name directly from the render graph, so this token is the only state
    // this struct needs for it.
    rg::PersistentTextureCacheToken transmittanceLutToken;

    VkDescriptorSetLayout multiScatteringLutDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> multiScatteringLutPipeline;
    ComputeDescriptorSet multiScatteringLutDescriptorSet;
    // Persistent cache owns the real RenderTexture - only consumed via resolved
    // handles by same-frame passes below, so a lightweight token is enough.
    rg::PersistentTextureCacheToken multiScatteringLutToken;

    struct SkyViewLutViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<Buffer> frameUniformsBuffer;
        // Persistent cache owns the real RenderTexture - DrawSkyBackground()
        // looks it up by name through the render graph instead of holding a
        // live pointer, so a token is all this struct needs to own here.
        rg::PersistentTextureCacheToken token;
    };
    VkDescriptorSetLayout skyViewLutDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> skyViewLutPipeline;
    std::unordered_map<std::string, SkyViewLutViewState> skyViewLutViewStates;

    struct AerialPerspectiveVolumeViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<Buffer> frameUniformsBuffer;
        std::optional<VolumeTexture> output;
    };
    VkDescriptorSetLayout aerialPerspectiveVolumeDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> aerialPerspectiveVolumePipeline;
    std::unordered_map<std::string, AerialPerspectiveVolumeViewState> aerialPerspectiveVolumeViewStates;

    struct AerialPerspectiveCompositeViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<RenderTexture> output;
    };
    VkDescriptorSetLayout aerialPerspectiveCompositeDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> aerialPerspectiveCompositePipeline;
    std::unordered_map<std::string, AerialPerspectiveCompositeViewState> aerialPerspectiveCompositeViewStates;

    struct AerialPerspectiveVolumeDebugSliceViewState {
        ComputeDescriptorSet descriptorSet;
        std::optional<RenderTexture> output;
    };
    VkDescriptorSetLayout aerialPerspectiveVolumeDebugSliceDescriptorSetLayout = VK_NULL_HANDLE;
    std::optional<ComputePipeline> aerialPerspectiveVolumeDebugSlicePipeline;
    std::unordered_map<std::string, AerialPerspectiveVolumeDebugSliceViewState>
        aerialPerspectiveVolumeDebugSliceViewStates;

    // Sky Background pass - folded in from the former, now-deleted
    // AtmosphereSkyBackgroundRenderer. A hand-built graphics pipeline (never
    // reflected from a ComputePipeline), so its own descriptor-set layout
    // needs explicit teardown in ~Impl() below - unlike every LUT pass
    // layout above, which is borrowed from its own ComputePipeline.
    VkDescriptorSetLayout skyBackgroundDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet skyBackgroundDescriptorSet = VK_NULL_HANDLE;
    std::unique_ptr<Pipeline> skyBackgroundPipeline;
    DescriptorSetLayoutHandle skyBackgroundSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    // Non-owning, cached purely so ~Impl() can call
    // Renderer::DestroyDescriptorSetLayout(skyBackgroundSetLayoutHandle) -
    // set the first time EnsureSkyBackgroundPipelineInitialized() runs.
    Renderer* ownerRenderer = nullptr;

    void EnsureTransmittanceLutInitialized(Renderer& renderer, const AtmosphereParametersGpu& params);
    void EnsureMultiScatteringLutInitialized(Renderer& renderer);
    void EnsureSkyViewLutInitialized(Renderer& renderer);
    SkyViewLutViewState& EnsureSkyViewLutViewInitialized(Renderer& renderer, const char* outputTextureName);
    void EnsureAerialPerspectiveVolumeInitialized(Renderer& renderer);
    AerialPerspectiveVolumeViewState& EnsureAerialPerspectiveVolumeViewInitialized(
        Renderer& renderer, const char* outputVolumeName);
    void EnsureAerialPerspectiveCompositeInitialized(Renderer& renderer);
    AerialPerspectiveCompositeViewState& EnsureAerialPerspectiveCompositeViewInitialized(
        Renderer& renderer, const char* outputTextureName, VkExtent2D extent);
    void EnsureAerialPerspectiveVolumeDebugSliceInitialized(Renderer& renderer);
    AerialPerspectiveVolumeDebugSliceViewState& EnsureAerialPerspectiveVolumeDebugSliceViewInitialized(
        Renderer& renderer, const char* outputTextureName, int width, int height);
    void EnsureSkyBackgroundPipelineInitialized(Renderer& renderer);

    // The real full-screen-triangle draw body - issued directly via
    // cmd.Native(), mirroring the former AtmosphereSkyBackgroundRenderer::Draw().
    // This struct already lives under src/Renderer/Internal - direct Vulkan
    // access here was always correct.
    void DrawSkyBackgroundPass(Renderer& renderer, rg::CommandBuffer& cmd, const Mat4& viewProjection,
        VkImageView skyViewLutView, VkSampler skyViewLutSampler, float eyeHeightKm, float planetRadiusKm,
        float skyExposure);

    // --- Mirrors of every public AtmosphereLutRenderer method - real bodies. ---
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
    // `renderGraph` is used purely to look up the Sky-View LUT named
    // `skyViewLutName` by name (DebugTextureSnapshotFor()) - this draw runs
    // directly inside another already-declared pass's own execute body, with
    // no PassContext/TextureHandle of its own to resolve through.
    void DrawSkyBackground(Renderer& renderer, rg::CommandBuffer& cmd, const rg::RenderGraph& renderGraph,
        const Mat4& viewProjection, const AtmosphereParametersGpu& params,
        const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure);
    // `sourceColorSampler`/`sourceDepthView`/`sourceDepthSampler`/`extent` are
    // the opaque handles the public AtmosphereLutRenderer::
    // AddAerialPerspectiveCompositePass() receives - unwrapped to real Vk*
    // values right at the top of this method's own body (AtmosphereLutRendererImpl.cpp),
    // never by the public-facing, Vulkan-free AtmosphereLutRenderer.cpp.
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
    rg::TextureHandle AddAerialPerspectiveVolumeDebugSlicePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
        rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
        std::uint32_t debugSliceIndex, const char* outputTextureName, rg::ViewScope viewScope,
        rg::RenderPassToggleRegistry* toggleRegistry);
    Renderer::CapturedRawPixels CaptureAerialPerspectiveVolumeSliceImmediate(Renderer& renderer,
        const char* aerialPerspectiveVolumeName, std::uint32_t sliceIndex, const char* outputTextureName);
};

} // namespace gte
