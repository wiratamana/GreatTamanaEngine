#include "AtmosphereLutRenderer.h"

#include "AtmosphereParameters.h"
#include "../ComputeDispatch.h"
#include "../RenderTarget.h"
#include "../Renderer.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Vulkan/DescriptorSetLayoutBuilder.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace gte {

namespace {

// MUST match Shaders/AtmosphereTransmittanceLut.comp's own
// `layout(local_size_x = 16, local_size_y = 16) in;` exactly - see
// ComputeDispatch.h's own header comment on why this pairing is a
// hand-maintained, per-shader convention rather than something the build
// system enforces (mirrors ComputeBlurValidation.cpp's own
// kBoxBlurLocalSizeX/Y precedent).
constexpr std::uint32_t kTransmittanceLutLocalSizeX = 16;
constexpr std::uint32_t kTransmittanceLutLocalSizeY = 16;

// Transmittance LUT resolution - see
// task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md,
// Section 2 ("256 x 256", cited from the cloned reference's own
// tTransmissionLutResolution, src/app.c lines ~203-206).
constexpr int kTransmittanceLutWidth = 256;
constexpr int kTransmittanceLutHeight = 256;

// MUST match Shaders/AtmosphereMultiScatteringLut.comp's own
// `layout(local_size_x = 16, local_size_y = 16) in;` exactly.
constexpr std::uint32_t kMultiScatteringLutLocalSizeX = 16;
constexpr std::uint32_t kMultiScatteringLutLocalSizeY = 16;

// Multi-Scattering LUT resolution - see
// task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md,
// Section 2 ("64 x 64", cited from the cloned reference's own
// tMultiscatterLutResolution, src/app.c lines ~203-206).
constexpr int kMultiScatteringLutWidth = 64;
constexpr int kMultiScatteringLutHeight = 64;

// MUST match Shaders/AtmosphereSkyViewLut.comp's own
// `layout(local_size_x = 8, local_size_y = 8) in;` exactly - THIS LUT
// deliberately uses 8x8, not 16x16, matching the cloned reference's own
// established convention for this one shader specifically (see
// _reference/pl-sky/shaders/sky_lut.comp).
constexpr std::uint32_t kSkyViewLutLocalSizeX = 8;
constexpr std::uint32_t kSkyViewLutLocalSizeY = 8;

// Sky-View LUT resolution - see
// task_manager/atmosphere-scattering-1/ATMOSPHERE_REFERENCE_NOTES.md,
// Section 2 ("200 x 100", cited from the cloned reference's own
// tSkyLutResolution, src/app.c lines ~203-206).
constexpr int kSkyViewLutWidth = 200;
constexpr int kSkyViewLutHeight = 100;

} // namespace

AtmosphereLutRenderer::~AtmosphereLutRenderer()
{
    // m_transmittanceLutOutput/m_multiScatteringLutOutput/
    // m_skyViewLutViewStates' own RenderTexture/Buffer members/
    // m_atmosphereParametersBuffer/m_transmittanceLutPipeline/
    // m_multiScatteringLutPipeline/m_skyViewLutPipeline are RAII types and
    // clean up themselves; m_transmittanceLutDescriptorSetLayout/
    // m_multiScatteringLutDescriptorSetLayout/
    // m_skyViewLutDescriptorSetLayout are plain Vulkan handles this class
    // owns directly, mirroring ComputeBlurValidation's own identical
    // destructor shape. Safe to call unconditionally - this object is only
    // ever owned for as long as the Renderer/VkDevice it was built against
    // is still alive (its owner is destroyed well before the device goes
    // away).
    if (m_transmittanceLutDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_transmittanceLutDescriptorSetLayout, nullptr);
    }
    if (m_multiScatteringLutDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_multiScatteringLutDescriptorSetLayout, nullptr);
    }
    if (m_skyViewLutDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_skyViewLutDescriptorSetLayout, nullptr);
    }
}

