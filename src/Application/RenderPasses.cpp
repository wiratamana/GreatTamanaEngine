#include "RenderPasses.h"

#include "../Game/Animation/AnimationSystem.h"
#include "../Game/Game.h"
#include "../Renderer/ComputePipeline.h"
#include "../Renderer/GpuSkinning/GpuSkinningPipelines.h"
#include "../Renderer/GpuSkinning/GpuSkinningRenderPassTags.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"

// editor-core-separation-2 campaign, PHASE2 - IFrameDebuggerCaptureRecorder
// (src/Core/FrameDebuggerCaptureRecorder.h) is the new, gte_core-owned
// abstract interface FrameDebuggerCaptureContext (src/Editor/
// FrameDebuggerCapture.h, still gte_editor-only) now implements - this file
// only ever holds/forwards a bare IFrameDebuggerCaptureRecorder* pointer
// (RenderPasses.h's own #include of that interface header already brings in
// the complete type), never dereferencing it itself, exactly like before.

#include <cstdint>

namespace gte {

// render-pass-3 campaign, PHASE2 (PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md)
// - definition of the declaration now moved to RenderPasses.h (this
// function's OWN body is completely UNCHANGED from its former anonymous-
// namespace version - only its linkage/declaration location moved, so
// Application.cpp's new "RenderOpaque" RenderPipeline provider can call it
// too).
void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers)
{
    for (const rg::BufferHandle& handle : gpuSkinningOutputBuffers) {
        pass.ReadBuffer(handle, rg::ResourceAccess::VertexBufferRead);
    }
}

