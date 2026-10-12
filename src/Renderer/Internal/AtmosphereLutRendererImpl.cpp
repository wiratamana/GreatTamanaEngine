#include "AtmosphereLutRendererImpl.h"

#include "../../Features/Atmosphere/AtmosphereParameters.h"
#include "../../Features/Atmosphere/AtmospherePassToggleLogic.h"
#include "../../Core/PipelineDesc.h"
#include "../Compute/ComputeDispatch.h"
#include "../RenderTarget.h"
#include "../Vulkan/DescriptorSetLayoutBuilder.h"
#include "../RenderGraph/CommandBuffer.h"
#include "../RenderGraph/RenderGraph.h"
#include "../RenderGraph/RenderFeatureScope.h"
#include "../RenderGraph/RenderPassToggleRegistry.h"
#include "../RenderGraph/RenderGraphPersistentResourceOwners.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gte {

namespace {

// Transmittance LUT resolution - see Shaders/AtmosphereTransmittanceLut.comp.
constexpr int kTransmittanceLutWidth = 256;
constexpr int kTransmittanceLutHeight = 256;

// Multi-Scattering LUT resolution.
constexpr int kMultiScatteringLutWidth = 64;
constexpr int kMultiScatteringLutHeight = 64;

// Sky-View LUT resolution.
constexpr int kSkyViewLutWidth = 200;
constexpr int kSkyViewLutHeight = 100;

// Aerial Perspective froxel-volume resolution.
constexpr int kAerialPerspectiveVolumeWidth = 128;
constexpr int kAerialPerspectiveVolumeHeight = 128;
constexpr int kAerialPerspectiveVolumeDepth = 32;

// Must match Shaders/AtmosphereAerialPerspectiveComposite.comp's push-constant block (96 bytes).
struct AerialPerspectiveCompositePushConstants {
    float invViewProjection[16];

    // xyz = camera world position, w = world units per km.
    float cameraWorldPositionAndScale[4];

    // x = strength, y = max distance (km), z = depth exponent, w = padding.
    float aerialPerspectiveStrengthAndPad[4];
};

// Must match Shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp's local_size_x/y (16x16).
constexpr std::uint32_t kAerialPerspectiveVolumeDebugSliceLocalSizeX = 16;
constexpr std::uint32_t kAerialPerspectiveVolumeDebugSliceLocalSizeY = 16;

// Must match Shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp's push-constant block (8 bytes).
struct AerialPerspectiveVolumeDebugSlicePushConstants {
    std::uint32_t sliceIndex = 0;
    std::uint32_t sliceCount = 1;
};

std::vector<std::byte> ReadShaderFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error(
            "AtmosphereLutRenderer::Impl: failed to open shader file '" + path + "' - was it compiled?");
    }
    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<std::byte> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    return buffer;
}

// Must match Shaders/AtmosphereSkyBackground.frag's push-constant block (mat4 + 3 floats + pad = 80 bytes).
struct SkyBackgroundPushConstants {
    float invViewProjection[16];
    float eyeHeightKm;
    float planetRadiusKm;
    float skyExposure;
    float _pad0;
};

} // namespace

AtmosphereLutRenderer::Impl::~Impl()
{
    // Every LUT pass's own descriptor-set layout is borrowed from its own
    // ComputePipeline's reflected layout - owned/destroyed by that pipeline,
    // never here. skyBackgroundDescriptorSetLayout is the one exception: a
    // hand-built graphics layout that genuinely needs explicit teardown.
    if (device != VK_NULL_HANDLE && skyBackgroundPipeline) {
        vkDeviceWaitIdle(device);
    }
    if (ownerRenderer != nullptr) {
        ownerRenderer->DestroyDescriptorSetLayout(skyBackgroundSetLayoutHandle);
    }
    if (skyBackgroundDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, skyBackgroundDescriptorSetLayout, nullptr);
    }
    // skyBackgroundDescriptorSet is allocated from Renderer's shared compute
    // descriptor pool - never individually freed.
}

void AtmosphereLutRenderer::Impl::EnsureTransmittanceLutInitialized(
    Renderer& renderer, const AtmosphereParametersGpu& params)
{
    if (transmittanceLutPipeline.has_value()) {
        return;
    }

    device = renderer.GetVulkanContextInfo().device;

    // Binding 0 = AtmosphereParametersGpu (read-only storage buffer), binding 1 = the output image2D.
    transmittanceLutPipeline.emplace(renderer.CreateComputePipeline("shaders/AtmosphereTransmittanceLut.comp.spv"));
    transmittanceLutDescriptorSetLayout = transmittanceLutPipeline->ReflectedDescriptorSetLayout(/*set=*/0);
    transmittanceLutDescriptorSet = renderer.AllocateComputeDescriptorSet(transmittanceLutDescriptorSetLayout);

    // CpuToGpu so Upload() can write straight into it every call - reused
    // verbatim by every other LUT pass below, never duplicated.
    atmosphereParametersBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereParametersGpu),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemoryUsage::CpuToGpu, "AtmosphereParametersGpu"));
    atmosphereParametersBuffer->Upload(&params, sizeof(AtmosphereParametersGpu));

}

