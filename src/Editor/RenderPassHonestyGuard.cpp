#include "RenderPassHonestyGuard.h"

namespace gte {

std::string RenderPassHonestyGuardTag::FormatMessage(const std::string& passName)
{
    return "Pass '" + passName
        + "' is marked DISABLED in RenderPassToggleRegistry but still executed and appears in this frame's "
          "captured Render Graph snapshot - the render pass and the Frame Debugger disagree.";
}

} // namespace gte
