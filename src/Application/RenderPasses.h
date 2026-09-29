#pragma once

// Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md, part 7
// of the wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - "Move
// the Real Engine to the Render Graph": thin, Application-layer wrapper
// functions turning "Game view" / "Scene view" / "Present" into real
// gte::rg::RenderGraph passes, declared via the exact same
// AddPass()/PassBuilder API every other pass uses (see
// src/Renderer/RenderGraph/RenderGraphBuilder.h).
//
// Deliberately living under src/Application/ (NOT src/Renderer/RenderGraph/)
// since these three passes encode ENGINE-SPECIFIC, Editor-aware knowledge -
// which RenderTexture is "Game," which is "Scene" - that
// Renderer/RenderGraph itself must never know about, per this codebase's
// own Clean Architecture rule (see AGENTS.md) and the exact same reasoning
// GpuTimingSlot's deliberately generic naming already established
// (src/Renderer/GpuTiming.h).
//
// Each function's body is a direct, literal translation of what
// Application::Run() used to do by hand for that same block: `setup`
// declares exactly one color-attachment write (plus, for Game/Scene, one
// depth-attachment write) against the handle it's given; `execute` calls
// Game::Render() (unchanged for every parameter this comment already
// described here - `renderer`, `aspectWidthOverHeight`,
// `viewProjectionOverride` - task_manager/frame-debugger-3/PHASE3 added one
// new, always-defaulted, campaign-specific trailing parameter,
// `frameDebuggerCapture`, on AddRenderOpaquePass() only (RENAMED from
// AddGameViewPass() by the Render Pass campaign's own PHASE2,
// task_manager/render-pass-1 - see that function's own doc comment below)
// inside a
// Renderer::BeginGraphPassRecording()/EndGraphPassRecording() bracket, so
// every Renderer::Submit() call Game/RenderSystem already makes internally
// keeps working completely unmodified - the render graph integration
// happens entirely BELOW Renderer::Submit(), never inside
// Game/RenderSystem/ECS.
//
// GPU Vertex Skinning campaign, Phase 5
// (GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md, Step 3.3)
// added AddGpuSkinningPasses() below, plus an optional
// `gpuSkinningOutputBuffers` parameter on AddRenderOpaquePass()/
// AddSceneViewPass()/AddPresentPass() - see each one's own doc comment.

#include "../Math/Mat4.h"
// render-pass-3 campaign, PHASE2 (PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md)
// - upgraded from a bare `namespace rg { class RenderGraphBuilder; }`
// forward declaration to a real, full #include: this header now declares
// DeclareGpuSkinningReads()/kGameClearColor/kGameClearDepth (below), moved
// here (from RenderPasses.cpp's own former anonymous namespace) so
// Application.cpp's new "RenderOpaque" RenderPipeline provider can reuse
// these SAME symbols bit-for-bit rather than duplicating them -
// DeclareGpuSkinningReads()'s own signature needs the real, complete
// rg::RenderGraphBuilder::PassBuilder nested type, which a bare forward
// declaration of the outer class can never provide.
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
// editor-core-separation-2 campaign, PHASE2 - this header now #includes the
// gte_core-owned IFrameDebuggerCaptureRecorder interface (src/Core/
// FrameDebuggerCaptureRecorder.h) instead of forward-declaring the concrete,
// gte_editor-only FrameDebuggerCaptureContext type - see that header's own
// doc comment and PHASE0_MASTER_STRATEGY.md's Locked Design Decision #1.
// AddRenderOpaquePass()/AddDrawSkyBackgroundPass() below only ever need a
// POINTER to the interface, never dereferencing it themselves (they only
// ever forward it onward into game.Render()). MUST be included here, at
// file scope (NOT from inside `namespace gte { ... }` below) - this header
// opens its own `namespace gte { ... }` block, and including it from
// inside an already-open `namespace gte { ... }` here would create a
// bogus nested `gte::gte` namespace instead of extending the real `gte`
// namespace.
#include "../Core/FrameDebuggerCaptureRecorder.h"

