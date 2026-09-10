#include "AtmosphereLutRenderer.h"

#include "../ComputeDispatch.h"
#include "../RenderTarget.h"
#include "../Renderer.h"
#include "../RenderGraph/RenderGraph.h"
#include "../Vulkan/DescriptorSetLayoutBuilder.h"

#include <cstdint>
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

} // namespace

AtmosphereLutRenderer::~AtmosphereLutRenderer()
{
    // m_transmittanceLutOutput/m_multiScatteringLutOutput/
    // m_atmosphereParametersBuffer/m_transmittanceLutPipeline/
    // m_multiScatteringLutPipeline are RAII types and clean up themselves;
    // m_transmittanceLutDescriptorSetLayout/
    // m_multiScatteringLutDescriptorSetLayout are plain Vulkan handles this
    // class owns directly, mirroring ComputeBlurValidation's own identical
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

} // namespace gte
