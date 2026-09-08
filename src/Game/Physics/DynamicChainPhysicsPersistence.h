#pragma once
#include "../../Physics/DynamicChainDefinition.h"

#include <string>
#include <vector>

namespace gte {

// task_manager/verlet-integration-11, PHASE4 - the ONE sanctioned way to
// persist a model's CURRENT, live DynamicJointSettings (damping/stiffness/
// mass - see DynamicChainDefinition.h) back into its own *.gta file's
// metadata section (RigFileData::jointPhysicsOverrides, PHASE1), so a future
// PhysicsSystem::RegisterDynamicChains() (PHASE2) re-applies them the next
// time this model path is instantiated, in this session or any future one.
//
// Flattens EVERY joint of EVERY chain in `chains` into one JointPhysicsOverride
// per joint (keyed by DynamicChainDefinition::jointBoneIndices - see
// PHASE0_MASTER_STRATEGY.md, Step 2.4) - this is a full SNAPSHOT of the
// model's current joint tuning, not a diff of only what a user actually
// touched this session; every joint's current value (whether hand-edited or
// still exactly the auto-detected default) is written, which is what makes a
// later ApplyJointPhysicsOverrides() (PHASE2) call reproduce the saved state
// exactly, with no ambiguity about which joints were "really" edited.
//
// Reads the EXISTING *.gta at `absoluteGtaPath`, decodes its CURRENT
// RigFileData, and rewrites ONLY jointPhysicsOverrides before writing the
// file back out - every other piece of that file (mesh payload, GUID, flags,
// version, skeleton, skin weights, morphs, materials) is preserved
// byte-for-byte/value-for-value unchanged. Deliberately conservative on
// failure: returns false (and writes NOTHING to disk) if the file can't be
// read, isn't an AssetType::Mesh *.gta, or its existing metadata can't be
// decoded as a RigFileData - this function must NEVER silently replace an
// undecodable file's metadata with a mostly-empty, freshly-constructed
// RigFileData, which would destroy that file's real skeleton/skin-weight/
// morph/material data (see PHASE0_MASTER_STRATEGY.md's risk register).
//
// Pure w.r.t. `chains` (never mutates it) - the one side effect is the file
// write itself. No ImGui/Editor dependency whatsoever, so this is callable
// (and Tier-1-testable against a real temp file, matching GtaFileTests.cpp/
// RigFileTests.cpp's own existing precedent for real-file-I/O tests with no
// GPU/SDL/ImGui involved) from anywhere, not just Panels/InspectorPanel.cpp.
// Does NOT itself refresh any MeshAssetGpuCatalog cache entry - that is a
// deliberately separate concern (PHASE3's RefreshCachedJointPhysicsOverridesFromDisk(),
// called by this phase's own Editor button handler) so this function stays
// free of any Game/Instantiation-layer dependency and remains trivially
// unit-testable on its own.
//
// `outErrorMessage`, if non-null, is set to a short, human-readable reason on
// failure (for a future Editor toast/log line) - left untouched on success.
bool SaveJointPhysicsOverridesToGtaFile(const std::string& absoluteGtaPath,
    const std::vector<DynamicChainDefinition>& chains, std::string* outErrorMessage = nullptr);

} // namespace gte