#include <volk.h>

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace gte {

class Game;
class Renderer;
class RenderTexture;

namespace rg {
class RenderGraphBuilder;
class RenderPassToggleRegistry;
} // namespace rg

// render-pass-3 campaign, PHASE2 (PHASE2_GPU_SKINNING_OPAQUE_BLACKBOARD_PROOF.md)
// - moved here from RenderPasses.cpp's own former anonymous namespace
// (UNCHANGED in value/behavior) so Application.cpp's new "RenderOpaque"
// RenderPipeline provider can declare a BIT-FOR-BIT IDENTICAL initial
// clear/GPU-skinning-read as this file's own AddRenderOpaquePass() below,
// rather than risking two independently-hand-maintained copies drifting
// apart. Matches Game::Render()'s own hardcoded `renderer.Clear(20, 20, 30,
// 255)` call EXACTLY (src/Game/Game.cpp).
inline constexpr std::array<float, 4> kGameClearColor{ 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };
// Far plane - matches FrameRecorder.cpp's own
// `depthAttachment.clearValue.depthStencil = { 1.0f, 0 }`.
inline constexpr float kGameClearDepth = 1.0f;

// Declares a phantom ResourceAccess::VertexBufferRead against every handle
// in `gpuSkinningOutputBuffers` - see GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md
// for why this is NOT dead code - do not remove even though the mesh vertex
// buffer is actually read via a real VkVertexInputAttributeDescription
// binding, never through this declared handle directly. render-pass-3
// campaign, PHASE2 - moved here (from RenderPasses.cpp's own former
// anonymous namespace, UNCHANGED body) for the exact same reason
// kGameClearColor/kGameClearDepth above were: Application.cpp's new
// "RenderOpaque" RenderPipeline provider calls this SAME helper too.
void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers);

// Render Pass campaign (task_manager/render-pass-1), PHASE2
// (PHASE2_RENDER_OPAQUE_SKY_SPLIT_AND_TRANSPARENT_STUB.md) - RENAMED from
// the original AddGameViewPass() (which used to ALSO draw the sky
// background, hand-fused into this same pass - see PHASE0_MASTER_STRATEGY.md's
// own Step 2 for the full history of why that was a hack). Declares the
// "RenderOpaque" pass: writes gameViewTarget's color+depth attachments
// (cleared - see RenderPasses.cpp's own kGameClearColor/kGameClearDepth
// constants, matching Game::Render()'s own hardcoded clear color exactly -
// this is the FIRST pass to touch this target this frame), and its
// `execute` calls Game::Render() with the ECS's own active Camera
// (RenderSystem::ResolveActiveCameraViewProjection() - unchanged). The Sky
// Background draw is now a real, separate pass - see AddDrawSkyBackgroundPass()
// below - this function no longer takes a `recordSkyBackground` parameter
// at all.
//
// `gpuSkinningOutputBuffers` (GPU Vertex Skinning campaign, Phase 5 - see
// GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md) is the set of
// buffer handles AddGpuSkinningPasses() (below) declared as
// ComputeShaderWrite this same Execute() call - each is additionally
// declared here as a phantom ResourceAccess::VertexBufferRead (see
// GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md), forcing
// the render graph's compiler/barrier planner to correctly order this draw
// pass AFTER whichever compute pass(es) wrote them. Empty (the default) in
// CPU skinning mode, or whenever no GPU-skinned model is currently
// animating - declaring a phantom read for a buffer this pass doesn't
// actually end up drawing this frame is a harmless, conservative
// over-synchronization, never a correctness problem.
//
// `frameDebuggerCapture` (task_manager/frame-debugger-3 campaign, PHASE3 -
// PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md, Step 3.4b) -
// forwarded straight through into this pass's own `game.Render(...)` call
// as its new, LAST, defaulted parameter (see Game.h's own updated Render()
// comment). `nullptr` (the default) whenever the Frame Debugger is not
// currently armed for this frame - Application::Run() is the ONLY caller
// that ever passes a real, non-null pointer here (see
// IEditorLayer::PrepareFrameDebuggerCaptureContext()), and ONLY at this one
// call site - AddSceneViewPass()/AddPresentPass() never receive one (Scene
// View is out of scope - Locked Design Decision #7 - and AddPresentPass()'s
// own direct-render fallback branch must NEVER be handed a real capture
// pointer either way, see that function's own doc comment below).
void AddRenderOpaquePass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    rg::TextureHandle gameViewTarget, float aspectWidthOverHeight,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {},
    IFrameDebuggerCaptureRecorder* frameDebuggerCapture = nullptr);

