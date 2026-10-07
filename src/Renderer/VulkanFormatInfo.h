#pragma once
#include <volk.h>

namespace gte {

// Bytes per texel for every VkFormat this engine creates a render target or
// depth buffer with. Returns 0 for anything else - callers must treat 0 as
// unsupported, never assume a size.
int BytesPerTexelForFormat(VkFormat format) noexcept;

} // namespace gte
