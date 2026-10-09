#pragma once

// Thin, Application-layer wrapper functions turning "Present" into a real
// gte::rg::RenderGraph pass, declared via the exact same AddPass()/
// PassBuilder API every other pass uses (see
// src/Renderer/RenderGraph/RenderGraphBuilder.h).
//
// Deliberately living under src/Application/ (NOT src/Renderer/RenderGraph/)
// since these passes encode ENGINE-SPECIFIC, Editor-aware knowledge - which
// RenderTexture is "Game," which is "Scene" - that Renderer/RenderGraph
// itself must never know about, per this codebase's own Clean Architecture
// rule.

#include "../Math/Mat4.h"
// This header declares DeclareGpuSkinningReads()/kGameClearColor/
// kGameClearDepth below, shared with Core.cpp's own "GpuSkinning"/
// "RenderOpaque" RenderPipeline providers - a real, full #include is needed
// since DeclareGpuSkinningReads()'s own signature needs the complete
// rg::RenderGraphBuilder::PassBuilder nested type.
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
// This header #includes the gte_core-owned IFrameDebuggerCaptureRecorder
// interface (src/Core/FrameDebuggerCaptureRecorder.h) instead of forward-
// declaring the concrete, gte_editor-only FrameDebuggerCaptureContext type.
// MUST be included here, at file scope (NOT from inside `namespace gte { ...
// }` below) - this header opens its own `namespace gte { ... }` block, and
// including it from inside an already-open `namespace gte { ... }` here
// would create a bogus nested `gte::gte` namespace instead of extending the
// real `gte` namespace.
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

namespace rg {
class RenderGraphBuilder;
class RenderPassToggleRegistry;
} // namespace rg

// Matches Game::Render()'s own hardcoded `renderer.Clear(20, 20, 30, 255)`
// call exactly (src/Game/Game.cpp).
inline constexpr std::array<float, 4> kGameClearColor{ 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };
// Far plane - matches FrameRecorder.cpp's own
// `depthAttachment.clearValue.depthStencil = { 1.0f, 0 }`.
inline constexpr float kGameClearDepth = 1.0f;

// Declares a phantom ResourceAccess::VertexBufferRead against every handle
// in `gpuSkinningOutputBuffers` - see GPU_SKINNING_PHASE3_RENDERGRAPH_SYNCHRONIZATION_STRATEGY_v2.md
// for why this is NOT dead code - do not remove even though the mesh vertex
// buffer is actually read via a real VkVertexInputAttributeDescription
// binding, never through this declared handle directly.
void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers);

// Declares the "Present" pass: writes swapchainImage's color attachment
// (always cleared, matching FrameRecorder::RecordFrame()'s own old
// unconditional-clear behavior). When `directGameRenderAspect` has a value
// (the release-build/"both Game and Scene panels hidden" degenerate case -
// see Application::Run()), ALSO declares a depth-attachment write and calls
// Game::Render() directly into the swapchain BEFORE `recordImGui` - this is
// deliberately the SAME single pass, or content ends up in an incorrect
// order (a separate direct-render pass followed by "Present" would
// double-clear the swapchain, erasing any just-rendered content).
// `recordImGui`, if set, is invoked last, still inside the same dynamic-
// rendering bracket. `gpuSkinningOutputBuffers` - only meaningful (and only
// ever declared) when this pass itself draws a GPU-skinned mesh directly.
//
// This pass's own direct-render fallback branch calls
// `game.Render(renderer, *directGameRenderAspect)` with NO explicit
// `frameDebuggerCapture` argument at all - relying on Game::Render()'s own
// `nullptr` default. This fallback path renders in place of, never
// alongside, a real Game View render this same frame - this function's own
// signature does NOT grow a `frameDebuggerCapture` parameter at all.
void AddPresentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle swapchainImage,
    std::optional<float> directGameRenderAspect, const std::function<void(VkCommandBuffer)>& recordImGui,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {});

// GPU Vertex Skinning campaign - declares one AddComputePass() per distinct
// model + output group AnimationSystem determined needs GPU skinning this
// frame (see Game::CollectGpuSkinningDispatchRequests()/
// AnimationSystem::CollectModelsNeedingGpuSkinningThisFrame()): imports that
// group's persistent output buffer (RenderGraphBuilder::ImportBuffer()) and
// dispatches its skinning compute kernel (Renderer::Dispatch(), inside a
// BeginGraphPassRecording()/EndGraphPassRecording() bracket).
//
// Returns the imported BufferHandle for every pass declared, in the same
// order - the caller threads this into whichever later pass draws a
// GPU-skinned mesh, so its own declared ResourceAccess::VertexBufferRead
// correctly orders it after this call's writes. A no-op (returns an empty
// vector, declares nothing) whenever no model currently needs GPU skinning
// this frame.
//
// Must be called from INSIDE the same RenderGraph::Execute() `build` lambda
// that will go on to declare whichever pass(es) consume these buffers this
// frame - a compute pass declared into a DIFFERENT Execute() call could
// never be ordered against them by the compiler at all.
//
// `toggleRegistry` (default nullptr) lets this function honestly consult
// RenderPassToggleRegistry::IsEnabled("GpuSkinning") once, whole-stage,
// mirroring Core.cpp's own OFFSCREEN "GpuSkinning" provider's identical
// single-whole-stage-switch shape - this is the direct-render-to-swapchain
// fallback's own separate call path (Core::Present()'s needsDirectGameRender
// branch), sharing the exact same "GpuSkinning" checkbox in the "Render
// Graph" panel.
std::vector<rg::BufferHandle> AddGpuSkinningPasses(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer,
    rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

} // namespace gte
