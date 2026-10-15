#include "FrameDebuggerHistory.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/OneShot.h"

namespace gte {

void FrameDebuggerCurrentCapture::CaptureFrame(Renderer& renderer, const rg::RenderGraph& /*renderGraph*/,
    const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource,
    FrameDebuggerCaptureContext& capture)
{
    FrameDebuggerHistoryEntry& entry = m_current.emplace();
    entry.snapshot = snapshot;

    // Fresh textures every capture: they always match the source's current size and format.
    const auto createCopyTarget = [&renderer](const RenderTexture& source, const char* name) {
        const Extent2D extent = source.Extent();
        return renderer.CreateRenderTexture(static_cast<int>(extent.width), static_cast<int>(extent.height),
            source.Format(), name, nullptr, /*allowStorageImageAccess=*/false, /*allowDepthSampledAccess=*/false,
            /*createDepthCompanion=*/true, /*createColorImage=*/true, /*transferSrc=*/false, /*transferDst=*/true);
    };
    entry.preview.emplace(createCopyTarget(gameViewSource, "FrameDebuggerCurrentCapturePreview"));
    if (compositedGameViewSource != nullptr) {
        entry.compositedPreview.emplace(
            createCopyTarget(*compositedGameViewSource, "FrameDebuggerCurrentCapturePreviewComposited"));
    } else {
        entry.compositedPreview.reset();
    }

    // The replay passes of this frame fill the step previews; move them before the next frame's Reset() clears them.
    entry.perObjectStepPreviews = std::move(capture.ReplayStepPreviews());

    // Both copies share one submission. Sources are live textures already in the shader-read state.
    rg::OneShot oneShot(renderer);
    const rg::ResourceState shaderRead = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const auto copy = [&](const RenderTexture& from, RenderTexture& to) {
        oneShot.CopyTexture(oneShot.ImportReadOnly(from, shaderRead), oneShot.Import(to, rg::FinalState::ShaderRead));
    };
    copy(gameViewSource, *entry.preview);
    if (compositedGameViewSource != nullptr) {
        copy(*compositedGameViewSource, *entry.compositedPreview);
    }
    oneShot.Run();
}

} // namespace gte
