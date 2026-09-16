#pragma once

#include <string>

namespace gte {

// Reads the EXISTING Mesh *.gta at `absoluteGtaPath`, decodes its current
// MeshData payload, overwrites its normals via
// RecomputeMeshNormalsFromGeometry() (MeshNormalRecompute.h), re-encodes,
// and writes the file back out - GUID, flags, version, and the METADATA
// section (RigFileData - skeleton/morphs/physics/materials/skin weights/
// jointPhysicsOverrides) are all preserved byte-for-byte/value-for-value
// unchanged; ONLY the payload's normals actually change. Mirrors
// src/Game/Physics/DynamicChainPhysicsPersistence.h's own
// SaveJointPhysicsOverridesToGtaFile() shape and conservative-on-failure
// contract exactly (see that header's own doc comment) - returns false (and
// writes NOTHING to disk) if the file can't be read, isn't an
// AssetType::Mesh *.gta, or its existing payload can't be decoded as a
// MeshData.
//
// Deliberately does NOT touch any GPU/MeshAssetGpuCatalog cache itself -
// see src/Game/Instantiation/MeshAssetGpuCatalog.h's
// InvalidateCachedMeshAsset() (a separate, explicit follow-up call an
// Editor caller makes after this succeeds - see Panels/InspectorPanel.cpp),
// exactly the same "persistence and cache invalidation are two separate,
// explicit steps" split SaveJointPhysicsOverridesToGtaFile()/
// RefreshCachedJointPhysicsOverridesFromDisk() already established.
//
// `outErrorMessage`, if non-null, is set to a short human-readable reason on
// failure - left untouched on success.
bool RecomputeAndSaveMeshNormalsToGtaFile(const std::string& absoluteGtaPath, std::string* outErrorMessage = nullptr);

} // namespace gte
