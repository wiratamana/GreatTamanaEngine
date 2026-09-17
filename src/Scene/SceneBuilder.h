#pragma once

#include "../Assets/AssetDatabase.h"
#include "../ECS/Registry.h"
#include "SceneDocument.h"

namespace gte {

// The ECS-facing bridge between a live Registry and a plain SceneDocument
// (Scene/SceneDocument.h) - the SAVE half (BuildSceneDocumentFromRegistry())
// and the "make room for a fresh Load" half (ClearSerializableSceneObjects())
// of this feature. Deliberately still Renderer-free - spawning new entities
// from a SceneDocument (the rest of "Load") needs a live Renderer
// (Game::CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile() both do), so
// THAT half lives in Editor/SceneIO.h instead, not here - keeping this file
// Tier-1-testable exactly like Scene/SceneJsonFormat.h.
//
// task_manager/scene-serialization-2/PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #2 widened this function's scope to EVERY entity in the
// Registry (previously: only PrimitiveSource/MeshAssetSource-tagged ROOT
// entities - see task_manager/scene-serialization-1/). Every entity reached
// by recursively walking ECS/TransformHierarchy.h's GetChildren() from the
// root list downward now gets its own SceneEntityRecord, with its exact
// parent/child structure preserved via each record's own document-local
// parentIndex - see PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md.
// An entity with no Transform component at all is out of scope (this engine
// has never created one through a normal spawn path).
//
// CORRECTNESS INVARIANT (still true, unchanged by this widened scope):
// resolving a MeshAssetSource::gtaPath below via `assetDatabase.FindByPath()`
// only works when that stored path and `assetDatabase`'s own
// RefreshFromDirectory()-populated index agree on what "the same file" looks
// like as a string - both normalize via std::filesystem::absolute()
// independently (see AssetDatabase.cpp). PHASE3 itself does not yet resolve
// any asset_guid at all (see SceneBuilder.cpp's own comment on this) -
// PHASE4 is what actually wires this invariant into real behavior again.

// Walks EVERY root entity in `registry` (ECS/TransformHierarchy.h's
// GetChildren(registry, kInvalidEntity)), and recursively every one of its
// descendants, producing exactly one SceneEntityRecord per entity reached -
// no entity is ever skipped based on which components it does or doesn't
// carry (contrast with scene-serialization-1's old root-only,
// PrimitiveSource/MeshAssetSource-only scope). Each record's `components`
// bag is built entirely generically: every ComponentTypeRegistry-registered
// type (ECS/Reflection/ComponentTypeRegistry.h), in
// AllSortedByTypeName() order, that this SPECIFIC entity actually has gets
// its own JSON object of serialized fields - there is no per-component-type
// branch anywhere in this function, so a future component becomes part of a
// saved scene automatically the moment it registers itself (PHASE2). Each
// record's own `siblingIndex` is copied directly from its Transform
// component (guaranteed present - every entity this engine's normal spawn
// paths ever create always gets one); `parentIndex` is the document-local
// array index of whichever entity was visited as this one's own immediate
// Transform::parent, or std::nullopt for a root. `assetGuid` is left empty
// by this phase (`assetDatabase` is accepted but not yet used here) - see
// PHASE4 for the MeshAssetSource -> stable Guid resolution pass this
// parameter exists for.
SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase);

// Destroys every root entity (and, via DestroyEntityAndDescendants(), all
// of its descendants) that carries PrimitiveSource OR MeshAssetSource -
// i.e. exactly what scene-serialization-1's own narrower
// BuildSceneDocumentFromRegistry() used to consider in scope. Everything
// else (the default Camera entity, or any future entity kind this function
// doesn't know about) is left completely untouched. Call this BEFORE
// spawning entities from a freshly-loaded SceneDocument (see
// Editor/SceneIO.h's LoadScene()) so a Load genuinely REPLACES this
// feature's own prior content rather than merging into it, without ever
// silently discarding something (like the Camera) this feature never owned
// in the first place.
//
// Deliberately UNCHANGED by this phase (PHASE3) despite
// BuildSceneDocumentFromRegistry() above now capturing every entity, not
// just Primitive/Asset roots - see
// PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md,
// section 3.5, for why this is a deliberately incomplete intermediate
// state (a repeated Load can accumulate duplicate Camera/Light entities
// until PHASE4 replaces this function with ClearEntireScene()).
void ClearSerializableSceneObjects(Registry& registry);

} // namespace gte