void AtmosphereLutRenderer::EnsureTransmittanceLutInitialized(Renderer& renderer, const AtmosphereParametersGpu& params)
{
    if (m_transmittanceLutPipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Binding convention (Vulkan/DescriptorSetLayoutBuilder.h's own
    // documented rule): binding 0 is the read-only AtmosphereParametersGpu
    // STORAGE buffer (NOT a true uniform buffer - see this class's own
    // header comment and the strategy document's own "Revision Notes"),
    // binding 1 is the output image2D - must match
    // Shaders/AtmosphereTransmittanceLut.comp exactly.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_transmittanceLutDescriptorSetLayout =
        layoutBuilder.AddStorageBuffer(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

    m_transmittanceLutPipeline.emplace(renderer.CreateComputePipeline("shaders/AtmosphereTransmittanceLut.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_transmittanceLutDescriptorSetLayout }));

    m_transmittanceLutDescriptorSet =
        ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_transmittanceLutDescriptorSetLayout));

    // A read-only storage buffer, CpuToGpu so Upload() can write straight
    // into it every call (see AddTransmittanceLutPass() below) - a 96-byte
    // buffer re-uploaded once per frame (Step 4's own "no dirty-flag
    // optimization yet") is negligible. REUSED VERBATIM by
    // AddMultiScatteringLutPass() below - Phase 4 does NOT create a second,
    // duplicate buffer for the same AtmosphereParametersGpu data.
    m_atmosphereParametersBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereParametersGpu),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemoryUsage::CpuToGpu, "AtmosphereParametersGpu"));
    m_atmosphereParametersBuffer->Upload(&params, sizeof(AtmosphereParametersGpu));

    // Texture2D (allowStorageImageAccess = true) - fixed
    // VK_FORMAT_R8G8B8A8_UNORM is correct for THIS LUT specifically (a
    // transmittance value is always in [0, 1] per channel by definition -
    // see AtmosphereMath::ComputeTransmittanceToTopOfAtmosphere()'s own
    // contract); do NOT copy this choice for Phase 4/5's own multi-
    // scattering/sky-radiance LUTs, which can exceed 1.0 and need a real
    // HDR format (RenderTexture with an explicit float format, or a future
    // Texture2D `format` parameter). CreateTexture2D() always uploads its
    // initial pixel data via a staging buffer - a zero-filled buffer here
    // is fine, since this pass's own compute write fully overwrites every
    // texel the very first time it runs anyway.
    const std::vector<std::uint8_t> zeroPixels(
        static_cast<std::size_t>(kTransmittanceLutWidth) * static_cast<std::size_t>(kTransmittanceLutHeight) * 4, 0);
    m_transmittanceLutOutput.emplace(renderer.CreateTexture2D(zeroPixels.data(), kTransmittanceLutWidth,
        kTransmittanceLutHeight, "AtmosphereTransmittanceLut", /*allowStorageImageAccess=*/true));
}

