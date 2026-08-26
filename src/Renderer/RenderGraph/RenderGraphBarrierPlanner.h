#pragma once

// Phase 5 (RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md, part 5 of
// the wider RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md campaign) - "Automatic
// GPU Safety Rules": replaces FrameRecorder::RecordFrame()'s hand-written,
// fixed-shape barrier code (exactly one color image, one optional depth
// image, oldLayout always VK_IMAGE_LAYOUT_UNDEFINED, finalLayout always one
// of exactly two hardcoded choices - see src/Renderer/FrameRecorder.cpp)
// with a small, DATA-DRIVEN barrier planner: given a resource's PREVIOUS
// known state (layout, access mask, pipeline stage) and its NEXT declared
// ResourceAccess (Phase 1 - see RenderGraphTypes.h), produce the exact
// VkImageMemoryBarrier2/VkBufferMemoryBarrier2 needed to transition between
// them - correctly, for an arbitrary number of passes touching an arbitrary
// number of resources in an arbitrary order.
//
// Split into a PURE decision half (Tier-1-testable - RequiredStateFor()/
// RequiresBarrier()/BuildImageMemoryBarrier2()/BuildBufferMemoryBarrier2(),
// none of which ever touch a live VkDevice/VkCommandBuffer - constructing a
// VkImageMemoryBarrier2/VkBufferMemoryBarrier2 is just populating a plain
// POD struct, no Vulkan call involved) and a THIN Vulkan-call half
// (EmitImageBarrier()/EmitBufferBarrier(), Tier 2 - simply building via the
// pure half above and then calling vkCmdPipelineBarrier2) - the exact same
// split this campaign's own Phase 1 established for ResourceAccess, and the
// exact same split GpuTiming.h (pure) / GpuTimingService.cpp (Vulkan calls)
// already proved out successfully in this same codebase.
//
// Per-resource state TRACKING across a whole compiled pass list (walking
// CompiledGraph::executionOrder, calling RequiredStateFor()/RequiresBarrier()
// for every declared read/write, and overwriting the tracked "current state"
// as it goes) is explicitly Phase 6's job (the execution engine), not this
// file's - this file only provides the two pure per-transition decisions
// and the thin Vulkan-call wrappers Phase 6 will drive in a loop. See
// RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md, Step 3.2.
//
// MVP scope is a SINGLE color attachment plus an optional depth attachment
// per pass - full MRT (multi-color-attachment) support is explicitly out of
// scope here and moved to Phase 9, alongside the matching Pipeline
// multi-format-attachment change it actually requires (see that phase's own
// V2 Revision Note 1). This file has no attachment-COUNT concept at all -
// it operates purely per-resource, so it is unaffected either way; the
// "single color attachment" constraint lives entirely in Phase 6's
// PassContext/RenderTarget resolution, not here.
//
// Nothing outside src/Renderer/RenderGraph/ includes this header yet, and
// nothing calls into it from production code yet - Phase 6 is the first
// real consumer.

#include "RenderGraphTypes.h"

#include <volk.h>

namespace gte::rg {

// A resource's known GPU-visible state at one point in time - what layout
// it's actually in (images only - see below), and what pipeline stage/
// access mask last touched it (or will next touch it). This is the whole
// "previous" / "next" pair RequiresBarrier()/BuildImageMemoryBarrier2()/
// BuildBufferMemoryBarrier2() operate on.
//
// `layout` is meaningless for a BUFFER transition (buffers have no concept
// of image layout) - callers building a buffer-to-buffer transition simply
// leave it at its default (VK_IMAGE_LAYOUT_UNDEFINED) and
// BuildBufferMemoryBarrier2() never reads it. This mirrors the strategy
// document's own "buffers use a dummy/ignored value here" note (Step 3.1).
struct ResourceState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2 accessMask = VK_ACCESS_2_NONE;

    friend bool operator==(const ResourceState&, const ResourceState&) noexcept = default;
};

