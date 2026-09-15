#include "FrameDebuggerHistory.h"

#include "../Renderer/Renderer.h"
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

void FrameDebuggerHistory::CaptureFrame(
    Renderer& renderer, const FrameDebuggerSnapshot& snapshot, RenderTexture& gameViewSource)
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
    // comment.
    renderer.ImmediateSubmit([&](VkCommandBuffer cmd) {
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