rg::TextureHandle AtmosphereLutRenderer::AddTransmittanceLutPass(
    rg::RenderGraphBuilder& builder, Renderer& renderer, const AtmosphereParametersGpu& params)
{
    EnsureTransmittanceLutInitialized(renderer, params);

    // Step 4's own "What We Will NOT Do": no dirty-flag optimization yet -
    // unconditionally re-upload the (tiny, 96-byte) parameters buffer and
    // recompute the whole LUT every single call.
    m_atmosphereParametersBuffer->Upload(&params, sizeof(AtmosphereParametersGpu));

    // Texture2D has no Target()/RenderTarget accessor of its own (unlike
    // RenderTexture) - build one by hand from its plain Vulkan handles; the
    // depth fields stay at their default (VK_NULL_HANDLE/VK_FORMAT_UNDEFINED),
    // since this compute pass never uses a depth attachment.
    RenderTarget target{};
    target.image = m_transmittanceLutOutput->Image();
    target.imageView = m_transmittanceLutOutput->View();
    target.extent = VkExtent2D{ static_cast<std::uint32_t>(m_transmittanceLutOutput->Width()),
        static_cast<std::uint32_t>(m_transmittanceLutOutput->Height()) };
    target.format = VK_FORMAT_R8G8B8A8_UNORM;

    // Always imported as VK_IMAGE_LAYOUT_UNDEFINED - mirrors
    // ComputeBlurValidation::AddPass()'s own identical reasoning: this
    // pass's own compute write fully overwrites every in-bounds texel every
    // time it runs, so whatever layout/contents this texture was left in
    // last frame never needs to be preserved.
    const rg::TextureHandle outputHandle =
        builder.ImportTexture("AtmosphereTransmittanceLut", target, VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddComputePass(
        "AtmosphereTransmittanceLutPass",
        [outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &renderer, outputHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            m_transmittanceLutDescriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, m_atmosphereParametersBuffer->Native()),
                    ComputeDescriptorWrite::StorageImage(1, dest.view),
                });

            const Extent3D groupCounts = ComputeGroupCount3D(
                Extent3D{ static_cast<std::uint32_t>(kTransmittanceLutWidth),
                    static_cast<std::uint32_t>(kTransmittanceLutHeight), 1 },
                Extent3D{ kTransmittanceLutLocalSizeX, kTransmittanceLutLocalSizeY, 1 });

            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            renderer.Dispatch(*m_transmittanceLutPipeline, m_transmittanceLutDescriptorSet.Native(), nullptr, 0,
                groupCounts.width, groupCounts.height, groupCounts.depth);
            renderer.EndGraphPassRecording();
        });

    return outputHandle;
}

void AtmosphereLutRenderer::EnsureMultiScatteringLutInitialized(Renderer& renderer)
{
    if (m_multiScatteringLutPipeline.has_value()) {
        return;
    }

    // By the time this is ever called, EnsureTransmittanceLutInitialized()
    // has ALREADY run (AddTransmittanceLutPass() is always called first, in
    // the SAME frame, at this campaign's own temporary validation call
    // site - see Application.cpp) - m_device/m_atmosphereParametersBuffer/
    // m_transmittanceLutOutput are therefore already valid here. This
    // method does NOT create a second AtmosphereParametersGpu buffer.
    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Binding convention (matches
    // Shaders/AtmosphereMultiScatteringLut.comp exactly): binding 0 is the
    // read-only AtmosphereParametersGpu STORAGE buffer (the SAME buffer
    // AddTransmittanceLutPass() already created/uploads - reused, not
    // duplicated), binding 1 is the read-only transmittanceLut combined
    // image sampler, binding 2 is the output image2D.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_multiScatteringLutDescriptorSetLayout = layoutBuilder.AddStorageBuffer(/*binding=*/0)
                                                   .AddCombinedImageSampler(/*binding=*/1)
                                                   .AddStorageImage(/*binding=*/2)
                                                   .Build();

    m_multiScatteringLutPipeline.emplace(
        renderer.CreateComputePipeline("shaders/AtmosphereMultiScatteringLut.comp.spv",
            std::vector<VkDescriptorSetLayout>{ m_multiScatteringLutDescriptorSetLayout }));

    m_multiScatteringLutDescriptorSet =
        ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_multiScatteringLutDescriptorSetLayout));

    // HDR output - RenderTexture with an explicit float format (per this
    // campaign's own "Revision Notes", at the top of the strategy
    // document): a multi-scattering "response" value can exceed 1.0,
    // unlike the Transmittance LUT's bounded [0, 1] output, so Texture2D's
    // fixed VK_FORMAT_R8G8B8A8_UNORM would clip it. Accepts RenderTexture's
    // always-created companion DepthBuffer as a harmless, here-unused cost
    // (this pass never draws/depth-tests anything) - see this phase's own
    // completion report for the explicit record of this choice.
    m_multiScatteringLutOutput.emplace(renderer.CreateRenderTexture(kMultiScatteringLutWidth, kMultiScatteringLutHeight,
        VK_FORMAT_R16G16B16A16_SFLOAT, "AtmosphereMultiScatteringLut", "AtmosphereMultiScatteringLutDepth",
        /*allowStorageImageAccess=*/true));
}

