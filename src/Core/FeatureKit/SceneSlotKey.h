#pragma once

#include "../../Renderer/RenderGraph/RenderPipeline.h"

#include <cstdint>

namespace gte::fx {

// Blackboard key a texture published to scene slot `slotIndex` is stored under for one view.
rg::RenderPassId SceneSlotKey(std::uint32_t slotIndex, rg::RenderViewId view) noexcept;

} // namespace gte::fx