// Render Pass campaign, PHASE2 - the Sky Background draw, now a REAL,
// separate Render Graph pass in its own right (previously hand-fused inside
// the old AddGameViewPass()'s own execute lambda - see PHASE0_MASTER_STRATEGY.md's
// own Step 2 for the full history of why that was a hack). MUST be declared
// AFTER AddRenderOpaquePass() in the SAME builder call, against the SAME
// `gameViewTarget` handle - this pass deliberately does NOT clear either
// attachment (VK_ATTACHMENT_LOAD_OP_LOAD for both color and depth), relying
// on AtmosphereSkyBackgroundRenderer's own already-existing EQUAL-depth-test
// pipeline (see DescribeSkyBackgroundPipelineState(),
// src/Editor/FrameDebuggerCapture.h/.cpp) to only paint pixels
// AddRenderOpaquePass() didn't already cover. A true no-op (declares
// NOTHING) if `recordSkyBackground` is empty (mirrors AddGpuSkinningPasses()'s
// own "add nothing when nothing to do" rule) - this can legitimately happen
// if a future caller has no sky to draw at all.
//
// `frameDebuggerCapture` - kept purely for signature symmetry with
// AddRenderOpaquePass() above (see RenderPasses.cpp's own doc comment on
// this parameter) - no longer dereferenced anywhere in this pass's own
// `execute` lambda. Render Pass campaign (task_manager/render-pass-1),
// PHASE4 (PHASE4_FRAME_DEBUGGER_GENERIC_TREE_REWORK.md, Step 3.4) removed
// the temporary `FrameDebuggerCaptureContext::RecordSkyBackgroundDraw()`
// bridge call this pass used to make - "DrawSkyBackground" is now
// generically discovered by the Frame Debugger's own tree-building logic
// (BuildRealFrameDebuggerSnapshot(), FrameDebuggerData.cpp) purely via its
// real pass name/category, exactly like every other Graphics-kind pass in
// its "view region" - no fabricated draw record needed at all.
void AddDrawSkyBackgroundPass(rg::RenderGraphBuilder& builder, Renderer& renderer, rg::TextureHandle gameViewTarget,
    const std::function<void(VkCommandBuffer)>& recordSkyBackground,
    IFrameDebuggerCaptureRecorder* frameDebuggerCapture = nullptr);

// Render Pass campaign, PHASE2 - the built-in "Render Transparent" pass - a
// real, permanent call site wired into Application::Run(), currently ALWAYS
// a no-op (mirrors AddGpuSkinningPasses()'s own "return/declare nothing when
// there is genuinely nothing to do" pattern) since
// RenderSystem::CollectTransparentRenderables() always returns empty today.
// A future transparency campaign's own job is to make THIS function's own
// body do real work once MeshRenderer gains a real isTransparent/renderQueue
// flag - this campaign's job is only to make sure the call site, the pass
// name, and its correct position in the frame (after DrawSkyBackground,
// before the Aerial Perspective composite pass) already exist and are
// already wired end-to-end. Runs BETWEEN Sky Background and the Aerial
// Perspective composite pass (the conventional forward-rendering order:
// opaque -> sky -> transparent -> post-processing).
void AddRenderTransparentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    rg::TextureHandle gameViewTarget, float aspectWidthOverHeight);

