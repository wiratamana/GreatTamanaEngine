#pragma once

#include "OneShot.h"
#include "RenderGraphBarrierPlanner.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace gte::rg {

// The state a one-shot run must leave a texture in; nullopt keeps whatever the passes left.
inline std::optional<ResourceState> PlanFinalState(const ResourceState& initial, FinalState final)
{
    switch (final) {
    case FinalState::Restore:
        return initial;
    case FinalState::ShaderRead:
        return RequiredStateFor(ResourceAccess::ShaderRead, false);
    case FinalState::Keep:
        return std::nullopt;
    }
    return std::nullopt;
}

// Hands each touched texture's state to `set(index, state)`. Call it only after a successful run, so a failed
// run leaves every tracked state as it was. Untouched and out-of-range handles are skipped.
template <typename SetFn>
void WriteBackStates(std::span<const TextureHandle> handles, std::span<const std::optional<ResourceState>> states, SetFn&& set)
{
    for (std::size_t i = 0; i < handles.size(); ++i) {
        const std::uint32_t index = handles[i].index;
        if (index < states.size() && states[index].has_value()) {
            set(i, *states[index]);
        }
    }
}

} // namespace gte::rg
