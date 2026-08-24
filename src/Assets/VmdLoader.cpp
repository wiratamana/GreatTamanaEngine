#include "VmdLoader.h"

#include <Saba/Model/MMD/VMDFile.h>

#include <cstring>

namespace gte {

namespace {

// --- saba/glm -> gte plain-type conversions ---------------------------
// Kept local to this .cpp on purpose - no saba::/glm:: type ever crosses
// VmdLoader.h's own public API (see that header's own doc comment). Same
// small helper shape as PmxLoader.cpp's own ToVec3()/ToQuat() - duplicated
// rather than shared, matching this engine's existing convention for such
// tiny, single-file-local conversions.

Vec3 ToVec3(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
Quat ToQuat(const glm::quat& q) { return Quat(q.x, q.y, q.z, q.w); }

void CopyInterpolation(const std::array<std::uint8_t, 64>& src, std::array<std::uint8_t, 64>* dst)
{
    std::memcpy(dst->data(), src.data(), src.size());
}

void CopyInterpolation(const std::array<std::uint8_t, 24>& src, std::array<std::uint8_t, 24>* dst)
{
    std::memcpy(dst->data(), src.data(), src.size());
}

BoneKeyframe ConvertBoneKeyframe(const saba::VMDMotion& motion)
{
    BoneKeyframe out;
    out.boneName = motion.m_boneName.ToUtf8String();
    out.frame = motion.m_frame;
    out.translation = ToVec3(motion.m_translate);
    out.rotation = ToQuat(motion.m_quaternion);
    CopyInterpolation(motion.m_interpolation, &out.interpolation);
    return out;
}

MorphKeyframe ConvertMorphKeyframe(const saba::VMDMorph& morph)
{
    MorphKeyframe out;
    out.morphName = morph.m_blendShapeName.ToUtf8String();
    out.frame = morph.m_frame;
    out.weight = morph.m_weight;
    return out;
}

CameraKeyframe ConvertCameraKeyframe(const saba::VMDCamera& camera)
{
    CameraKeyframe out;
    out.frame = camera.m_frame;
    out.distance = camera.m_distance;
    out.interest = ToVec3(camera.m_interest);
    out.rotateRadians = ToVec3(camera.m_rotate);
    CopyInterpolation(camera.m_interpolation, &out.interpolation);
    out.fieldOfViewDegrees = camera.m_viewAngle;
    out.isPerspective = camera.m_isPerspective != 0;
    return out;
}

LightKeyframe ConvertLightKeyframe(const saba::VMDLight& light)
{
    LightKeyframe out;
    out.frame = light.m_frame;
    out.color = ToVec3(light.m_color);
    out.direction = ToVec3(light.m_position);
    return out;
}

ShadowKeyframe ConvertShadowKeyframe(const saba::VMDShadow& shadow)
{
    ShadowKeyframe out;
    out.frame = shadow.m_frame;
    out.shadowType = shadow.m_shadowType;
    out.distance = shadow.m_distance;
    return out;
}

IkKeyframe ConvertIkKeyframe(const saba::VMDIk& ik)
{
    IkKeyframe out;
    out.frame = ik.m_frame;
    out.visible = ik.m_show != 0;
    out.states.reserve(ik.m_ikInfos.size());
    for (const auto& info : ik.m_ikInfos) {
        IkEnableState state;
        state.ikBoneName = info.m_name.ToUtf8String();
        state.enabled = info.m_enable != 0;
        out.states.push_back(std::move(state));
    }
    return out;
}

} // namespace

VmdLoadResult LoadVmdMotion(const std::string& filePath)
{
    VmdLoadResult result;

    saba::VMDFile vmdFile;
    if (!saba::ReadVMDFile(&vmdFile, filePath.c_str())) {
        result.success = false;
        result.message = "Failed to read VMD file: " + filePath;
        return result;
    }

    result.motion.modelName = vmdFile.m_header.m_modelName.ToUtf8String();

    result.motion.boneKeyframes.reserve(vmdFile.m_motions.size());
    for (const auto& motion : vmdFile.m_motions) {
        result.motion.boneKeyframes.push_back(ConvertBoneKeyframe(motion));
    }

    result.motion.morphKeyframes.reserve(vmdFile.m_morphs.size());
    for (const auto& morph : vmdFile.m_morphs) {
        result.motion.morphKeyframes.push_back(ConvertMorphKeyframe(morph));
    }

    result.motion.cameraKeyframes.reserve(vmdFile.m_cameras.size());
    for (const auto& camera : vmdFile.m_cameras) {
        result.motion.cameraKeyframes.push_back(ConvertCameraKeyframe(camera));
    }

    result.motion.lightKeyframes.reserve(vmdFile.m_lights.size());
    for (const auto& light : vmdFile.m_lights) {
        result.motion.lightKeyframes.push_back(ConvertLightKeyframe(light));
    }

    result.motion.shadowKeyframes.reserve(vmdFile.m_shadows.size());
    for (const auto& shadow : vmdFile.m_shadows) {
        result.motion.shadowKeyframes.push_back(ConvertShadowKeyframe(shadow));
    }

    result.motion.ikKeyframes.reserve(vmdFile.m_iks.size());
    for (const auto& ik : vmdFile.m_iks) {
        result.motion.ikKeyframes.push_back(ConvertIkKeyframe(ik));
    }

    result.success = true;
    result.message = "Loaded VMD file: " + filePath
        + " (" + std::to_string(result.motion.boneKeyframes.size()) + " bone keyframes, "
        + std::to_string(result.motion.morphKeyframes.size()) + " morph keyframes, "
        + std::to_string(result.motion.cameraKeyframes.size()) + " camera keyframes, "
        + std::to_string(result.motion.lightKeyframes.size()) + " light keyframes, "
        + std::to_string(result.motion.shadowKeyframes.size()) + " shadow keyframes, "
        + std::to_string(result.motion.ikKeyframes.size()) + " IK keyframes)";
    return result;
}

} // namespace gte
