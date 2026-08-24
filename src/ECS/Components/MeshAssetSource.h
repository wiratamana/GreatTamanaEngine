#pragma once

#include <string>

namespace gte {

// Plain marker/metadata component - the absolute *.gta filesystem path
// (UTF-8, matching Game::CreateMeshEntityFromGtaFile()'s own
// `absoluteGtaPath` convention) a ROOT entity was spawned from (see
// Game::CreateMeshEntityFromGtaFile(), src/Game/Game.cpp). Attached ONLY to
// the root entity of a spawned model (never its per-material part
// children), so a later call - e.g. Game::PlayAnimationOnEntity() - can
// look back up which model this entity's own cached skinning/rig data (see
// Game.h's private SkinnedMeshData cache) belongs to, purely by
// re-looking it up through the same path-keyed cache EnsureMeshAsset()
// already maintains, rather than embedding a pointer/reference into that
// cache directly here (a plain std::string is exactly as cheap/safe to
// copy around a Registry as Name's own value - see
// ECS/Components/Name.h - and never dangles if the cache itself is ever
// cleared/rebuilt later).
struct MeshAssetSource {
    std::string gtaPath;
};

} // namespace gte
