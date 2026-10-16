#pragma once

// Application-layer render graph passes (Present, GPU skinning). They live here, not in Renderer/RenderGraph,
// because they know which RenderTexture is "Game" and which is "Scene".

#include "../Math/Mat4.h"
// Full include: DeclareGpuSkinningReads() takes the nested PassBuilder type.
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
// Include at file scope: the header opens its own `namespace gte`, so including it inside one would nest gte::gte.
#include "../Core/FrameDebuggerCaptureRecorder.h"

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

// Matches the clear color Game::Render() uses.
inline constexpr std::array<float, 4> kGameClearColor{ 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };
// Far-plane depth clear.
inline constexpr float kGameClearDepth = 1.0f;

// Declares a VertexBufferRead on every skinning output buffer so the compiler orders the skinning writes before
// the draw. The mesh reads the buffer through its vertex input binding, so this declaration is required.
void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers);

// Declares the "Present" pass: writes the swapchain color attachment (always cleared). With `directGameRenderAspect`
// set (both view panels hidden) it also writes depth and calls Game::Render() into the swapchain first, in the same
// pass so the swapchain is cleared once; `recordImGui` runs last. The direct render passes no capture.
void AddPresentPass(rg::RenderGraphBuilder& builder, Game& game, Renderer& renderer, rg::TextureHandle swapchainImage,
    std::optional<float> directGameRenderAspect, const std::function<void(const rg::RecordingContext&)>& recordImGui,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers = {});

// Declares one compute dispatch per model/output group that needs GPU skinning this frame and imports its output
// buffer. Returns the handles in dispatch order. Call inside the Execute() build lambda that declares the consumers;
// `toggleRegistry` is consulted once for the "GpuSkinning" toggle.
std::vector<rg::BufferHandle> AddGpuSkinningPasses(
    rg::RenderGraphBuilder& builder, Game& game, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

} // namespace gte
