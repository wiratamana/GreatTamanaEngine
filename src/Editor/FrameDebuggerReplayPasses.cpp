// Defines FrameDebuggerCaptureContext::AddReplayPasses() - the concrete
// implementation of IFrameDebuggerCaptureRecorder::AddReplayPasses()
// (src/Core/FrameDebuggerCaptureRecorder.h). Reached from gte_core-tier
// Core::BuildFrame() only through a virtual call on a null-checked
// IFrameDebuggerCaptureRecorder* pointer - never a gte_editor-only
// free-function symbol by name.

#include "../Application/RenderPasses.h"

#include "../Game/Game.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderTexture.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
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

// Permanent, ever-growing pool of "FrameDebuggerReplayStepN" pass names.
// Must never be a per-frame/per-capture temporary - RenderGraphBuilder
// pass names must stay valid for the rest of the process's lifetime.
// std::deque (never std::vector) so growing this pool never invalidates
// a c_str() pointer already handed out from a previous capture.
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

// See this method's own doc comment in src/Core/FrameDebuggerCaptureRecorder.h
// for the full contract.
std::vector<rg::TextureHandle> FrameDebuggerCaptureContext::AddReplayPasses(rg::RenderGraphBuilder& builder,
    Game& game, Renderer& renderer, float aspectWidthOverHeight, std::size_t objectCount,
    const std::vector<rg::BufferHandle>& gpuSkinningOutputBuffers,
    const std::function<void(VkCommandBuffer)>& recordBackgroundStep, RenderTexture& gameTarget,
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
    const bool includeBackgroundStep = static_cast<bool>(recordBackgroundStep);
    const std::size_t totalStepCount = objectCount + (includeBackgroundStep ? 1 : 0);
    if (totalStepCount == 0) {
        return destHandles;
    }
    destHandles.reserve(totalStepCount);

    // Same width/height/format as the real GameView target, read directly
    // off `gameTarget` - never hardcoded.
    const VkExtent2D extent = gameTarget.Extent();
    const int width = static_cast<int>(extent.width);
    const int height = static_cast<int>(extent.height);
    const VkFormat format = gameTarget.Format();

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
            VK_IMAGE_LAYOUT_UNDEFINED, destinations[i].Sampler(), destinations[i].DepthSampler());

        // isBackgroundStep is true for exactly one index: the extra,
        // dedicated step (only reachable when includeBackgroundStep is
        // true, always equal to objectCount). Every other index is a real
        // per-object step and never draws the background.
        const bool isBackgroundStep = includeBackgroundStep && (i == objectCount);
        // Real per-object steps redraw objects [0..i]. The one dedicated
        // background step redraws every real object, then the background -
        // matching the real "GameView" pass's own true order.
        const std::size_t maxDrawCount = isBackgroundStep ? objectCount : (i + 1);

        // These N debug-only replay passes declare through AddRenderPass(),
        // tagged FrameDebuggerInternal so BuildRealFrameDebuggerSnapshot()'s
        // own view-region walk skips them instead of leaking them into the
        // tree as spurious extra leaves.
        builder.AddRenderPass(passName, rg::PassKind::Graphics, rg::ViewScope::GameView,
            rg::RenderPassCategory::FrameDebuggerInternal,
            [destHandle, gpuSkinningOutputBuffers](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.WriteColorAttachment(destHandle, kGameClearColor);
                pass.WriteDepthStencilAttachment(destHandle, kGameClearDepth);
                DeclareGpuSkinningReads(pass, gpuSkinningOutputBuffers);
            },
            [&game, &renderer, aspectWidthOverHeight, maxDrawCount, isBackgroundStep, recordBackgroundStep](
                rg::PassContext& ctx) {
                renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
                // frameDebuggerCapture is ALWAYS nullptr here, never the
                // real, armed capture context - RecordDraw()/RecordEntityDraw()
                // are not idempotent/deduplicated by draw identity, so a real
                // capture pointer here would balloon DrawRecords() into
                // O(N^2) duplicated entries.
                game.Render(renderer, aspectWidthOverHeight, nullptr, /*frameDebuggerCapture=*/nullptr, maxDrawCount);
                renderer.EndGraphPassRecording();
                // The background is drawn on exactly one dedicated step.
                if (isBackgroundStep && recordBackgroundStep) {
                    recordBackgroundStep(ctx.cmd);
                }
            });

        destHandles.push_back(destHandle);
    }

    SetReplayStepPreviews(std::move(destinations));
    return destHandles;
}

} // namespace gte