rg::TextureHandle AtmosphereLutRenderer::Impl::AddTransmittanceLutPass(
    rg::RenderGraphBuilder& builder, Renderer& renderer, const AtmosphereParametersGpu& params,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame =
        toggleRegistry == nullptr || toggleRegistry->IsEnabled("AtmosphereTransmittanceLutPass");
    if (!ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, /*allUpstreamHandlesValid=*/true)) {
        return rg::TextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Atmosphere / Sky");

    EnsureTransmittanceLutInitialized(renderer, params);

    // No dirty-flag optimization - re-upload the tiny parameters buffer and
    // recompute the whole LUT every call.
    atmosphereParametersBuffer->Upload(&params, sizeof(AtmosphereParametersGpu));

    // VK_FORMAT_R8G8B8A8_UNORM is correct here - a transmittance value is
    // always in [0, 1] per channel. Registered under the exact literal name
    // "AtmosphereTransmittanceLut" (empty owner) - AtmosphereTransmittanceLutValidation.cpp
    // already depends on that exact name via DebugTextureSnapshotFor().
    rg::TextureDesc desc{};
    desc.width = static_cast<std::uint32_t>(kTransmittanceLutWidth);
    desc.height = static_cast<std::uint32_t>(kTransmittanceLutHeight);
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.usage = rg::TextureUsage::Storage;
    const rg::TextureHandle outputHandle =
        builder.GetOrCreatePersistentTexture(transmittanceLutToken, "", "AtmosphereTransmittanceLut", desc);

    builder.AddRenderPass(
        "AtmosphereTransmittanceLutPass", rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, outputHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            transmittanceLutDescriptorSet.Rewrite(
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, atmosphereParametersBuffer->Native()),
                    ComputeDescriptorWrite::StorageImage(1, dest.view),
                });

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*transmittanceLutPipeline);
            cmd.BindDescriptorSet(transmittanceLutDescriptorSet.Native());
            cmd.DispatchOverSize(
                static_cast<std::uint32_t>(kTransmittanceLutWidth), static_cast<std::uint32_t>(kTransmittanceLutHeight), 1);
        },
        // GPU Skinning and the Atmosphere shared LUTs run first, before everything else.
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

    return outputHandle;
}

void AtmosphereLutRenderer::Impl::EnsureMultiScatteringLutInitialized(Renderer& renderer)
{
    if (multiScatteringLutPipeline.has_value()) {
        return;
    }

    // EnsureTransmittanceLutInitialized() has always already run by the time
    // this is called - device/atmosphereParametersBuffer are already valid.
    device = renderer.GetVulkanContextInfo().device;

    // Binding 0 = AtmosphereParametersGpu (reused), binding 1 = the
    // transmittance LUT combined image sampler, binding 2 = the output image2D.
    multiScatteringLutPipeline.emplace(
        renderer.CreateComputePipeline("shaders/AtmosphereMultiScatteringLut.comp.spv"));
    multiScatteringLutDescriptorSetLayout = multiScatteringLutPipeline->ReflectedDescriptorSetLayout(/*set=*/0);
    multiScatteringLutDescriptorSet = renderer.AllocateComputeDescriptorSet(multiScatteringLutDescriptorSetLayout);
}

rg::TextureHandle AtmosphereLutRenderer::Impl::AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame =
        toggleRegistry == nullptr || toggleRegistry->IsEnabled("AtmosphereMultiScatteringLutPass");
    if (!ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, transmittanceLutHandle.IsValid())) {
        return rg::TextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Atmosphere / Sky");

    (void)params; // Already uploaded into atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.

    EnsureMultiScatteringLutInitialized(renderer);

    // HDR output - a multi-scattering response value can exceed 1.0, so an
    // 8-bit UNORM format would clip it. Only ever read by other same-frame
    // render-graph passes below (never exposed outside the graph), so the
    // persistent cache owns it - no manual RenderTexture/RenderTarget needed.
    rg::TextureDesc desc{};
    desc.width = static_cast<std::uint32_t>(kMultiScatteringLutWidth);
    desc.height = static_cast<std::uint32_t>(kMultiScatteringLutHeight);
    desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    desc.usage = rg::TextureUsage::Storage;
    const rg::TextureHandle outputHandle = builder.GetOrCreatePersistentTexture(
        multiScatteringLutToken, rg::kPersistentOwnerAtmosphere, "MultiScatteringLut", desc);

    builder.AddRenderPass(
        "AtmosphereMultiScatteringLutPass", rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [transmittanceLutHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, transmittanceLutHandle, outputHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture transmittance = ctx.resolveTexture(transmittanceLutHandle);
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            multiScatteringLutDescriptorSet.Rewrite(
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, atmosphereParametersBuffer->Native()),
                    ComputeDescriptorWrite::CombinedImageSampler(1, transmittance.view, transmittance.sampler),
                    ComputeDescriptorWrite::StorageImage(2, dest.view),
                });

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*multiScatteringLutPipeline);
            cmd.BindDescriptorSet(multiScatteringLutDescriptorSet.Native());
            cmd.DispatchOverSize(
                static_cast<std::uint32_t>(kMultiScatteringLutWidth), static_cast<std::uint32_t>(kMultiScatteringLutHeight), 1);
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

    return outputHandle;
}

void AtmosphereLutRenderer::Impl::EnsureSkyViewLutInitialized(Renderer& renderer)
{
    if (skyViewLutPipeline.has_value()) {
        return;
    }

    device = renderer.GetVulkanContextInfo().device;

    // Binding 0 = AtmosphereParametersGpu, binding 1 = AtmosphereFrameUniforms
    // (per-view, see skyViewLutViewStates), binding 2 = transmittanceLut,
    // binding 3 = multiScatteringLut, binding 4 = the output image2D.
    skyViewLutPipeline.emplace(renderer.CreateComputePipeline("shaders/AtmosphereSkyViewLut.comp.spv"));
    skyViewLutDescriptorSetLayout = skyViewLutPipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

AtmosphereLutRenderer::Impl::SkyViewLutViewState& AtmosphereLutRenderer::Impl::EnsureSkyViewLutViewInitialized(
    Renderer& renderer, const char* outputTextureName)
{
    const auto existing = skyViewLutViewStates.find(outputTextureName);
    if (existing != skyViewLutViewStates.end()) {
        return existing->second;
    }

    SkyViewLutViewState state;
    state.descriptorSet = renderer.AllocateComputeDescriptorSet(skyViewLutDescriptorSetLayout);
    state.frameUniformsBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereFrameUniforms),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemoryUsage::CpuToGpu, "AtmosphereFrameUniforms"));

    const auto insertedPair = skyViewLutViewStates.emplace(outputTextureName, std::move(state));
    return insertedPair.first->second;
}

