#pragma once

#include <string>

namespace gte {

// A purely cosmetic, OPTIONAL display name for an entity - plain data only,
// same spirit as Transform/MeshRenderer (see AGENTS.md, "Entity-Component-
// System": components are plain data, never behavior). An entity with no
// Name component (or an empty Name::value) falls back to whatever
// synthesized "Entity %u" label the Editor's "Hierarchy" panel has always
// shown (see Panels/HierarchyPanel.cpp's BuildEntityLabel()) - adding this
// component is always additive/opt-in, never a behavior change for an
// existing entity that never gets one.
//
// First real use: Game::CreateMeshEntityFromGtaFile() (src/Game/Game.cpp)
// names a spawned model's ROOT entity after its source *.gta file's own
// name (minus extension), and each of its child "part" entities after the
// PMX material it came from, when the source .pmx actually named that
// material - see that function's own doc comment (Game.h) for the exact
// naming rule. Nothing stops any other future entity (a primitive, a
// hand-authored scene object, ...) from getting a Name too.
struct Name {
    std::string value;
};

} // namespace gte
