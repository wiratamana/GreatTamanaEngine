#pragma once

#include "EntityBlueprint.h"
#include "Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <array>

namespace gte {

class Renderer;
class RenderSystem;

// Replaces Game::EnsureDefaultPipeline()/EnsurePrimitiveMesh() - owns the
// one shared default Pipeline and the per-PrimitiveType mesh cache (lazily
// created, then reused forever after - every "Create Cube" click shares the
// exact same MeshHandle, only each entity's own Transform differs, exactly
// mirroring Unity's own built-in primitives). Genuinely needs a live
// Renderer/RenderSystem to do its real job (uploading GPU resources), so -
// like Buffer/RenderTexture/Pipeline - it falls into this codebase's
// already-accepted "Tier 2, no automated coverage yet" bucket (see
// AGENTS.md, "Testability & Regression Safety"); the PURE part it feeds
// into (EntityBlueprint/EntityInstantiator) is what's actually tested.
class PrimitiveGpuCatalog {
public:
    // Lazily creates/uploads/caches `type`'s pipeline+mesh (exactly once per
    // distinct PrimitiveType, reused forever after) and returns a ready-to-
    // instantiate, ONE-NODE EntityBlueprint (no children - a primitive spawn
    // request needs nothing more than that).
    EntityBlueprint Resolve(RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type);

private:
    PipelineHandle EnsureDefaultPipeline(RenderSystem& renderSystem, Renderer& renderer);
    MeshHandle EnsurePrimitiveMesh(RenderSystem& renderSystem, Renderer& renderer, PrimitiveType type);

    PipelineHandle m_defaultPipeline;
    // Indexed by static_cast<std::size_t>(PrimitiveType) - kInvalidMeshHandle
    // (the array's default-constructed value) means "not generated yet".
    std::array<MeshHandle, 5> m_primitiveMeshes;
};

} // namespace gte
