#include "AtmosphereLutRendererImpl.h"

#include "AtmosphereParameters.h"
#include "AtmospherePassToggleLogic.h"
#include "../../Core/PipelineDesc.h"
#include "../../Renderer/RenderTarget.h"
#include "../../Renderer/RenderGraph/CommandBuffer.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../../Renderer/RenderGraph/RenderGraphPersistentResourceOwners.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gte {

namespace {

// Transmittance LUT resolution.
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

// Push constants for AtmosphereAerialPerspectiveComposite.comp (96 bytes).
struct AerialPerspectiveCompositePushConstants {
    float invViewProjection[16];

    // xyz = camera world position, w = world units per km.
    float cameraWorldPositionAndScale[4];

    // x = strength, y = max distance (km), z = depth exponent, w = padding.
    float aerialPerspectiveStrengthAndPad[4];
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

// Push constants for AtmosphereSkyBackground.frag (80 bytes).
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
    if (ownerRenderer != nullptr && skyBackgroundPipeline) {
        ownerRenderer->WaitForGpuIdle();
    }
    // Clean up manually created graphics descriptor set layout.
    if (ownerRenderer != nullptr) {
        ownerRenderer->DestroyDescriptorSetLayout(skyBackgroundSetLayoutHandle);
    }
}

void AtmosphereLutRenderer::Impl::EnsureTransmittanceLutInitialized(
    Renderer& renderer, const AtmosphereParametersGpu& params)
{
    if (transmittanceLutPipeline.has_value()) {
        return;
    }

    // Bindings: 0 = AtmosphereParametersGpu, 1 = output storage image.
    transmittanceLutPipeline.emplace(renderer.CreateComputePipeline("shaders/AtmosphereTransmittanceLut.comp.spv"));
    transmittanceLutDescriptorSetLayoutHandle = transmittanceLutPipeline->ReflectedDescriptorSetLayoutHandle(/*set=*/0);

    // Shared parameter buffer across all LUT passes.
    atmosphereParametersBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereParametersGpu),
        BufferUsage::StorageBuffer, BufferMemoryUsage::CpuToGpu, "AtmosphereParametersGpu"));
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

    // Re-upload parameters every frame.
    atmosphereParametersBuffer->Upload(&params, sizeof(AtmosphereParametersGpu));

    // Output texture in [0, 1] UNORM format.
    rg::TextureDesc desc{};
    desc.width = static_cast<std::uint32_t>(kTransmittanceLutWidth);
    desc.height = static_cast<std::uint32_t>(kTransmittanceLutHeight);
    desc.format = PixelFormat::RGBA8_UNorm;
    desc.usage = rg::TextureUsage::Storage;
    const rg::TextureHandle outputHandle =
        builder.GetOrCreatePersistentTexture(transmittanceLutToken, "", "AtmosphereTransmittanceLut", desc);

    builder.AddRenderPass(
        "AtmosphereTransmittanceLutPass", rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [this, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);

            pass.BindDescriptorSetLayout(transmittanceLutDescriptorSetLayoutHandle);
            pass.BindStorageBuffer(0, atmosphereParametersBuffer->ObjectHandle());
            pass.BindStorageImage(1, outputHandle);
        },
        [this](rg::PassContext& ctx) {
            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*transmittanceLutPipeline);
            cmd.BindDescriptorSet(ctx.BoundDescriptorSet());
            cmd.DispatchOverSize(
                static_cast<std::uint32_t>(kTransmittanceLutWidth), static_cast<std::uint32_t>(kTransmittanceLutHeight), 1);
        },
        // Runs before opaque passes.
        rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::PreOpaques);

    return outputHandle;
}

