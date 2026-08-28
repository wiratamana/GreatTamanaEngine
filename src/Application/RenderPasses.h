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
// Game::Render() (UNCHANGED signature) inside a
// Renderer::BeginGraphPassRecording()/EndGraphPassRecording() bracket, so
// every Renderer::Submit() call Game/RenderSystem already makes internally
// keeps working completely unmodified - the render graph integration
// happens entirely BELOW Renderer::Submit(), never inside
// Game/RenderSystem/ECS.
//
// GPU Vertex Skinning campaign, Phase 5
// (GPU_SKINNING_PHASE5_RUNTIME_CPU_GPU_SWITCH_STRATEGY_v2.md, Step 3.3)
// added AddGpuSkinningPasses() below, plus an optional
// `gpuSkinningOutputBuffers` parameter on AddGameViewPass()/
// AddSceneViewPass()/AddPresentPass() - see each one's own doc comment.

#include "../Math/Mat4.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <functional>
#include <optional>
#include <vector>

namespace gte {

class Game;
class Renderer;
class RenderTexture;

namespace rg {
class RenderGraphBuilder;
} // namespace rg

// Declares the "GameView" pass: writes gameViewTarget's color+depth
// attachments (cleared - see RenderPasses.cpp's own kGameClearColor/
// kGameClearDepth constants, matching Game::Render()'s own hardcoded clear
// color exactly), and its `execute` calls Game::Render() with the ECS's own
// active Camera (RenderSystem::ResolveActiveCameraViewProjection() -
// unchanged).
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
void AddGameViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle gameViewTarget,
    float aspectWidthOverHeight, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {});

// The Scene-view equivalent of AddGameViewPass() above - `execute` calls
// Game::Render() with `sceneViewProjection` as its viewProjectionOverride
// (the Editor's own independently-orbitable EditorCamera - see
// IEditorLayer::SceneViewProjection()), bypassing ECS camera resolution for
// this view only, exactly as Application::Run() already did before this
// migration. `gpuSkinningOutputBuffers` - see AddGameViewPass() above.
void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {});

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
// AddGameViewPass() above; only meaningful (and only ever declared) when
// `directGameRenderAspect` also has a value, since that's the only case
// where this pass itself draws a GPU-skinned mesh directly.
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
// AddGameViewPass()/AddSceneViewPass()/AddPresentPass()'s own
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
std::vector<rg::BufferHandle> AddGpuSkinningPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer);

} // namespace gte
