#include "RenderGraphTypes.h"

namespace gte::rg {

bool IsWriteAccess(ResourceAccess access) noexcept
{
    // Deliberately NO `default:` case - see RenderGraphTypes.h's own
    // comment on this function: a future ResourceAccess enumerator added
    // without updating this switch must produce a compiler warning (this
    // engine's convention for every exhaustive enum switch - see
    // GpuTiming.cpp/FrameGraphData.cpp for the same precedent), not
    // silently fall through to some default answer.
    switch (access) {
    case ResourceAccess::ColorAttachmentWrite:
        return true;
    case ResourceAccess::DepthStencilAttachmentReadWrite:
        return true; // Reads AND writes - counts as a write for dependency-ordering purposes.
    case ResourceAccess::ShaderRead:
        return false;
    case ResourceAccess::TransferSrc:
        return false; // The SOURCE of a copy is only ever read.
    case ResourceAccess::TransferDst:
        return true; // The DESTINATION of a copy is written.
    case ResourceAccess::ComputeShaderRead:
        return false;
    case ResourceAccess::ComputeShaderWrite:
        return true;
    case ResourceAccess::IndirectCommandRead:
        return false; // The indirect-draw buffer is only ever READ by vkCmdDraw(Indexed)Indirect.
    case ResourceAccess::VertexBufferRead:
        return false; // A vertex buffer bound for drawing is only ever READ by the vertex-input stage.
    }
    return false;
}

const char* ToString(ResourceAccess access) noexcept
{
    // Deliberately NO `default:` case - see IsWriteAccess() above.
    switch (access) {
    case ResourceAccess::ColorAttachmentWrite:
        return "ColorAttachmentWrite";
    case ResourceAccess::DepthStencilAttachmentReadWrite:
        return "DepthStencilAttachmentReadWrite";
    case ResourceAccess::ShaderRead:
        return "ShaderRead";
    case ResourceAccess::TransferSrc:
        return "TransferSrc";
    case ResourceAccess::TransferDst:
        return "TransferDst";
    case ResourceAccess::ComputeShaderRead:
        return "ComputeShaderRead";
    case ResourceAccess::ComputeShaderWrite:
        return "ComputeShaderWrite";
    case ResourceAccess::IndirectCommandRead:
        return "IndirectCommandRead";
    case ResourceAccess::VertexBufferRead:
        return "VertexBufferRead";
    }
    return "Unknown";
}

// Render Pass campaign (task_manager/render-pass-1), PHASE1 - see
// RenderGraphTypes.h's own comment on PassKind for why this exists.
const char* ToString(PassKind kind) noexcept
{
    // Deliberately NO `default:` case - see this file's own header comment.
    switch (kind) {
    case PassKind::Graphics:
        return "Graphics";
    case PassKind::Compute:
        return "Compute";
    }
    return "Unknown";
}

// Render Pass campaign, PHASE1 - see RenderGraphTypes.h's own comment on
// RenderPassCategory for why this exists.
const char* ToString(RenderPassCategory category) noexcept
{
    // Deliberately NO `default:` case - see this file's own header comment.
    switch (category) {
    case RenderPassCategory::General:
        return "General";
    case RenderPassCategory::AtmosphereLut:
        return "AtmosphereLut";
    case RenderPassCategory::GpuSkinning:
        return "GpuSkinning";
    case RenderPassCategory::Debug:
        return "Debug";
    }
    return "Unknown";
}

// Frame Debugger Pass-Ownership campaign (task_manager/render-pass-2),
// PHASE1 - see RenderGraphTypes.h's own comment on RenderPassDrawKind for
// why this exists.
const char* ToString(RenderPassDrawKind drawKind) noexcept
{
    // Deliberately NO `default:` case - see this file's own header comment.
    switch (drawKind) {
    case RenderPassDrawKind::DrawMesh:
        return "DrawMesh";
    case RenderPassDrawKind::DrawQuad:
        return "DrawQuad";
    case RenderPassDrawKind::Blit:
        return "Blit";
    }
    return "Unknown";
}

// render-pass-3 campaign (task_manager/render-pass-3), PHASE1 - see
// RenderGraphTypes.h's own comment on RenderPassEvent for why this exists.
const char* ToString(RenderPassEvent renderPassEvent) noexcept
{
    // Deliberately NO `default:` case - see this file's own header comment.
    switch (renderPassEvent) {
    case RenderPassEvent::BeforeEverything:
        return "BeforeEverything";
    case RenderPassEvent::PreOpaques:
        return "PreOpaques";
    case RenderPassEvent::Opaques:
        return "Opaques";
    case RenderPassEvent::AfterOpaques:
        return "AfterOpaques";
    case RenderPassEvent::Transparents:
        return "Transparents";
    case RenderPassEvent::AfterTransparents:
        return "AfterTransparents";
    case RenderPassEvent::AfterEverything:
        return "AfterEverything";
    }
    return "Unknown";
}

// Multi-Render-Target (MRT) campaign (task_manager/mrt-1), PHASE2 - see
// RenderGraphTypes.h's own doc comment on this function. Plain value
// comparison against entry 0 - no Vulkan call, no live VkDevice/VkImage
// involved, which is exactly why this is Tier-1-testable (see
// tests/Renderer/RenderGraph/RenderGraphTypesTests.cpp).
std::optional<std::size_t> FindMismatchedColorAttachmentExtent(const std::vector<VkExtent2D>& extents) noexcept
{
    if (extents.size() < 2) {
        return std::nullopt; // 0 or 1 extent - nothing to compare against, vacuously no mismatch.
    }
    const VkExtent2D& first = extents[0];
    for (std::size_t i = 1; i < extents.size(); ++i) {
        if (extents[i].width != first.width || extents[i].height != first.height) {
            return i;
        }
    }
    return std::nullopt;
}

} // namespace gte::rg
