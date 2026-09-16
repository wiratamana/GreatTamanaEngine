#include "FrameDebuggerHistory.h"

#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
// frame-debugger-5 campaign, PHASE5 (closing-phase live-smoke-test bug fix) -
// Encoding::ConvertHdrRgba16fToRgba8(), the SAME debug exposure/tonemap
// GET /get_texture already applies for this engine's one genuinely HDR 2D
// texture format (VK_FORMAT_R16G16B16A16_SFLOAT) - see the new
// HdrComputePassCopySource struct below for the full root-cause writeup.
#include "../Encoding/HdrColorVisualization.h"

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

// frame-debugger-5 campaign, PHASE5 (closing-phase live-smoke-test bug fix) -
// a discovered compute-pass write whose real, CURRENT physical texture
// format is this engine's one genuinely HDR 2D-texture format
// (VK_FORMAT_R16G16B16A16_SFLOAT - AtmosphereMultiScatteringLutPass/
// AtmosphereSkyViewLutPass/AtmosphereAerialPerspectiveVolumeDebugSlicePass).
// CONFIRMED LIVE during this phase's own required HTTP-driven smoke test: a
// plain vkCmdCopyImage preserving the raw HDR bits (ComputePassCopySource's
// own straight-copy path above) is technically correct - the bytes really
// are copied - but VISUALLY INDISTINGUISHABLE FROM SOLID BLACK once
// displayed by the Inspector's plain ImGui::Image(), because these LUTs'
// real physical magnitudes are tiny fractions of 1.0 (see
// src/Encoding/HdrColorVisualization.cpp's own "typically small in absolute
// terms" doc comment) - exactly the same reason GET /get_texture ALREADY
// applies a debug-only 400x exposure + Reinhard tonemap
// (Encoding::ConvertHdrRgba16fToRgba8()) before ever PNG-encoding this same
// format. Fixed here by reusing that EXACT SAME already-shipped, already-
// tested function (never a second, independently-maintained copy of its
// exposure math) via a CPU round-trip (Renderer::CaptureImagePixels() +
// ConvertHdrRgba16fToRgba8() + upload as VK_FORMAT_R8G8B8A8_UNORM), mirroring
// this same file's own volume-texture-preview upload sequence further below
// - see CaptureFrame()'s own body for the exact sequence. Kept as a SEPARATE
// struct/loop from ComputePassCopySource's straight-copy path (rather than a
// branch inside that same loop) so every existing, already-verified LDR
// (R8G8B8A8_UNORM/BGRA8_UNORM) compute-pass preview - Transmittance LUT, the
// Aerial Perspective Composite pass's own "GameViewComposited" write, any
// future non-HDR compute pass - keeps using the exact same zero-risk,
// single-ImmediateSubmit() GPU-to-GPU copy it already used before this fix,
// byte-for-byte unchanged.
struct HdrComputePassCopySource {
    std::string passName;
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

    // frame-debugger-5 campaign, PHASE5 (closing-phase live-smoke-test bug
    // fix) - every discovered write whose real, CURRENT format is this
    // engine's one genuinely HDR 2D-texture format is routed to a SEPARATE
    // CPU-round-trip loop further below (see HdrComputePassCopySource's own
    // doc comment above) instead of the straight-copy path every other
    // (LDR) compute-pass write still uses, unchanged.
    std::vector<HdrComputePassCopySource> hdrComputeCopySources;

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

