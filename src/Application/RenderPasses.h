#pragma once

// Application-layer render graph passes shared by the offscreen and present graphs.

// Full include: DeclareGpuSkinningReads() takes the nested PassBuilder type.
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphTypes.h"
// Include at file scope: the header opens its own `namespace gte`, so including it inside one would nest gte::gte.
#include "../Core/FrameDebuggerCaptureRecorder.h"

#include <array>
#include <functional>
#include <vector>

namespace gte {

// Display-referred clear color of the swapchain image.
inline constexpr std::array<float, 4> kGameClearColor{ 20.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 1.0f };
// Linear clear color of scene color targets (the display color 20/255, 20/255, 30/255).
inline constexpr std::array<float, 4> kSceneClearColor{ 0.0070f, 0.0070f, 0.0130f, 1.0f };
// Far-plane depth clear.
inline constexpr float kGameClearDepth = 1.0f;

// Declares a VertexBufferRead on every skinning output buffer so the compiler orders the skinning writes
// before the draws that read them as vertex input.
void DeclareGpuSkinningReads(
    rg::RenderGraphBuilder::PassBuilder& pass, const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers);

// Declares the "Present" pass: writes the swapchain color attachment, then runs `recordImGui`.
// `clearSwapchain` clears the image first; otherwise the earlier contents are kept.
void AddPresentPass(rg::RenderGraphBuilder& builder, rg::TextureHandle swapchainImage, bool clearSwapchain,
    const std::function<void(const rg::RecordingContext&)>& recordImGui);

} // namespace gte
