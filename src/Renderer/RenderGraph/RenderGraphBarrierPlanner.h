#pragma once

// Synchronization2 barrier planner: builds the barrier for a resource's transition from its previous
// state to the next declared access. Pure half: RequiredStateFor, RequiresBarrier, Build*Barrier2.
// Thin half: Emit*Barrier. Tracking state across passes is the executor's job.

#include "RenderGraphTypes.h"

#include <volk.h>

#include <cstdint>
#include <string>


namespace gte::rg {

// A resource's GPU-visible state at one point: image layout plus the last (or next) stage and access.
// `layout` is ignored for buffers; leave it UNDEFINED.
struct ResourceState {
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    VkAccessFlags2 accessMask = VK_ACCESS_2_NONE;

    friend bool operator==(const ResourceState&, const ResourceState&) noexcept = default;
};

// The state a resource must be in for a pass to perform `access`. Exhaustive switch without a default.
// `isDepthResource` is a debug-only sanity check against `access`; it does not change the result.
ResourceState RequiredStateFor(ResourceAccess access, bool isDepthResource) noexcept;

// True when `access` targets a texture's depth state, false for its color state. Only
// DepthStencilAttachmentReadWrite targets depth; compute storage accesses target color.
// Meaningless for buffers.
bool TargetsDepthState(ResourceAccess access) noexcept;

// True when `access`, declared as a texture write, needs a color-attachment rendering bracket.
// Only ColorAttachmentWrite does; compute storage writes do not.
bool IsColorAttachmentWriteAccess(ResourceAccess access) noexcept;

// True when any of layout, stage mask or access mask differs; identical consecutive states need no barrier.
bool RequiresBarrier(const ResourceState& previous, const ResourceState& next) noexcept;

// Per-layer counterpart of RequiresBarrier() for a usage that targets one array layer. Does not
// mutate `layerState`; the caller updates the tracked state after emitting.
struct TextureArraySubresourceDecision {
    bool requiresBarrier = false;
    VkImageSubresourceRange range{};
};

TextureArraySubresourceDecision DecideTextureArrayLayerTransition(
    const ResourceState& layerState, VkImageAspectFlags aspect, std::uint32_t layerIndex,
    const ResourceState& next) noexcept;

// Fills a VkImageMemoryBarrier2 from two decided states. No Vulkan call; queue family ownership
// transfer is not supported (both indices are VK_QUEUE_FAMILY_IGNORED).
VkImageMemoryBarrier2 BuildImageMemoryBarrier2(
    VkImage image, VkImageSubresourceRange subresourceRange, const ResourceState& previous, const ResourceState& next) noexcept;

// Buffer counterpart; `layout` is ignored.
VkBufferMemoryBarrier2 BuildBufferMemoryBarrier2(
    VkBuffer buffer, uint64_t offset, uint64_t size, const ResourceState& previous, const ResourceState& next) noexcept;

// Builds the barrier and issues it with one vkCmdPipelineBarrier2. No decision logic: callers check
// RequiresBarrier() first when they want to skip a redundant call.
void EmitImageBarrier(
    VkCommandBuffer cmd, VkImage image, VkImageSubresourceRange subresourceRange, const ResourceState& previous, const ResourceState& next);

void EmitBufferBarrier(
    VkCommandBuffer cmd, VkBuffer buffer, uint64_t offset, uint64_t size, const ResourceState& previous, const ResourceState& next);

// Short editor label for an image layout (e.g. "RT", "Shader Read"); "Other" when unlisted.
const char* ShortLayoutLabel(VkImageLayout layout) noexcept;

// "<old> -> <new>" label for the editor Inspector, built once at execute time.
inline std::string BarrierTransitionLabel(VkImageLayout oldLayout, VkImageLayout newLayout)
{
    return std::string(ShortLayoutLabel(oldLayout)) + " -> " + ShortLayoutLabel(newLayout);
}

} // namespace gte::rg