rg::TextureHandle AtmosphereLutRenderer::AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle)
{
    (void)params; // Already uploaded into m_atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.

    EnsureMultiScatteringLutInitialized(renderer);

    const rg::TextureHandle outputHandle = builder.ImportTexture(
        "AtmosphereMultiScatteringLut", m_multiScatteringLutOutput->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddComputePass(
        "AtmosphereMultiScatteringLutPass",
        [transmittanceLutHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            // The real dependency declaration that makes RenderGraphCompiler
            // order this pass strictly after AddTransmittanceLutPass()'s
            // own write - see this phase's own strategy document, Step 2,
            // and BoxBlur.comp's own sceneViewHandle precedent for why
            // ShaderRead (not ComputeShaderRead) is the correct access value
            // for a compute shader's sampler2D read. Verified via this
            // phase's own Step 5 ordering-proof (see completion report) -
            // temporarily commenting this line out did NOT change the
            // captured LUT's bytes (the Transmittance LUT recomputes
            // identically every frame from fixed parameters, so a stale vs.
            // fresh read of it is visually indistinguishable here) - a
            // documented, inconclusive-by-visual-diff result, not a
            // "nothing happened" one; see the completion report for the
            // full reasoning and why the barrier this declares is still
            // correctness-critical regardless.
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &renderer, outputHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            // m_transmittanceLutOutput is the SAME Texture2D
            // AddTransmittanceLutPass() itself writes through - its own
            // Sampler() is used directly here rather than resolved via
            // PassContext::resolveTexture(), since an IMPORTED texture's
            // resolved sampler is always VK_NULL_HANDLE (mirrors
            // ComputeBlurValidation::AddPass()'s own identical reasoning
            // for sceneViewSampler).
            m_multiScatteringLutDescriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, m_atmosphereParametersBuffer->Native()),
                    ComputeDescriptorWrite::CombinedImageSampler(
                        1, m_transmittanceLutOutput->View(), m_transmittanceLutOutput->Sampler()),
                    ComputeDescriptorWrite::StorageImage(2, dest.view),
                });

            const Extent3D groupCounts = ComputeGroupCount3D(
                Extent3D{ static_cast<std::uint32_t>(kMultiScatteringLutWidth),
                    static_cast<std::uint32_t>(kMultiScatteringLutHeight), 1 },
                Extent3D{ kMultiScatteringLutLocalSizeX, kMultiScatteringLutLocalSizeY, 1 });

            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            renderer.Dispatch(*m_multiScatteringLutPipeline, m_multiScatteringLutDescriptorSet.Native(), nullptr, 0,
                groupCounts.width, groupCounts.height, groupCounts.depth);
            renderer.EndGraphPassRecording();
        });

    return outputHandle;
}