rg::TextureHandle AtmosphereLutRenderer::Impl::AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle, const char* outputTextureName,
    rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame =
        toggleRegistry == nullptr || toggleRegistry->IsEnabled("AtmosphereSkyViewLutPass");
    if (!ShouldDeclareAtmospherePassThisFrame(
            passEnabledThisFrame, transmittanceLutHandle.IsValid() && multiScatteringLutHandle.IsValid())) {
        return rg::TextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Atmosphere / Sky");

    (void)params; // Already uploaded into atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.

    EnsureSkyViewLutInitialized(renderer);
    SkyViewLutViewState& viewState = EnsureSkyViewLutViewInitialized(renderer, outputTextureName);

    // No dirty-flag optimization - this per-view buffer is unconditionally
    // re-uploaded and the whole LUT recomputed every call.
    viewState.frameUniformsBuffer->Upload(&frameUniforms, sizeof(AtmosphereFrameUniforms));

    // HDR output - near-sun sky radiance routinely exceeds 1.0. DrawSkyBackground()
    // looks this up by name through the render graph (DebugTextureSnapshotFor()),
    // so the persistent cache owns it - no manually-held RenderTexture needed.
    rg::TextureDesc desc{};
    desc.width = static_cast<std::uint32_t>(kSkyViewLutWidth);
    desc.height = static_cast<std::uint32_t>(kSkyViewLutHeight);
    desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    desc.usage = rg::TextureUsage::Storage;
    const rg::TextureHandle outputHandle = builder.GetOrCreatePersistentTexture(
        viewState.token, rg::kPersistentOwnerAtmosphere, outputTextureName, desc);

    builder.AddRenderPass(
        "AtmosphereSkyViewLutPass", rg::PassKind::Compute, viewScope, rg::RenderPassCategory::General,
        [transmittanceLutHandle, multiScatteringLutHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(multiScatteringLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &viewState, transmittanceLutHandle, multiScatteringLutHandle,
            outputHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture transmittance = ctx.resolveTexture(transmittanceLutHandle);
            const rg::PassContext::ResolvedTexture multiScattering = ctx.resolveTexture(multiScatteringLutHandle);
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            viewState.descriptorSet.Rewrite(
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, atmosphereParametersBuffer->Native()),
                    ComputeDescriptorWrite::StorageBuffer(1, viewState.frameUniformsBuffer->Native()),
                    ComputeDescriptorWrite::CombinedImageSampler(2, transmittance.view, transmittance.sampler),
                    ComputeDescriptorWrite::CombinedImageSampler(3, multiScattering.view, multiScattering.sampler),
                    ComputeDescriptorWrite::StorageImage(4, dest.view),
                });

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*skyViewLutPipeline);
            cmd.BindDescriptorSet(viewState.descriptorSet.Native());
            cmd.DispatchOverSize(
                static_cast<std::uint32_t>(kSkyViewLutWidth), static_cast<std::uint32_t>(kSkyViewLutHeight), 1);
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

    return outputHandle;
}

void AtmosphereLutRenderer::Impl::EnsureAerialPerspectiveVolumeInitialized(Renderer& renderer)
{
    if (aerialPerspectiveVolumePipeline.has_value()) {
        return;
    }

    device = renderer.GetVulkanContextInfo().device;

    // Binding 0 = AtmosphereParametersGpu (reused), binding 1 =
    // AtmosphereFrameUniforms (per-view), binding 2 = transmittanceLut,
    // binding 3 = multiScatteringLut, binding 4 = the output image3D.
    aerialPerspectiveVolumePipeline.emplace(
        renderer.CreateComputePipeline("shaders/AtmosphereAerialPerspectiveVolume.comp.spv"));
    aerialPerspectiveVolumeDescriptorSetLayout = aerialPerspectiveVolumePipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

AtmosphereLutRenderer::Impl::AerialPerspectiveVolumeViewState&
AtmosphereLutRenderer::Impl::EnsureAerialPerspectiveVolumeViewInitialized(Renderer& renderer, const char* outputVolumeName)
{
    const auto existing = aerialPerspectiveVolumeViewStates.find(outputVolumeName);
    if (existing != aerialPerspectiveVolumeViewStates.end()) {
        return existing->second;
    }

    AerialPerspectiveVolumeViewState state;
    state.descriptorSet = renderer.AllocateComputeDescriptorSet(aerialPerspectiveVolumeDescriptorSetLayout);
    state.frameUniformsBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereFrameUniforms),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemoryUsage::CpuToGpu, "AtmosphereFrameUniforms"));

    // Persistent 3D VolumeTexture, created once - the render graph has no
    // pooled/transient VolumeTexture kind yet, only ever an imported one, so
    // this must stay manually owned (not a RenderGraphPersistentResourceCache
    // gap specific to this file - the cache itself is color-2D-texture only).
    state.output.emplace(renderer.CreateVolumeTexture(kAerialPerspectiveVolumeWidth, kAerialPerspectiveVolumeHeight,
        kAerialPerspectiveVolumeDepth, VK_FORMAT_R16G16B16A16_SFLOAT, outputVolumeName));

    const auto insertedPair = aerialPerspectiveVolumeViewStates.emplace(outputVolumeName, std::move(state));
    return insertedPair.first->second;
}

