#include "VolumeTexturePreviewRenderer.h"

#include "ComputeDispatch.h"
#include "Renderer.h"
#include "Vulkan/DescriptorSetLayoutBuilder.h"
#include "VolumeTexturePreviewMath.h"

#include <stdexcept>
#include <utility>

namespace gte {

namespace {

// MUST match src/Shaders/VolumeTexturePreview.comp's own `layout(push_constant)`
// block exactly - every Vec3 followed immediately by one float gives a
// natural 16-byte-aligned group with no compiler-inserted padding (Vec3 is
// three tightly-packed floats, alignof 4 - see Math/Vec3.h), matching
// std140's own vec3-then-scalar packing rule.
struct PushConstants {
    Vec3 eyePosition;
    float tanHalfFovY = 0.0f;
    Vec3 forward;
    float densityScale = 0.0f;
    Vec3 right;
    std::int32_t stepCount = 0;
    Vec3 up;
    float _padding0 = 0.0f;
    Vec3 boxHalfExtents;
    float _padding1 = 0.0f;
    // atmosphere-scattering-2 campaign, Phase 4
    // (task_manager/atmosphere-scattering-2/PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md)
    // - appended at the tail so every existing field above keeps its exact
    // pre-Phase-4 offset. This `int` immediately followed by a `float` packs
    // into 8 bytes with no gap needed - the preceding field (`_padding1`)
    // ends the struct's existing groups exactly 16-byte aligned (5 groups of
    // 16 bytes = offset 80), so this new 8-byte tail needs no extra padding
    // of its own either.
    std::int32_t interpretationMode = 0; // VolumeTexturePreviewInterpretation, as int.
    float aerialPreviewExposure = 0.0f; // Only meaningful when interpretationMode == 1; 0 when mode == 0 (unused).
};

// atmosphere-scattering-2 campaign, Phase 4 - fixed exposure multiplier
// applied ONLY when interpretationMode == AtmosphereAerialPerspective,
// BEFORE the shader's own Reinhard tonemap - chosen empirically so this
// campaign's own typical in-scattering magnitudes (see
// AERIAL_PERSPECTIVE_INVESTIGATION_FINDINGS.md's own cited numbers, ~1e-5 to
// ~1e-3 pre-Phase-3, larger after Phase 3's exaggeration multiplier) land in
// a visually legible mid-range rather than crushing to black. Re-tune this
// constant (not kDensityScale, not any call site) if Phase 3's own final
// chosen exaggeration default changes substantially later.
static constexpr float kAerialPreviewExposure = 2000.0f;

} // namespace

VolumeTexturePreviewRenderer::~VolumeTexturePreviewRenderer()
{
    // m_pipeline/m_outputTexture are RAII types and clean up themselves;
    // m_volumeSampler/m_descriptorSetLayout are plain Vulkan handles this
    // class owns directly, mirroring AtmosphereLutRenderer's own identical
    // destructor shape. m_descriptorSet (a ComputeDescriptorSet) owns no
    // Vulkan handle of its own - the descriptor set it wraps was allocated
    // from a shared pool and is never individually freed (see
    // ComputeDescriptorSet.h's own class comment).
    if (m_volumeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_volumeSampler, nullptr);
    }
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    }
}

void VolumeTexturePreviewRenderer::EnsureInitialized(Renderer& renderer)
{
    if (m_initialized) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Trilinear, clamp-to-edge in all three axes - mirrors VolumeTexture's
    // own sampler description exactly (see VolumeTexture.cpp). VolumeTarget
    // itself, unlike VolumeTexture, carries no sampler of its own, so this
    // class must build its own.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.25f;
    if (vkCreateSampler(m_device, &samplerInfo, nullptr, &m_volumeSampler) != VK_SUCCESS) {
        throw std::runtime_error("VolumeTexturePreviewRenderer: vkCreateSampler failed.");
    }

    // Binding convention (matches src/Shaders/VolumeTexturePreview.comp
    // exactly): binding 0 = the source volume's own trilinear sampler3D
    // (read-only combined image sampler), binding 1 = the output image2D.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_descriptorSetLayout = layoutBuilder.AddCombinedImageSampler(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/VolumeTexturePreview.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_descriptorSetLayout }, pushConstantRange));

    m_descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout));

    // A zero-filled buffer is correct and sufficient here - the very first
    // compute dispatch overwrites every pixel anyway via imageStore (see
    // Texture2D.h's own doc comment for why a freshly-created Texture2D is
    // otherwise left in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, which is
    // this class's own m_outputTextureState initial value below).
    const std::vector<std::uint8_t> zeroFilledPixels(static_cast<std::size_t>(kOutputWidth) * kOutputHeight * 4, 0);
    m_outputTexture.emplace(renderer.CreateTexture2D(zeroFilledPixels.data(), kOutputWidth, kOutputHeight,
        "VolumeTexturePreviewOutput", /*allowStorageImageAccess=*/true));

    m_outputTextureState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);

    m_initialized = true;
}

