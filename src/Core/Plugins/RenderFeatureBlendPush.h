#pragma once

#include "RenderFeatureDescriptor.h"

#include <cstdint>

namespace gte {

// Push block of RenderFeatureBlend.comp (16 bytes); .x is the blend mode as a float.
struct RenderFeatureBlendPush {
    float blendModeAndPad[4] = {};
};
static_assert(sizeof(RenderFeatureBlendPush) == 16);

constexpr RenderFeatureBlendPush MakeRenderFeatureBlendPush(RenderFeatureBlendMode mode) noexcept
{
    RenderFeatureBlendPush push;
    push.blendModeAndPad[0] = static_cast<float>(static_cast<std::uint32_t>(mode));
    return push;
}

} // namespace gte