// task_manager/frame-debugger-7 campaign, PHASE3
// (PHASE3_UNIFIED_STEP_TIMELINE_AND_PER_DRAW_REPLAY_RENDERING.md) - adds N
// debug-only, self-contained Render Graph passes (one per real object this
// frame's "RenderOpaque" pass will draw) - see this method's own doc
// comment now (editor-core-separation-2 campaign, PHASE2) in
// src/Core/FrameDebuggerCaptureRecorder.h, gte::IFrameDebuggerCaptureRecorder::
// AddReplayPasses(), for the full contract this DECLARATION used to carry
// here as a free function, gte::AddFrameDebuggerReplayPasses(). That free
// function is GONE - Core::BuildFrame() now calls this through a null-
// checked IFrameDebuggerCaptureRecorder* pointer instead
// (`frameDebuggerCapture->AddReplayPasses(...)`), never a gte_editor-only
// free-function symbol by name (closing editor-core-separation-2's own
// "Defect B" - see PHASE0_MASTER_STRATEGY.md). Its body is still physically
// defined in src/Editor/FrameDebuggerReplayPasses.cpp, now as
// FrameDebuggerCaptureContext::AddReplayPasses().

// render-pass-3 campaign, PHASE3
// (PHASE3_FULL_PRODUCTION_PASS_MIGRATION_AND_VIEW_UNIFICATION.md, Step 3.4) -
// AddSceneViewPass() (the old, single, FUSED Scene-View pass - opaque draw +
// sky background + the Editor's own ground-grid overlay, all inside one
// builder.AddPass("SceneView", ...) call) is REMOVED. Scene View now goes
// through the exact SAME "RenderOpaque"/"DrawSkyBackground"/
// "RenderTransparent" RenderPipeline providers Game View already uses (see
// Application.cpp's RegisterOffscreenRenderPipelineProviders()) - each one
// now genuinely declares a SEPARATE PassRecord per view, tagged
// ViewScope::SceneView for Scene's own copy. The one thing Scene View needs
// that Game View does not - the ground-grid overlay - is folded into the
// "RenderTransparent" provider's own body, gated on
// RenderPassViewData::recordSceneOverlay being set (see
// src/Application/RenderPassViewData.h). Confirmed zero remaining callers
// via search_in_dir before deletion - see this phase's own completion
// report.

// Declares the "Present" pass: writes swapchainImage's color attachment
// (always cleared, matching FrameRecorder::RecordFrame()'s own old
// unconditional-clear behavior). When `directGameRenderAspect` has a value
// (the release-build/"both Game and Scene panels hidden" degenerate case -
// see Application::Run()), ALSO declares a depth-attachment write and calls
// Game::Render() directly into the swapchain BEFORE `recordImGui` - this is
// deliberately the SAME single pass or content ends up in an incorrect
// order (a separate "GameView-direct" pass followed by "Present" would
// double-clear the swapchain, erasing Game's own just-rendered content -
// see RENDERGRAPH_PHASE7_COMPLETION_REPORT.md for the full reasoning).
// `recordImGui`, if set, is invoked last, still inside the same dynamic-
// rendering bracket - mirroring IEditorLayer::Render()'s existing
// recordExtra contract exactly. `gpuSkinningOutputBuffers` - see
// AddRenderOpaquePass() above; only meaningful (and only ever declared) when
// where this pass itself draws a GPU-skinned mesh directly.
//
// task_manager/frame-debugger-3 campaign, PHASE3
// (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md, Step 3.4b) -
// this pass's own direct-render fallback branch (`directGameRenderAspect`
// having a value) calls `game.Render(renderer, *directGameRenderAspect)`
// with NO explicit `frameDebuggerCapture` argument at all - relying on
// Game::Render()'s own `nullptr` default rather than accidentally
// forwarding AddRenderOpaquePass()'s own armed pointer here. This is
// deliberate and correct (this fallback path renders in place of, never
// alongside, the real Game View render (RenderOpaque/DrawSkyBackground
// passes) this same frame - the two are mutually exclusive per frame by
// construction, since AddRenderOpaquePass()/AddDrawSkyBackgroundPass() are
// simply never declared at all in the frame where this fallback runs), and
// is exactly why this function's own signature does NOT grow a
// `frameDebuggerCapture` parameter at all - there being no such parameter
// to accidentally misuse here is the safest possible guard against ever
// wiring the same shared variable into both call sites by mistake.
void AddPresentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle swapchainImage,
    std::optional<float> directGameRenderAspect, const std::function<void(VkCommandBuffer)>& recordImGui,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {});

