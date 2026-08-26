#include "RenderPasses.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"

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

} // namespace

void AddGameViewPass(
    rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle gameViewTarget, float aspectWidthOverHeight)
{
    builder.AddPass(
        "GameView",
        [gameViewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(gameViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(gameViewTarget, kGameClearDepth);
        },
        [&game, &renderer, aspectWidthOverHeight](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            game.Render(renderer, aspectWidthOverHeight);
            renderer.EndGraphPassRecording();
        });
}

void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection)
{
    builder.AddPass(
        "SceneView",
        [sceneViewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(sceneViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(sceneViewTarget, kGameClearDepth);
        },
        [&game, &renderer, aspectWidthOverHeight, sceneViewProjection](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            game.Render(renderer, aspectWidthOverHeight, &sceneViewProjection);
            renderer.EndGraphPassRecording();
        });
}

void AddPresentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle swapchainImage,
    std::optional<float> directGameRenderAspect, const std::function<void(VkCommandBuffer)>& recordImGui)
{
    builder.AddPass(
        "Present",
        [swapchainImage, directGameRenderAspect](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(swapchainImage, kGameClearColor);
            if (directGameRenderAspect.has_value()) {
                pass.WriteDepthStencilAttachment(swapchainImage, kGameClearDepth);
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

} // namespace gte