// Pure decision: what GPU-visible state does a resource need to be in for a
// pass to legally perform `access` against it? A straightforward,
// EXHAUSTIVE switch over ResourceAccess (Phase 1) - deliberately NO
// `default:` case, mirroring IsWriteAccess()/ToString()'s own rule in
// RenderGraphTypes.h/.cpp, so a future ResourceAccess enumerator added
// without updating this function fails to compile here until it is.
//
// `isDepthResource` is asserted against `access` in debug builds (a
// ColorAttachmentWrite request against a depth resource, or a
// DepthStencilAttachmentReadWrite request against a non-depth resource, is
// always a caller mistake - these two ResourceAccess values are already
// mutually exclusive by name, so this parameter exists purely as a
// same-call-site sanity check, not because it changes either access
// kind's own resulting ResourceState) - both asserts are compiled out
// entirely in a release (NDEBUG) build, zero cost. For ShaderRead/
// TransferSrc/TransferDst, `isDepthResource` has no effect at all (either a
// depth or a color resource can legally be sampled/copied) - callers pass
// whatever is actually true for documentation purposes only.
//
// This function's job stops at "what state is needed" - it does NOT decide
// whether a barrier is actually required to reach it (see RequiresBarrier()
// below) or emit anything (see EmitImageBarrier()/EmitBufferBarrier()
// below).
ResourceState RequiredStateFor(ResourceAccess access, bool isDepthResource) noexcept;

// Pure decision: is a barrier even NEEDED to transition a resource from
// `previous` to `next`? A resource read by two consecutive ShaderRead
// passes with an IDENTICAL layout/stage/access needs no barrier at all
// between them - a well-known Vulkan optimization (see the Vulkan spec's
// own synchronization guidance: a memory dependency that changes nothing
// is a no-op) this planner must not skip. Pure value equality - true for
// ANY difference in layout, stage mask, or access mask alone.
bool RequiresBarrier(const ResourceState& previous, const ResourceState& next) noexcept;

// Pure: populates a VkImageMemoryBarrier2 from two already-decided
// ResourceStates - no Vulkan call of any kind, just filling in a plain POD
// struct's fields (srcQueueFamilyIndex/dstQueueFamilyIndex are always
// VK_QUEUE_FAMILY_IGNORED - see "What We Will NOT Do": no queue-family-
// ownership-transfer support in the MVP). This is what makes the exact
// barrier-field values this function produces Tier-1-testable without any
// live VkDevice/VkCommandBuffer at all - see
// tests/Renderer/RenderGraph/RenderGraphBarrierPlannerTests.cpp.
VkImageMemoryBarrier2 BuildImageMemoryBarrier2(
    VkImage image, VkImageSubresourceRange subresourceRange, const ResourceState& previous, const ResourceState& next) noexcept;

// Pure buffer counterpart - see BuildImageMemoryBarrier2() above. `layout`
// on either ResourceState is ignored entirely (see this file's own
// ResourceState comment).
VkBufferMemoryBarrier2 BuildBufferMemoryBarrier2(
    VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, const ResourceState& previous, const ResourceState& next) noexcept;

// Thin Vulkan-call half: builds the barrier via BuildImageMemoryBarrier2()
// above and issues it via a single vkCmdPipelineBarrier2 call. Contains NO
// decision logic of its own - callers are expected to have already checked
// RequiresBarrier() themselves if they want to skip a redundant call
// (mirroring GpuTimingService's own "thin" recording methods, which never
// decide anything RequiredStateFor()/ResolveGpuTimingStatus() didn't
// already decide for them).
void EmitImageBarrier(
    VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange subresourceRange, const ResourceState& previous, const ResourceState& next);

// Thin Vulkan-call buffer counterpart - see EmitImageBarrier() above.
void EmitBufferBarrier(
    VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, const ResourceState& previous, const ResourceState& next);

} // namespace gte::rg
