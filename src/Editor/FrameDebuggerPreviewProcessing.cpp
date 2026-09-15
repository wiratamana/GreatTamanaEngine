#include "FrameDebuggerPreviewProcessing.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace gte {

namespace {

float RemapLevels(float value, float levelsBlack, float levelsWhite) noexcept
{
    const float denom = std::max(levelsWhite - levelsBlack, 1e-5f);
    const float remapped = (value - levelsBlack) / denom;
    return std::clamp(remapped, 0.0f, 1.0f);
}

// MUST match src/Shaders/FrameDebuggerPreview.comp's own
// `layout(push_constant)` block exactly - three plain scalars, no vec3/
// std140 alignment concerns at all (unlike VolumeTexturePreviewRenderer's
// own PushConstants, which packs several Vec3s).
struct PushConstants {
    std::int32_t channel = 0; // FrameDebuggerPreviewChannel, as int.
    float levelsBlack = 0.0f;
    float levelsWhite = 1.0f;
};

constexpr std::uint32_t ComputeGroupCountLocal(std::uint32_t totalItems, std::uint32_t localGroupSize) noexcept
{
    return (totalItems + localGroupSize - 1) / localGroupSize;
}

} // namespace

std::array<float, 4> ApplyFrameDebuggerPreviewTransform(
    std::array<float, 4> srcRgba, FrameDebuggerPreviewChannel channel, float levelsBlack, float levelsWhite) noexcept
{
    const std::array<float, 4> leveled{
        RemapLevels(srcRgba[0], levelsBlack, levelsWhite),
        RemapLevels(srcRgba[1], levelsBlack, levelsWhite),
        RemapLevels(srcRgba[2], levelsBlack, levelsWhite),
        RemapLevels(srcRgba[3], levelsBlack, levelsWhite),
    };

    // Deliberately NO `default:` case - see RenderGraphTypes.cpp's own
    // identical precedent (IsWriteAccess()/ToString()): a future
    // FrameDebuggerPreviewChannel enumerator added without updating this
    // switch must produce a compiler warning, never silently fall through.
    switch (channel) {
    case FrameDebuggerPreviewChannel::All:
        return leveled;
    case FrameDebuggerPreviewChannel::R:
        return { leveled[0], leveled[0], leveled[0], 1.0f };
    case FrameDebuggerPreviewChannel::G:
        return { leveled[1], leveled[1], leveled[1], 1.0f };
    case FrameDebuggerPreviewChannel::B:
        return { leveled[2], leveled[2], leveled[2], 1.0f };
    case FrameDebuggerPreviewChannel::A:
        return { leveled[3], leveled[3], leveled[3], 1.0f };
    }
    return leveled;
}

FrameDebuggerPreviewRenderer::~FrameDebuggerPreviewRenderer()
{
    // m_pipeline/m_outputTexture are RAII types and clean up themselves;
    // m_descriptorSetLayout is a plain Vulkan handle this class owns
    // directly - mirrors VolumeTexturePreviewRenderer's own identical
    // destructor shape. m_descriptorSet (a ComputeDescriptorSet) owns no
    // Vulkan handle of its own (allocated from a shared pool, never
    // individually freed - see ComputeDescriptorSet.h's own class comment).
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    }
}

void FrameDebuggerPreviewRenderer::EnsureInitialized(Renderer& renderer)
{
    if (m_initialized) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Binding convention (matches src/Shaders/FrameDebuggerPreview.comp
    // exactly - see Vulkan/DescriptorSetLayoutBuilder.h's own documented
    // "read-only Texture first, RWTexture output last" ordering): binding 0
    // = the source retained history preview texture's own combined image
    // sampler (read-only), binding 1 = this class's own persistent output
    // storage image.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_descriptorSetLayout = layoutBuilder.AddCombinedImageSampler(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/FrameDebuggerPreview.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_descriptorSetLayout }, pushConstantRange));

    m_descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout));

    m_initialized = true;
}

void FrameDebuggerPreviewRenderer::EnsureOutputTexture(Renderer& renderer, int width, int height)
{
    if (m_outputTexture.has_value() && m_outputWidth == width && m_outputHeight == height) {
        return; // Already the right size - nothing to (re)create.
    }

    // A zero-filled buffer is correct and sufficient here - the very next
    // real dispatch overwrites every pixel via imageStore anyway (mirrors
    // VolumeTexturePreviewRenderer::EnsureInitialized()'s own identical
    // reasoning for its own persistent output texture).
    const std::vector<std::uint8_t> zeroFilledPixels(static_cast<std::size_t>(width) * height * 4, 0);
    m_outputTexture.emplace(renderer.CreateTexture2D(zeroFilledPixels.data(), width, height,
        "FrameDebuggerPreviewScratch", /*allowStorageImageAccess=*/true));

    m_outputTextureState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    m_outputWidth = width;
    m_outputHeight = height;
}