rg::VolumeTextureHandle AtmosphereLutRenderer::Impl::AddAerialPerspectiveVolumePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle, const char* outputVolumeName,
    rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame = toggleRegistry == nullptr
        || toggleRegistry->IsEnabled("AtmosphereAerialPerspectiveVolumePass");
    if (!ShouldDeclareAtmospherePassThisFrame(
            passEnabledThisFrame, transmittanceLutHandle.IsValid() && multiScatteringLutHandle.IsValid())) {
        return rg::VolumeTextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Atmosphere / Sky");

    (void)params; // Already uploaded into atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.

    EnsureAerialPerspectiveVolumeInitialized(renderer);
    AerialPerspectiveVolumeViewState& viewState =
        EnsureAerialPerspectiveVolumeViewInitialized(renderer, outputVolumeName);

    // No dirty-flag optimization - re-uploaded and the whole volume recomputed every call.
    viewState.frameUniformsBuffer->Upload(&frameUniforms, sizeof(AtmosphereFrameUniforms));

    const rg::VolumeTextureHandle outputHandle =
        builder.ImportVolumeTexture(outputVolumeName, viewState.output->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddRenderPass(
        "AtmosphereAerialPerspectiveVolumePass", rg::PassKind::Compute, viewScope, rg::RenderPassCategory::General,
        [transmittanceLutHandle, multiScatteringLutHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(multiScatteringLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteVolumeTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &viewState, transmittanceLutHandle, multiScatteringLutHandle,
            outputHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture transmittance = ctx.resolveTexture(transmittanceLutHandle);
            const rg::PassContext::ResolvedTexture multiScattering = ctx.resolveTexture(multiScatteringLutHandle);
            const rg::PassContext::ResolvedVolumeTexture dest = ctx.resolveVolumeTexture(outputHandle);

            viewState.descriptorSet.Rewrite(
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, atmosphereParametersBuffer->Native()),
                    ComputeDescriptorWrite::StorageBuffer(1, viewState.frameUniformsBuffer->Native()),
                    ComputeDescriptorWrite::CombinedImageSampler(2, transmittance.view, transmittance.sampler),
                    ComputeDescriptorWrite::CombinedImageSampler(3, multiScattering.view, multiScattering.sampler),
                    ComputeDescriptorWrite::StorageImage(4, dest.view),
                });

            // One dispatch group covers exactly one froxel column - the
            // pipeline's reflected local Z size is 1; each invocation loops
            // every Z slice internally, so the real depth is never passed here.
            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*aerialPerspectiveVolumePipeline);
            cmd.BindDescriptorSet(viewState.descriptorSet.Native());
            cmd.DispatchOverSize(static_cast<std::uint32_t>(kAerialPerspectiveVolumeWidth),
                static_cast<std::uint32_t>(kAerialPerspectiveVolumeHeight), 1);
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

    return outputHandle;
}

void AtmosphereLutRenderer::Impl::EnsureAerialPerspectiveCompositeInitialized(Renderer& renderer)
{
    if (aerialPerspectiveCompositePipeline.has_value()) {
        return;
    }

    device = renderer.GetVulkanContextInfo().device;

    // binding 0 = sourceColor, binding 1 = sourceDepth, binding 2 =
    // aerialPerspectiveVolume, binding 3 = the output image2D, plus push constants.
    aerialPerspectiveCompositePipeline.emplace(
        renderer.CreateComputePipeline("shaders/AtmosphereAerialPerspectiveComposite.comp.spv"));
    aerialPerspectiveCompositeDescriptorSetLayoutHandle =
        aerialPerspectiveCompositePipeline->ReflectedDescriptorSetLayoutHandle(/*set=*/0);
}

AtmosphereLutRenderer::Impl::AerialPerspectiveCompositeViewState&
AtmosphereLutRenderer::Impl::EnsureAerialPerspectiveCompositeViewInitialized(
    Renderer& renderer, const char* outputTextureName, Extent2D extent)
{
    const auto existing = aerialPerspectiveCompositeViewStates.find(outputTextureName);
    if (existing != aerialPerspectiveCompositeViewStates.end()) {
        return existing->second;
    }

    AerialPerspectiveCompositeViewState state;

    // Explicit VK_FORMAT_R8G8B8A8_UNORM, never the swapchain's own negotiated
    // format. CompositedOutput()/FinalizeAerialPerspectiveCompositeForSampling()
    // hand this RenderTexture directly to the Editor/HTTP capture outside any
    // render-graph pass, so it must stay manually owned, not a cache handle.
    const int width = extent.width > 0 ? static_cast<int>(extent.width) : 1;
    const int height = extent.height > 0 ? static_cast<int>(extent.height) : 1;
    state.output.emplace(renderer.CreateRenderTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, outputTextureName,
        /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true));

    const auto insertedPair = aerialPerspectiveCompositeViewStates.emplace(outputTextureName, std::move(state));
    return insertedPair.first->second;
}

