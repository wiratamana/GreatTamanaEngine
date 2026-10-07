#include "TextureArrayValidation.h"

#include "../Core/Logging.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gte {

namespace {

// 64x64x4 - small enough to be a trivial, throwaway harness workload, large
// enough to visibly distinguish 4 solid-color layers in a screenshot.
constexpr std::uint32_t kArrayWidth = 64;
constexpr std::uint32_t kArrayHeight = 64;
constexpr std::uint32_t kArrayLayers = 4;
constexpr VkFormat kArrayFormat = VK_FORMAT_R8G8B8A8_UNORM;

// Literal names this phase's own source document/strategy require - see
// TextureArrayValidation.h's own header comment. Static-storage-duration
// string literals, matching RenderGraphBuilder::CreateTextureArray()/
// ImportTexture()'s own "name must be static-storage-duration" contract.
constexpr const char* kArrayName = "ManualVerifyArray";
constexpr const char* kFillPassName = "TextureArrayValidationFill";
constexpr std::array<const char*, 4> kLayerOutputNames = {
    "ManualVerifyArrayLayer0",
    "ManualVerifyArrayLayer1",
    "ManualVerifyArrayLayer2",
    "ManualVerifyArrayLayer3",
};
constexpr std::array<const char*, 4> kSlicePassNames = {
    "TextureArrayValidationSlice0",
    "TextureArrayValidationSlice1",
    "TextureArrayValidationSlice2",
    "TextureArrayValidationSlice3",
};

struct SlicePushConstants {
    std::uint32_t layerIndex = 0;
};

} // namespace

TextureArrayValidation::~TextureArrayValidation()
{
    // m_fillPipeline/m_slicePipeline/m_layerOutputs[*] are RAII types and
    // clean up themselves. m_fillDescriptorSetLayout/m_sliceDescriptorSetLayout
    // are BORROWED from their own ComputePipeline's own reflected
    // ReflectedDescriptorSetLayout(0) - owned/destroyed by that
    // ComputePipeline itself (ComputePipeline::Destroy()'s own
    // m_ownedReflectedLayouts cleanup) - mirrors GBufferValidation::~GBufferValidation()'s
    // own identical reasoning. Never destroyed here.
}

void TextureArrayValidation::EnsureInitialized(Renderer& renderer)
{
    if (m_fillPipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Pass A pipeline - binding 0 is the RWTexture2DArray output.
    m_fillPipeline.emplace(renderer.CreateComputePipeline("shaders/TextureArrayValidationFill.comp.spv"));
    m_fillDescriptorSetLayout = m_fillPipeline->ReflectedDescriptorSetLayout(/*set=*/0);
    m_fillDescriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_fillDescriptorSetLayout));

    // Pass B pipeline - binding 0 is the read-only TextureArray input
    // (storage image, imageLoad), binding 1 is the per-layer 2D output.
    m_slicePipeline.emplace(renderer.CreateComputePipeline("shaders/TextureArrayValidationSlice.comp.spv"));
    m_sliceDescriptorSetLayout = m_slicePipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    for (std::size_t i = 0; i < kLayerOutputNames.size(); ++i) {
        m_sliceDescriptorSets[i] =
            ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_sliceDescriptorSetLayout));
        m_layerOutputs[i].emplace(renderer.CreateRenderTexture(static_cast<int>(kArrayWidth),
            static_cast<int>(kArrayHeight), kArrayFormat, kLayerOutputNames[i], /*depthDebugName=*/nullptr,
            /*allowStorageImageAccess=*/true));
    }
}

