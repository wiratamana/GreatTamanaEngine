#include "FrameDebuggerHistory.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"

namespace gte {

// task_manager/frame-debugger-7 campaign, PHASE4
// (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.2/3.3) - `renderGraph` is
// intentionally UNUSED by this method's own body as of this phase (see this
// method's own doc comment, FrameDebuggerHistory.h, for why the parameter
// itself was nonetheless kept) - every compute-pass-specific texture/volume-
// texture discovery and copy that used to read it (the `computeWrites`/
// `computeVolumeWrites` loops, `HdrComputePassCopySource`/
// `ComputePassCopySource`, `m_volumePreviewRenderer`) was REMOVED outright
// this phase, per PHASE0's Locked Design Decision #3.
void FrameDebuggerCurrentCapture::CaptureFrame(Renderer& renderer, const rg::RenderGraph& /*renderGraph*/,
    const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource,
    FrameDebuggerCaptureContext& capture)
{
    // task_manager/frame-debugger-7 campaign, PHASE1
    // (PHASE1_REMOVE_HISTORY_AND_SINGLE_CAPTURE_LIFECYCLE.md) - there is only
    // ever one slot now: emplace() destroys whatever the previous capture
    // held (if any - RAII, via FrameDebuggerHistoryEntry's own destructor)
    // and default-constructs a brand-new one in place, which this function
    // then populates exactly like the old ring buffer's "next slot" used to.
    FrameDebuggerHistoryEntry& entry = m_current.emplace();
    entry.snapshot = snapshot;

    const VkExtent2D extent = gameViewSource.Extent();

    // Always freshly (re)created, never Resize()d in place - the simplest
    // way to guarantee this capture's own retained texture matches
    // gameViewSource's CURRENT size/format exactly on every single real
    // capture (the live Game View can be resized between two captures), and
    // it sidesteps needing to track any previous VkImageLayout across past
    // captures: a freshly (re)created RenderTexture's color image always
    // starts life in VK_IMAGE_LAYOUT_UNDEFINED (see RenderTexture.cpp's own
    // Create()), so the destination barrier below never needs to know or
    // trust what state was left over from last time.
    entry.preview.emplace(renderer.CreateRenderTexture(
        static_cast<int>(extent.width), static_cast<int>(extent.height), gameViewSource.Format(),
        "FrameDebuggerCurrentCapturePreview"));

    // (frame-debugger-4 campaign, PHASE1) - the second retained copy, only
    // when a real composited source exists this capture. Uses the
    // COMPOSITED source's own extent/format (which may legitimately differ
    // in size from gameViewSource's own extent between two captures if a
    // resize landed asymmetrically - in practice both always match the SAME
    // Game View panel's current content-region size, but this function must
    // not assume that). A distinct debug name keeps GPU-memory-debugger
    // tooling (if any reads RenderTexture debug names) able to tell the two
    // apart.
    const bool hasCompositedSource = (compositedGameViewSource != nullptr);
    if (hasCompositedSource) {
        const VkExtent2D compositedExtent = compositedGameViewSource->Extent();
        entry.compositedPreview.emplace(renderer.CreateRenderTexture(static_cast<int>(compositedExtent.width),
            static_cast<int>(compositedExtent.height), compositedGameViewSource->Format(),
            "FrameDebuggerCurrentCapturePreviewComposited"));
    } else {
        entry.compositedPreview.reset();
    }

    // task_manager/frame-debugger-7 campaign, PHASE4
    // (PHASE4_PREVIEW_WIRING_AND_DATA_MODEL.md, Step 3.2 point 3) - moves
    // Phase 3's transient `FrameDebuggerCaptureContext::ReplayStepPreviews()`
    // vector (real, per-object accumulated Game View images, one per real
    // object drawn this frame - populated only on an explicit capture-
    // trigger frame, by `AddFrameDebuggerReplayPasses()`,
    // `src/Application/RenderPasses.cpp`) into this capture's own permanent
    // `perObjectStepPreviews` field, strictly BEFORE the next armed frame's
    // own `FrameDebuggerCaptureContext::Reset()` call would otherwise wipe
    // it - the Step 3.0 two-bool pending/serviced handshake (Phase 3)
    // already guarantees this method runs later the SAME frame those replay
    // passes were declared/executed, satisfying that ordering requirement.
    entry.perObjectStepPreviews = std::move(capture.ReplayStepPreviews());

    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    const rg::ResourceState shaderRead = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const rg::ResourceState transferSrc = rg::RequiredStateFor(rg::ResourceAccess::TransferSrc, false);
    const rg::ResourceState transferDst = rg::RequiredStateFor(rg::ResourceAccess::TransferDst, false);
    const rg::ResourceState freshImageState{}; // VK_IMAGE_LAYOUT_UNDEFINED default - matches a just-created image exactly.

    // Mirrors Renderer::CaptureImagePixels()'s own transition-copy-
    // transition-back discipline (see that method's own doc comment,
    // Renderer.cpp) - same barriers, just a vkCmdCopyImage into another
    // live GPU image instead of a vkCmdCopyImageToBuffer into host memory.
    // A genuinely EXTRA, explicit, on-demand GPU submission - acceptable
    // ONLY because CaptureFrame() itself happens at most once per real
    // capture trigger (never every frame) - see this class's own header
    // comment. The pre-composite copy and the post-composite copy (when
    // present) happen inside this SAME ImmediateSubmit() call
    // (frame-debugger-4's own Locked Design Decision #8 - "one single
    // ImmediateSubmit() call... never N separate submissions") - a single
    // GPU submission + fence wait for the WHOLE CaptureFrame() call.
    //
    // task_manager/frame-debugger-7 campaign, PHASE4 - the frame-debugger-5
    // campaign's own THIRD copy loop (one more transition-copy-transition-
    // back sequence per discovered compute-pass write texture, plus a
    // separate CPU-round-trip loop for HDR compute-pass writes, plus a
    // separate ray-marched-volume-preview loop) was REMOVED outright this
    // phase - per-compute-pass distinct textures are no longer retained at
    // all (PHASE0's Locked Design Decision #3); every leaf now shows the
    // accumulated Game View image as of that exact step instead (`preview`/
    // `compositedPreview`/`perObjectStepPreviews`, picked by
    // `ChooseFrameDebuggerPreviewSource()`, FrameDebuggerData.h/.cpp).
    renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
        // Pre-composite copy - byte-for-byte the existing, already-correct
        // sequence, unchanged.
        rg::EmitImageBarrier(cmd, gameViewSource.Image(), range, shaderRead, transferSrc);
        rg::EmitImageBarrier(cmd, entry.preview->Image(), range, freshImageState, transferDst);

        VkImageCopy region{};
        region.srcSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.dstSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
        region.extent = VkExtent3D{ extent.width, extent.height, 1 };
        vkCmdCopyImage(cmd, gameViewSource.Image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, entry.preview->Image(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // Restore the source back to ShaderRead (a later ImGui sample of
        // the SAME live Game View texture this same frame must be
        // unaffected), and leave the retained copy in ShaderRead too, ready
        // for ImGui::Image() display.
        rg::EmitImageBarrier(cmd, gameViewSource.Image(), range, transferSrc, shaderRead);
        rg::EmitImageBarrier(cmd, entry.preview->Image(), range, transferDst, shaderRead);

        // (frame-debugger-4 campaign, PHASE1) - post-composite copy, only
        // when requested this capture.
        if (hasCompositedSource) {
            const VkExtent2D compositedExtent = compositedGameViewSource->Extent();
            rg::EmitImageBarrier(cmd, compositedGameViewSource->Image(), range, shaderRead, transferSrc);
            rg::EmitImageBarrier(cmd, entry.compositedPreview->Image(), range, freshImageState, transferDst);
            VkImageCopy compositedRegion{};
            compositedRegion.srcSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            compositedRegion.dstSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            compositedRegion.extent = VkExtent3D{ compositedExtent.width, compositedExtent.height, 1 };
            vkCmdCopyImage(cmd, compositedGameViewSource->Image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                entry.compositedPreview->Image(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &compositedRegion);
            rg::EmitImageBarrier(cmd, compositedGameViewSource->Image(), range, transferSrc, shaderRead);
            rg::EmitImageBarrier(cmd, entry.compositedPreview->Image(), range, transferDst, shaderRead);
        }
    });
}

} // namespace gte
