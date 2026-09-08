#pragma once

#include <filesystem>

namespace gte {

class Game;
class Renderer;

// Hardcoded save/load target for this campaign - see
// task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md for why this
// is deliberately a single, fixed path rather than a user-chosen one. Always
// ResolveProjectRootDirectory() / "TestScene.gtscene" - a *.gtscene text
// file (Scene/SceneTextFormat.h) directly under the Project folder, never
// wrapped as a tracked *.gta AssetDatabase asset itself (it does not get
// its own Guid - see PHASE0's "What We Will NOT Do").
std::filesystem::path DefaultScenePath();

// Serializes `game`'s current ECS world (Scene/SceneBuilder.h's
// BuildSceneDocumentFromRegistry()) and writes it, as text
// (Scene/SceneTextFormat.h's SerializeSceneDocument()), to
// DefaultScenePath() - creating the Project folder first if it doesn't
// exist yet (mirrors Assets/GtaFile.cpp's WriteGtaFile()'s own "creates any
// missing parent directories first" convention). An AssetDatabase is
// scanned fresh, right here, against ResolveProjectRootDirectory() - only
// used to resolve each asset-spawned root's gtaPath into a stable Guid (see
// SceneBuilder.h) - never persisted/cached across calls. Always OVERWRITES
// whatever was previously at DefaultScenePath(), with no confirmation
// prompt (per this campaign's own "keep it simple" scope). Returns false
// (and leaves the previous file, if any, untouched where avoidable) on any
// I/O failure - never throws.
bool SaveScene(Game& game);

// Reads DefaultScenePath(), parses it (Scene/SceneTextFormat.h's
// DeserializeSceneDocument()), and - only if that succeeds - replaces
// `game`'s current scene content: Scene/SceneBuilder.h's
// ClearSerializableSceneObjects() destroys every entity this feature owns
// (leaving anything it doesn't own, e.g. the default Camera - see PHASE1 -
// untouched), then every SceneObjectRecord in the parsed document is spawned
// via `game`'s own existing public API
// (Game::CreatePrimitiveEntity()/CreateMeshEntityFromGtaFile()) and its
// Transform/Name are set to match the record. `renderer` is needed because
// both of those spawn methods need one to build/upload GPU mesh data.
//
// An Asset record whose Guid no longer resolves via a fresh
// AssetDatabase::FindByGuid() scan (the referenced *.gta was moved/deleted
// since the scene was last saved) is skipped gracefully - that one object
// is simply not restored, everything else in the file still loads normally.
//
// Returns false or DOES NOT modify `game`'s registry at all when
// DefaultScenePath() doesn't exist or fails to parse (a malformed/missing
// file never partially clears the current scene) - never throws.
bool LoadScene(Game& game, Renderer& renderer);

} // namespace gte
