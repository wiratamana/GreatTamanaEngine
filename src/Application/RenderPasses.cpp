#include "RenderPasses.h"

#include "../Game/Animation/AnimationSystem.h"
#include "../Game/Game.h"
#include "../Renderer/ComputeDispatch.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"

#include <cstdint>

namespace gte {

namespace {

// Matches Game::Render()'s own hardcoded `renderer.Clear(20, 20, 30, 255)`
// call EXACTLY (src/Game/Game.cpp) - duplicated here rather than queried
// from Game/Renderer at Setup time, since a pass's clear color must be
// declared BEFORE its own `execute` callback (the one that actually calls
// Game::Render()) ever runs - see this file's own header comment. If
// Game::Render()'s own hardcoded clear color ever changes, update this
// constant to match.
constexpr std::array<float, 4> kGameClearColor{ 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };

// Far plane - matches FrameRecorder.cpp's own
// `depthAttachment.clearValue.depthStencil = { 1.0f, 0 }`.
constexpr float kGameClearDepth = 1.0f;

// Declares a phantom ResourceAccess::VertexBufferRead against every handle
// in `gpuSkinningOutputBuffers` - see this file's own RenderPasses.h header
// comment (AddGameViewPass()'s doc comment in particular) and
// GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md for why
// this is NOT dead code - do not remove even though the mesh vertex buffer
// is actually read via a real VkVertexInputAttributeDescription binding,
// never through this declared handle directly.
void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    for (const rg::BufferHandle& handle : gpuSkinningOutputBuffers) {
        pass.ReadBuffer(handle, rg::ResourceAccess::VertexBufferRead);
    }
}

} // namespace

void AddGameViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle gameViewTarget,
    float aspectWidthOverHeight, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    builder.AddPass(
        "GameView",
        [gameViewTarget, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(gameViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(gameViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
        },
        [&game, &renderer, aspectWidthOverHeight](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            game.Render(renderer, aspectWidthOverHeight);
            renderer.EndGraphPassRecording();
        });
}

void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    builder.AddPass(
        "SceneView",
        [sceneViewTarget, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(sceneViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(sceneViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
        },
        [&game, &renderer, aspectWidthOverHeight, sceneViewProjection](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            game.Render(renderer, aspectWidthOverHeight, &sceneViewProjection);
            renderer.EndGraphPassRecording();
        });
}

void AddPresentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle swapchainImage,
    std::optional<float> directGameRenderAspect, const std::function<void(VkCommandBuffer)>& recordImGui,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    builder.AddPass(
        "Present",
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
                game.Render(renderer, *directGameRenderAspect);
                renderer.EndGraphPassRecording();
            }
            if (recordImGui) {
                recordImGui(ctx.cmd);
            }
        });
}

void FinalizeRenderTextureForExternalSampling(VkCommandBuffer cmd, RenderTexture& texture)
{
    const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
    const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    rg::EmitImageBarrier(cmd, texture.Image(), range, previous, next);
}

std::vector<rg::BufferHandle> AddGpuSkinningPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer)
{
    std::vector<rg::BufferHandle> handles;

    const std::vector<AnimationSystem::GpuSkinningDispatchRequest> requests = game.CollectGpuSkinningDispatchRequests();
    if (requests.empty()) {
        return handles;
    }

    GpuSkinningPipelines& pipelines = game.GetGpuSkinningPipelines();
    handles.reserve(requests.size());

    for (const AnimationSystem::GpuSkinningDispatchRequest& request : requests) {
        const rg::BufferHandle handle =
            builder.ImportBuffer(request.name, request.outputBuffer, request.outputBufferSize);

        builder.AddComputePass(
            request.name,
            [handle](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteBuffer(handle, rg::ResourceAccess::ComputeShaderWrite);
            },
            [&renderer, &pipelines, request](rg::PassContext& ctx) {
                const ComputePipeline& pipeline =
                    request.textured ? pipelines.PositionNormalUvPipeline() : pipelines.PositionNormalPipeline();
                const std::uint32_t vertexCount = request.vertexCount;

                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                renderer.Dispatch(pipeline, request.descriptorSet, &vertexCount, sizeof(vertexCount),
                    ComputeGroupCount(vertexCount, kSkinningLocalSizeX), 1, 1);
                renderer.EndGraphPassRecording();
            });

        handles.push_back(handle);
    }

    return handles;
}

} // namespace gte
