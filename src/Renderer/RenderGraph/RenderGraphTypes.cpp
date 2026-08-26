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
    }
    return "Unknown";
}

} // namespace gte::rg
