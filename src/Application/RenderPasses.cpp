#include "RenderPasses.h"

#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"

namespace gte {

void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    for (const rg::BufferHandle& handle : gpuSkinningOutputBuffers) {
        pass.ReadBuffer(handle, rg::ResourceAccess::VertexBufferRead);
    }
}

void AddPresentPass(rg::RenderGraphBuilder& builder, rg::TextureHandle swapchainImage, bool clearSwapchain,
    const std::function<void(const rg::RecordingContext&)>& recordImGui)
{
    builder.AddRenderPass(
        "Present", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [swapchainImage, clearSwapchain](rg::RenderGraphBuilder::PassBuilder& pass) {
            if (clearSwapchain) {
                pass.WriteColorAttachment(swapchainImage, kGameClearColor);
            } else {
                pass.WriteColorAttachment(swapchainImage);
            }
        },
        [recordImGui](rg::PassContext& ctx) {
            if (recordImGui) {
                recordImGui(ctx.Recording());
            }
        });
}

} // namespace gte