rg::TextureHandle AtmosphereLutRenderer::Impl::AddAerialPerspectiveCompositePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, rg::TextureHandle sourceColorHandle, SamplerHandle sourceColorSampler,
    rg::TextureHandle sourceDepthHandle, ImageViewHandle sourceDepthView, SamplerHandle sourceDepthSampler,
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    const Mat4& invViewProjection, Vec3 cameraWorldPosition, float aerialPerspectiveStrength, float maxDistanceKm,
    float depthExponent, Extent2D extent, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    // Genuinely zero raw Vulkan types touched anywhere in this function -
    // `extent` stays the engine's own Vulkan-free Extent2D the whole way
    // through, including RenderTexture::Extent() below, which now also
    // returns Extent2D (see RenderTexture.h).

    const bool passEnabledThisFrame = toggleRegistry == nullptr
        || toggleRegistry->IsEnabled("AtmosphereAerialPerspectiveCompositePass");
    const bool willDeclare =
        ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, aerialPerspectiveVolumeHandle.IsValid());
    if (!willDeclare) {
        return rg::TextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Atmosphere / Sky");
    EnsureAerialPerspectiveCompositeInitialized(renderer);
    AerialPerspectiveCompositeViewState& viewState =
        EnsureAerialPerspectiveCompositeViewInitialized(renderer, outputTextureName, extent);

    const Extent2D currentExtent = viewState.output->Extent();
    if (currentExtent.width != extent.width || currentExtent.height != extent.height) {
        // Resizes are rare/user-driven (dragging the Game/Scene panel's
        // border) - a full device stall here is the simplest correct thing.
        vkDeviceWaitIdle(device);
        viewState.output->Resize(static_cast<int>(extent.width), static_cast<int>(extent.height));
    }

    const rg::TextureHandle outputHandle = builder.ImportTexture(outputTextureName, viewState.output->Target(),
        VK_IMAGE_LAYOUT_UNDEFINED, viewState.output->Sampler(), viewState.output->DepthSampler(), &*viewState.output);

    // The Aerial Perspective Volume's own trilinear sampler - looked up by
    // name (the same name this frame's own AddAerialPerspectiveVolumePass()
    // call registered it under), since an imported VolumeTextureHandle never
    // carries a sampler of its own. Resolved to a stable, opaque SamplerHandle
    // (never a raw VkSampler) via CachedSamplerHandle - re-minted only if the
    // underlying sampler ever actually changes.
    const auto aerialVolumeIt = aerialPerspectiveVolumeViewStates.find(aerialPerspectiveVolumeName);
    const SamplerHandle aerialVolumeSamplerHandle =
        (aerialVolumeIt != aerialPerspectiveVolumeViewStates.end() && aerialVolumeIt->second.output.has_value())
        ? aerialVolumeIt->second.volumeSamplerHandle.Resolve(aerialVolumeIt->second.output->Sampler())
        : SamplerHandle::Invalid;

    AerialPerspectiveCompositePushConstants pushConstants{};
    std::memcpy(pushConstants.invViewProjection, invViewProjection.Data(), sizeof(pushConstants.invViewProjection));
    pushConstants.cameraWorldPositionAndScale[0] = cameraWorldPosition.x;
    pushConstants.cameraWorldPositionAndScale[1] = cameraWorldPosition.y;
    pushConstants.cameraWorldPositionAndScale[2] = cameraWorldPosition.z;
    pushConstants.cameraWorldPositionAndScale[3] = kWorldUnitsPerKilometer;
    pushConstants.aerialPerspectiveStrengthAndPad[0] = aerialPerspectiveStrength;
    pushConstants.aerialPerspectiveStrengthAndPad[1] = maxDistanceKm;
    pushConstants.aerialPerspectiveStrengthAndPad[2] = depthExponent;
    pushConstants.aerialPerspectiveStrengthAndPad[3] = 0.0f;

    const DescriptorSetLayoutHandle bindingLayout = aerialPerspectiveCompositeDescriptorSetLayoutHandle;

    builder.AddRenderPass(
        "AtmosphereAerialPerspectiveCompositePass", rg::PassKind::Compute, viewScope, rg::RenderPassCategory::General,
        [sourceColorHandle, sourceColorSampler, sourceDepthHandle, sourceDepthView, sourceDepthSampler,
            aerialPerspectiveVolumeHandle, aerialVolumeSamplerHandle, outputHandle,
            bindingLayout](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(sourceColorHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(sourceDepthHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.ReadVolumeTexture(aerialPerspectiveVolumeHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);

            // Compute-binding vocabulary - orthogonal to the Read*/Write*
            // declarations above (see PassBuilder::BindDescriptorSetLayout()'s
            // own doc comment). The render graph resolves/rewrites/caches the
            // real VkDescriptorSet internally, keyed by (pass name, ViewScope) -
            // no ComputeDescriptorSet/raw Vk* value touched anywhere in this file.
            pass.BindDescriptorSetLayout(bindingLayout);
            pass.BindCombinedImageSampler(0, sourceColorHandle, sourceColorSampler);
            pass.BindCombinedImageSampler(1, sourceDepthView, sourceDepthSampler);
            pass.BindCombinedImageSampler(2, aerialPerspectiveVolumeHandle, aerialVolumeSamplerHandle);
            pass.BindStorageImage(3, outputHandle);
        },
        [this, pushConstants, extent](rg::PassContext& ctx) {
            AerialPerspectiveCompositePushConstants localPushConstants = pushConstants;

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*aerialPerspectiveCompositePipeline);
            cmd.BindDescriptorSet(ctx.BoundDescriptorSet());
            cmd.SetPushConstants(localPushConstants);
            cmd.DispatchOverSize(extent.width, extent.height, 1);
        },
        // Every PostComposite pass must be provably ordered after the generic compositor chain's seed point.
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::AfterEverything);
    return outputHandle;
}

RenderTexture* AtmosphereLutRenderer::Impl::CompositedOutput(const char* outputTextureName) noexcept
{
    const auto it = aerialPerspectiveCompositeViewStates.find(outputTextureName);
    if (it == aerialPerspectiveCompositeViewStates.end() || !it->second.output.has_value()) {
        return nullptr;
    }
    return &it->second.output.value();
}

void AtmosphereLutRenderer::Impl::FinalizeAerialPerspectiveCompositeForSampling(
    rg::CommandBuffer& cmd, const char* outputTextureName)
{
    const auto it = aerialPerspectiveCompositeViewStates.find(outputTextureName);
    if (it == aerialPerspectiveCompositeViewStates.end() || !it->second.output.has_value()) {
        return;
    }
    it->second.output->FinalizeForExternalSampling(cmd.Native());
}

int AtmosphereLutRenderer::Impl::AerialPerspectiveVolumeDepth(const char* aerialPerspectiveVolumeName) const noexcept
{
    const auto it = aerialPerspectiveVolumeViewStates.find(aerialPerspectiveVolumeName);
    if (it == aerialPerspectiveVolumeViewStates.end() || !it->second.output.has_value()) {
        return 0;
    }
    return it->second.output->Depth();
}

void AtmosphereLutRenderer::Impl::EnsureAerialPerspectiveVolumeDebugSliceInitialized(Renderer& renderer)
{
    if (aerialPerspectiveVolumeDebugSlicePipeline.has_value()) {
        return;
    }

    device = renderer.GetVulkanContextInfo().device;

    // binding 0 = the source volume's own trilinear sampler3D, binding 1 = the output image2D, plus push constants.
    aerialPerspectiveVolumeDebugSlicePipeline.emplace(
        renderer.CreateComputePipeline("shaders/AtmosphereAerialPerspectiveVolumeDebugSlice.comp.spv"));
    aerialPerspectiveVolumeDebugSliceDescriptorSetLayout =
        aerialPerspectiveVolumeDebugSlicePipeline->ReflectedDescriptorSetLayout(/*set=*/0);
}

AtmosphereLutRenderer::Impl::AerialPerspectiveVolumeDebugSliceViewState&
AtmosphereLutRenderer::Impl::EnsureAerialPerspectiveVolumeDebugSliceViewInitialized(
    Renderer& renderer, const char* outputTextureName, int width, int height)
{
    const auto existing = aerialPerspectiveVolumeDebugSliceViewStates.find(outputTextureName);
    if (existing != aerialPerspectiveVolumeDebugSliceViewStates.end()) {
        return existing->second;
    }

    AerialPerspectiveVolumeDebugSliceViewState state;
    state.descriptorSet = renderer.AllocateComputeDescriptorSet(aerialPerspectiveVolumeDebugSliceDescriptorSetLayout);

    // rgba16f - matches the aerial-perspective volume's own HDR format
    // exactly. CaptureAerialPerspectiveVolumeSliceImmediate() manipulates
    // this RenderTexture's raw VkImage directly outside any render-graph
    // pass, so it must stay manually owned.
    state.output.emplace(renderer.CreateRenderTexture(width, height, VK_FORMAT_R16G16B16A16_SFLOAT, outputTextureName,
        /*depthDebugName=*/nullptr, /*allowStorageImageAccess=*/true));

    const auto insertedPair = aerialPerspectiveVolumeDebugSliceViewStates.emplace(outputTextureName, std::move(state));
    return insertedPair.first->second;
}

rg::TextureHandle AtmosphereLutRenderer::Impl::AddAerialPerspectiveVolumeDebugSlicePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    std::uint32_t debugSliceIndex, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    if (toggleRegistry != nullptr
        && !toggleRegistry->IsEnabled("AtmosphereAerialPerspectiveVolumeDebugSlicePass")) {
        return rg::TextureHandle{};
    }
    const rg::RenderFeatureScope scope(builder, "Atmosphere / Sky");

    EnsureAerialPerspectiveVolumeDebugSliceInitialized(renderer);

    // The same AerialPerspectiveVolumeViewState AddAerialPerspectiveVolumePass()
    // itself created for `aerialPerspectiveVolumeName` this session - looked
    // up again purely for its own Width()/Height()/Depth()/View()/Sampler().
    const auto volumeIt = aerialPerspectiveVolumeViewStates.find(aerialPerspectiveVolumeName);
    if (volumeIt == aerialPerspectiveVolumeViewStates.end() || !volumeIt->second.output.has_value()) {
        // Programmer error - AddAerialPerspectiveVolumePass() for this exact
        // name must have already run earlier this same frame.
        return rg::TextureHandle{};
    }
    const VolumeTexture& sourceVolume = *volumeIt->second.output;
    const int volumeWidth = sourceVolume.Width();
    const int volumeHeight = sourceVolume.Height();
    const std::uint32_t sliceCount = static_cast<std::uint32_t>(sourceVolume.Depth());
    const std::uint32_t clampedSliceIndex = sliceCount > 0 ? std::min(debugSliceIndex, sliceCount - 1) : 0;
    const VkImageView sourceVolumeView = sourceVolume.View();
    const VkSampler sourceVolumeSampler = sourceVolume.Sampler();

    AerialPerspectiveVolumeDebugSliceViewState& viewState =
        EnsureAerialPerspectiveVolumeDebugSliceViewInitialized(renderer, outputTextureName, volumeWidth, volumeHeight);

    const rg::TextureHandle outputHandle = builder.ImportTexture(outputTextureName, viewState.output->Target(),
        VK_IMAGE_LAYOUT_UNDEFINED, viewState.output->Sampler(), viewState.output->DepthSampler());

    AerialPerspectiveVolumeDebugSlicePushConstants pushConstants{};
    pushConstants.sliceIndex = clampedSliceIndex;
    pushConstants.sliceCount = sliceCount;

    builder.AddRenderPass(
        "AtmosphereAerialPerspectiveVolumeDebugSlicePass", rg::PassKind::Compute, viewScope, rg::RenderPassCategory::General,
        [aerialPerspectiveVolumeHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadVolumeTexture(aerialPerspectiveVolumeHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &viewState, sourceVolumeView, sourceVolumeSampler, outputHandle, pushConstants, volumeWidth,
            volumeHeight](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            viewState.descriptorSet.Rewrite(
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, sourceVolumeView, sourceVolumeSampler),
                    ComputeDescriptorWrite::StorageImage(1, dest.view),
                });

            AerialPerspectiveVolumeDebugSlicePushConstants localPushConstants = pushConstants;

            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*aerialPerspectiveVolumeDebugSlicePipeline);
            cmd.BindDescriptorSet(viewState.descriptorSet.Native());
            cmd.SetPushConstants(localPushConstants);
            cmd.DispatchOverSize(static_cast<std::uint32_t>(volumeWidth), static_cast<std::uint32_t>(volumeHeight), 1);
        },
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

    return outputHandle;
}

