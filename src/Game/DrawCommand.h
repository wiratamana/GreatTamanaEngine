#pragma once

#include "../ECS/Entity.h"
#include "../Math/Mat4.h"
#include "../Renderer/MeshHandle.h"
#include "../Renderer/PipelineHandle.h"
#include "../Renderer/MaterialTextureHandle.h"

namespace gte {

// One queued draw call as plain data extracted from the ECS: handles plus the world matrix.
// Holds no live GPU state, so RenderSystem::CollectRenderables() needs only a Registry.
struct DrawCommand {
    Entity entity; // Source entity, so captures can attribute the draw to it.
    MeshHandle mesh;
    PipelineHandle pipeline;
    MaterialTextureHandle texture; // kInvalidMaterialTextureHandle (the default) means "no material texture".
    Mat4 model = Mat4::Identity(); // Mat4's own default ctor is all-zero, NOT identity.
};

} // namespace gte