        if (textureSnapshot->target.format == VK_FORMAT_R16G16B16A16_SFLOAT) {
            hdrComputeCopySources.push_back(HdrComputePassCopySource{ write.passName,
                textureSnapshot->target.image, textureSnapshot->target.extent, textureSnapshot->colorState });
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

    // frame-debugger-5 campaign, PHASE5 (closing-phase live-smoke-test bug
    // fix) - one CPU round-trip per discovered HDR
    // (VK_FORMAT_R16G16B16A16_SFLOAT) compute-pass 2D write, GENUINELY
    // SEPARATE from the main copy loop's single ImmediateSubmit() above
    // (mirrors the volume-texture ray-march loop's own identical "separate
    // submission, still on-demand-only" reasoning immediately below - see
    // HdrComputePassCopySource's own doc comment for the full root-cause
    // writeup). Renderer::CaptureImagePixels() reads back the source's real,
    // CURRENT tracked GPU state (never assuming ShaderRead), restoring it
    // afterward - byte-for-byte the same discipline the straight-copy path
    // above already applies. Encoding::ConvertHdrRgba16fToRgba8() is the
    // EXACT SAME, already-tested function GET /get_texture already uses for
    // this one format - never a second, independently-maintained copy of its
    // exposure/tonemap math.
    for (const HdrComputePassCopySource& hdrSource : hdrComputeCopySources) {
        const Renderer::CapturedRawPixels raw = renderer.CaptureImagePixels(hdrSource.image,
            VK_IMAGE_ASPECT_COLOR_BIT, VK_FORMAT_R16G16B16A16_SFLOAT, hdrSource.extent, hdrSource.colorState,
            /*bytesPerPixel=*/8);

        std::vector<std::uint8_t> convertedPixels(
            static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
        if (!Encoding::ConvertHdrRgba16fToRgba8(
                raw.pixels.data(), raw.format, raw.width, raw.height, convertedPixels.data())) {
            // Defensive only - raw.format is always exactly
            // VK_FORMAT_R16G16B16A16_SFLOAT here (the same value just
            // checked before this write was ever added to
            // hdrComputeCopySources above), so ConvertHdrRgba16fToRgba8()
            // can never actually reject it in practice - never crash or
            // fabricate a wrong-content entry if it somehow did.
            continue;
        }

        char hdrDebugNameBuffer[96];
        std::snprintf(hdrDebugNameBuffer, sizeof(hdrDebugNameBuffer), "FrameDebuggerHistorySlot%dCompute%.48s",
            writeIndex, hdrSource.passName.c_str());

        // Mirrors the volume-texture-preview upload sequence immediately
        // below exactly (fresh VK_FORMAT_R8G8B8A8_UNORM RenderTexture +
        // staging-buffer upload) - the SAME reason: this loop only has
        // CPU-side pixels ready to upload, not a live GPU image to
        // vkCmdCopyImage from directly.
        RenderTexture hdrPreviewTexture =
            renderer.CreateRenderTexture(raw.width, raw.height, VK_FORMAT_R8G8B8A8_UNORM, hdrDebugNameBuffer);

        const VkDeviceSize uploadSize =
            static_cast<VkDeviceSize>(raw.width) * static_cast<VkDeviceSize>(raw.height) * 4;
        Buffer stagingBuffer =
            renderer.CreateBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, BufferMemoryUsage::CpuToGpu);
        stagingBuffer.Upload(convertedPixels.data(), static_cast<std::size_t>(uploadSize));

        const VkImage hdrPreviewImage = hdrPreviewTexture.Image();
        renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
            rg::EmitImageBarrier(cmd, hdrPreviewImage, range, freshImageState, transferDst);

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            copyRegion.imageOffset = VkOffset3D{ 0, 0, 0 };
            copyRegion.imageExtent =
                VkExtent3D{ static_cast<std::uint32_t>(raw.width), static_cast<std::uint32_t>(raw.height), 1 };
            vkCmdCopyBufferToImage(
                cmd, stagingBuffer.Native(), hdrPreviewImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            // Leave the retained copy in ShaderRead, ready for
            // ImGui::Image() display - matching every other retained
            // preview texture in this method.
            rg::EmitImageBarrier(cmd, hdrPreviewImage, range, transferDst, shaderRead);
        });

        entry.computePassPreviews.push_back(
            FrameDebuggerComputePassPreview{ hdrSource.passName, std::move(hdrPreviewTexture) });
    }
    // NEW (frame-debugger-5 campaign, PHASE4
    // PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md) - Step A' (pure,
    // CPU-side, mirrors Step A above): discover every real, surviving
    // compute-dispatch pass's own FIRST VolumeTexture-kind write this same
    // frame - a SEPARATE list from computeWrites above (a pass could, in
    // principle, have both kinds of write in a future engine - not true of
    // any pass today).
    const std::vector<FrameDebuggerComputePassVolumeTextureWrite> computeVolumeWrites =
        CollectComputePassVolumeTextureWrites(graphSnapshot);