// Render Pass campaign (task_manager/render-pass-1), PHASE2 - RENAMED from
// AddGameViewPass() (see RenderPasses.h's own doc comment). Declared via the
// new AddRenderPass() chokepoint (RenderGraphBuilder.h, PHASE1) instead of
// plain AddPass() - PassKind::Graphics, ViewScope::GameView,
// RenderPassCategory::General (the default for this campaign's non-
// Atmosphere passes - see PHASE1's own RenderPassCategory doc comment). No
// longer draws the sky background at all - see AddDrawSkyBackgroundPass()
// below for that, now a real, separate pass.
void AddRenderOpaquePass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    rg::TextureHandle gameViewTarget, float aspectWidthOverHeight,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers, IFrameDebuggerCaptureRecorder* frameDebuggerCapture)
{
    builder.AddRenderPass(
        "RenderOpaque", rg::PassKind::Graphics, rg::ViewScope::GameView, rg::RenderPassCategory::General,
        [gameViewTarget, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(gameViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(gameViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
        },
        [&game, &renderer, aspectWidthOverHeight, frameDebuggerCapture](rg::PassContext& ctx) {
            // Per-draw-call granularity: builds a CommandBuffer from this
            // pass's own PassContext and threads it into Game::Render() so
            // RenderSystem::Draw()'s per-entity loop issues every resolved
            // draw through CommandBuffer::Draw() instead of calling
            // renderer.Submit() directly - CommandBuffer::Draw() opens/
            // closes its own BeginGraphPassRecording()/EndGraphPassRecording()
            // bracket per draw, so this pass no longer needs to do so itself
            // (see CommandBuffer.h's own doc comment).
            rg::CommandBuffer cmd = ctx.Cmd();
            // frameDebuggerCapture is forwarded onward, as a bare pointer,
            // into game.Render() below - exactly like PHASE1's own Step
            // 3.1b requires for a CORE, always-compiled file such as this
            // one (see task_manager/frame-debugger-3/
            // PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md's own
            // Step 3.4b).
            game.Render(renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture, std::nullopt, {},
                VK_NULL_HANDLE, &cmd);
        });
}

// Render Pass campaign, PHASE2 - see RenderPasses.h's own doc comment for
// the full contract. A true no-op (declares nothing at all) when
// `recordSkyBackground` is empty, mirroring AddGpuSkinningPasses()'s own
// "add nothing when nothing to do" pattern. Frame Debugger Pass-Ownership
// campaign (task_manager/render-pass-2), PHASE1 - this pass is now
// explicitly tagged rg::RenderPassDrawKind::DrawQuad (a real, hand-verified
// full-screen-triangle draw, see AtmosphereSkyBackgroundRenderer.cpp's own
// vkCmdDraw(cmd, 3, 1, 0, 0)) so a future consumer (PHASE2 of that campaign)
// can label its Frame Debugger child event correctly without ever
// hardcoding a pass-name string match.
void AddDrawSkyBackgroundPass(rg::RenderGraphBuilder& builder, Renderer& renderer, rg::TextureHandle gameViewTarget,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground, IFrameDebuggerCaptureRecorder* frameDebuggerCapture)
{
    // Render Pass campaign (task_manager/render-pass-1), PHASE4 - this
    // parameter is kept for signature symmetry with AddRenderOpaquePass()
    // above, but is no longer dereferenced anywhere in this function's own
    // body (see Step 3.4's removal of the temporary
    // RecordSkyBackgroundDraw() bridge call below) - explicitly cast to
    // void so this stays a deliberate, documented no-op parameter, not an
    // accidental dead one.
    (void)frameDebuggerCapture;

    if (!recordSkyBackground) {
        return;
    }

    builder.AddRenderPass(
        "DrawSkyBackground", rg::PassKind::Graphics, rg::ViewScope::GameView, rg::RenderPassCategory::General,
        [gameViewTarget](rg::RenderGraphBuilder::PassBuilder& pass) {
            // Deliberately NO clear value on either attachment (std::nullopt
            // - VK_ATTACHMENT_LOAD_OP_LOAD) - this pass must never erase
            // AddRenderOpaquePass()'s own just-written pixels/depth. See
            // RenderPasses.h's own doc comment for the EQUAL-depth-test
            // reasoning that makes this safe.
            pass.WriteColorAttachment(gameViewTarget);
            pass.WriteDepthStencilAttachment(gameViewTarget);
        },
        [&renderer, recordSkyBackground](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            recordSkyBackground(ctx.cmd);
            // Render Pass campaign (task_manager/render-pass-1), PHASE4
            // (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.4) - the
            // `frame-debugger-8` campaign's own temporary
            // `frameDebuggerCapture->RecordSkyBackgroundDraw(...)` bridge
            // call that used to live here is REMOVED - "DrawSkyBackground"
            // is now a real, separate Render Graph pass the Frame Debugger
            // generically discovers by name/category (see
            // FrameDebuggerData.cpp's BuildRealFrameDebuggerSnapshot()), so
            // it no longer needs to fabricate a draw record at all.
            renderer.EndGraphPassRecording();
        },
        rg::RenderPassDrawKind::DrawQuad); // Frame Debugger Pass-Ownership campaign (render-pass-2), PHASE1 -
            // see AtmosphereSkyBackgroundRenderer.cpp's own vkCmdDraw(cmd, 3, 1, 0, 0)
            // - a real full-screen-triangle draw, never a per-object mesh draw.
}

// Render Pass campaign, PHASE2 - see RenderPasses.h's own doc comment for
// the full contract. Always a no-op today (declares nothing at all) - see
// RenderSystem::CollectTransparentRenderables()'s own doc comment for why.
void AddRenderTransparentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    rg::TextureHandle gameViewTarget, float aspectWidthOverHeight)
{
    (void)builder;
    (void)renderer;
    (void)gameViewTarget;
    (void)aspectWidthOverHeight;

    const std::vector<DrawCommand> transparentCommands = RenderSystem::CollectTransparentRenderables(game.GetRegistry());
    if (transparentCommands.empty()) {
        return;
    }

    // Unreachable today - RenderSystem::CollectTransparentRenderables()
    // always returns empty (see its own doc comment) until a future
    // transparency campaign gives MeshRenderer a real isTransparent/
    // renderQueue flag. Left deliberately unimplemented beyond this early
    // return - see RenderPasses.h's own doc comment and PHASE2's own "What
    // We Will NOT Do".
}

// editor-core-separation-2 campaign, PHASE2 - gte::AddFrameDebuggerReplayPasses()
// (the free function this comment used to describe, and its DECLARATION in
// RenderPasses.h) is GONE entirely - it is now
// FrameDebuggerCaptureContext::AddReplayPasses(), a member function of the
// interface it implements (gte::IFrameDebuggerCaptureRecorder, src/Core/
// FrameDebuggerCaptureRecorder.h), called through a null-checked
// IFrameDebuggerCaptureRecorder* pointer from Core::BuildFrame() instead of
// by name (closing "Defect B" - see PHASE0_MASTER_STRATEGY.md). Its body
// still lives entirely in src/Editor/FrameDebuggerReplayPasses.cpp
// (unconditional #include of FrameDebuggerCapture.h, no #if guard needed -
// that file only ever compiles as part of the Editor source list) - see
// that file for the real body. Never defined in this file, exactly as
// before.

// render-pass-3 campaign, PHASE3 - AddSceneViewPass() REMOVED (see
// RenderPasses.h's own updated doc comment at this same location for the
// full "why").

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
                // fire for a frame rendered this way. This IS reachable with
                // Frame Debugger armed: arming only needs the window open and
                // enabled (FrameDebuggerPanel::PrepareCaptureContextForThisFrame()),
                // independent of gameViewVisible/sceneViewVisible - both views
                // being hidden/collapsed at once is enough to land here.
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
    // better-render-pass-1 campaign, PHASE4
    // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md) - `renderer`
    // is no longer dereferenced anywhere in this function's own body (the
    // real dispatch below now goes entirely through rg::CommandBuffer, which
    // reaches its own Renderer* via PassContext::Cmd() instead) - kept as a
    // parameter for signature stability (every existing call site still
    // passes one), explicitly cast to void so this stays a deliberate,
    // documented no-op parameter, mirroring AddRenderTransparentPass()'s own
    // precedent immediately below in this same file.
    (void)renderer;
    std::vector<rg::BufferHandle> handles;

    // editor-core-separation-21 campaign, PHASE4 (fixing PHASE3's
    // confirmed-lie finding #9) - a single, whole-stage on/off switch,
    // mirroring Core.cpp's own OFFSCREEN "GpuSkinning" provider's identical
    // shape (both consult the SAME registry entry name, so one checkbox
    // honestly gates whichever of the two paths is actually reachable this
    // frame).
    if (toggleRegistry != nullptr && !toggleRegistry->NoteDeclaredAndCheckEnabled("GpuSkinning")) {
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

                // better-render-pass-1 campaign, PHASE4
                // (PHASE4_MIGRATE_CULLING_AND_GPU_SKINNING_COMPUTE_PASSES.md)
                // - migrated onto rg::CommandBuffer (PHASE3) -
                // DispatchOverSize() computes the correct groupX/Y/Z from
                // the bound pipeline's own reflected LocalGroupSize() (==
                // {256,1,1}, matching the old kSkinningLocalSizeX constant
                // exactly) via ComputeDispatch.h's existing
                // ComputeGroupCount3D(), degrading correctly to the previous
                // 1D ComputeGroupCount() call for this flat vertexCount
                // dispatch.
                rg::CommandBuffer cmd = ctx.Cmd();
                cmd.BindComputePipeline(pipeline);
                cmd.BindDescriptorSet(request.descriptorSet);
                cmd.SetPushConstants(vertexCount);
                cmd.DispatchOverSize(vertexCount, 1, 1);
            },
            rg::RenderPassDrawKind::DrawMesh, rg::RenderPassEvent::Opaques, kGpuSkinningDispatchPassTag.bit);

        handles.push_back(handle);
    }

    return handles;
}

} // namespace gte