const Texture2D& FrameDebuggerPreviewRenderer::RenderPreview(Renderer& renderer, const RenderTexture& source,
    FrameDebuggerPreviewChannel channel, float levelsBlack, float levelsWhite)
{
    EnsureInitialized(renderer);

    const VkExtent2D extent = source.Extent();
    EnsureOutputTexture(renderer, static_cast<int>(extent.width), static_cast<int>(extent.height));

    // Every call may serve a DIFFERENT currently-viewed history entry (a
    // Frame-History Prev/Next navigation lands on a genuinely different
    // RenderTexture, with a genuinely different VkImageView, every single
    // time a real capture happens - see FrameDebuggerHistory::CaptureFrame()'s
    // own doc comment) - binding 0 must be refreshed every call. Binding 1
    // never actually changes across calls (the same output texture is
    // reused, unless its size just changed above), but costs nothing extra
    // to rewrite alongside binding 0 every time - mirrors
    // VolumeTexturePreviewRenderer::RenderPreview()'s own identical
    // reasoning.
    m_descriptorSet.Rewrite(m_device,
        std::vector<ComputeDescriptorWrite>{
            ComputeDescriptorWrite::CombinedImageSampler(0, source.View(), source.Sampler()),
            ComputeDescriptorWrite::StorageImage(1, m_outputTexture->View()),
        });

    PushConstants pushConstants{};
    pushConstants.channel = static_cast<std::int32_t>(channel);
    pushConstants.levelsBlack = levelsBlack;
    pushConstants.levelsWhite = levelsWhite;

    // `source` is a retained FrameDebuggerHistory preview texture, always
    // left in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, expected to be
    // sampled from the FRAGMENT shader stage (rg::RequiredStateFor(ShaderRead,
    // false) - the exact state FrameDebuggerHistory::CaptureFrame()/
    // FrameDebuggerPanel::EnsurePreviewDescriptor() both leave/expect it in).
    // A COMBINED IMAGE SAMPLER read from a COMPUTE shader instead needs a
    // different PIPELINE STAGE/ACCESS mask (same layout though) - no
    // ResourceAccess enumerator exists for this exact combination, so this
    // ResourceState is built by hand, exactly mirroring
    // VolumeTexturePreviewRenderer::RenderPreview()'s own volumeSampledState.
    const rg::ResourceState sourcePreviousState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const rg::ResourceState sourceComputeSampledState{
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
    };
    const rg::ResourceState outputWriteState = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
    const rg::ResourceState outputPreviousState = m_outputTextureState;
    // Left in ShaderRead (fragment-stage) afterward too, so
    // FrameDebuggerPanel can wrap either the raw retained texture OR this
    // processed scratch texture with the exact same
    // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL constant when building its
    // own ImGui descriptor (see EnsurePreviewDescriptor()).
    const rg::ResourceState outputFinalState = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);

    const VkImage sourceImage = source.Image();
    const VkImage outputImage = m_outputTexture->Image();
    const VkPipeline pipelineHandle = m_pipeline->Native();
    const VkPipelineLayout pipelineLayout = m_pipeline->Layout();
    const VkDescriptorSet rawDescriptorSet = m_descriptorSet.Native();
    const std::uint32_t outputWidth = extent.width;
    const std::uint32_t outputHeight = extent.height;

    renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
        const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

        // Never renderer.Dispatch() here - that method only works from
        // inside an active render-graph pass recording
        // (BeginGraphPassRecording()/EndGraphPassRecording()) and asserts/
        // silently no-ops otherwise; FrameDebuggerPanel::Build() runs during
        // ImGui UI construction, never inside such a bracket - so every
        // Vulkan call below is issued directly against this plain
        // ImmediateSubmit() callback instead, mirroring
        // VolumeTexturePreviewRenderer::RenderPreview()'s own identical
        // precedent/comment.
        rg::EmitImageBarrier(cmd, sourceImage, range, sourcePreviousState, sourceComputeSampledState);
        rg::EmitImageBarrier(cmd, outputImage, range, outputPreviousState, outputWriteState);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineHandle);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &rawDescriptorSet, 0, nullptr);
        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pushConstants), &pushConstants);

        const std::uint32_t groupCountX = ComputeGroupCountLocal(outputWidth, kLocalSizeX);
        const std::uint32_t groupCountY = ComputeGroupCountLocal(outputHeight, kLocalSizeY);
        vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

        // Restore the source retained texture to its real, expected state -
        // a later ImGui sample of the SAME texture (e.g. flipping Channels
        // back to "All") must see it exactly as FrameDebuggerHistory/
        // FrameDebuggerPanel still believe it is.
        rg::EmitImageBarrier(cmd, sourceImage, range, sourceComputeSampledState, sourcePreviousState);

        // Transition the freshly-written output straight to ShaderRead too
        // (rather than leaving it in GENERAL) - see outputFinalState's own
        // comment above for why.
        rg::EmitImageBarrier(cmd, outputImage, range, outputWriteState, outputFinalState);
    });

    m_outputTextureState = outputFinalState;

    return *m_outputTexture;
}

} // namespace gte
