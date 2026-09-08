#pragma once

#include "../Assets/AssetTypes.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"
#include "../Renderer/Primitives/PrimitiveMeshGenerator.h"

#include <string>
#include <vector>

namespace gte {

// Which kind of scene object one SceneObjectRecord describes - the engine
// currently only knows how to (re)create two kinds of top-level scene
// object (see task_manager/scene-serialization-1/PHASE0_MASTER_STRATEGY.md):
// one spawned from a built-in PrimitiveType (Game::CreatePrimitiveEntity()),
// and one spawned from an imported asset file, referenced by its stable
// AssetDatabase Guid (Game::CreateMeshEntityFromGtaFile()). Explicit numeric
// values are NOT pinned here (unlike AssetType) because this enum is never
// stored as a raw integer in the .gtscene TEXT format itself - see
// SceneTextFormat.cpp, which writes/reads it as the literal strings
// "Primitive"/"Asset".
enum class SceneObjectKind {
    Primitive,
    Asset,
};

// One serializable top-level scene object - a ROOT entity only (see
// Scene/SceneBuilder.h's own doc comment for why a multi-part asset's CHILD
// entities are never individually represented here). Plain data, no
// Registry/Entity/Renderer dependency of any kind - the exact same
// "component-style" plain-struct philosophy AGENTS.md already documents for
// every real ECS component (see ECS/Components/Transform.h), just living
// outside the ECS entirely since a SceneDocument is a serialization-time
// snapshot, never a live entity.
struct SceneObjectRecord {
    SceneObjectKind kind = SceneObjectKind::Primitive;

    // Optional cosmetic display name (see ECS/Components/Name.h) - empty
    // means "no Name component on the entity this record produces/came
    // from".
    std::string name;

    // World-space* transform this object is spawned/restored with -
    // *actually the entity's own Transform::position/rotation/scale, which
    // is genuinely world-space for every entity this campaign ever
    // serializes, since only ROOT entities (Transform::parent ==
    // kInvalidEntity) are ever captured - see SceneBuilder.h.
    Vec3 position = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 scale = Vec3::One();

    // Meaningful only when kind == SceneObjectKind::Primitive - which
    // built-in shape to recreate via Game::CreatePrimitiveEntity().
    PrimitiveType primitiveType = PrimitiveType::Cube;

    // Meaningful only when kind == SceneObjectKind::Asset -
    // AssetDatabase::FindByGuid()'s key for resolving this back to an
    // absolute *.gta path at load time (see Scene/SceneBuilder.h and
    // Editor/SceneIO.h). Guid::Invalid() (the default) when kind ==
    // Primitive. DeserializeSceneDocument() (see SceneTextFormat.h below)
    // NEVER produces a kind == Asset record with an Invalid() Guid here -
    // that combination is rejected as a parse failure instead (v2 - see
    // SceneTextFormat.h's own doc comment).
    Guid assetGuid;
};

// A whole serializable scene - just a flat list of top-level object
// records. Deliberately NOT a tree/hierarchy (see SceneObjectRecord's own
// doc comment above) - every record is spawned independently and
// positioned/rotated/scaled in world space.
struct SceneDocument {
    std::vector<SceneObjectRecord> objects;
};

} // namespace gte
