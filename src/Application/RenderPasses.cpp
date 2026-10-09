#include "RenderPasses.h"

#include "../Game/Animation/AnimationSystem.h"
#include "../Game/Game.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"

// IFrameDebuggerCaptureRecorder (src/Core/FrameDebuggerCaptureRecorder.h) is
// the gte_core-owned abstract interface FrameDebuggerCaptureContext (src/
// Editor/FrameDebuggerCapture.h, still gte_editor-only) implements - this
// file only ever holds/forwards a bare IFrameDebuggerCaptureRecorder*
// pointer, never dereferencing it itself.

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
    std::optional<float> directGameRenderAspect, const std::function<void(VkCommandBuffer)>& recordImGui,
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
                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                // Fallback regime: no CommandBuffer is threaded through here,
                // so RenderSystem::Draw() never calls CommandBuffer::Draw()
                // in this branch - Frame Debugger per-draw capture cannot
                // fire for a frame rendered this way.
                game.Render(renderer, *directGameRenderAspect);
                renderer.EndGraphPassRecording();
            }
            if (recordImGui) {
                recordImGui(ctx.cmd);
            }
        });
}

std::vector<rg::BufferHandle> AddGpuSkinningPasses(
    rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::RenderPassToggleRegistry* toggleRegistry)
{
    // `renderer` is no longer dereferenced anywhere in this function's own
    // body (the real dispatch below goes entirely through rg::CommandBuffer,
    // which reaches its own Renderer* via PassContext::Cmd() instead) - kept
    // as a parameter for signature stability.
    (void)renderer;
    std::vector<rg::BufferHandle> handles;

    // A single, whole-stage on/off switch, mirroring Core.cpp's own
    // OFFSCREEN "GpuSkinning" provider's identical shape (both consult the
    // SAME registry entry name, so one checkbox honestly gates whichever of
    // the two paths is actually reachable this frame).
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
        const rg::BufferHandle handle =
            builder.ImportBuffer(request.name, request.outputBuffer, request.outputBufferSize);

        builder.AddRenderPass(
            request.name, rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::General,
            [handle](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteBuffer(handle, rg::ResourceAccess::ComputeShaderWrite);
            },
            [&pipelines, request](rg::PassContext& ctx) {
                const ComputePipeline& pipeline =
                    request.textured ? pipelines.PositionNormalUvPipeline() : pipelines.PositionNormalPipeline();
                const std::uint32_t vertexCount = request.vertexCount;

                // DispatchOverSize() computes the correct groupX/Y/Z from the
                // bound pipeline's own reflected LocalGroupSize() (== {256,1,1})
                // via ComputeDispatch.h's existing ComputeGroupCount3D().
                rg::CommandBuffer cmd = ctx.Cmd();
                cmd.BindComputePipeline(pipeline);
                cmd.BindDescriptorSet(request.descriptorSet);
                cmd.SetPushConstants(vertexCount);
                cmd.DispatchOverSize(vertexCount, 1, 1);
            },
            rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques);

        handles.push_back(handle);
    }

    return handles;
}

} // namespace gte