// Transitions `texture`'s COLOR image from the ColorAttachmentWrite state a
// GameView/SceneView pass (above) leaves it in, to a real ShaderRead state -
// needed because Dear ImGui samples the Game/Scene RenderTexture entirely
// on its own, outside the render graph's own resource model, via its own
// descriptor set (see RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md's V2
// Revision Note 4) - no pass ever declares a ReadTexture() for it, so
// nothing inside the graph itself would ever trigger this transition.
// Must be called against the SAME command buffer the render graph's
// offscreen Execute() call just recorded into, AFTER that call returns (so
// the pass's own vkCmdBeginRendering/vkCmdEndRendering bracket has already
// closed) and BEFORE that command buffer is ended/submitted - see
// Application::Run().
void FinalizeRenderTextureForExternalSampling(VkCommandBuffer cmd, RenderTexture& texture);

// GPU Vertex Skinning campaign, Phase 5, Step 3.3 ("Who actually issues the
// vkCmdDispatch?") - declares one AddComputePass() per distinct model +
// output group AnimationSystem determined needs GPU skinning this frame
// (see Game::CollectGpuSkinningDispatchRequests()/
// AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()): imports
// that group's persistent output buffer (RenderGraphBuilder::ImportBuffer())
// and dispatches its skinning compute kernel (Renderer::Dispatch(), inside
// a BeginGraphPassRecording()/EndGraphPassRecording() bracket, exactly
// mirroring src/Editor/ComputeBlurValidation.cpp's own proven pattern).
//
// Returns the imported BufferHandle for every pass declared, in the same
// order - the caller (Application::Run()) threads this straight into
// AddRenderOpaquePass()/AddSceneViewPass()/AddPresentPass()'s own
// `gpuSkinningOutputBuffers` parameter, so those passes' declared
// ResourceAccess::VertexBufferRead correctly orders them after this call's
// own writes. A no-op (returns an empty vector, declares nothing) whenever
// no model currently needs GPU skinning this frame - e.g. CPU mode is
// active, or no rigged model is currently playing an animation at all.
//
// Must be called from INSIDE the same RenderGraph::Execute() `build`
// lambda that will go on to declare the GameView/SceneView/Present pass(es)
// consuming these buffers this frame - a compute pass declared into a
// DIFFERENT Execute() call could never be ordered against them by the
// compiler at all (each Execute() call compiles/executes its own,
// completely independent graph).
// editor-core-separation-21 campaign, PHASE4 (fixing PHASE3's confirmed-lie
// finding #9) - `toggleRegistry` (default nullptr) lets this function
// honestly consult RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled(
// "GpuSkinning") ONCE, whole-stage, mirroring Core.cpp's own OFFSCREEN
// "GpuSkinning" provider's identical single-whole-stage-switch shape
// (Core.cpp ~line 476) - this is the direct-render-to-swapchain fallback's
// own separate call path (Core::Present()'s needsDirectGameRender branch),
// which used to declare every per-request dispatch pass unconditionally
// with zero registry consult at all, even though both paths share the
// exact same "GpuSkinning" checkbox in the "Render Graph" panel.
std::vector<rg::BufferHandle> AddGpuSkinningPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

} // namespace gte
