#pragma once

#include "Renderer/MeshHandle.h"
#include "Renderer/PipelineHandle.h"

namespace gte {

// The engine's second real component (see AGENTS.md, "Entity-Component-
// System" and Components/Transform.h for the first). References a live
// Mesh/Pipeline purely by handle (see Renderer/ResourcePool.h) - never by
// embedding either directly, exactly the rule AGENTS.md already lays out
// for this component by name ("A component that needs a live GPU resource
// (e.g. a future MeshRenderer) must reference it by handle/value data").
// Plain data only, same spirit as Transform - no behavior, no ownership of
// any GPU/Vulkan resource.
//
// RenderSystem (src/Game/RenderSystem.h) is what actually resolves these
// handles against live Mesh/Pipeline objects and issues the draw - this
// struct alone never touches Renderer/Vulkan, keeping it just as
// Tier-1-testable as Transform (see RegistryTests.cpp/RenderSystemTests.cpp).
struct MeshRenderer {
    MeshHandle mesh;
    PipelineHandle pipeline;
};

} // namespace gte
