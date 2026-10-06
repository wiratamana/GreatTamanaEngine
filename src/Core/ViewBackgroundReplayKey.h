#pragma once

#include "../Renderer/RenderGraph/RenderPipeline.h"

namespace gte {
using rg::operator""_passId;

// Key a feature publishes its view-background replay callback under.
constexpr rg::RenderPassId kViewBackgroundReplayCallbackKey = "Core.ViewBackgroundReplayCallback"_passId;
} // namespace gte
