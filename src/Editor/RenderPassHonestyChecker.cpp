#include "RenderPassHonestyChecker.h"

namespace gte {

std::vector<std::string> DetectRenderPassHonestyMismatches(
    const std::vector<rg::RenderGraphPassSnapshot>& passes,
    const std::function<bool(const std::string&)>& isEnabledLookup)
{
    std::vector<std::string> mismatches;

    if (!isEnabledLookup) {
        return mismatches;
    }

    for (const rg::RenderGraphPassSnapshot& pass : passes) {
        if (pass.isCulled) {
            // A culled pass is a real, legitimate, unrelated reason a pass
            // never ran this frame - never a "the toggle registry is being
            // ignored" contradiction, regardless of what its own toggle
            // entry reports.
            continue;
        }

        if (!isEnabledLookup(pass.name)) {
            // Non-culled (genuinely executed/would-be-shown) AND the
            // registry reports it disabled - the render pass and the Frame
            // Debugger disagree.
            mismatches.push_back(pass.name);
        }
    }

    return mismatches;
}

} // namespace gte
