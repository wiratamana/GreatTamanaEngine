#pragma once

// Render Graph panel grouping tag for the engine's own core scene-drawing
// passes (ClearViewTarget, RenderOpaque, DrawSkyBackground) - lives in
// Core, since Core itself declares/owns these passes. Registered once, in
// Core::RegisterOffscreenRenderPipelineProviders().

#include "../Renderer/RenderGraph/RenderGraphTypes.h"

namespace gte {

inline constexpr rg::RenderPassTag kSceneRenderingPassTag{ 1ull << 3 };

} // namespace gte