Renderer::CapturedRawPixels AtmosphereLutRenderer::Impl::CaptureAerialPerspectiveVolumeSliceImmediate(
    Renderer& renderer, const char* aerialPerspectiveVolumeName, std::uint32_t sliceIndex, const char* outputTextureName)
{
    const auto volumeIt = aerialPerspectiveVolumeViewStates.find(aerialPerspectiveVolumeName);
    if (volumeIt == aerialPerspectiveVolumeViewStates.end() || !volumeIt->second.output.has_value()) {
        // Caller-ordering error - AddAerialPerspectiveVolumePass() for this
        // exact name must have already run at least once this session.
        return Renderer::CapturedRawPixels{};
    }

    EnsureAerialPerspectiveVolumeDebugSliceInitialized(renderer);

    const VolumeTexture& sourceVolume = *volumeIt->second.output;
    const int volumeWidth = sourceVolume.Width();
    const int volumeHeight = sourceVolume.Height();
    const std::uint32_t sliceCount = static_cast<std::uint32_t>(sourceVolume.Depth());
    const std::uint32_t clampedSliceIndex = sliceCount > 0 ? std::min(sliceIndex, sliceCount - 1) : 0;
    const VkImage sourceVolumeImage = sourceVolume.Image();
    const VkImageView sourceVolumeView = sourceVolume.View();
    const VkSampler sourceVolumeSampler = sourceVolume.Sampler();

    AerialPerspectiveVolumeDebugSliceViewState& viewState =
        EnsureAerialPerspectiveVolumeDebugSliceViewInitialized(renderer, outputTextureName, volumeWidth, volumeHeight);

    viewState.descriptorSet.Rewrite(
        std::vector<ComputeDescriptorWrite>{
            ComputeDescriptorWrite::CombinedImageSampler(0, sourceVolumeView, sourceVolumeSampler),
            ComputeDescriptorWrite::StorageImage(1, viewState.output->View()),
        });

    AerialPerspectiveVolumeDebugSlicePushConstants pushConstants{};
    pushConstants.sliceIndex = clampedSliceIndex;
    pushConstants.sliceCount = sliceCount;

    // The state AddAerialPerspectiveCompositePass()'s own
    // ReadVolumeTexture(..., ShaderRead) call leaves the source volume in by
    // the end of this session's most recently completed real frame.
    const rg::ResourceState sourceVolumePreviousState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    // A combined-image-sampler read from a compute shader has no existing
    // ResourceAccess enumerator to reuse via RequiredStateFor() - built by hand.
    const rg::ResourceState sourceVolumeSampledState{
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    };

    const VkImage outputImage = viewState.output->Image();
    // This dedicated inspection-only output's own real current layout
    // doesn't matter - this dispatch's imageStore() unconditionally
    // overwrites every texel, so transitioning FROM VK_IMAGE_LAYOUT_UNDEFINED
    // is always correct; ImmediateSubmit() fully blocks on its own fence, so
    // every previous call already completed before this one starts recording.
    const rg::ResourceState outputDiscardState{};
    const rg::ResourceState outputWriteState = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);

    const DescriptorSetHandle debugSliceSet = RegisterDescriptorSet(viewState.descriptorSet.Native());

    renderer.ImmediateSubmit([&](rg::CommandBuffer& cmd) {
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        rg::EmitImageBarrier(cmd.Native(), sourceVolumeImage, range, sourceVolumePreviousState, sourceVolumeSampledState);
        rg::EmitImageBarrier(cmd.Native(), outputImage, range, outputDiscardState, outputWriteState);

        const Extent3D groupCounts = ComputeGroupCount3D(
            Extent3D{ static_cast<std::uint32_t>(volumeWidth), static_cast<std::uint32_t>(volumeHeight), 1 },
            Extent3D{ kAerialPerspectiveVolumeDebugSliceLocalSizeX, kAerialPerspectiveVolumeDebugSliceLocalSizeY, 1 });
        cmd.DispatchCompute(*aerialPerspectiveVolumeDebugSlicePipeline, debugSliceSet, pushConstants, groupCounts.width,
            groupCounts.height, groupCounts.depth);

        // Restore the source volume image to its real previous state - the
        // next real frame's own RenderGraph::Execute() call must find it
        // exactly where it left it.
        rg::EmitImageBarrier(cmd.Native(), sourceVolumeImage, range, sourceVolumeSampledState, sourceVolumePreviousState);
    });

    UnregisterDescriptorSet(debugSliceSet);

    const VkExtent2D extent{ static_cast<std::uint32_t>(volumeWidth), static_cast<std::uint32_t>(volumeHeight) };
    return renderer.CaptureImagePixels(
        outputImage, VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, extent, outputWriteState);
}

