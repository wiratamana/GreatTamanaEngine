#include "RenderPasses.h"

#include "../Game/Animation/AnimationSystem.h"
#include "../Game/Game.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/MeshPass.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <cstdint>

namespace gte {

void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    for (const rg::BufferHandle& handle : gpuSkinningOutputBuffers) {
        pass.ReadBuffer(handle, rg::ResourceAccess::VertexBufferRead);
    }
}

void AddPresentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle swapchainImage,
    std::optional<float> directGameRenderAspect, const std::function<void(const rg::RecordingContext&)>& recordImGui,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    builder.AddRenderPass(
        "Present", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [swapchainImage, directGameRenderAspect, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(swapchainImage, kGameClearColor);
            if (directGameRenderAspect.has_value()) {
                pass.WriteDepthStencilAttachment(swapchainImage, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
            }
        },
        [&game, &renderer, directGameRenderAspect, recordImGui](rg::PassContext& ctx) {
            if (directGameRenderAspect.has_value()) {
                // Fallback regime: no CommandBuffer is threaded through, so per-draw capture cannot fire here.
                const rg::SceneRecording recording(ctx);
                game.Render(renderer, *directGameRenderAspect);
            }
            if (recordImGui) {
                recordImGui(ctx.Recording());
            }
        });
}

std::vector<rg::BufferHandle> AddGpuSkinningPasses(
    rg::RenderGraphBuilder& builder, Game& game, rg::RenderPassToggleRegistry* toggleRegistry)
{
    std::vector<rg::BufferHandle> handles;

    // One checkbox gates both this path and the offscreen "GpuSkinning" provider.
    if (toggleRegistry != nullptr && !toggleRegistry->IsEnabled("GpuSkinning")) {
        return handles;
    }

    const std::vector<AnimationSystem::GpuSkinningDispatchRequest> requests = game.CollectGpuSkinningDispatchRequests();
    if (requests.empty()) {
        return handles;
    }

    GpuSkinningPipelines& pipelines = game.GetGpuSkinningPipelines();
    handles.reserve(requests.size());

    for (const AnimationSystem::GpuSkinningDispatchRequest& request : requests) {
        GpuSkinningDispatch dispatch = request.dispatch;
        dispatch.outputHandle = builder.Import(*dispatch.output, request.name);
        pipelines.Declare(builder, dispatch, rg::ViewScope::Shared, rg::RenderPassEvent::Opaques);
        handles.push_back(dispatch.outputHandle);
    }

    return handles;
}

} // namespace gte
