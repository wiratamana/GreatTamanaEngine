#include "DynamicChainPhysicsPersistence.h"

#include "../../Assets/AssetTypes.h"
#include "../../Assets/GtaFile.h"
#include "../../Assets/RigFile.h"
#include "../../Encoding/Utf8Path.h"

#include <filesystem>

namespace gte {

namespace {

void SetError(std::string* outErrorMessage, const char* message)
{
    if (outErrorMessage != nullptr) {
        *outErrorMessage = message;
    }
}

} // namespace

bool SaveJointPhysicsOverridesToGtaFile(
    const std::string& absoluteGtaPath, const std::vector<DynamicChainDefinition>& chains, std::string* outErrorMessage)
{
    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absoluteGtaPath));
    if (!gta.has_value()) {
        SetError(outErrorMessage, "Could not read the *.gta file (missing, unreadable, or bad magic).");
        return false;
    }
    if (gta->header.Type() != AssetType::Mesh) {
        SetError(outErrorMessage, "This *.gta file is not an AssetType::Mesh asset.");
        return false;
    }

    std::optional<RigFileData> rig = DecodeRigDataFromBytes(gta->metadata);
    if (!rig.has_value()) {
        // Deliberately refuse rather than fabricate a fresh RigFileData -
        // see this function's own header comment. A Mesh *.gta with a
        // DynamicChainRig-bearing entity spawned from it is guaranteed to
        // already have decodable rig metadata (PhysicsSystem::
        // RegisterDynamicChains() would never have detected any chain
        // otherwise) - reaching this branch means something is genuinely
        // wrong with the file, not merely "this model has no rig data yet."
        SetError(outErrorMessage, "This *.gta file's existing metadata could not be decoded - refusing to overwrite it.");
        return false;
    }

    // Flatten every joint of every chain into one JointPhysicsOverride each -
    // a full current-state snapshot, not a diff (see header comment).
    std::vector<JointPhysicsOverride> overrides;
    std::size_t totalJoints = 0;
    for (const DynamicChainDefinition& chain : chains) {
        totalJoints += chain.jointBoneIndices.size();
    }
    overrides.reserve(totalJoints);
    for (const DynamicChainDefinition& chain : chains) {
        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
            const DynamicJointSettings& settings = chain.jointSettings[i];
            overrides.push_back(JointPhysicsOverride{ chain.jointBoneIndices[i], settings.damping, settings.stiffness, settings.mass });
        }
    }
    rig->jointPhysicsOverrides = std::move(overrides);

    const std::vector<std::uint8_t> newMetadata = EncodeRigDataToBytes(*rig);
    const bool wrote = WriteGtaFile(PathFromUtf8(absoluteGtaPath), gta->header.Type(), gta->header.Id(),
        gta->header.Flags(), newMetadata, gta->payload, gta->header.version);
    if (!wrote) {
        SetError(outErrorMessage, "Failed to write the *.gta file back to disk.");
        return false;
    }
    return true;
}

} // namespace gte
