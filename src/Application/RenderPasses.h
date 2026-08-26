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

#include "../Math/Mat4.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"

#include <volk.h>

#include <functional>
#include <optional>

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
void AddGameViewPass(
    rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle gameViewTarget, float aspectWidthOverHeight);

// The Scene-view equivalent of AddGameViewPass() above - `execute` calls
// Game::Render() with `sceneViewProjection` as its viewProjectionOverride
// (the Editor's own independently-orbitable EditorCamera - see
// IEditorLayer::SceneViewProjection()), bypassing ECS camera resolution for
// this view only, exactly as Application::Run() already did before this
// migration.
void AddSceneViewPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle sceneViewTarget,
    float aspectWidthOverHeight, const Mat4& sceneViewProjection);

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
// recordExtra contract exactly.
void AddPresentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle swapchainImage,
    std::optional<float> directGameRenderAspect, const std::function<void(VkCommandBuffer)>& recordImGui);

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

} // namespace gte
