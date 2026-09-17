#pragma once

#include "../Assets/AssetDatabase.h"
#include "../ECS/Registry.h"
#include "SceneDocument.h"

namespace gte {

// The ECS-facing bridge between a live Registry and a plain SceneDocument
// (Scene/SceneDocument.h) - the SAVE half (BuildSceneDocumentFromRegistry())
// and the "make room for a fresh Load" half (ClearEntireScene()) of this
// feature. Deliberately still Renderer-free - spawning new entities from a
// SceneDocument (the rest of "Load") needs a live Renderer
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
// CORRECTNESS INVARIANT (now actually wired in, as of PHASE4 - see
// PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md, section 3.1):
// resolving a MeshAssetSource::gtaPath below via `assetDatabase.FindByPath()`
// only works when that stored path and `assetDatabase`'s own
// RefreshFromDirectory()-populated index agree on what "the same file" looks
// like as a string - both normalize via std::filesystem::absolute()
// independently (see AssetDatabase.cpp). PHASE3 deferred this resolution
// entirely (every asset_guid was left empty); PHASE4 is what actually
// resolves a MeshAssetSource root's gtaPath to its tracked AssetRecord's own
// stable Guid and writes it into that root's own SceneEntityRecord::assetGuid,
// which Editor/SceneIO.cpp's LoadScene() then consumes (via
// AssetDatabase::FindByGuid()) to reconstruct a REAL, GPU-backed
// MeshRenderer via Game::CreateMeshEntityFromGtaFile() instead of a bare
// entity.

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
// Transform::parent, or std::nullopt for a root.
//
// `assetGuid` - populated by a dedicated resolution pass (PHASE4) that runs
// right after the recursive walk finishes: for every entity that carries a
// live MeshAssetSource component, `assetDatabase.FindByPath(gtaPath)` is
// looked up and, if it resolves, that asset's own stable Guid::ToString() is
// written into this SAME entity's own record. Left empty when the entity
// has no MeshAssetSource, or when its gtaPath simply isn't (or is no longer)
// a tracked asset - the entity's record is still saved, generically, with
// whatever Transform/Name it has; it will simply come back on Load as a
// bare entity with no re-derived mesh, since Load has no Guid to resolve
// (a deliberate, small improvement over scene-serialization-1's old "skip
// the whole entity silently").
SceneDocument BuildSceneDocumentFromRegistry(Registry& registry, const AssetDatabase& assetDatabase);

// Destroys EVERY root entity (and, via DestroyEntityAndDescendants(), every
// one of its descendants) - i.e. genuinely empties the ENTIRE Registry of
// every entity, no exceptions, no tag-based filtering. Correct now that
// this campaign's scope is "every entity is serializable" (see PHASE0's
// Locked Design Decision #2) - there is no longer any entity kind this
// feature does NOT own, so there is nothing left to selectively preserve.
// Call this BEFORE spawning entities from a freshly-loaded SceneDocument
// (see Editor/SceneIO.h's LoadScene()) so a Load genuinely REPLACES this
// feature's own prior content rather than merging into it.
//
// PHASE4 (task_manager/scene-serialization-2/
// PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md, section 3.3)
// replaces the OLD, narrower ClearSerializableSceneObjects() (which only
// ever destroyed a PrimitiveSource/MeshAssetSource-tagged ROOT, leaving
// e.g. the default Camera untouched - a deliberately incomplete
// intermediate state PHASE3 left behind) with this genuinely unconditional
// function. Because THIS call fully completes before Editor/SceneIO.cpp's
// LoadScene() Pass A begins, that Pass's own by-Name reconciliation
// (matching an asset root's saved children against the live children
// CreateMeshEntityFromGtaFile() just (re-)created) can never observe a
// stale, pre-Load entity - the Registry contains ONLY entities that SAME
// LoadScene() call has created so far, by construction.
void ClearEntireScene(Registry& registry);

} // namespace gte
