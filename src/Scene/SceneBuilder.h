#pragma once

#include "../Assets/AssetDatabase.h"
#include "../ECS/Registry.h"
#include "SceneDocument.h"

namespace gte {

// The ECS-facing bridge between a live Registry and a plain SceneDocument
// (Scene/SceneDocument.h) - the SAVE half (BuildSceneDocumentFromRegistry())
// and the "make room for a fresh Load" half (ClearSerializableSceneObjects())
// of task_manager/scene-serialization-1. Deliberately still Renderer-free -
// spawning new entities from a SceneDocument (the rest of "Load") needs a
// live Renderer (Game::CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile()
// both do), so THAT half lives in Editor/SceneIO.h instead, not here -
// keeping this file Tier-1-testable exactly like Scene/SceneTextFormat.h.
//
// Only ROOT entities (Transform::parent == kInvalidEntity, per
// ECS/TransformHierarchy.h's GetChildren(registry, kInvalidEntity)) are ever
// considered - a reparented primitive/asset root living somewhere deeper in
// the hierarchy is a documented, out-of-scope limitation (see
// task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md, "What We
// Will NOT Do"). A multi-part asset's own CHILD "submesh part" entities
// (spawned by Game::CreateMeshEntityFromGtaFile() underneath its root) are
// NEVER individually walked/serialized here - only the root's own Guid +
// Transform is captured; every child is discarded on save and fully
// re-derived fresh from the asset again at load time (see PHASE0's Design
// Decision #3).
//
// CORRECTNESS INVARIANT (v2): resolving a MeshAssetSource::gtaPath below via
// `assetDatabase.FindByPath()` only works when that stored path and
// `assetDatabase`'s own RefreshFromDirectory()-populated index agree on
// what "the same file" looks like as a string - both normalize via
// std::filesystem::absolute() independently (see AssetDatabase.cpp), so a
// MeshAssetSource::gtaPath captured via any means OTHER than an
// already-absolute, already-`SDL_GetBasePath()`-rooted path (today's only
// real source - Panels/ProjectPanel.cpp's drag-and-drop payload) is not
// guaranteed to resolve. This is true today by construction, not by luck,
// but is not re-derived/re-checked anywhere in this file itself - see
// task_manager/scene-serialization-1/PHASE4_SCENE_BUILDER_REGISTRY_ASSETDATABASE_BRIDGE.md's
// own "Correctness invariant" note for the full reasoning, and keep it in
// mind before changing how any caller constructs the path handed to
// Game::CreateMeshEntityFromGtaFile().

// Walks every root entity in `registry` and returns a SceneDocument
// describing exactly the ones this feature knows how to serialize:
//   - a root entity carrying PrimitiveSource -> a SceneObjectKind::Primitive
//     record (primitiveType copied verbatim from the component).
//   - a root entity carrying MeshAssetSource -> a SceneObjectKind::Asset
//     record, ONLY if `assetDatabase.FindByPath(gtaPath)` actually resolves
//     to a tracked asset (its Guid is copied into the record) - an
//     asset-spawned root whose source *.gta file is no longer tracked by
//     `assetDatabase` (moved/deleted/never imported through it) is SKIPPED
//     entirely, since it has no stable Guid to serialize a reference by.
//   - any other root entity (no PrimitiveSource AND no MeshAssetSource -
//     e.g. the engine's own default Camera entity, see PHASE1) is SKIPPED
//     entirely - it is not part of this feature's serialization scope.
// Every included record's position/rotation/scale is copied directly from
// that root entity's own Transform component (guaranteed present - every
// entity Instantiate() creates always gets one), and its `name` is copied
// from a Name component if present, otherwise left empty. Entities are
// visited in GetChildren()'s own iteration order (creation order, unless a
// Remove() elsewhere has since reshuffled it) - the resulting SceneDocument's
// `objects` order is therefore stable but not independently meaningful.
SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase);

// Destroys every root entity (and, via DestroyEntityAndDescendants(), all
// of its descendants) that BuildSceneDocumentFromRegistry() above would
// have included in a SceneDocument - i.e. every root carrying
// PrimitiveSource OR MeshAssetSource. Everything else (the default Camera
// entity, or any future entity kind this feature doesn't know about) is
// left completely untouched. Call this BEFORE spawning entities from a
// freshly-loaded SceneDocument (see Editor/SceneIO.h's LoadScene()) so a
// Load genuinely REPLACES this feature's own prior content rather than
// merging into it, without ever silently discarding something (like the
// Camera) this feature never owned in the first place.
void ClearSerializableSceneObjects(Registry& registry);

} // namespace gte
