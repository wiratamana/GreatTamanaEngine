#include "VolumeTexturePreviewRenderer.h"

#include "Compute/ComputeDispatch.h"
#include "Renderer.h"
#include "RenderGraph/CommandBuffer.h"
#include "VolumeTexturePreviewMath.h"

#include <stdexcept>
#include <utility>

namespace gte {

namespace {

// Must match the `layout(push_constant)` block in src/Shaders/VolumeTexturePreview.comp.
// Each Vec3 followed by one float forms a 16-byte group with no padding (std140 vec3-then-scalar packing).
struct PushConstants {
    Vec3 eyePosition;
    float tanHalfFovY = 0.0f;
    Vec3 forward;
    float densityScale = 0.0f;
    Vec3 right;
    std::int32_t stepCount = 0;
    Vec3 up;
    float _padding0 = 0.0f;
    // Box half-extents when shapeMode == 0. When shapeMode == 1 it carries (farHalfWidth, farHalfHeight, halfDepth),
    // a different order than FrustumProxy declares them.
    Vec3 boxHalfExtents;
    float _padding1 = 0.0f;
    std::int32_t interpretationMode = 0; // VolumeTexturePreviewInterpretation, as int.
    float aerialPreviewExposure = 0.0f; // Only used when interpretationMode == 1.
    std::int32_t shapeMode = 0; // 0 = axis-aligned box (IntersectRayBox), 1 = tapering frustum (IntersectRayFrustum).
};

// Exposure applied before the shader's Reinhard tonemap for the aerial perspective interpretation.
// Its in-scattering values are tiny, so this lifts them into a legible mid-range.
static constexpr float kAerialPreviewExposure = 3200.0f;

} // namespace

VolumeTexturePreviewInterpretation SelectVolumeTexturePreviewInterpretation(const std::string& volumeTextureName)
{
    const bool isAerialPerspectiveVolume = volumeTextureName.rfind("AtmosphereAerialPerspectiveVolume", 0) == 0;
    return isAerialPerspectiveVolume ? VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum
                                      : VolumeTexturePreviewInterpretation::GenericDensityInAlpha;
}

VolumeTexturePreviewRenderer::~VolumeTexturePreviewRenderer()
{
    // m_pipeline and m_outputTexture clean up themselves. m_descriptorSetLayout is borrowed from m_pipeline
    // (destroying it here would double-free). m_descriptorSet comes from a shared pool and is never freed alone.
    if (m_volumeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_volumeSampler, nullptr);
    }
}

void VolumeTexturePreviewRenderer::EnsureInitialized(Renderer& renderer)
{
    if (m_initialized) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Trilinear, clamp-to-edge in all three axes, like VolumeTexture's sampler (VolumeTarget carries none).
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

    // Layout and push constants come from the compiled shader's reflection:
    // binding 0 is the source volume's sampler3D, binding 1 the output image2D.
    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/VolumeTexturePreview.comp.spv"));
    m_descriptorSetLayout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    m_descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout));

    // Zero-filled is enough: the first dispatch overwrites every pixel. A new Texture2D starts in
    // SHADER_READ_ONLY_OPTIMAL, which is m_outputTextureState's initial value below.
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

    const VolumeCameraSetup setup = (interpretation == VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum)
        ? ComputeAerialPerspectivePreviewCameraSetup(static_cast<int>(volume.extent.width),
              static_cast<int>(volume.extent.height), static_cast<int>(volume.extent.depth))
        : ComputeVolumeCameraSetup(static_cast<int>(volume.extent.width), static_cast<int>(volume.extent.height),
              static_cast<int>(volume.extent.depth));

    // Each call may preview a different volume, so binding 0 is rewritten every time (binding 1 rides along).
    m_descriptorSet.Rewrite(
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
    pushConstants.interpretationMode = static_cast<std::int32_t>(interpretation);
    pushConstants.aerialPreviewExposure =
        (interpretation == VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum) ? kAerialPreviewExposure : 0.0f;

    // The aerial perspective preview raymarches a frustum: boxHalfExtents carries its far size and half depth.
    if (interpretation == VolumeTexturePreviewInterpretation::AerialPerspectiveFrustum) {
        pushConstants.shapeMode = 1;
        const FrustumProxy frustum = ComputeAerialPerspectivePreviewFrustum();
        pushConstants.boxHalfExtents = Vec3(frustum.farHalfWidth, frustum.farHalfHeight, frustum.halfDepth);
    }

    // A compute-stage sampler read has no ResourceAccess enumerator (ShaderRead maps to the fragment stage,
    // ComputeShaderRead to GENERAL), so this state is built by hand.
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

    renderer.ImmediateSubmit([&](rg::CommandBuffer& cmd) {
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        rg::EmitImageBarrier(cmd.Native(), volumeImage, range, previousState, volumeSampledState);
        rg::EmitImageBarrier(cmd.Native(), outputImage, range, outputPreviousState, outputWriteState);

        vkCmdBindPipeline(cmd.Native(), VK_PIPELINE_BIND_POINT_COMPUTE, pipelineHandle);
        vkCmdBindDescriptorSets(cmd.Native(), VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &rawDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd.Native(), pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

        const std::uint32_t groupCountX = ComputeGroupCount(static_cast<std::uint32_t>(kOutputWidth), kLocalSizeX);
        const std::uint32_t groupCountY = ComputeGroupCount(static_cast<std::uint32_t>(kOutputHeight), kLocalSizeY);
        vkCmdDispatch(cmd.Native(), groupCountX, groupCountY, 1);

        // Restore the volume's previous state: later graph frames still expect it.
        rg::EmitImageBarrier(cmd.Native(), volumeImage, range, volumeSampledState, previousState);

        // The output stays in GENERAL; CaptureImagePixels() transitions it from the state it is told.
    });

    m_outputTextureState = outputWriteState;

    // The output is R8G8B8A8, never BGRA, so no channel swizzle is needed.
    const Renderer::CapturedRawPixels raw = renderer.CaptureImagePixels(m_outputTexture->Image(),
        VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R8G8B8A8_UNORM,
        Extent2D{ static_cast<std::uint32_t>(kOutputWidth), static_cast<std::uint32_t>(kOutputHeight) },
        m_outputTextureState);
    // CaptureImagePixels() restores GENERAL, which m_outputTextureState already holds.

    CapturedRawPixels result;
    result.pixels = raw.pixels;
    result.width = raw.width;
    result.height = raw.height;
    return result;
}

} // namespace gte
