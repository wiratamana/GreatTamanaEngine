#include "FrameDebuggerHistory.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"

#include <algorithm>
#include <cstdio>

namespace gte {

FrameDebuggerHistoryWriteState AdvanceFrameDebuggerHistoryWriteState(
    FrameDebuggerHistoryWriteState state, int capacity) noexcept
{
    state.nextWriteIndex = (state.nextWriteIndex + 1) % capacity;
    if (state.count < capacity) {
        ++state.count;
    }
    return state;
}

int ClampFrameDebuggerHistoryCursor(int cursor, int count) noexcept
{
    if (count <= 0) {
        return 0;
    }
    return std::clamp(cursor, 0, count - 1);
}

int FrameDebuggerHistory::StorageIndexForLogicalIndex(int logicalIndex) const noexcept
{
    // When the ring buffer hasn't wrapped yet (count < kCapacity),
    // nextWriteIndex == count and logical index 0 (the oldest entry) is
    // simply storage slot 0 - the `base` below correctly resolves to 0 in
    // that case. Once full (count == kCapacity), nextWriteIndex is exactly
    // the slot about to be overwritten NEXT - i.e. the CURRENT oldest entry
    // - so it is the correct base for logical index 0 in that regime too.
    const int base = (m_writeState.count == kCapacity) ? m_writeState.nextWriteIndex : 0;
    return (base + logicalIndex) % kCapacity;
}

namespace {

// frame-debugger-5 campaign, PHASE3 - one discovered compute-pass write's
// real, CURRENT physical source (image/extent) plus its own real, currently
// TRACKED GPU state (colorState) - resolved once, up front (Step B, before
// any Vulkan barrier call), then consumed inside the single ImmediateSubmit()
// lambda below. Deliberately a small, local, capture-scoped struct - never
// exposed outside this .cpp file.
struct ComputePassCopySource {
    VkImage image = VK_NULL_HANDLE;
    VkExtent2D extent{};
    rg::ResourceState colorState;
};

} // namespace

void FrameDebuggerHistory::CaptureFrame(Renderer& renderer, const rg::RenderGraph& renderGraph,
    const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource, RenderTexture* compositedGameViewSource)
{
    const int writeIndex = m_writeState.nextWriteIndex;
    FrameDebuggerHistoryEntry& entry = m_entries[static_cast<std::size_t>(writeIndex)];
    entry.snapshot = snapshot;

    const VkExtent2D extent = gameViewSource.Extent();

    // Always freshly (re)created, never Resize()d in place - the simplest
    // way to guarantee this slot's own retained texture matches
    // gameViewSource's CURRENT size/format exactly on every single real
    // capture (the live Game View can be resized between two captures that
    // both happen to land on the same ring-buffer slot index), and it
    // sidesteps needing to track this slot's own previous VkImageLayout
    // across many past captures: a freshly (re)created RenderTexture's
    // color image always starts life in VK_IMAGE_LAYOUT_UNDEFINED (see
    // RenderTexture.cpp's own Create()), so the destination barrier below
    // never needs to know or trust what state this slot was left in last
    // time. "Lazy" in the sense this file's own header comment/PHASE3's own
    // plan describes still holds: no RenderTexture is ever created for a
    // slot index that has never actually been captured into.
    char debugNameBuffer[48];
    std::snprintf(debugNameBuffer, sizeof(debugNameBuffer), "FrameDebuggerHistorySlot%d", writeIndex);
    entry.preview.emplace(renderer.CreateRenderTexture(static_cast<int>(extent.width),
        static_cast<int>(extent.height), gameViewSource.Format(), debugNameBuffer));

    // NEW (frame-debugger-4 campaign, PHASE1) - the second retained copy,
    // only when a real composited source exists this capture. Uses the
    // COMPOSITED source's own extent/format (which may legitimately differ
    // in size from gameViewSource's own extent between two captures if a
    // resize landed asymmetrically - in practice both always match the SAME
    // Game View panel's current content-region size, but this function must
    // not assume that). A distinct debug-name suffix ("Composited") keeps
    // GPU-memory-debugger tooling (if any reads RenderTexture debug names)
    // able to tell the two apart.
    const bool hasCompositedSource = (compositedGameViewSource != nullptr);
    char compositedDebugNameBuffer[48];
    if (hasCompositedSource) {
        std::snprintf(compositedDebugNameBuffer, sizeof(compositedDebugNameBuffer),
            "FrameDebuggerHistorySlot%dComposited", writeIndex);
        const VkExtent2D compositedExtent = compositedGameViewSource->Extent();
        entry.compositedPreview.emplace(renderer.CreateRenderTexture(static_cast<int>(compositedExtent.width),
            static_cast<int>(compositedExtent.height), compositedGameViewSource->Format(), compositedDebugNameBuffer));
    } else {
        entry.compositedPreview.reset();
    }

    // NEW (frame-debugger-5 campaign, PHASE3
    // PHASE3_GENERIC_PER_PASS_RETAINED_PREVIEW_CAPTURE.md, Step 3.3) -
    // Step A (pure, CPU-side, before touching Vulkan at all): re-fetch THIS
    // SAME frame's own already-built rg::RenderGraphSnapshot (the identical
    // snapshot FrameDebuggerPanel::TriggerCapture() already built moments ago
    // to construct `snapshot` above) and discover every real, surviving
    // compute-dispatch pass's own FIRST Texture-kind write.
    const rg::RenderGraphSnapshot graphSnapshot =
        renderGraph.LastSnapshot(rg::ExecuteTimingMode::SynchronousImmediateReadback);
    const std::vector<FrameDebuggerComputePassTextureWrite> computeWrites =
        CollectComputePassTextureWrites(graphSnapshot);

    // Step B: resolve each discovered write-texture name into its real,
    // CURRENT physical texture + tracked GPU state via the already-existing
    // RenderGraphDebugTextureRegistry (RenderGraph::DebugTextureSnapshotFor())
    // - the SAME registry GET /get_texture/GET /list_textures already rely
    // on. entry.computePassPreviews is entirely REBUILT from scratch every
    // single real capture (see that field's own doc comment,
    // FrameDebuggerHistory.h) - never merely appended to.
    entry.computePassPreviews.clear();
    std::vector<ComputePassCopySource> computeCopySources;
    computeCopySources.reserve(computeWrites.size());

    for (const FrameDebuggerComputePassTextureWrite& write : computeWrites) {
        const std::optional<rg::DebugTextureSnapshot> textureSnapshot =
            renderGraph.DebugTextureSnapshotFor(write.writeTextureName);
        if (!textureSnapshot.has_value()) {
            // Defensive only (see this method's own header-comment contract)
            // - should not happen for a name just read straight out of this
            // SAME frame's own graphSnapshot, but never crash / fabricate an
            // entry if it somehow does.
            continue;
        }

        char computeDebugNameBuffer[96];
        std::snprintf(computeDebugNameBuffer, sizeof(computeDebugNameBuffer),
            "FrameDebuggerHistorySlot%dCompute%.48s", writeIndex, write.passName.c_str());

        // Aggregate-initialized directly (NOT default-constructed then
        // assigned) - RenderTexture (FrameDebuggerComputePassPreview::preview)
        // has no default constructor at all, so this member must be
        // initialized directly from CreateRenderTexture()'s own return value
        // via aggregate list-initialization (guaranteed copy elision, C++17)
        // rather than default-construct-then-move-assign.
        FrameDebuggerComputePassPreview computePreview{ write.passName,
            renderer.CreateRenderTexture(static_cast<int>(textureSnapshot->target.extent.width),
                static_cast<int>(textureSnapshot->target.extent.height), textureSnapshot->target.format,
                computeDebugNameBuffer) };

        computeCopySources.push_back(
            ComputePassCopySource{ textureSnapshot->target.image, textureSnapshot->target.extent,
                textureSnapshot->colorState });
        entry.computePassPreviews.push_back(std::move(computePreview));
    }

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
    // comment. The pre-composite copy, the post-composite copy (when
    // present), AND (frame-debugger-5, PHASE3) every discovered compute-pass
    // copy all happen inside this SAME ImmediateSubmit() call
    // (frame-debugger-4's own Locked Design Decision #8 - "one single
    // ImmediateSubmit() call... never N separate submissions") - a single
    // GPU submission + fence wait for the WHOLE CaptureFrame() call
    // (2 + computeCopySources.size() total copies), never several.
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
        // for PHASE4's own ImGui::Image() display.
        rg::EmitImageBarrier(cmd, gameViewSource.Image(), range, transferSrc, shaderRead);
        rg::EmitImageBarrier(cmd, entry.preview->Image(), range, transferDst, shaderRead);

        // NEW (frame-debugger-4 campaign, PHASE1) - post-composite copy,
        // only when requested this capture.
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

        // NEW (frame-debugger-5 campaign, PHASE3) - one more transition-copy-
        // transition-back sequence per discovered compute-pass write texture.
        // CRITICAL: the SOURCE's "previous" state for the first barrier below
        // is `source.colorState` - this pass's own REAL, CURRENTLY-TRACKED
        // GPU state, read straight out of the registry (Step B above) -
        // deliberately NEVER assuming ShaderRead the way the two hardcoded
        // copies above do. An arbitrary compute pass's output may legitimately
        // be left in a different tracked state (e.g. General/ShaderReadWrite
        // for a storage image a later pass still needs to read/write) - and
        // the source is restored back to that SAME real state afterward
        // (never left in TransferSrc for a later graph-recorded frame to trip
        // over), mirroring AtmosphereLutRenderer's own
        // FinalizeAerialPerspectiveCompositeForSampling() "restore afterward"
        // discipline. The retained DESTINATION copy itself is left in
        // ShaderRead (matching the other two retained copies above), ready
        // for ImGui::Image() display.
        for (std::size_t i = 0; i < computeCopySources.size(); ++i) {
            const ComputePassCopySource& source = computeCopySources[i];
            const RenderTexture& destination = entry.computePassPreviews[i].preview;

            rg::EmitImageBarrier(cmd, source.image, range, source.colorState, transferSrc);
            rg::EmitImageBarrier(cmd, destination.Image(), range, freshImageState, transferDst);

            VkImageCopy computeRegion{};
            computeRegion.srcSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            computeRegion.dstSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            computeRegion.extent = VkExtent3D{ source.extent.width, source.extent.height, 1 };
            vkCmdCopyImage(cmd, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, destination.Image(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &computeRegion);

            rg::EmitImageBarrier(cmd, source.image, range, transferSrc, source.colorState);
            rg::EmitImageBarrier(cmd, destination.Image(), range, transferDst, shaderRead);
        }
    });

    m_writeState = AdvanceFrameDebuggerHistoryWriteState(m_writeState, kCapacity);
    m_cursor = ClampFrameDebuggerHistoryCursor(m_writeState.count - 1, m_writeState.count);
}

void FrameDebuggerHistory::StepCursor(int delta) noexcept
{
    m_cursor = ClampFrameDebuggerHistoryCursor(m_cursor + delta, m_writeState.count);
}

const FrameDebuggerHistoryEntry* FrameDebuggerHistory::CurrentEntry() const noexcept
{
    if (m_writeState.count <= 0) {
        return nullptr;
    }
    return &m_entries[static_cast<std::size_t>(StorageIndexForLogicalIndex(m_cursor))];
}

} // namespace gte
