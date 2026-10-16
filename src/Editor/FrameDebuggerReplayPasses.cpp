// Defines FrameDebuggerCaptureContext::AddReplayPasses(), the Frame Debugger's replay passes.
// Core reaches it only through the IFrameDebuggerCaptureRecorder interface.

#include "../Application/RenderPasses.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/MeshPass.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "FrameDebuggerCapture.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../Renderer/RenderGraph/RenderFeatureScope.h"

#include <cstdio>
#include <deque>
#include <string>
#include <utility>

namespace gte {

namespace {

// Permanent pool of "FrameDebuggerReplayStepN" pass names: graph pass names must outlive the process's frames.
// A deque, so growing it never invalidates a c_str() pointer already handed out.
std::deque<std::string>& ReplayStepPassNamePool()
{
    static std::deque<std::string> pool;
    return pool;
}

// Returns the stable name of replay step `index`, growing the pool on first use.
// The same pointer is returned for that index for the rest of the session.
const char* ReplayStepPassName(std::size_t index)
{
    std::deque<std::string>& pool = ReplayStepPassNamePool();
    while (pool.size() <= index) {
        pool.push_back("FrameDebuggerReplayStep" + std::to_string(pool.size()));
    }
    return pool[index].c_str();
}

} // namespace

// See this method's own doc comment in src/Core/FrameDebuggerCaptureRecorder.h
// for the full contract.
std::vector<rg::TextureHandle> FrameDebuggerCaptureContext::AddReplayPasses(rg::RenderGraphBuilder& builder,
    Game& game, Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const rg::FullscreenReplay& backgroundReplay, RenderTexture& gameTarget,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    std::vector<rg::TextureHandle> destHandles;

    // One whole-mechanism on/off switch, checked before any per-step
    // RenderTexture/pass declaration.
    if (toggleRegistry != nullptr && !toggleRegistry->IsEnabled("FrameDebuggerReplay")) {
        return destHandles;
    }
    // AddRenderPass() auto-registers every pass it declares, including
    // these N ephemeral replay passes - give them a real owner so none of
    // them ever lands under "ENGINE_UNOWNED" in the Render Graph panel.
    const rg::RenderFeatureScope scope(builder, "Frame Debugger");

    // A scene with zero mesh entities still draws the background every
    // frame, so it must still get exactly one selectable replay step.
    const bool includeBackgroundStep = static_cast<bool>(backgroundReplay);
    const std::size_t totalStepCount = objectCount + (includeBackgroundStep ? 1 : 0);
    if (totalStepCount == 0) {
        return destHandles;
    }
    destHandles.reserve(totalStepCount);

    // Same width/height/format as the real GameView target, read directly
    // off `gameTarget` - never hardcoded.
    const Extent2D extent = gameTarget.Extent();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    const PixelFormat format = gameTarget.Format();

    std::vector<RenderTexture> destinations;
    destinations.reserve(totalStepCount); // ESSENTIAL - every destHandle below imports a POINTER-STABLE
                                           // Target() from this vector; it must never reallocate after this point.
    for (std::size_t i = 0; i < totalStepCount; ++i) {
        // These stack buffers are unrelated to the pass NAME below
        // (ReplayStepPassName()) - safe only because these RenderTextures
        // are always freshly (re)created every capture, never Resize()d.
        char debugNameBuffer[48];
        std::snprintf(debugNameBuffer, sizeof(debugNameBuffer), "FrameDebuggerReplayStep%zuColor", i);
        char depthDebugNameBuffer[48];
        std::snprintf(depthDebugNameBuffer, sizeof(depthDebugNameBuffer), "FrameDebuggerReplayStep%zuDepth", i);
        destinations.push_back(
            renderer.CreateRenderTexture(width, height, format, debugNameBuffer, depthDebugNameBuffer));
    }

    for (std::size_t i = 0; i < totalStepCount; ++i) {
        const char* passName = ReplayStepPassName(i); // Never a per-call temporary.

        const rg::TextureHandle destHandle = builder.ImportTexture(passName, destinations[i].Target(),
            ImageLayout::Undefined, destinations[i].SamplerObjectHandle(), destinations[i].DepthSamplerObjectHandle());

        // True for exactly one index: the extra step at objectCount, which also draws the background.
        // Every other step is a per-object step.
        const bool isBackgroundStep = includeBackgroundStep && (i == objectCount);
        // Real per-object steps redraw objects [0..i]. The one dedicated
        // background step redraws every real object, then the background -
        // matching the real "GameView" pass's own true order.
        const std::size_t maxDrawCount = isBackgroundStep ? objectCount : (i + 1);

        // Tagged FrameDebuggerInternal so the snapshot builder's view-region walk skips these debug-only
        // passes instead of listing them as extra leaves.
        builder.AddRenderPass(passName, rg::PassKind::Graphics, rg::ViewScope::GameView,
            rg::RenderPassCategory::FrameDebuggerInternal,
            [destHandle, gpuSkinningOutputBuffers, isBackgroundStep, backgroundReplay](
                rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(destHandle, kGameClearColor);
                pass.WriteDepthStencilAttachment(destHandle, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
                if (isBackgroundStep) {
                    backgroundReplay.declare(pass);
                }
            },
            [&game, &renderer, aspectWidthOverHeight, maxDrawCount, isBackgroundStep, backgroundReplay](
                rg::PassContext& ctx) {
                {
                    // The capture context stays null: recording draws into it again would duplicate every record.
                    const rg::SceneRecording recording(ctx);
                    game.Render(renderer, aspectWidthOverHeight, nullptr, /*frameDebuggerCapture=*/nullptr, maxDrawCount);
                }
                // The background is drawn on exactly one step, after the scene.
                if (isBackgroundStep) {
                    backgroundReplay.record(ctx);
                }
            });

        destHandles.push_back(destHandle);
    }

    SetReplayStepPreviews(std::move(destinations));
    return destHandles;
}

} // namespace gte
