#pragma once

#include "../../Renderer/RenderGraph/RenderGraphHandles.h"

#include <cstdint>
#include <string_view>

namespace gte::fx {

// Blackboard key of the channel "<featureName>.<channelName>" for one view. Pure: same inputs, same key.
std::uint64_t ChannelKey(std::string_view featureName, std::string_view channelName, rg::RenderViewId view);

} // namespace gte::fx