VolumeTexturePreviewRenderer::CapturedRawPixels VolumeTexturePreviewRenderer::RenderPreview(
    Renderer& renderer, const VolumeTarget& volume, const rg::ResourceState& previousState,
    VolumeTexturePreviewInterpretation interpretation)
{
    EnsureInitialized(renderer);

    const VolumeCameraSetup setup = (interpretation == VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective)
        ? ComputeAtmosphereAerialPerspectivePreviewCameraSetup(static_cast<int>(volume.extent.width),
              static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth))
        : ComputeVolumeCameraSetup(static_cast<int>(volume.extent.width), static_cast<int>(volume.extent.height),
              static_cast<int>(volume.extent.depth));

    // Every call serves a DIFFERENT volume image (unlike a per-model cache
    // elsewhere in this engine that only rewrites once) - binding 0 must be
    // refreshed every time. Binding 1 (the output image) never actually
    // changes across calls since the SAME output texture is reused every
    // time, but costs nothing extra to rewrite alongside binding 0.
    m_descriptorSet.Rewrite(m_device,
        std::vector<ComputeDescriptorWrite>{
            ComputeDescriptorWrite::CombinedImageSampler(0, volume.imageView, m_volumeSampler),
            ComputeDescriptorWrite::StorageImage(1, m_outputTexture->View()),
        });

    PushConstants pushConstants{};
    pushConstants.eyePosition = setup.eyePosition;
    pushConstants.tanHalfFovY = setup.tanHalfFovY;
    pushConstants.forward = setup.forward;
    pushConstants.densityScale = kDensityScale;
    pushConstants.right = setup.right;
    pushConstants.stepCount = kStepCount;
    pushConstants.up = setup.up;
    pushConstants.boxHalfExtents = setup.boxHalfExtents;
    // atmosphere-scattering-2 campaign, Phase 4 - the exposure value is only
    // meaningful when interpretationMode == AtmosphereAerialPerspective;
    // left at 0 (its PushConstants default) for the generic interpretation,
    // exactly mirroring this struct's own doc comment above.
    pushConstants.interpretationMode = static_cast<std::int32_t>(interpretation);
    pushConstants.aerialPreviewExposure =
        (interpretation == VolumeTexturePreviewInterpretation::AtmosphereAerialPerspective) ? kAerialPreviewExposure : 0.0f;

    // A COMBINED IMAGE SAMPLER read from a COMPUTE shader has no existing
    // ResourceAccess enumerator to reuse via RequiredStateFor() -
    // ResourceAccess::ShaderRead maps to the FRAGMENT shader stage, and
    // ResourceAccess::ComputeShaderRead maps to VK_IMAGE_LAYOUT_GENERAL (a
    // storage-image/imageLoad case, not a sampler read) - so this
    // ResourceState is built by hand (see this phase's own strategy
    // document, Step 3.4).
    const rg::ResourceState volumeSampledState{
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    };
    const rg::ResourceState outputWriteState = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
    const rg::ResourceState outputPreviousState = m_outputTextureState;

    const VkImage volumeImage = volume.image;
    const VkImage outputImage = m_outputTexture->Image();
    const VkPipeline pipelineHandle = m_pipeline->Native();
    const VkPipelineLayout pipelineLayout = m_pipeline->Layout();
    const VkDescriptorSet rawDescriptorSet = m_descriptorSet.Native();

    renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        // Never renderer.Dispatch() here - that method is gated to
        // render-graph-pass recording only (asserts/no-ops otherwise, see
        // Renderer::Dispatch()'s own doc comment), and this dispatch runs
        // inside a plain ImmediateSubmit() callback instead - so every
        // Vulkan call below is issued directly.
        rg::EmitImageBarrier(cmd, volumeImage, range, previousState, volumeSampledState);
        rg::EmitImageBarrier(cmd, outputImage, range, outputPreviousState, outputWriteState);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineHandle);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &rawDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

        const std::uint32_t groupCountX = ComputeGroupCount(static_cast<std::uint32_t>(kOutputWidth), kLocalSizeX);
        const std::uint32_t groupCountY = ComputeGroupCount(static_cast<std::uint32_t>(kOutputHeight), kLocalSizeY);
        vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

        // Restore the volume image to its real previous state - a later
        // graph-recorded frame touching the SAME volume texture must see
        // it exactly as the render graph itself still believes it is.
        rg::EmitImageBarrier(cmd, volumeImage, range, volumeSampledState, previousState);

        // Deliberately leave m_outputTexture's image in GENERAL - the next
        // step reads it back via Renderer::CaptureImagePixels(), which
        // performs its own transition dance given whatever ResourceState it
        // is told the image is CURRENTLY in.
    });

    m_outputTextureState = outputWriteState;

    // This output format is never BGRA (VK_FORMAT_R8G8B8A8_UNORM), so no
    // Encoding::ConvertBgraToRgbaInPlace() swizzle is ever needed for this
    // capture kind - see this phase's own strategy document, Step 3.4g.
    const Renderer::CapturedRawPixels raw = renderer.CaptureImagePixels(m_outputTexture->Image(),
        VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R8G8B8A8_UNORM,
        VkExtent2D{ static_cast<std::uint32_t>(kOutputWidth), static_cast<std::uint32_t>(kOutputHeight) },
        m_outputTextureState, /*bytesPerPixel=*/4);
    // CaptureImagePixels() restores the image to GENERAL afterward (per its
    // own contract) - m_outputTextureState is already GENERAL, so no
    // further update is needed here.

    CapturedRawPixels result;
    result.pixels = raw.pixels;
    result.width = raw.width;
    result.height = raw.height;
    return result;
}

} // namespace gte