void AtmosphereLutRenderer::EnsureSkyViewLutInitialized(Renderer& renderer)
{
    if (m_skyViewLutPipeline.has_value()) {
        return;
    }

    // By the time this is ever called, EnsureTransmittanceLutInitialized()
    // has ALREADY run (AddTransmittanceLutPass() is always called first, in
    // the SAME frame, at this campaign's own temporary validation call
    // site - see Application.cpp) - m_device/m_atmosphereParametersBuffer/
    // m_transmittanceLutOutput/m_multiScatteringLutOutput are therefore
    // already valid here. This method does NOT create a second
    // AtmosphereParametersGpu buffer.
    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Binding convention (matches Shaders/AtmosphereSkyViewLut.comp
    // exactly): binding 0 = AtmosphereParametersGpu (the SAME buffer
    // AddTransmittanceLutPass() already created/uploads - reused, not
    // duplicated), binding 1 = AtmosphereFrameUniforms (a SECOND read-only
    // storage buffer, per-VIEW - see m_skyViewLutViewStates), binding 2 =
    // the read-only transmittanceLut combined image sampler, binding 3 =
    // the read-only multiScatteringLut combined image sampler, binding 4 =
    // the output image2D.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_skyViewLutDescriptorSetLayout = layoutBuilder.AddStorageBuffer(/*binding=*/0)
                                          .AddStorageBuffer(/*binding=*/1)
                                          .AddCombinedImageSampler(/*binding=*/2)
                                          .AddCombinedImageSampler(/*binding=*/3)
                                          .AddStorageImage(/*binding=*/4)
                                          .Build();

    m_skyViewLutPipeline.emplace(renderer.CreateComputePipeline(
        "shaders/AtmosphereSkyViewLut.comp.spv", std::vector<VkDescriptorSetLayout>{ m_skyViewLutDescriptorSetLayout }));
}

AtmosphereLutRenderer::SkyViewLutViewState& AtmosphereLutRenderer::EnsureSkyViewLutViewInitialized(
    Renderer& renderer, const char* outputTextureName)
{
    const auto existing = m_skyViewLutViewStates.find(outputTextureName);
    if (existing != m_skyViewLutViewStates.end()) {
        return existing->second;
    }

    SkyViewLutViewState state;
    state.descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_skyViewLutDescriptorSetLayout));
    state.frameUniformsBuffer.emplace(renderer.CreateBuffer(sizeof(AtmosphereFrameUniforms),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, BufferMemoryUsage::CpuToGpu, "AtmosphereFrameUniforms"));

    // HDR RenderTexture output, per this phase's own strategy document's
    // "Revision Notes" - the sky's own near-sun radiance routinely exceeds
    // 1.0, so Texture2D's fixed VK_FORMAT_R8G8B8A8_UNORM would clip it.
    // `outputTextureName` is reused directly as this RenderTexture's own
    // GpuMemoryTracker debug name (guaranteed static-storage-duration by
    // AddSkyViewLutPass()'s own contract) - depthDebugName is left null
    // (this pass never draws/depth-tests anything, same accepted
    // "harmless, here-unused cost" as the Multi-Scattering LUT's own
    // companion DepthBuffer).
    state.output.emplace(renderer.CreateRenderTexture(kSkyViewLutWidth, kSkyViewLutHeight,
        VK_FORMAT_R16G16B16A16_SFLOAT, outputTextureName, /*depthDebugName=*/nullptr,
        /*allowStorageImageAccess=*/true));

    const auto insertedPair = m_skyViewLutViewStates.emplace(outputTextureName, std::move(state));
    return insertedPair.first->second;
}

