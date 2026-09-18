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

// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step 3.3)
// - FrameDebuggerCaptureContext (src/Editor/FrameDebuggerCapture.h) is an
// Editor-only type. This real #include, AND every actual dereference of a
// `capture` REFERENCE below (AddFrameDebuggerReplayPasses()'s own body),
// must stay wrapped in `#if GTE_ENABLE_EDITOR` - a GTE_ENABLE_EDITOR=OFF
// build compiles this whole file fine either way (RenderPasses.h's own
// forward declaration is enough for the function's SIGNATURE), but would
// FAIL TO LINK if an unconditional call site referenced a type/function
// that's never compiled into that configuration at all - mirrors
// src/Game/RenderSystem.cpp's own identical PHASE1 precedent, applied here
// to a CALLEE's body instead of a passthrough parameter (see
// RenderPasses.h's own updated AddFrameDebuggerReplayPasses() doc comment).
#if GTE_ENABLE_EDITOR
#include "../Editor/FrameDebuggerCapture.h"
#endif

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <utility>

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

// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md, Step
// 3.3b) - permanent (whole-process-lifetime), ever-growing pool of
// "FrameDebuggerReplayStepN" pass names. MUST NOT be a per-frame/per-
// capture temporary: RenderGraphBuilder::AddPass()'s own `name` parameter,
// and RenderGraphNameSlotTable's own persistent-across-Execute()-calls
// name table, both require a name that is valid for the rest of this
// process's lifetime, never just "this frame" - a stack buffer or a
// function-local std::string/std::vector would produce a real, confirmed
// dangling-pointer / use-after-free bug the very next time ANY capture
// happens (the second, third, ... capture this session) - see this
// campaign's own phase document for the full reasoning. std::deque (never
// std::vector) so growing this pool NEVER moves an already-handed-out
// std::string's own character storage - a std::vector<std::string>
// growing/reallocating would invalidate every c_str() pointer already
// stored inside a PREVIOUSLY-declared PassRecord::name, which is exactly
// the same class of bug this whole mechanism exists to avoid.
std::deque<std::string>& ReplayStepPassNamePool()
{
    static std::deque<std::string> pool;
    return pool;
}

