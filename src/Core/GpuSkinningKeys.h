#pragma once

#include "../Renderer/RenderGraph/RenderPipeline.h"

namespace gte {

// Blackboard key of the GPU skinning output buffers (std::vector<rg::BufferHandle>). Published once per frame.
inline constexpr rg::RenderPassId kGpuSkinningOutputsKey =
    rg::operator""_passId("GpuSkinning.OutputBuffers", sizeof("GpuSkinning.OutputBuffers") - 1);

} // namespace gte
