#pragma once

#include "RenderGraphHandles.h"

#include <cassert>
#include <cstddef>

namespace gte::rg {

// Array index of a scope. Shared has its own index; an unknown value never aliases another scope.
constexpr std::size_t ViewScopeIndex(ViewScope scope) noexcept
{
    const std::size_t index = static_cast<std::size_t>(scope);
    assert(index < kViewScopeCount && "ViewScope out of range");
    return index;
}

} // namespace gte::rg