rg::TextureHandle AtmosphereLutRenderer::AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle, const char* outputTextureName)
{
    (void)params; // Already uploaded into m_atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.

    EnsureSkyViewLutInitialized(renderer);
    SkyViewLutViewState& viewState = EnsureSkyViewLutViewInitialized(renderer, outputTextureName);

    // Step 4's own "no dirty-flag optimization" - this per-view buffer is
    // unconditionally re-uploaded and the whole LUT recomputed every
    // single call, exactly like AddTransmittanceLutPass()'s own contract.
    viewState.frameUniformsBuffer->Upload(&frameUniforms, sizeof(AtmosphereFrameUniforms));

    const rg::TextureHandle outputHandle =
        builder.ImportTexture(outputTextureName, viewState.output->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddComputePass(
        "AtmosphereSkyViewLutPass",
        [transmittanceLutHandle, multiScatteringLutHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            // Real dependency declarations - order this pass strictly
            // after AddTransmittanceLutPass()/AddMultiScatteringLutPass()'s
            // own writes this same frame (see RenderGraphCompiler), same
            // ShaderRead convention as AddMultiScatteringLutPass() above.
            pass.ReadTexture(transmittanceLutHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(multiScatteringLutHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &renderer, &viewState, outputHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            // m_transmittanceLutOutput/m_multiScatteringLutOutput are the
            // SAME Texture2D/RenderTexture AddTransmittanceLutPass()/
            // AddMultiScatteringLutPass() themselves write through - their
            // own View()/Sampler() are used directly here rather than
            // resolved via PassContext::resolveTexture(), since an
            // IMPORTED texture's resolved sampler is always
            // VK_NULL_HANDLE (mirrors AddMultiScatteringLutPass()'s own
            // identical reasoning). `&viewState` stays valid across any
            // future insertion into m_skyViewLutViewStates - references
            // to an already-inserted std::unordered_map element are never
            // invalidated by inserting MORE elements (only erasing that
            // element would invalidate it), so capturing it by reference
            // here is safe even if a Scene View call inserts a second
            // entry into the same map later in the SAME frame (Phase 7).
            viewState.descriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::StorageBuffer(0, m_atmosphereParametersBuffer->Native()),
                    ComputeDescriptorWrite::StorageBuffer(1, viewState.frameUniformsBuffer->Native()),
                    ComputeDescriptorWrite::CombinedImageSampler(
                        2, m_transmittanceLutOutput->View(), m_transmittanceLutOutput->Sampler()),
                    ComputeDescriptorWrite::CombinedImageSampler(
                        3, m_multiScatteringLutOutput->View(), m_multiScatteringLutOutput->Sampler()),
                    ComputeDescriptorWrite::StorageImage(4, dest.view),
                });

            const Extent3D groupCounts = ComputeGroupCount3D(
                Extent3D{ static_cast<std::uint32_t>(kSkyViewLutWidth), static_cast<std::uint32_t>(kSkyViewLutHeight), 1 },
                Extent3D{ kSkyViewLutLocalSizeX, kSkyViewLutLocalSizeY, 1 });

            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            renderer.Dispatch(*m_skyViewLutPipeline, viewState.descriptorSet.Native(), nullptr, 0, groupCounts.width,
                groupCounts.height, groupCounts.depth);
            renderer.EndGraphPassRecording();
        });

    return outputHandle;
}

AtmosphereFrameUniforms ResolveAtmosphereFrameUniforms(Registry& registry, Vec3 eyeWorldPosition)
{
    // TODO(ATMOSPHERE_PHASE8): replace with real DirectionalLight
    // resolution - `registry` is accepted now purely so this function's
    // own SIGNATURE never needs to change once that phase lands; it is not
    // read at all yet (see this function's own declaration in
    // AtmosphereLutRenderer.h for the full reasoning).
    (void)registry;

    AtmosphereFrameUniforms uniforms;

    const AtmosphereParametersGpu earthParameters = MakeDefaultEarthAtmosphereParameters();
    const Vec3 eyeAtmosphereKm = WorldPositionToAtmosphereSpaceKm(eyeWorldPosition);
    const float heightAboveGroundKm = eyeAtmosphereKm.y > 0.0f ? eyeAtmosphereKm.y : 0.0f;

    // Planet-centered frame (Length(positionKm) == planetRadiusKm +
    // heightAboveGroundKm, see AtmosphereMath.h's own COORDINATE
    // CONVENTION) - the camera's horizontal world position is deliberately
    // NOT folded in here, matching how every LUT pass in this campaign
    // (and the cloned reference itself) only ever cares about height above
    // the ground, never full 3D world position.
    uniforms.cameraPositionAtmosphere = Vec3(0.0f, earthParameters.planetRadiusKm + heightAboveGroundKm, 0.0f);

    // TODO(ATMOSPHERE_PHASE8): replace with real DirectionalLight
    // resolution - hardcoded placeholder: a fixed 45-degree-elevation sun
    // direction (azimuth 0), per this phase's own strategy document.
    uniforms.sunDirection = Normalize(Vec3(0.70710678f, 0.70710678f, 0.0f));

    // Sun color (1.0, 0.95, 0.85) * illuminance scale 3.0 - transcribed
    // verbatim from the cloned reference (see
    // ATMOSPHERE_REFERENCE_NOTES.md, Section 1).
    uniforms.sunIlluminance = Vec3(1.0f, 0.95f, 0.85f) * 3.0f;

    return uniforms;
}

} // namespace gte
