#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

namespace gte {

// Enabled flag of one feature per view scope. The shared scope is never gated.
struct ViewScopeEnable {
    bool primary = true;
    bool secondary = true;

    constexpr bool Allows(rg::ViewScope scope) const noexcept
    {
        switch (scope) {
        case rg::ViewScope::Primary:
            return primary;
        case rg::ViewScope::Secondary:
            return secondary;
        case rg::ViewScope::Shared:
            break;
        }
        return true;
    }

    // The shared scope has no flag; setting it does nothing.
    constexpr void Set(rg::ViewScope scope, bool enabled) noexcept
    {
        if (scope == rg::ViewScope::Primary) {
            primary = enabled;
        } else if (scope == rg::ViewScope::Secondary) {
            secondary = enabled;
        }
    }
};

// A feature runs in a scope when its global override and the scope flag are both on.
constexpr bool RunsInScope(bool globalEnabled, const ViewScopeEnable& flags, rg::ViewScope scope) noexcept
{
    return globalEnabled && flags.Allows(scope);
}

} // namespace gte
