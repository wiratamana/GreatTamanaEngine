#pragma once

#include "RenderPassToggleRegistry.h"

#include <cstddef>
#include <vector>

namespace gte::rg {

// True when any pass's (name, enabled) pair differs between two RenderPassToggleRegistry::ListAll()
// snapshots (`everDeclaredThisSession` is ignored). Both are sorted by name, so an index-aligned
// compare suffices; a size mismatch counts as changed.
inline bool DidRenderPassToggleEnabledStatesChange(
    const std::vector<RenderPassToggleState>& before, const std::vector<RenderPassToggleState>& after) noexcept
{
    if (before.size() != after.size()) {
        return true;
    }
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (before[i].name != after[i].name || before[i].enabled != after[i].enabled) {
            return true;
        }
    }
    return false;
}

} // namespace gte::rg