void AtmosphereLutRenderer::Impl::EnsureMultiScatteringLutInitialized(Renderer& renderer)
{
    if (multiScatteringLutPipeline.has_value()) {
        return;
    }

    // Bindings: 0 = params, 1 = transmittance LUT sampler, 2 = output image.
    multiScatteringLutPipeline.emplace(
        renderer.CreateComputePipeline("shaders/AtmosphereMultiScatteringLut.comp.spv"));
    multiScatteringLutDescriptorSetLayoutHandle =
        multiScatteringLutPipeline->ReflectedDescriptorSetLayoutHandle(/*set=*/0);
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

    (void)params; // Already uploaded by AddTransmittanceLutPass.

    EnsureMultiScatteringLutInitialized(renderer);

    // HDR format to prevent clipping values > 1.0.
    rg::TextureDesc desc{};
    desc.width = static_cast<std::uint32_t>(kMultiScatteringLutWidth);
    desc.height = static_cast<std::uint32_t>(kMultiScatteringLutHeight);
    desc.format = PixelFormat::RGBA16_Float;
    desc.usage = rg::TextureUsage::Storage;
    const rg::TextureHandle outputHandle = builder.GetOrCreatePersistentTexture(
        multiScatteringLutToken, rg::kPersistentOwnerAtmosphere, "MultiScatteringLut", desc);

    builder.AddRenderPass(
        "AtmosphereMultiScatteringLutPass", rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [this, transmittanceLutHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);

            pass.BindDescriptorSetLayout(multiScatteringLutDescriptorSetLayoutHandle);
            pass.BindStorageBuffer(0, atmosphereParametersBuffer->ObjectHandle());
            pass.BindCombinedImageSampler(1, transmittanceLutHandle);
            pass.BindStorageImage(2, outputHandle);
        },
        [this](rg::PassContext& ctx) {
            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*multiScatteringLutPipeline);
            cmd.BindDescriptorSet(ctx.BoundDescriptorSet());
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

    // Bindings: 0 = params, 1 = view uniforms, 2 = transmittance, 3 = multi-scattering, 4 = output image.
    skyViewLutPipeline.emplace(renderer.CreateComputePipeline("shaders/AtmosphereSkyViewLut.comp.spv"));
    skyViewLutDescriptorSetLayoutHandle = skyViewLutPipeline->ReflectedDescriptorSetLayoutHandle(/*set=*/0);
}

AtmosphereLutRenderer::Impl::SkyViewLutViewState& AtmosphereLutRenderer::Impl::EnsureSkyViewLutViewInitialized(
    Renderer& renderer, const char* outputTextureName)
{
    const auto existing = skyViewLutViewStates.find(outputTextureName);
    if (existing != skyViewLutViewStates.end()) {
        return existing->second;
    }

    SkyViewLutViewState state;
    state.frameUniformsBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereFrameUniforms),
        BufferUsage::StorageBuffer, BufferMemoryUsage::CpuToGpu, "AtmosphereFrameUniforms"));

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

    (void)params; // Already uploaded by AddTransmittanceLutPass.

    EnsureSkyViewLutInitialized(renderer);
    SkyViewLutViewState& viewState = EnsureSkyViewLutViewInitialized(renderer, outputTextureName);

    // Re-upload per-view frame uniforms every frame.
    viewState.frameUniformsBuffer->Upload(&frameUniforms, sizeof(AtmosphereFrameUniforms));

    // HDR format for sky radiance.
    rg::TextureDesc desc{};
    desc.width = static_cast<std::uint32_t>(kSkyViewLutWidth);
    desc.height = static_cast<std::uint32_t>(kSkyViewLutHeight);
    desc.format = PixelFormat::RGBA16_Float;
    desc.usage = rg::TextureUsage::Storage;
    const rg::TextureHandle outputHandle = builder.GetOrCreatePersistentTexture(
        viewState.token, rg::kPersistentOwnerAtmosphere, outputTextureName, desc);

    builder.AddRenderPass(
        "AtmosphereSkyViewLutPass", rg::PassKind::Compute, viewScope, rg::RenderPassCategory::General,
        [this, &viewState, transmittanceLutHandle, multiScatteringLutHandle, outputHandle](
            rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(multiScatteringLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);

            pass.BindDescriptorSetLayout(skyViewLutDescriptorSetLayoutHandle);
            pass.BindStorageBuffer(0, atmosphereParametersBuffer->ObjectHandle());
            pass.BindStorageBuffer(1, viewState.frameUniformsBuffer->ObjectHandle());
            pass.BindCombinedImageSampler(2, transmittanceLutHandle);
            pass.BindCombinedImageSampler(3, multiScatteringLutHandle);
            pass.BindStorageImage(4, outputHandle);
        },
        [this](rg::PassContext& ctx) {
            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*skyViewLutPipeline);
            cmd.BindDescriptorSet(ctx.BoundDescriptorSet());
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

    // Bindings: 0 = params, 1 = view uniforms, 2 = transmittance, 3 = multi-scattering, 4 = output 3D image.
    aerialPerspectiveVolumePipeline.emplace(
        renderer.CreateComputePipeline("shaders/AtmosphereAerialPerspectiveVolume.comp.spv"));
    aerialPerspectiveVolumeDescriptorSetLayoutHandle =
        aerialPerspectiveVolumePipeline->ReflectedDescriptorSetLayoutHandle(/*set=*/0);
}