IEditorLayer::TextureArrayValidationHandles TextureArrayValidation::AddPass(
    rg::RenderGraphBuilder& builder, Renderer& renderer, rg::RenderPassToggleRegistry* toggleRegistry)
{
    // editor-core-separation-21 campaign, PHASE4's own precedent - an
    // ADDITIONAL, independent gate on top of the caller's own
    // ctx.showTextureArrayValidationOutput feature toggle.
    const bool enabled =
        toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("TextureArrayValidation");
    if (!enabled) {
        return IEditorLayer::TextureArrayValidationHandles{};
    }

    EnsureInitialized(renderer);

    // PHASE5's own source document (Section 6, item 4) requires this EXACT,
    // non-default descriptor: hasDepth = false, usage = TextureUsage::Storage
    // - the struct's own default (hasDepth = true) is a depth image and
    // cannot be imageStore'd/imageLoad'd into by a compute shader at all.
    rg::TextureArrayDesc desc{};
    desc.width = kArrayWidth;
    desc.height = kArrayHeight;
    desc.arrayLayers = kArrayLayers;
    desc.format = kArrayFormat;
    desc.hasDepth = false;
    desc.isCubemap = false;
    desc.usage = rg::TextureUsage::Storage;

    // POOLED/transient - RenderGraphResourcePool::AcquireTextureArray()'s
    // own real point (see this class's own header comment): a fresh,
    // identical TextureArrayDesc every frame should reuse the SAME
    // underlying TextureArray2D entry, never allocate a new one.
    const rg::TextureArrayHandle arrayHandle = builder.CreateTextureArray(kArrayName, desc);

    // --- Pass A: "TextureArrayValidationFill" (Compute) -------------------
    builder.AddRenderPass(
        kFillPassName, rg::PassKind::Compute, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
        [arrayHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteTextureArray(arrayHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, arrayHandle](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTextureArray dest = ctx.resolveTextureArray(arrayHandle);

            m_fillDescriptorSet.Rewrite(
                m_device, std::vector<ComputeDescriptorWrite>{ ComputeDescriptorWrite::StorageImage(0, dest.view) });

            // Log the resolved VkImage handle behind "ManualVerifyArray" -
            // PHASE5's own requirement, so a live verification session can
            // correlate this log line (via GET /get_logs) with whatever
            // GET /get_texture?texture_name=ManualVerifyArrayLayerN
            // captures. ctx.textureArrays is a plain, non-owning pointer
            // into RenderGraph::ExecuteCompiledGraph()'s own stack-local
            // physicalTextureArrays vector (see RenderGraph.h's own
            // PassContext doc comment) - safe to index directly here since
            // arrayHandle.index is guaranteed valid for a pass that
            // declared a write against it this same frame.
            VkImage resolvedImage = VK_NULL_HANDLE;
            if (ctx.textureArrays != nullptr && arrayHandle.index < ctx.textureArrays->size()) {
                resolvedImage = (*ctx.textureArrays)[arrayHandle.index].target.image;
            }
            GTE_LOG_INFO("TextureArrayValidation",
                "Pass A (Fill) ran - ManualVerifyArray resolved VkImage=0x"
                    + [](VkImage image) {
                          char buffer[32];
                          std::snprintf(buffer, sizeof(buffer), "%016llx",
                              static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(image)));
                          return std::string(buffer);
                      }(resolvedImage));

            rg::CommandBuffer cmd = ctx.Cmd();
            cmd.BindComputePipeline(*m_fillPipeline);
            cmd.BindDescriptorSet(m_fillDescriptorSet.Native());
            cmd.DispatchOverSize(kArrayWidth, kArrayHeight, kArrayLayers);
        });
    m_writtenThisFrame = true;

    // --- Pass B x4: "TextureArrayValidationSliceN" (Compute) --------------
    // Mirrors AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()'s
    // own CONFIRMED precedent - see this class's own header comment.
    IEditorLayer::TextureArrayValidationHandles handles;
    std::array<rg::TextureHandle*, 4> handleSlots = { &handles.layer0, &handles.layer1, &handles.layer2,
        &handles.layer3 };

    for (std::size_t i = 0; i < kLayerOutputNames.size(); ++i) {
        const rg::TextureHandle outputHandle = builder.ImportTexture(kLayerOutputNames[i], m_layerOutputs[i]->Target(),
            VK_IMAGE_LAYOUT_UNDEFINED, m_layerOutputs[i]->Sampler(), m_layerOutputs[i]->DepthSampler(),
            &*m_layerOutputs[i]);
        *handleSlots[i] = outputHandle;

        const std::uint32_t layerIndex = static_cast<std::uint32_t>(i);
        ComputeDescriptorSet& sliceDescriptorSet = m_sliceDescriptorSets[i];

        builder.AddRenderPass(
            kSlicePassNames[i], rg::PassKind::Compute, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
            [arrayHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                // A storage-image READ (imageLoad), not a sampled read - see
                // this class's own header comment on why PassContext's
                // resolveTextureArray() exposes no sampler for a pooled
                // TextureArray today.
                pass.ReadTextureArray(arrayHandle, rg::ResourceAccess::ComputeShaderRead);
                pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
            },
            [this, &sliceDescriptorSet, arrayHandle, outputHandle, layerIndex](rg::PassContext& ctx) {
                const rg::PassContext::ResolvedTextureArray src = ctx.resolveTextureArray(arrayHandle);
                const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

                sliceDescriptorSet.Rewrite(m_device,
                    std::vector<ComputeDescriptorWrite>{
                        ComputeDescriptorWrite::StorageImage(0, src.view),
                        ComputeDescriptorWrite::StorageImage(1, dest.view),
                    });

                SlicePushConstants pushConstants{};
                pushConstants.layerIndex = layerIndex;

                rg::CommandBuffer cmd = ctx.Cmd();
                cmd.BindComputePipeline(*m_slicePipeline);
                cmd.BindDescriptorSet(sliceDescriptorSet.Native());
                cmd.SetPushConstants(pushConstants);
                cmd.DispatchOverSize(kArrayWidth, kArrayHeight);
            });
    }

    return handles;
}

void TextureArrayValidation::FinalizeForSampling(VkCommandBuffer cmd)
{
    if (!m_writtenThisFrame) {
        return;
    }
    m_writtenThisFrame = false;

    for (auto& output : m_layerOutputs) {
        if (output.has_value()) {
            output->FinalizeForExternalSampling(cmd);
        }
    }
}

} // namespace gte
