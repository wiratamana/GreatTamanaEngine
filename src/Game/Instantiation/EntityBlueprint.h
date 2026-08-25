#pragma once

#include "../../ECS/Components/Transform.h"
#include "../../Renderer/MeshHandle.h"
#include "../../Renderer/PipelineHandle.h"
#include "../../Renderer/TextureHandle.h"

#include <string>
#include <vector>

namespace gte {

// A tiny, in-memory "prefab" - the plain-data shape both primitive
// instantiation (PrimitiveGpuCatalog.h) and imported Mesh-asset
// instantiation (MeshAssetGpuCatalog.h) resolve down to, so
// EntityInstantiator.h's Instantiate() is the ONE shared function that
// actually turns "what to spawn" into live ECS entities/components - see
// GameInstantiationRefactorProposal.txt, Step 3.0.
//
// Deliberately plain, inert data - no behavior, no Renderer/ECS dependency
// at all, the exact same "component-style" philosophy already applied to
// every real ECS component (see AGENTS.md, "Entity-Component-System"). A
// primitive spawn request is a single node with no children; an imported
// multi-part mesh is a bare ROOT node (no MeshRenderer of its own, tagged
// with its source *.gta path) plus one CHILD node per submesh part -
// mirroring Game::CreateMeshEntityFromGtaFile()'s pre-refactor root/child
// construction, just expressed as data instead of hand-built ECS calls.
struct EntityBlueprintNode {
    // Display name for this node's Name component - empty means "no Name
    // component at all" (see ECS/Components/Name.h - a component is only
    // ever added when there's something meaningful to show).
    std::string name;

    // kInvalidMeshHandle means "this node carries no MeshRenderer of its
    // own" - the bare hierarchy-root case (a multi-part model's empty root
    // entity, which only exists to parent its real parts together).
    MeshHandle mesh;
    PipelineHandle pipeline;
    // kInvalidTextureHandle (the default) means "untextured".
    TextureHandle texture;

    // Local position/rotation/scale override for the spawned entity's
    // Transform - identity (Transform{}'s own default) unless set. Only the
    // position/rotation/scale fields are ever read by EntityInstantiator;
    // `parent`/`siblingIndex` are always resolved by the actual attach step
    // (ECS/TransformHierarchy.h's SetParent()) instead, never copied
    // verbatim from here.
    Transform localTransform;

    // Non-empty only on a node that should carry a MeshAssetSource
    // component (see ECS/Components/MeshAssetSource.h) - the model ROOT
    // node spawned from an imported *.gta file, recording exactly which
    // asset it came from so a later PlayAnimationOnEntity()-equivalent call
    // can look its cached rig data back up.
    std::string meshAssetSourcePath;

    std::vector<EntityBlueprintNode> children;
};

// A blueprint IS just its own root node - kept as a type alias (rather than
// a wrapping struct) since a one-node blueprint (a primitive) and a
// root+children blueprint (an imported mesh) are both exactly "one node,
// possibly with children" - there is nothing else a "blueprint" needs to be
// beyond that.
using EntityBlueprint = EntityBlueprintNode;

} // namespace gte