void AtmosphereLutRenderer::Impl::EnsureSkyBackgroundPipelineInitialized(Renderer& renderer)
{
    if (skyBackgroundPipeline) {
        return;
    }
    device = renderer.GetVulkanContextInfo().device;
    ownerRenderer = &renderer;

    // Binding 0 = the Sky-View LUT combined image sampler - fragment stage only.
    DescriptorSetLayoutBuilder layoutBuilder(device);
    skyBackgroundDescriptorSetLayout =
        layoutBuilder.AddCombinedImageSampler(/*binding=*/0, VK_SHADER_STAGE_FRAGMENT_BIT).Build();
    skyBackgroundDescriptorSet = renderer.AllocateComputeDescriptorSet(skyBackgroundDescriptorSetLayout).Native();

    // Opaque-handle twin, identically defined, fed to the pipeline builder -
    // pipeline-layout-compatible with the real set allocated above.
    constexpr DescriptorBindingDesc kBindings[] = {
        { .binding = 0, .kind = DescriptorKind::CombinedImageSampler, .stages = ShaderStageMask::Fragment },
    };
    skyBackgroundSetLayoutHandle = renderer.CreateDescriptorSetLayout(kBindings);

    // Depth test enabled with CompareOp::Equal (survives only at a pixel
    // whose depth is still exactly the frame's own clear value, 1.0), depth
    // write disabled. Opaque blend - the sky is the base layer for every
    // pixel it survives the depth test at.
    PipelineDesc desc = PipelinePreset::DepthEqualNoWrite(renderer.ColorPixelFormat(), renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::None; // Full-screen triangle from gl_VertexIndex - no vertex input.
    desc.cullMode = CullMode::None;
    desc.layout.extraSetLayouts = std::span{ &skyBackgroundSetLayoutHandle, 1 };
    desc.layout.pushConstantByteSize = sizeof(SkyBackgroundPushConstants);
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/AtmosphereSkyBackground.vert.spv");
    const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/AtmosphereSkyBackground.frag.spv");
    skyBackgroundPipeline = std::make_unique<Pipeline>(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
}

void AtmosphereLutRenderer::Impl::DrawSkyBackgroundPass(Renderer& renderer, rg::CommandBuffer& cmd,
    const Mat4& viewProjection, VkImageView skyViewLutView, VkSampler skyViewLutSampler, float eyeHeightKm,
    float planetRadiusKm, float skyExposure)
{
    device = renderer.GetVulkanContextInfo().device;

    Mat4 invViewProjection;
    if (!viewProjection.TryInverse(invViewProjection)) {
        return; // Degenerate camera matrix this frame - draw nothing rather than assert/crash.
    }

    EnsureSkyBackgroundPipelineInitialized(renderer);

    ComputeDescriptorSet descriptorSet(skyBackgroundDescriptorSet, device);
    descriptorSet.Rewrite(
        std::vector<ComputeDescriptorWrite>{
            ComputeDescriptorWrite::CombinedImageSampler(0, skyViewLutView, skyViewLutSampler),
        });

    SkyBackgroundPushConstants pushConstants{};
    std::memcpy(pushConstants.invViewProjection, invViewProjection.Data(), sizeof(pushConstants.invViewProjection));
    pushConstants.eyeHeightKm = eyeHeightKm;
    pushConstants.planetRadiusKm = planetRadiusKm;
    pushConstants.skyExposure = skyExposure;

    const VkPipeline pipeline = skyBackgroundPipeline->Native();
    const VkPipelineLayout layout = skyBackgroundPipeline->Layout();
    const VkCommandBuffer rawCmd = cmd.Native();
    const VkDescriptorSet rawDescriptorSet = skyBackgroundDescriptorSet;

    vkCmdBindPipeline(rawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(rawCmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &rawDescriptorSet, 0, nullptr);
    vkCmdPushConstants(rawCmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pushConstants), &pushConstants);
    vkCmdDraw(rawCmd, 3, 1, 0, 0);
}

void AtmosphereLutRenderer::Impl::DrawSkyBackground(Renderer& renderer, rg::CommandBuffer& cmd,
    const rg::RenderGraph& renderGraph, const Mat4& viewProjection, const AtmosphereParametersGpu& params,
    const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure)
{
    // Combined key GetOrCreatePersistentTexture() registered this view's Sky-View
    // LUT under - see AddSkyViewLutPass(). This draw runs nested inside another
    // already-declared pass's own execute body, with no TextureHandle/PassContext
    // of its own to resolve through, so a by-name lookup is the correct way to
    // reach it (mirrors ValidateAerialPerspectiveSkyPurity()'s own precedent).
    const std::string key = std::string(rg::kPersistentOwnerAtmosphere) + "::" + skyViewLutName;
    const std::optional<rg::DebugTextureSnapshot> snapshot = renderGraph.DebugTextureSnapshotFor(key);
    if (!snapshot.has_value()) {
        // Programmer error - AddSkyViewLutPass() for this exact name must
        // have already run earlier this same frame. Degrades to "draw
        // nothing" (an invisible sky) rather than crashing.
        return;
    }

    // AtmosphereFrameUniforms::cameraPositionAtmosphere.y is already the
    // "view height from the planet's center" quantity (planetRadiusKm +
    // heightAboveGroundKm) - the exact same value the Sky-View LUT itself reads.
    DrawSkyBackgroundPass(renderer, cmd, viewProjection, snapshot->target.imageView, snapshot->sampler,
        frameUniforms.cameraPositionAtmosphere.y, params.planetRadiusKm, skyExposure);
}

} // namespace gte
