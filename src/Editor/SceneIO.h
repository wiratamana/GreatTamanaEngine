#pragma once

#include <filesystem>

namespace gte {

class Game;
class Renderer;

// Hardcoded save/load target for this campaign - see
// task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md for why this
// is deliberately a single, fixed path rather than a user-chosen one. Always
// ResolveProjectRootDirectory() / "TestScene.gtscene" - a *.gtscene JSON
// file (Scene/SceneJsonFormat.h, since
// task_manager/scene-serialization-2/PHASE3) directly under the Project
// folder, never wrapped as a tracked *.gta AssetDatabase asset itself (it
// does not get its own Guid - see scene-serialization-1's PHASE0's "What We
// Will NOT Do").
std::filesystem::path DefaultScenePath();

// Serializes `game`'s current ECS world (Scene/SceneBuilder.h's
// BuildSceneDocumentFromRegistry() - walks EVERY entity in the Registry,
// full parent/child hierarchy, generic per-component-type field capture)
// and writes it, as JSON text (Scene/SceneJsonFormat.h's
// SerializeSceneDocument()), to DefaultScenePath() - creating the Project
// folder first if it doesn't exist yet (mirrors Assets/GtaFile.cpp's
// WriteGtaFile()'s own "creates any missing parent directories first"
// convention). An AssetDatabase is scanned fresh, right here, against
// ResolveProjectRootDirectory() - passed through to
// BuildSceneDocumentFromRegistry() for its own (PHASE4) asset_guid
// resolution step; never persisted/cached across calls. Always OVERWRITES
// whatever was previously at DefaultScenePath(), with no confirmation
// prompt (per this campaign's own "keep it simple" scope). Returns false
// (and leaves the previous file, if any, untouched where avoidable) on any
// I/O failure - never throws.
bool SaveScene(Game& game);

// Reads DefaultScenePath(), parses it (Scene/SceneJsonFormat.h's
// DeserializeSceneDocument()), and - only if that succeeds - replaces
// `game`'s current scene content with a GENERIC, hierarchy-aware
// reconstruction (task_manager/scene-serialization-2/
// PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md):
// Scene/SceneBuilder.h's ClearSerializableSceneObjects() destroys every
// entity this feature owns (leaving anything it doesn't own, e.g. the
// default Camera - untouched; see that function's own doc comment for the
// PHASE4 follow-up), then every SceneEntityRecord in the parsed document is
// recreated as a bare entity, reparented to match its saved parentIndex,
// and has every one of its saved `components` fields applied generically
// via ComponentTypeRegistry (ECS/Reflection/ComponentTypeRegistry.h) -
// NEVER a hand-written per-component-type copy. `renderer` is accepted for
// signature stability with the eventual (PHASE4) recipe-spawn path, which
// needs one to build/upload GPU mesh data - THIS phase's own generic
// reconstruction does not yet use it directly.
//
// PHASE3's own known, deliberately incomplete limitation (see that phase's
// own completion report for the full list): a PrimitiveSource/
// MeshAssetSource-tagged entity round-trips its Transform/Name/tag fields
// correctly but gets NO MeshRenderer after Load (not visible) - PHASE4 adds
// the recipe-aware spawn/reconciliation this needs.
//
// Returns false or DOES NOT modify `game`'s registry at all when
// DefaultScenePath() doesn't exist or fails to parse (a malformed/missing
// file never partially clears the current scene) - never throws.
bool LoadScene(Game& game, Renderer& renderer);

} // namespace gte
