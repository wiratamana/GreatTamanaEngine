#pragma once

#include "Renderer/MeshHandle.h"
#include "Renderer/PipelineHandle.h"
#include "Renderer/TextureHandle.h"

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
// handles against live Mesh/Pipeline/MaterialTexture objects and issues the
// draw - this struct alone never touches Renderer/Vulkan, keeping it just as
// Tier-1-testable as Transform (see RegistryTests.cpp/RenderSystemTests.cpp).
struct MeshRenderer {
    MeshHandle mesh;
    PipelineHandle pipeline;

    // OPTIONAL per-submesh diffuse texture (see Renderer/MaterialTexture.h)
    // - kInvalidTextureHandle (the default) means "no texture", exactly
    // what every pre-existing MeshRenderer{ mesh, pipeline } call site
    // (primitives, the demo scene, a materialless imported mesh) still
    // produces unchanged. Set only for a per-material submesh entity a
    // textured PMX import spawns (see Game::CreateMeshEntityFromGtaFile(),
    // src/Game/Game.cpp) - `pipeline` for such an entity must be one built
    // with VertexLayout::PositionNormalUv/useMaterialTexture (see
    // Pipeline.h) for this to actually get sampled; RenderSystem::Draw()
    // resolves it into the VkDescriptorSet Renderer::Submit() binds.
    TextureHandle texture;
};

} // namespace gte
