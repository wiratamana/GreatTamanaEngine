#pragma once

#include "../Core/GraphicsHandles.h"

#include "../Assets/AssetDatabase.h"
#include "../ECS/Registry.h"
#include "SceneDocument.h"

namespace gte {

// Bridge between a live Registry and a plain SceneDocument: save (build), clear, and load (reconstruct).

// Builds one record per entity, walking the Transform hierarchy from the roots down. Components are
// captured generically through ComponentTypeRegistry. Mesh asset roots get their asset guid resolved
// through `assetDatabase` when the path is a tracked asset.
SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase);

// Destroys every entity and its descendants, so a load replaces the scene instead of merging into it.
void ClearEntireScene(Registry& registry);

class Game;
class Renderer;

// Spawns every record of `document` into `game`: primitives and mesh assets as GPU-backed entities (asset
// children reconciled by name), the rest as bare entities; then applies components, parenting and a default camera.
// Call ClearEntireScene() first. `sceneServicesSetLayout` is Core's scene-services layout.
void ReconstructSceneFromDocument(Game& game, Renderer& renderer, const SceneDocument& document,
    const AssetDatabase& assetDatabase, DescriptorSetLayoutHandle sceneServicesSetLayout);

} // namespace gte
