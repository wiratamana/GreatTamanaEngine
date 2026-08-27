#include "ComputeBlurValidation.h"

#include "../Renderer/ComputeDispatch.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <cstdint>
#include <vector>

namespace gte {

namespace {

// MUST match Shaders/BoxBlur.comp's own `layout(local_size_x = 16,
// local_size_y = 16) in;` exactly - see ComputeDispatch.h's own header
// comment on why this pairing is a hand-maintained, per-shader convention
// rather than something the build system enforces.
constexpr std::uint32_t kBoxBlurLocalSizeX = 16;
constexpr std::uint32_t kBoxBlurLocalSizeY = 16;

} // namespace

ComputeBlurValidation::~ComputeBlurValidation()
{
    // m_blurredOutput/m_pipeline are RAII types and clean up themselves;
    // m_descriptorSetLayout is a plain Vulkan handle this class owns
    // directly (mirrors GpuResourceFactory's own m_materialSetLayout - see
    // GpuResourceFactory.cpp's Destroy()). Safe to call unconditionally -
    // the device is already idle by this point: ImGuiEditorLayer's own
    // destructor calls vkDeviceWaitIdle() before any of its members
    // (including this one) are destroyed.
    if (m_descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
    }
}

void ComputeBlurValidation::EnsureInitialized(Renderer& renderer, VkExtent2D initialExtent)
{
    if (m_pipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Binding convention (see Vulkan/DescriptorSetLayoutBuilder.h's own
    // documented rule): binding 0 is the read-only `Texture` input,
    // binding 1 is the `RWTexture` output - must match Shaders/BoxBlur.comp
    // exactly.
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_descriptorSetLayout = layoutBuilder.AddCombinedImageSampler(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

    // Plain, per-shader push-constant convention (see ComputePipeline.h) -
    // two uint32s (width, height), matching Shaders/BoxBlur.comp's own
    // `PushConstants` block exactly.
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(std::uint32_t) * 2;

    m_pipeline.emplace(renderer.CreateComputePipeline(
        "shaders/BoxBlur.comp.spv", std::vector<VkDescriptorSetLayout>{ m_descriptorSetLayout }, pushConstantRange));

    m_descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout));

    // Explicit VK_FORMAT_R8G8B8A8_UNORM (never VK_FORMAT_UNDEFINED/
    // Renderer::ColorFormat()) - see this class's own header comment for
    // the full reasoning: this texture is never bound to the SAME
    // Pipeline as the swapchain/Game/Scene views (it's only ever sampled
    // via ImGui::Image()), so there is no reason to inherit the
    // swapchain's own negotiated color format - whose storage-image
    // support is NOT guaranteed on every driver/GPU (see
    // COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md's own Step
    // 6, and COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md's own Step
    // 6, Finding 2/4) - while VK_FORMAT_R8G8B8A8_UNORM (Texture2D's own
    // already-proven format) is broadly supported.
    const int width = initialExtent.width > 0 ? static_cast<int>(initialExtent.width) : 1;
    const int height = initialExtent.height > 0 ? static_cast<int>(initialExtent.height) : 1;
    m_blurredOutput.emplace(renderer.CreateRenderTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, "BlurredSceneOutput",
        "BlurredSceneOutputDepth", /*allowStorageImageAccess=*/true));
}

rg::TextureHandle ComputeBlurValidation::AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    rg::TextureHandle sceneViewHandle, VkSampler sceneViewSampler, VkExtent2D sceneExtent)
{
    EnsureInitialized(renderer, sceneExtent);

    const VkExtent2D currentExtent = m_blurredOutput->Extent();
    if (currentExtent.width != sceneExtent.width || currentExtent.height != sceneExtent.height) {
        // Resizes are rare/user-driven (dragging the "Scene" panel's
        // border) - a full device stall here is the simplest correct
        // thing, mirroring ImGuiEditorLayer::GameViewTarget()/
        // SceneViewTarget()'s own identical discipline for their own
        // RenderTextures.
        vkDeviceWaitIdle(m_device);
        m_blurredOutput->Resize(static_cast<int>(sceneExtent.width), static_cast<int>(sceneExtent.height));
    }

    // Always imported as VK_IMAGE_LAYOUT_UNDEFINED - mirrors
    // RenderPasses.h's own AddGameViewPass()/AddSceneViewPass() call sites
    // (Application::Run()) exactly: this pass's own compute write fully
    // overwrites every in-bounds pixel every time it runs, so the
    // previous frame's actual contents/layout never need to be preserved.
    const rg::TextureHandle outputHandle =
        builder.ImportTexture("BlurredSceneOutput", m_blurredOutput->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

    builder.AddComputePass(
        "ComputeBlurValidation",
        [sceneViewHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(sceneViewHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        // Every captured handle/value here is captured BY VALUE, never by
        // reference to a `build`-lambda-local variable - see
        // COMPUTE_PHASE6_COMPLETION_REPORT.md's own "Handoff notes" for
        // exactly why (a dangling-reference bug this phase's own
        // predecessor already found and fixed once). `this`/`&renderer`
        // are both long-lived (this object outlives the whole Execute()
        // call; `renderer` is Application's own Renderer member) - safe to
        // capture by reference/pointer.
        [this, &renderer, sceneViewHandle, outputHandle, sceneViewSampler, sceneExtent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture source = ctx.resolveTexture(sceneViewHandle);
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            m_descriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, source.view, sceneViewSampler),
                    ComputeDescriptorWrite::StorageImage(1, dest.view),
                });

            const std::uint32_t pushConstants[2] = { sceneExtent.width, sceneExtent.height };
            const Extent3D groupCounts = ComputeGroupCount3D(Extent3D{ sceneExtent.width, sceneExtent.height, 1 },
                Extent3D{ kBoxBlurLocalSizeX, kBoxBlurLocalSizeY, 1 });

            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            renderer.Dispatch(*m_pipeline, m_descriptorSet.Native(), pushConstants, sizeof(pushConstants),
                groupCounts.width, groupCounts.height, groupCounts.depth);
            renderer.EndGraphPassRecording();
        });

    m_writtenThisFrame = true;
    return outputHandle;
}

void ComputeBlurValidation::FinalizeForSampling(VkCommandBuffer cmd)
{
    if (!m_writtenThisFrame) {
        return;
    }
    m_writtenThisFrame = false;

    const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
    const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    rg::EmitImageBarrier(cmd, m_blurredOutput->Image(), range, previous, next);
}

} // namespace gte
