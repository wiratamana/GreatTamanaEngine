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
// full parent/child hierarchy, generic per-component-type field capture,
// PLUS - as of task_manager/scene-serialization-2/PHASE4 - a resolved
// asset_guid for every MeshAssetSource root whose gtaPath is currently a
// tracked asset) and writes it, as JSON text (Scene/SceneJsonFormat.h's
// SerializeSceneDocument()), to `scenePath` - creating its parent directory
// first if it doesn't exist yet (mirrors Assets/GtaFile.cpp's
// WriteGtaFile()'s own "creates any missing parent directories first"
// convention). An AssetDatabase is scanned fresh, right here, against
// ResolveProjectRootDirectory() - passed through to
// BuildSceneDocumentFromRegistry() for its own asset_guid resolution step;
// never persisted/cached across calls. Always OVERWRITES whatever was
// previously at `scenePath`, with no confirmation prompt (per this
// campaign's own "keep it simple" scope). Returns false (and leaves the
// previous file, if any, untouched where avoidable) on any I/O failure -
// never throws.
//
// task_manager/scene-serialization-2/PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md -
// added as an explicit-path sibling to the original zero-argument
// SaveScene(Game&) below, so POST /save_scene can target a caller-supplied
// path. The AssetDatabase scan still always happens against
// ResolveProjectRootDirectory() regardless of `scenePath` - only the actual
// scene-file WRITE location is parameterized.
bool SaveScene(Game& game, const std::filesystem::path& scenePath);

// Unchanged behavior - still what Editor/DockLayout.cpp's Ctrl+S calls -
// forwards to the explicit-path overload above with DefaultScenePath().
bool SaveScene(Game& game);

// Reads `scenePath`, parses it (Scene/SceneJsonFormat.h's
// DeserializeSceneDocument()), and - only if that succeeds - replaces
// `game`'s current scene content with a full, recipe-aware reconstruction
// (task_manager/scene-serialization-2/
// PHASE4_RECIPE_SPAWN_RECONCILIATION_AND_LOAD_CORRECTNESS.md):
//
// Scene/SceneBuilder.h's ClearEntireScene() first destroys EVERY entity
// currently in the Registry, unconditionally (superseding PHASE3's own,
// narrower ClearSerializableSceneObjects()) - a Load genuinely REPLACES the
// whole prior scene, never merges into it. Then, for every SceneEntityRecord
// in the parsed document, in a recipe-aware Pass A:
//   - a record carrying a "PrimitiveSource" key is spawned via
//     Game::CreatePrimitiveEntity() (a REAL, GPU-backed primitive, not a
//     bare entity);
//   - a record carrying a non-empty assetGuid that still resolves against a
//     freshly-scanned AssetDatabase is spawned via
//     Game::CreateMeshEntityFromGtaFile() (a REAL, GPU-backed multi-part
//     mesh) - its own saved CHILD records are then reconciled BY NAME
//     against the live child "part" entities that call just created, so a
//     hand-edited child part Transform survives the round trip too
//     (superseding scene-serialization-1's old Design Decision #3 - see
//     that phase's own strategy file, section 3.2, for the full by-Name
//     matching algorithm and its documented edge cases);
//   - everything else (Camera/Light/an empty Transform+Name node/an
//     unresolvable-asset's now-orphaned saved child) is created as a bare
//     entity, same as PHASE3.
// Every record then has its saved `components` fields applied generically
// via ComponentTypeRegistry (ECS/Reflection/ComponentTypeRegistry.h) -
// NEVER a hand-written per-component-type copy - and its saved parent/
// sibling-index restored. `renderer` is what the recipe-spawn helpers above
// need to build/upload real GPU mesh data.
//
// Also calls Game::EnsureDefaultCameraExists() once more, itself, right
// before returning true - harmless/idempotent when a Camera record WAS
// present in the loaded document (that method's own live-count guard makes
// it a no-op), and guarantees a Camera exists immediately after this
// function returns rather than only on the next rendered frame.
//
// Returns false or DOES NOT modify `game`'s registry at all when
// `scenePath` doesn't exist or fails to parse (a malformed/missing file
// never partially clears the current scene) - never throws.
//
// task_manager/scene-serialization-2/PHASE5_NETWORK_SAVE_LOAD_SCENE_ENDPOINTS.md -
// added as an explicit-path sibling to the original zero-argument
// LoadScene(Game&, Renderer&) below, so POST /load_scene can target a
// caller-supplied path.
bool LoadScene(Game& game, Renderer& renderer, const std::filesystem::path& scenePath);

// Unchanged behavior - still what Editor/DockLayout.cpp's Ctrl+O calls -
// forwards to the explicit-path overload above with DefaultScenePath().
bool LoadScene(Game& game, Renderer& renderer);

} // namespace gte