    for (const FrameDebuggerComputePassVolumeTextureWrite& volumeWrite : computeVolumeWrites) {
        const std::optional<rg::DebugVolumeTextureSnapshot> volumeSnapshot =
            renderGraph.DebugVolumeTextureSnapshotFor(volumeWrite.writeVolumeTextureName);
        if (!volumeSnapshot.has_value()) {
            // Defensive only (mirrors the 2D case above) - should not happen
            // for a name just read straight out of this SAME frame's own
            // graphSnapshot, but never crash / fabricate an entry if it does.
            continue;
        }

        // The SAME interpretation-selection rule GET /get_texture's own
        // volume branch already uses (Application.cpp) - extracted into one
        // shared, named, pure function so both real call sites agree
        // (VolumeTexturePreviewRenderer.h's SelectVolumeTexturePreviewInterpretation()).
        const VolumeTexturePreviewInterpretation interpretation =
            SelectVolumeTexturePreviewInterpretation(volumeWrite.writeVolumeTextureName);

        // RenderPreview() is its OWN self-contained ImmediateSubmit() call
        // (plus a second, internal one of its own for the CPU readback) -
        // genuinely SEPARATE from the main copy-loop's single
        // ImmediateSubmit() above. frame-debugger-4's own Locked Design
        // Decision #8 ("one single ImmediateSubmit() call... never N
        // separate submissions") is specifically about that main
        // whole-frame + per-2D-pass copy loop - it does not apply to this
        // deliberately self-contained utility class this phase merely calls
        // into (see PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md's own
        // Step 3.2 item 3). This means one CaptureFrame() call, once
        // volume-writing passes exist, now issues MORE THAN ONE separate GPU
        // submission total - a deliberate, accepted, still genuinely
        // ON-DEMAND cost (this whole method only runs on Enable-edge/Step/
        // explicit-Capture-click, never per real frame).
        const VolumeTexturePreviewRenderer::CapturedRawPixels raw = m_volumePreviewRenderer.RenderPreview(
            renderer, volumeSnapshot->target, volumeSnapshot->state, interpretation);

        char volumeDebugNameBuffer[96];
        std::snprintf(volumeDebugNameBuffer, sizeof(volumeDebugNameBuffer),
            "FrameDebuggerHistorySlot%dCompute%.48s", writeIndex, volumeWrite.passName.c_str());

        // FrameDebuggerComputePassPreview::preview is a plain RenderTexture
        // (PHASE3), never a Texture2D - RenderPreview() only returns
        // CPU-side pixels (it was built for an HTTP JSON response, not a
        // live GPU texture ready for ImGui::Image()), so this phase uploads
        // them into a freshly created RenderTexture via a staging buffer,
        // mirroring GpuResourceFactory::CreateTexture2D()'s own internal
        // upload sequence exactly (UNDEFINED -> TRANSFER_DST ->
        // SHADER_READ_ONLY_OPTIMAL) - see PHASE4's own Step 3.2 item 4 for
        // why this (rather than widening FrameDebuggerComputePassPreview
        // into a tagged union) is the resolved shape.
        RenderTexture volumePreviewTexture =
            renderer.CreateRenderTexture(raw.width, raw.height, VK_FORMAT_R8G8B8A8_UNORM, volumeDebugNameBuffer);

        const VkDeviceSize uploadSize =
            static_cast<VkDeviceSize>(raw.width) * static_cast<VkDeviceSize>(raw.height) * 4;
        Buffer stagingBuffer =
            renderer.CreateBuffer(uploadSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, BufferMemoryUsage::CpuToGpu);
        stagingBuffer.Upload(raw.pixels.data(), static_cast<std::size_t>(uploadSize));

        const VkImage volumePreviewImage = volumePreviewTexture.Image();
        renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
            rg::EmitImageBarrier(cmd, volumePreviewImage, range, freshImageState, transferDst);

            VkBufferImageCopy copyRegion{};
            copyRegion.bufferOffset = 0;
            copyRegion.bufferRowLength = 0;
            copyRegion.bufferImageHeight = 0;
            copyRegion.imageSubresource = VkImageSubresourceLayers{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            copyRegion.imageOffset = VkOffset3D{ 0, 0, 0 };
            copyRegion.imageExtent =
                VkExtent3D{ static_cast<std::uint32_t>(raw.width), static_cast<std::uint32_t>(raw.height), 1 };
            vkCmdCopyBufferToImage(
                cmd, stagingBuffer.Native(), volumePreviewImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion);

            // Leave the retained copy in ShaderRead, ready for
            // ImGui::Image() display - matching every other retained
            // preview texture in this method.
            rg::EmitImageBarrier(cmd, volumePreviewImage, range, transferDst, shaderRead);
        });

        entry.computePassPreviews.push_back(
            FrameDebuggerComputePassPreview{ volumeWrite.passName, std::move(volumePreviewTexture) });
    }

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