AtmosphereLutRenderer::Impl::AerialPerspectiveVolumeViewState&
AtmosphereLutRenderer::Impl::EnsureAerialPerspectiveVolumeViewInitialized(Renderer& renderer, const char* outputVolumeName)
{
    const auto existing = aerialPerspectiveVolumeViewStates.find(outputVolumeName);
    if (existing != aerialPerspectiveVolumeViewStates.end()) {
        return existing->second;
    }

    AerialPerspectiveVolumeViewState state;
    state.frameUniformsBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereFrameUniforms),
        BufferUsage::StorageBuffer, BufferMemoryUsage::CpuToGpu, "AtmosphereFrameUniforms"));

    // Persistent 3D volume texture owned per view.
    state.output.emplace(renderer.CreateVolumeTexture(kAerialPerspectiveVolumeWidth, kAerialPerspectiveVolumeHeight,
        kAerialPerspectiveVolumeDepth, PixelFormat::RGBA16_Float, outputVolumeName));

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

    (void)params; // Already uploaded by AddTransmittanceLutPass.

    EnsureAerialPerspectiveVolumeInitialized(renderer);
    AerialPerspectiveVolumeViewState& viewState =
        EnsureAerialPerspectiveVolumeViewInitialized(renderer, outputVolumeName);

    viewState.frameUniformsBuffer->Upload(&frameUniforms, sizeof(AtmosphereFrameUniforms));

    const rg::VolumeTextureHandle outputHandle =
        builder.ImportVolumeTexture(outputVolumeName, viewState.output->Target(), ImageLayout::Undefined);

    builder.AddRenderPass(
        "AtmosphereAerialPerspectiveVolumePass", rg::PassKind::Compute, viewScope, rg::RenderPassCategory::General,
        [this, &viewState, transmittanceLutHandle, multiScatteringLutHandle, outputHandle](
            rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(multiScatteringLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteVolumeTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);

            pass.BindDescriptorSetLayout(aerialPerspectiveVolumeDescriptorSetLayoutHandle);
            pass.BindStorageBuffer(0, atmosphereParametersBuffer->ObjectHandle());
            pass.BindStorageBuffer(1, viewState.frameUniformsBuffer->ObjectHandle());
            pass.BindCombinedImageSampler(2, transmittanceLutHandle);
            pass.BindCombinedImageSampler(3, multiScatteringLutHandle);
            pass.BindStorageImage(4, outputHandle);
        },
        [this](rg::PassContext& ctx) {
            // Each 2D invocation column marches through all Z slices internally.
            auto cmd = ctx.Cmd();
            cmd.BindComputePipeline(*aerialPerspectiveVolumePipeline);
            cmd.BindDescriptorSet(ctx.BoundDescriptorSet());
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

    // Bindings: 0 = color, 1 = depth, 2 = volume, 3 = output image.
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

    // Manually owned texture for editor inspection and display output.
    const int width = extent.width > 0 ? static_cast<int>(extent.width) : 1;
    const int height = extent.height > 0 ? static_cast<int>(extent.height) : 1;
    state.output.emplace(renderer.CreateRenderTexture(width, height, PixelFormat::RGBA8_UNorm, outputTextureName,
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
        // Wait idle before resizing render texture.
        renderer.WaitForGpuIdle();
        viewState.output->Resize(static_cast<int>(extent.width), static_cast<int>(extent.height));
    }

    const rg::TextureHandle outputHandle = builder.ImportTexture(outputTextureName, viewState.output->Target(),
        ImageLayout::Undefined, viewState.output->Sampler(), viewState.output->DepthSampler(), &*viewState.output);

    // Resolve volume sampler handle.
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
        // Run after main scene composition passes.
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

void AtmosphereLutRenderer::Impl::EnsureSkyBackgroundPipelineInitialized(Renderer& renderer)
{
    if (skyBackgroundPipeline) {
        return;
    }
    ownerRenderer = &renderer;

    // Binding 0: Sky-View LUT combined image sampler.
    constexpr DescriptorBindingDesc kBindings[] = {
        { .binding = 0, .kind = DescriptorKind::CombinedImageSampler, .stages = ShaderStageMask::Fragment },
    };
    skyBackgroundSetLayoutHandle = renderer.CreateDescriptorSetLayout(kBindings);
    skyBackgroundDescriptorSet = renderer.AllocateComputeDescriptorSet(skyBackgroundSetLayoutHandle);

    // Draw full-screen triangle where depth equals clear depth (1.0).
    PipelineDesc desc = PipelinePreset::DepthEqualNoWrite(renderer.ColorPixelFormat(), renderer.DepthPixelFormat());
    desc.vertexLayout = VertexLayout::None;
    desc.cullMode = CullMode::None;
    desc.layout.extraSetLayouts = std::span{ &skyBackgroundSetLayoutHandle, 1 };
    desc.layout.pushConstantByteSize = sizeof(SkyBackgroundPushConstants);
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/AtmosphereSkyBackground.vert.spv");
    const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/AtmosphereSkyBackground.frag.spv");
    skyBackgroundPipeline = std::make_unique<Pipeline>(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
}

void AtmosphereLutRenderer::Impl::DrawSkyBackgroundPass(Renderer& renderer, rg::CommandBuffer& cmd,
    const Mat4& viewProjection, ImageViewHandle skyViewLutView, SamplerHandle skyViewLutSampler, float eyeHeightKm,
    float planetRadiusKm, float skyExposure)
{
    Mat4 invViewProjection;
    if (!viewProjection.TryInverse(invViewProjection)) {
        return;
    }

    EnsureSkyBackgroundPipelineInitialized(renderer);

    // Unwrap to native Vulkan only right here, at the single leaf call that
    // actually needs it - GraphicsHandleRegistry.h is the only place these
    // handles ever convert back to a real Vk* value.
    skyBackgroundDescriptorSet.Rewrite(
        std::vector<ComputeDescriptorWrite>{
            ComputeDescriptorWrite::CombinedImageSampler(0, Native(skyViewLutView), Native(skyViewLutSampler)),
        });

    SkyBackgroundPushConstants pushConstants{};
    std::memcpy(pushConstants.invViewProjection, invViewProjection.Data(), sizeof(pushConstants.invViewProjection));
    pushConstants.eyeHeightKm = eyeHeightKm;
    pushConstants.planetRadiusKm = planetRadiusKm;
    pushConstants.skyExposure = skyExposure;

    cmd.BindPipeline(*skyBackgroundPipeline);
    cmd.BindDescriptorSet(0, skyBackgroundDescriptorSet.Native());
    cmd.PushConstants(pushConstants, ShaderStageMask::Fragment);
    cmd.Draw(3);
}

void AtmosphereLutRenderer::Impl::DrawSkyBackground(Renderer& renderer, rg::CommandBuffer& cmd,
    const rg::RenderGraph& renderGraph, const Mat4& viewProjection, const AtmosphereParametersGpu& params,
    const AtmosphereFrameUniforms& frameUniforms, const char* skyViewLutName, float skyExposure)
{
    // Retrieve Sky-View LUT snapshot by name from render graph.
    const std::string key = std::string(rg::kPersistentOwnerAtmosphere) + "::" + skyViewLutName;
    const std::optional<rg::DebugTextureSnapshot> snapshot = renderGraph.DebugTextureSnapshotFor(key);
    if (!snapshot.has_value()) {
        return;
    }

    // Resolve this frame's raw Vk snapshot fields into stable, engine-owned
    // handles once here - DrawSkyBackgroundPass() itself never sees a raw
    // VkImageView/VkSampler.
    const ImageViewHandle skyViewLutView = skyViewLutImageViewHandle.Resolve(snapshot->target.imageView);
    const SamplerHandle skyViewLutSampler = skyViewLutSamplerHandle.Resolve(snapshot->sampler);

    // cameraPositionAtmosphere.y stores view height from planet center.
    DrawSkyBackgroundPass(renderer, cmd, viewProjection, skyViewLutView, skyViewLutSampler,
        frameUniforms.cameraPositionAtmosphere.y, params.planetRadiusKm, skyExposure);
}

} // namespace gte
