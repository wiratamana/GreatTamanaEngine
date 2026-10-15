#pragma once

// Early-exit guard for providers with side effects: call it at the top of the body, before anything is
// published or stored, so a disabled pass leaks nothing. A null registry means "enabled". Read-only.

#include "RenderPassToggleRegistry.h"

#include <string_view>

namespace gte::rg {

inline bool ShouldDeclareBuiltInPassThisFrame(const RenderPassToggleRegistry* registry, std::string_view name)
{
    return registry == nullptr || registry->IsEnabled(name);
}

} // namespace gte::rg