// Returns a STABLE, permanent const char* naming replay step `index` -
// lazily grows the pool the first time `index` is ever requested, then
// reuses the SAME std::string (and therefore the SAME pointer) for that
// index forever afterwards, across every future capture this session.
const char* ReplayStepPassName(std::size_t index)
{
    std::deque<std::string>& pool = ReplayStepPassNamePool();
    while (pool.size() <= index) {
        pool.push_back("FrameDebuggerReplayStep" + std::to_string(pool.size()));
    }
    return pool[index].c_str();
}

} // namespace

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
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers, FrameDebuggerCaptureContext* frameDebuggerCapture)
{
    builder.AddRenderPass(
        "RenderOpaque", rg::PassKind::Graphics, rg::ViewScope::GameView, rg::RenderPassCategory::General,
        [gameViewTarget, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(gameViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(gameViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
        },
        [&game, &renderer, aspectWidthOverHeight, frameDebuggerCapture](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            // frameDebuggerCapture is forwarded onward, as a bare pointer,
            // into game.Render() below - exactly like PHASE1's own Step
            // 3.1b requires for a CORE, always-compiled file such as this
            // one (see task_manager/frame-debugger-3/
            // PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md's own
            // Step 3.4b).
            game.Render(renderer, aspectWidthOverHeight, nullptr, frameDebuggerCapture);
            renderer.EndGraphPassRecording();
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
    const std::function<void(VkCommandBuffer)>& recordSkyBackground, FrameDebuggerCaptureContext* frameDebuggerCapture)
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

// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md) - see
// this function's own doc comment in RenderPasses.h for the full contract.
// The GTE_ENABLE_EDITOR guard below is why this function is declared here,
// unconditionally, but only ever has a REAL body in an Editor build - see
// this file's own top-of-file comment for the full "why".
std::vector<rg::TextureHandle> AddFrameDebuggerReplayPasses(rg::RenderGraphBuilder& builder, Game& game,
    Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground, RenderTexture& gameTarget,
    FrameDebuggerCaptureContext& capture)
{
    std::vector<rg::TextureHandle> destHandles;
#if GTE_ENABLE_EDITOR
    // frame-debugger-8 campaign, PHASE2 - `includeSkyStep`/`totalStepCount`
    // REPLACE the old `if (objectCount == 0) return;` early-out. A real
    // scene with ZERO mesh entities (e.g. Camera + Directional Light only)
    // still genuinely draws the sky every frame - it must still get
    // exactly one real, selectable replay step, not zero (this is a real,
    // confirmed, second fix this phase makes as a natural side effect of
    // the redesign below - see PHASE0_MASTER_STRATEGY.md's own Definition
    // of Done, "zero mesh entities" bullet).
    const bool includeSkyStep = static_cast<bool>(recordSkyBackground);
    const std::size_t totalStepCount = objectCount + (includeSkyStep ? 1 : 0);
    if (totalStepCount == 0) {
        return destHandles;
    }
    destHandles.reserve(totalStepCount);

    // Same width/height/format as the real GameView target, read directly
    // off `gameTarget` (never hardcoded - see AGENTS.md's "Render Target
    // Format Matching") - `gameTarget` itself is NEVER written to here.
    const VkExtent2D extent = gameTarget.Extent();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    const VkFormat format = gameTarget.Format();

    std::vector<RenderTexture> destinations;
    destinations.reserve(totalStepCount); // ESSENTIAL - every destHandle below imports a POINTER-STABLE
                                           // Target() from this vector; it must never reallocate after this point.
    for (std::size_t i = 0; i < totalStepCount; ++i) {
        // Step 3.3b - these debugName/depthDebugName stack buffers are a
        // COMPLETELY SEPARATE, unrelated concern from the pass NAME below
        // (ReplayStepPassName()) - safe ONLY because these RenderTextures
        // are always freshly (re)created every capture, NEVER Resize()d in
        // place (mirrors FrameDebuggerHistory.cpp's own identical
        // reasoning for its own retained-preview textures).
        char debugNameBuffer[48];
        std::snprintf(debugNameBuffer, sizeof(debugNameBuffer), "FrameDebuggerReplayStep%zuColor", i);
        char depthDebugNameBuffer[48];
        std::snprintf(depthDebugNameBuffer, sizeof(depthDebugNameBuffer), "FrameDebuggerReplayStep%zuDepth", i);
        // Depth is created automatically (Renderer::CreateRenderTexture()
        // always builds it against Renderer::DepthFormat() internally) -
        // see Step 3.3a: a RenderTexture already carries its own paired
        // depth buffer, no second array needed.
        destinations.push_back(
            renderer.CreateRenderTexture(width, height, format, debugNameBuffer, depthDebugNameBuffer));
    }

    for (std::size_t i = 0; i < totalStepCount; ++i) {
        const char* passName = ReplayStepPassName(i); // Step 3.3b - NEVER a per-call temporary.

        const rg::TextureHandle destHandle =
            builder.ImportTexture(passName, destinations[i].Target(), VK_IMAGE_LAYOUT_UNDEFINED);

        // frame-debugger-8 campaign, PHASE2 - THE fix. `isSkyStep` is true
        // for EXACTLY ONE index: the extra, dedicated step this phase adds
        // (only reachable when includeSkyStep is true, and only ever equal
        // to `objectCount` - i.e. the very last index in [0, totalStepCount)).
        // Every OTHER index (a real per-object step, i in [0, objectCount))
        // NEVER draws sky, no matter what - this is the actual bug fix:
        // the OLD code's own "if (i + 1 == objectCount && recordSkyBackground)"
        // branch inside the per-object loop is GONE, not just moved.
        const bool isSkyStep = includeSkyStep && (i == objectCount);
        // Real per-object steps redraw objects [0..i] (maxDrawCount = i+1,
        // UNCHANGED from before). The one dedicated sky step redraws EVERY
        // real object (maxDrawCount = objectCount) and then, ADDITIONALLY,
        // the sky - matching the real "GameView" pass's own true order
        // (every entity, then sky, see AddGameViewPass()).
        const std::size_t maxDrawCount = isSkyStep ? objectCount : (i + 1);

        // Render Pass campaign (task_manager/render-pass-1), PHASE4
        // (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.3b) - these N
        // debug-only replay passes now declare through the AddRenderPass()
        // chokepoint (PHASE1), tagged rg::RenderPassCategory::Debug (pulled
        // forward from PHASE5's originally-planned scope) - this is what lets
        // BuildRealFrameDebuggerSnapshot()'s own "view region" walk
        // (FrameDebuggerData.cpp) skip these passes instead of leaking them
        // into the tree as spurious extra leaves, even on the exact capture
        // frame that declares them. Same `name`/`setup`/`execute`, zero
        // behavior change beyond this new stamped metadata.
        builder.AddRenderPass(passName, rg::PassKind::Graphics, rg::ViewScope::GameView, rg::RenderPassCategory::Debug,
            [destHandle, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(destHandle, kGameClearColor);
                pass.WriteDepthStencilAttachment(destHandle, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
            },
            [&game, &renderer, aspectWidthOverHeight, maxDrawCount, isSkyStep, recordSkyBackground](
                rg::PassContext& ctx) {
                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                // frameDebuggerCapture is ALWAYS nullptr here, NEVER the
                // real, armed capture context - see this function's own
                // pre-existing correctness-critical comment (unchanged
                // reasoning, still applies): RecordDraw()/RecordEntityDraw()
                // are not idempotent/deduplicated by draw identity, so
                // feeding a real capture pointer into these N+1 replay
                // passes' own game.Render() calls would silently balloon
                // capture.DrawRecords() into O(N^2) duplicated entries.
                game.Render(renderer, aspectWidthOverHeight, nullptr, /*frameDebuggerCapture=*/nullptr, maxDrawCount);
                renderer.EndGraphPassRecording();
                // Sky is drawn ON EXACTLY ONE dedicated step now - the
                // frame-debugger-8 campaign's own fix for the "last
                // object's own preview silently already included sky"
                // bug (see PHASE0_MASTER_STRATEGY.md Step 2, point 5).
                if (isSkyStep && recordSkyBackground) {
                    recordSkyBackground(ctx.cmd);
                }
            });

        destHandles.push_back(destHandle);
    }

    capture.SetReplayStepPreviews(std::move(destinations));
#else
    (void)builder;
    (void)game;
    (void)renderer;
    (void)aspectWidthOverHeight;
    (void)objectCount;
    (void)gpuSkinningOutputBuffers;
    (void)recordSkyBackground;
    (void)gameTarget;
    (void)capture;
#endif
    return destHandles;
}

void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer, const Mat4&)>& recordSceneOverlay,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground)
{
    builder.AddPass(
        "SceneView", rg::ViewScope::SceneView,
        [sceneViewTarget, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(sceneViewTarget, kGameClearColor);
            pass.WriteDepthStencilAttachment(sceneViewTarget, kGameClearDepth);
            DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
        },
        [&game, &renderer, aspectWidthOverHeight, sceneViewProjection, recordSceneOverlay,
            recordSkyBackground](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            game.Render(renderer, aspectWidthOverHeight, &sceneViewProjection);
            renderer.EndGraphPassRecording();
            // Sky background BEFORE the grid overlay - see this file's own
            // header comment (AddSceneViewPass()'s doc comment) for the
            // full ordering reasoning.
            if (recordSkyBackground) {
                recordSkyBackground(ctx.cmd);
            }
            if (recordSceneOverlay) {
                recordSceneOverlay(ctx.cmd, sceneViewProjection);
            }
        });
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
                // task_manager/frame-debugger-3 campaign, PHASE3, Step 3.4b
                // - deliberately NO frameDebuggerCapture argument here at
                // all (relies on Game::Render()'s own nullptr default) -
                // see this file's own RenderPasses.h AddPresentPass() doc
                // comment for why this fallback branch must NEVER receive
                // a real capture pointer.
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

        builder.AddRenderPass(
            request.name, rg::PassKind::Compute, rg::ViewScope::Shared, rg::RenderPassCategory::GpuSkinning,
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
