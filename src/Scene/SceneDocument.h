#pragma once

// task_manager/scene-serialization-2/PHASE3_JSON_SCENE_DOCUMENT_AND_HIERARCHY_SAVE_PLUS_GENERIC_LOAD.md
// replaced this file's old flat, fixed-schema SceneObjectKind/SceneObjectRecord
// shape (root-only, PrimitiveSource/MeshAssetSource-only - see
// task_manager/scene-serialization-1/) with a hierarchy-aware, generic
// component-bag shape that can represent ANY entity in the Registry, with
// its full parent/child structure - see PHASE0_MASTER_STRATEGY.md's
// Appendix A for the exact on-disk JSON shape this maps onto 1:1.

#include <nlohmann/json.hpp> // PHASE0 Locked Design Decision #1 - src/Scene/ now legitimately depends on this.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gte {

// One serializable entity - see PHASE0_MASTER_STRATEGY.md's Appendix A for
// the full on-disk shape this maps onto 1:1. Deliberately NOT tied to a
// live Entity/Registry - a pure, Tier-1-testable snapshot, same spirit as
// the OLD SceneObjectRecord it replaces.
struct SceneEntityRecord {
    // Index into the OWNING SceneDocument::entities array of this entity's
    // OWN parent, or std::nullopt for a root entity. NEVER a raw Entity
    // handle (those are session-local and meaningless across a save/load
    // round trip) - always a document-local, 0-based array index.
    std::optional<std::size_t> parentIndex;

    std::uint32_t siblingIndex = 0;

    // Empty means "not an asset-recipe root" - see PHASE4 for how/when this
    // gets populated on Save and consumed on Load. Guid::ToString() format
    // (32 lowercase hex chars) when present. This phase (PHASE3) never
    // populates this field - every entity's record leaves it empty; PHASE4
    // is what actually resolves a MeshAssetSource root's gtaPath to a
    // stable Guid and writes it here.
    std::string assetGuid;

    // Generic component bag - one key per registered ComponentTypeRegistry
    // typeName this entity actually has (e.g. "Transform", "Name",
    // "Camera") mapped to that component's own serialized fields. Built/
    // consumed entirely generically by SceneBuilder.cpp/Editor/SceneIO.cpp -
    // NEVER hand-parsed field-by-field the way the old SceneTextFormat.cpp
    // was.
    nlohmann::json components = nlohmann::json::object();
};

// A whole serializable scene - a flat array of SceneEntityRecord values,
// with hierarchy captured via each record's own parentIndex (an index into
// THIS SAME array) rather than a nested tree structure - this keeps
// (de)serialization, and Editor/SceneIO.cpp's own two-pass Load
// reconstruction, simple array iteration with no recursive JSON walking
// required.
struct SceneDocument {
    std::vector<SceneEntityRecord> entities;
};

} // namespace gte
