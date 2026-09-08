#pragma once

#include "../../Renderer/Primitives/PrimitiveMeshGenerator.h"

namespace gte {

// Plain marker/metadata component - the PrimitiveType (see
// Renderer/Primitives/PrimitiveMeshGenerator.h) an entity was spawned with
// via Game::CreatePrimitiveEntity() (src/Game/Game.cpp) - the primitive-spawn
// counterpart of MeshAssetSource.h's gtaPath (which instead records which
// *.gta file an ASSET-spawned root came from). A MeshRenderer's
// MeshHandle/PipelineHandle alone cannot be reversed back into "this was a
// Cube" (they are just opaque resource-pool indices - see
// Renderer/ResourcePool.h) - this component is what makes that possible,
// needed by task_manager/scene-serialization-1's
// Scene/SceneBuilder.h::BuildSceneDocumentFromRegistry() to serialize a
// primitive entity back out to a *.gtscene file. Attached ONLY to the one
// entity CreatePrimitiveEntity() itself creates (a primitive spawn is
// always a single node with no children - see EntityBlueprint.h) - never to
// an asset-spawned entity, which carries MeshAssetSource instead, never
// both.
struct PrimitiveSource {
    PrimitiveType type = PrimitiveType::Cube;
};

} // namespace gte
