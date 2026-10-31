#include "FrameDebuggerCapturedFrame.h"

#include "../Profiling/ScopeTimer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/OneShot.h"

#include <stdexcept>
#include <utility>

namespace gte {

void FrameDebuggerCurrentCapture::CaptureFrame(Renderer& renderer, const FrameDebuggerSnapshot& snapshot,
    RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource, FrameDebuggerCaptureContext& capture)
{
    FrameDebuggerCapturedFrame frame;
    frame.snapshot = snapshot;

    // Fresh textures every capture: they always match the source's current size and format.
    const auto createCopyTarget = [&renderer](const RenderTexture& source, const char* name) {
        const Extent2D extent = source.Extent();
        return renderer.CreateRenderTexture(static_cast<int>(extent.width), static_cast<int>(extent.height),
            source.Format(), name, nullptr, /*allowStorageImageAccess=*/false, /*allowDepthSampledAccess=*/false,
            /*createDepthCompanion=*/true, /*createColorImage=*/true, /*transferSrc=*/false, /*transferDst=*/true);
    };
    frame.preview.emplace(createCopyTarget(gameViewSource, "FrameDebuggerCurrentCapturePreview"));
    if (compositedGameViewSource != nullptr) {
        frame.compositedPreview.emplace(
            createCopyTarget(*compositedGameViewSource, "FrameDebuggerCurrentCapturePreviewComposited"));
    }

    // Move the replay previews out before the next Reset() clears them.
    frame.perObjectStepPreviews = std::move(capture.ReplayStepPreviews());

    // Both copies share one submission. Sources are live textures already in the shader-read state.
    rg::OneShot oneShot(renderer);
    const rg::ResourceState shaderRead = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const auto copy = [&](const RenderTexture& from, RenderTexture& to) {
        oneShot.CopyTexture(oneShot.ImportReadOnly(from, shaderRead), oneShot.Import(to, rg::FinalState::ShaderRead));
    };
    copy(gameViewSource, *frame.preview);
    if (compositedGameViewSource != nullptr) {
        copy(*compositedGameViewSource, *frame.compositedPreview);
    }
    bool copied = false;
    {
        GTE_PROFILE_BLOCKED("Wait.OneShotSubmit");
        copied = oneShot.Run();
    }
    if (!copied) {
        throw std::runtime_error("the GPU copy of the Game view failed");
    }

    m_current.emplace(std::move(frame));
}

} // namespace gte
