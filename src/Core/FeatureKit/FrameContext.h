#pragma once

// Kit-private: what a frame callback needs behind its verbs. Not reachable from FeatureKit.h.

#include "ChannelNames.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderPassToggleRegistry.h"

#include <cstdint>
#include <string_view>

namespace gte {
class Core;
} // namespace gte

namespace gte::fx::detail {

struct FrameContext {
    rg::RenderGraphBuilder* builder = nullptr;
    rg::RenderPassBlackboard* blackboard = nullptr;
    Core* core = nullptr; // Null in builder-only tests.
    rg::RenderViewId view;
    std::string_view featureName;
    rg::RenderPassEvent event = rg::RenderPassEvent::Opaques;
    const rg::RenderPassToggleRegistry* toggles = nullptr; // Null means "everything enabled".
    rg::ViewScope scope = rg::ViewScope::Shared;
    std::uint64_t serial = 0;
    ChannelNames* channels = nullptr; // Null skips the key-clash check.
};

} // namespace gte::fx::detail
