#pragma once

#include "../../ECS/Entity.h"
#include "../../ECS/Registry.h"
#include "EntityBlueprint.h"

namespace gte {

// The single shared function BOTH primitive spawning (PrimitiveGpuCatalog.h)
// AND imported Mesh-asset spawning (MeshAssetGpuCatalog.h) ultimately call to
// turn a resolved EntityBlueprint (plain "what to spawn" data) into live ECS
// entities/components - see GameInstantiationRefactorProposal.txt, Step 3.0.
//
// Recursively walks `blueprint`'s tree: creates one entity per node, adds a
// Transform (from that node's own localTransform, identity by default),
// a MeshRenderer (only for a node whose `mesh` is valid - i.e. NOT a bare
// hierarchy-root node), a Name (only when `name` is non-empty), and a
// MeshAssetSource (only when `meshAssetSourcePath` is non-empty). Every
// child is attached under its own freshly-created parent entity via
// ECS/TransformHierarchy.h's SetParent() (worldPositionStays = true,
// mirroring Game::CreateMeshEntityFromGtaFile()'s pre-refactor behavior),
// which is also what appends it as its parent's next sibling-ordered child.
//
// `parent` lets a caller attach the freshly-instantiated blueprint's own
// root under an already-existing entity (kInvalidEntity, the default, means
// "spawn as a new scene-root entity" - the common case for both
// PrimitiveGpuCatalog and MeshAssetGpuCatalog today).
//
// Needs nothing but a Registry& and plain data - no Renderer, no GPU, no
// file I/O - so this is genuinely Tier-1-testable exactly like
// ECS/TransformHierarchy.h itself (see tests/Game/EntityInstantiatorTests.cpp).
// Returns the spawned root entity.
Entity Instantiate(Registry& registry, const EntityBlueprint& blueprint, Entity parent = kInvalidEntity);

} // namespace gte
