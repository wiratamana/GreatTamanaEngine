#include "PmxLoader.h"

#include <Saba/Model/MMD/PMXFile.h>

#include <filesystem>

namespace gte {

namespace {

// --- saba/glm -> gte plain-type conversions ---------------------------
// Kept local to this .cpp on purpose - no saba::/glm:: type ever crosses
// PmxLoader.h's own public API (see that header's own doc comment).

Vec3 ToVec3(const glm::vec3& v) { return Vec3(v.x, v.y, v.z); }
Vec4 ToVec4(const glm::vec4& v) { return Vec4(v.x, v.y, v.z, v.w); }
Quat ToQuat(const glm::quat& q) { return Quat(q.x, q.y, q.z, q.w); }

bool HasBoneFlag(saba::PMXBoneFlags flags, saba::PMXBoneFlags bit)
{
    return (static_cast<std::uint16_t>(flags) & static_cast<std::uint16_t>(bit)) != 0;
}

// --- Materials / Textures --------------------------------------------------

// std::filesystem::path(const std::string&) goes through the OS's native
// narrow encoding (the current ANSI codepage on Windows), NOT UTF-8 - same
// pitfall Game.cpp's own Utf8PathFromGamePath() helper exists to avoid (see
// that file's comment). `filePath` here is already UTF-8 (LoadPmxModel()'s
// own parameter contract - see PmxLoader.h), so it must go through the
// std::u8string round-trip too, same as PathToUtf8()/Utf8ToPath() do
// elsewhere in this engine (AssetImporter.cpp/Game.cpp) - duplicated locally
// rather than shared, for the same "src/Assets/ never depends on
// src/Editor/" reasoning AssetImporter.cpp's own PathToUtf8() comment gives.
std::filesystem::path Utf8ToPath(const std::string& utf8)
{
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

std::string PathToUtf8(const std::filesystem::path& path)
{
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// Resolves one PMX texture record (a path RELATIVE to the .pmx file itself,
// using whatever separator the authoring tool happened to write - PMX does
// not mandate forward slashes) into an ABSOLUTE, UTF-8 path - see
// MaterialData::textures' own doc comment for why this has to happen here,
// at import time, rather than later. std::filesystem::path's own operator/
// treats a backslash exactly like a forward slash on Windows (this engine's
// only target platform - see AGENTS.md), so no manual separator rewriting
// is needed. Does NOT check the resolved path actually exists - a dangling
// reference (moved/deleted texture, or one the .pmx references but never
// shipped) is left for a later consumer (Game::EnsureMeshAsset()) to
// discover and degrade gracefully from, exactly like every other
// best-effort file lookup in this engine.
std::string ResolveTexturePath(const std::filesystem::path& pmxDirectory, const std::string& rawTextureName)
{
    if (rawTextureName.empty()) {
        return std::string();
    }
    const std::filesystem::path resolved = pmxDirectory / Utf8ToPath(rawTextureName);
    return PathToUtf8(resolved.lexically_normal());
}

SphereTextureMode ConvertSphereMode(saba::PMXSphereMode mode)
{
    switch (mode) {
    case saba::PMXSphereMode::None: return SphereTextureMode::Disabled;
    case saba::PMXSphereMode::Mul: return SphereTextureMode::Multiply;
    case saba::PMXSphereMode::Add: return SphereTextureMode::Add;
    case saba::PMXSphereMode::SubTexture: return SphereTextureMode::SubTexture;
    }
    return SphereTextureMode::Disabled;
}

bool HasDrawModeFlag(saba::PMXDrawModeFlags flags, saba::PMXDrawModeFlags bit)
{
    return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(bit)) != 0;
}

Material ConvertMaterial(const saba::PMXMaterial& material)
{
    Material out;
    out.name = material.m_name;
    out.englishName = material.m_englishName;

    out.diffuse = ToVec4(material.m_diffuse);
    out.specular = ToVec3(material.m_specular);
    out.specularPower = material.m_specularPower;
    out.ambient = ToVec3(material.m_ambient);

    out.bothFacesVisible = HasDrawModeFlag(material.m_drawMode, saba::PMXDrawModeFlags::BothFace);
    out.drawEdge = HasDrawModeFlag(material.m_drawMode, saba::PMXDrawModeFlags::DrawEdge);
    out.edgeColor = ToVec4(material.m_edgeColor);
    out.edgeSize = material.m_edgeSize;

    out.textureIndex = material.m_textureIndex;
    out.sphereTextureIndex = material.m_sphereTextureIndex;
    out.sphereMode = ConvertSphereMode(material.m_sphereMode);

    out.useSharedToon = material.m_toonMode == saba::PMXToonMode::Common;
    if (out.useSharedToon) {
        // saba stores the shared toon index directly in m_toonTextureIndex
        // for PMXToonMode::Common (0-9, toon01.bmp..toon10.bmp) - never a
        // MaterialData::textures index in that case.
        out.sharedToonIndex = static_cast<std::uint8_t>(material.m_toonTextureIndex);
    } else {
        out.toonTextureIndex = material.m_toonTextureIndex;
    }

    out.indexCount = static_cast<std::uint32_t>(material.m_numFaceVertices);

    return out;
}

// Converts the WHOLE texture list + material list together (rather than
// two independent per-record converters like ConvertBone()/ConvertMorph()
// above) purely because texture PATH RESOLUTION (see ResolveTexturePath()
// above) needs `filePath`'s own directory, which only LoadPmxModel() itself
// has in hand - every other Convert*() function in this file is a pure,
// context-free per-record mapping.
MaterialData ConvertMaterialData(const saba::PMXFile& pmxFile, const std::string& filePath)
{
    MaterialData out;

    const std::filesystem::path pmxDirectory = Utf8ToPath(filePath).parent_path();
    out.textures.reserve(pmxFile.m_textures.size());
    for (const auto& texture : pmxFile.m_textures) {
        out.textures.push_back(ResolveTexturePath(pmxDirectory, texture.m_textureName));
    }

    out.materials.reserve(pmxFile.m_materials.size());
    for (const auto& material : pmxFile.m_materials) {
        out.materials.push_back(ConvertMaterial(material));
    }

    return out;
}

// --- Vertex skin weights (BDEF1/BDEF2/BDEF4/SDEF/QDEF) ------------------

VertexSkinWeights ConvertVertexSkinWeights(const saba::PMXVertex& vertex)
{
    VertexSkinWeights out;

    switch (vertex.m_weightType) {
    case saba::PMXVertexWeight::BDEF1:
        out.type = VertexWeightType::BDEF1;
        out.boneIndices[0] = vertex.m_boneIndices[0];
        out.boneWeights[0] = 1.0f;
        break;

    case saba::PMXVertexWeight::BDEF2:
        out.type = VertexWeightType::BDEF2;
        out.boneIndices[0] = vertex.m_boneIndices[0];
        out.boneIndices[1] = vertex.m_boneIndices[1];
        out.boneWeights[0] = vertex.m_boneWeights[0];
        out.boneWeights[1] = 1.0f - vertex.m_boneWeights[0];
        break;

    case saba::PMXVertexWeight::BDEF4:
        out.type = VertexWeightType::BDEF4;
        for (int i = 0; i < 4; ++i) {
            out.boneIndices[i] = vertex.m_boneIndices[i];
            out.boneWeights[i] = vertex.m_boneWeights[i];
        }
        break;

    case saba::PMXVertexWeight::SDEF:
        out.type = VertexWeightType::SDEF;
        out.boneIndices[0] = vertex.m_boneIndices[0];
        out.boneIndices[1] = vertex.m_boneIndices[1];
        out.boneWeights[0] = vertex.m_boneWeights[0];
        out.boneWeights[1] = 1.0f - vertex.m_boneWeights[0];
        out.sdefC = ToVec3(vertex.m_sdefC);
        out.sdefR0 = ToVec3(vertex.m_sdefR0);
        out.sdefR1 = ToVec3(vertex.m_sdefR1);
        break;

    case saba::PMXVertexWeight::QDEF:
        // Same 4-bone/4-weight shape as BDEF4 - see MeshData.h's own
        // VertexWeightType::QDEF comment. NOTE: saba's own QDEF reader
        // (third_party/saba/src/Saba/Model/MMD/PMXFile.cpp's ReadVertex())
        // has a known upstream indexing quirk (it never populates
        // m_boneWeights[2]), so a QDEF vertex's third weight may come
        // through as whatever that array slot happened to already hold
        // rather than a value actually read from the file. QDEF is an
        // extremely rare PMX 2.1 extension in practice; every real-world
        // model this engine has been verified against uses BDEF1/BDEF2/
        // BDEF4/SDEF only.
        out.type = VertexWeightType::QDEF;
        for (int i = 0; i < 4; ++i) {
            out.boneIndices[i] = vertex.m_boneIndices[i];
            out.boneWeights[i] = vertex.m_boneWeights[i];
        }
        break;
    }

    return out;
}

// --- Bones ---------------------------------------------------------------

Bone ConvertBone(const saba::PMXBone& bone)
{
    Bone out;
    out.name = bone.m_name;
    out.englishName = bone.m_englishName;
    out.position = ToVec3(bone.m_position);
    out.parentBoneIndex = bone.m_parentBoneIndex;
    out.deformDepth = bone.m_deformDepth;

    out.rotatable = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::AllowRotate);
    out.translatable = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::AllowTranslate);
    out.visible = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::Visible);
    out.controllable = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::AllowControl);
    out.deformAfterPhysics = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::DeformAfterPhysics);

    out.tailIsBone = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::TargetShowMode);
    if (out.tailIsBone) {
        out.tailBoneIndex = bone.m_linkBoneIndex;
    } else {
        out.tailOffset = ToVec3(bone.m_positionOffset);
    }

    out.appendRotate = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::AppendRotate);
    out.appendTranslate = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::AppendTranslate);
    if (out.appendRotate || out.appendTranslate) {
        out.appendLocal = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::AppendLocal);
        out.appendBoneIndex = bone.m_appendBoneIndex;
        out.appendWeight = bone.m_appendWeight;
    }

    out.hasFixedAxis = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::FixedAxis);
    if (out.hasFixedAxis) {
        out.fixedAxis = ToVec3(bone.m_fixedAxis);
    }

    out.hasLocalAxis = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::LocalAxis);
    if (out.hasLocalAxis) {
        out.localXAxis = ToVec3(bone.m_localXAxis);
        out.localZAxis = ToVec3(bone.m_localZAxis);
    }

    out.hasExternalParent = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::DeformOuterParent);
    if (out.hasExternalParent) {
        out.externalParentKey = bone.m_keyValue;
    }

    out.isIk = HasBoneFlag(bone.m_boneFlag, saba::PMXBoneFlags::IK);
    if (out.isIk) {
        out.ikTargetBoneIndex = bone.m_ikTargetBoneIndex;
        out.ikIterationCount = bone.m_ikIterationCount;
        out.ikAngleLimitRadians = bone.m_ikLimit;

        out.ikLinks.reserve(bone.m_ikLinks.size());
        for (const auto& link : bone.m_ikLinks) {
            Bone::IkLink ikLink;
            ikLink.boneIndex = link.m_ikBoneIndex;
            ikLink.hasAngleLimit = link.m_enableLimit != 0;
            ikLink.angleLimitMin = ToVec3(link.m_limitMin);
            ikLink.angleLimitMax = ToVec3(link.m_limitMax);
            out.ikLinks.push_back(ikLink);
        }
    }

    return out;
}

// --- Morphs ---------------------------------------------------------------

MorphType ConvertMorphType(saba::PMXMorphType type)
{
    switch (type) {
    case saba::PMXMorphType::Group: return MorphType::Group;
    case saba::PMXMorphType::Position: return MorphType::Position;
    case saba::PMXMorphType::Bone: return MorphType::Bone;
    case saba::PMXMorphType::UV: return MorphType::Uv;
    case saba::PMXMorphType::AddUV1: return MorphType::AddUv1;
    case saba::PMXMorphType::AddUV2: return MorphType::AddUv2;
    case saba::PMXMorphType::AddUV3: return MorphType::AddUv3;
    case saba::PMXMorphType::AddUV4: return MorphType::AddUv4;
    case saba::PMXMorphType::Material: return MorphType::Material;
    case saba::PMXMorphType::Flip: return MorphType::Flip;
    case saba::PMXMorphType::Impluse: return MorphType::Impulse;
    }
    return MorphType::Group;
}

Morph ConvertMorph(const saba::PMXMorph& morph)
{
    Morph out;
    out.name = morph.m_name;
    out.englishName = morph.m_englishName;
    out.controlPanel = morph.m_controlPanel;
    out.type = ConvertMorphType(morph.m_morphType);

    out.positionOffsets.reserve(morph.m_positionMorph.size());
    for (const auto& m : morph.m_positionMorph) {
        out.positionOffsets.push_back({ m.m_vertexIndex, ToVec3(m.m_position) });
    }

    out.uvOffsets.reserve(morph.m_uvMorph.size());
    for (const auto& m : morph.m_uvMorph) {
        out.uvOffsets.push_back({ m.m_vertexIndex, ToVec4(m.m_uv) });
    }

    out.boneOffsets.reserve(morph.m_boneMorph.size());
    for (const auto& m : morph.m_boneMorph) {
        Morph::BoneOffset offset;
        offset.boneIndex = m.m_boneIndex;
        offset.translation = ToVec3(m.m_position);
        offset.rotation = ToQuat(m.m_quaternion);
        out.boneOffsets.push_back(offset);
    }

    out.materialOffsets.reserve(morph.m_materialMorph.size());
    for (const auto& m : morph.m_materialMorph) {
        Morph::MaterialOffset offset;
        offset.materialIndex = m.m_materialIndex;
        offset.op = (m.m_opType == saba::PMXMorph::MaterialMorph::OpType::Mul)
            ? Morph::MaterialOffset::OpType::Multiply
            : Morph::MaterialOffset::OpType::Add;
        offset.diffuse = ToVec4(m.m_diffuse);
        offset.specular = ToVec3(m.m_specular);
        offset.specularPower = m.m_specularPower;
        offset.ambient = ToVec3(m.m_ambient);
        offset.edgeColor = ToVec4(m.m_edgeColor);
        offset.edgeSize = m.m_edgeSize;
        offset.textureFactor = ToVec4(m.m_textureFactor);
        offset.sphereTextureFactor = ToVec4(m.m_sphereTextureFactor);
        offset.toonTextureFactor = ToVec4(m.m_toonTextureFactor);
        out.materialOffsets.push_back(offset);
    }

    out.groupOffsets.reserve(morph.m_groupMorph.size());
    for (const auto& m : morph.m_groupMorph) {
        out.groupOffsets.push_back({ m.m_morphIndex, m.m_weight });
    }

    out.flipOffsets.reserve(morph.m_flipMorph.size());
    for (const auto& m : morph.m_flipMorph) {
        out.flipOffsets.push_back({ m.m_morphIndex, m.m_weight });
    }

    out.impulseOffsets.reserve(morph.m_impulseMorph.size());
    for (const auto& m : morph.m_impulseMorph) {
        Morph::ImpulseOffset offset;
        offset.rigidBodyIndex = m.m_rigidbodyIndex;
        offset.isLocal = m.m_localFlag != 0;
        offset.velocity = ToVec3(m.m_translateVelocity);
        offset.torque = ToVec3(m.m_rotateTorque);
        out.impulseOffsets.push_back(offset);
    }

    return out;
}

// --- Physics: rigid bodies + joints ---------------------------------------

RigidBodyShape ConvertRigidBodyShape(saba::PMXRigidbody::Shape shape)
{
    switch (shape) {
    case saba::PMXRigidbody::Shape::Sphere: return RigidBodyShape::Sphere;
    case saba::PMXRigidbody::Shape::Box: return RigidBodyShape::Box;
    case saba::PMXRigidbody::Shape::Capsule: return RigidBodyShape::Capsule;
    }
    return RigidBodyShape::Sphere;
}

RigidBodyMotionType ConvertRigidBodyMotionType(saba::PMXRigidbody::Operation op)
{
    switch (op) {
    case saba::PMXRigidbody::Operation::Static: return RigidBodyMotionType::Static;
    case saba::PMXRigidbody::Operation::Dynamic: return RigidBodyMotionType::Dynamic;
    case saba::PMXRigidbody::Operation::DynamicAndBoneMerge: return RigidBodyMotionType::DynamicAndBoneMerge;
    }
    return RigidBodyMotionType::Static;
}

RigidBody ConvertRigidBody(const saba::PMXRigidbody& body)
{
    RigidBody out;
    out.name = body.m_name;
    out.englishName = body.m_englishName;
    out.boneIndex = body.m_boneIndex;
    out.group = body.m_group;
    out.collisionGroupMask = body.m_collisionGroup;
    out.shape = ConvertRigidBodyShape(body.m_shape);
    out.shapeSize = ToVec3(body.m_shapeSize);
    out.translate = ToVec3(body.m_translate);
    out.rotateRadians = ToVec3(body.m_rotate);
    out.mass = body.m_mass;
    out.linearDamping = body.m_translateDimmer;
    out.angularDamping = body.m_rotateDimmer;
    out.restitution = body.m_repulsion;
    out.friction = body.m_friction;
    out.motionType = ConvertRigidBodyMotionType(body.m_op);
    return out;
}

JointType ConvertJointType(saba::PMXJoint::JointType type)
{
    switch (type) {
    case saba::PMXJoint::JointType::SpringDOF6: return JointType::SpringDof6;
    case saba::PMXJoint::JointType::DOF6: return JointType::Dof6;
    case saba::PMXJoint::JointType::P2P: return JointType::P2P;
    case saba::PMXJoint::JointType::ConeTwist: return JointType::ConeTwist;
    case saba::PMXJoint::JointType::Slider: return JointType::Slider;
    case saba::PMXJoint::JointType::Hinge: return JointType::Hinge;
    }
    return JointType::SpringDof6;
}

Joint ConvertJoint(const saba::PMXJoint& joint)
{
    Joint out;
    out.name = joint.m_name;
    out.englishName = joint.m_englishName;
    out.type = ConvertJointType(joint.m_type);
    out.rigidBodyAIndex = joint.m_rigidbodyAIndex;
    out.rigidBodyBIndex = joint.m_rigidbodyBIndex;
    out.translate = ToVec3(joint.m_translate);
    out.rotateRadians = ToVec3(joint.m_rotate);
    out.translateLowerLimit = ToVec3(joint.m_translateLowerLimit);
    out.translateUpperLimit = ToVec3(joint.m_translateUpperLimit);
    out.rotateLowerLimit = ToVec3(joint.m_rotateLowerLimit);
    out.rotateUpperLimit = ToVec3(joint.m_rotateUpperLimit);
    out.springTranslateFactor = ToVec3(joint.m_springTranslateFactor);
    out.springRotateFactor = ToVec3(joint.m_springRotateFactor);
    return out;
}

} // namespace

PmxLoadResult LoadPmxModel(const std::string& filePath)
{
    PmxLoadResult result;

    saba::PMXFile pmxFile;
    if (!saba::ReadPMXFile(&pmxFile, filePath.c_str())) {
        result.success = false;
        result.message = "Failed to read PMX file: " + filePath;
        return result;
    }

    const std::size_t vertexCount = pmxFile.m_vertices.size();
    result.mesh.positions.reserve(vertexCount);
    result.mesh.normals.reserve(vertexCount);
    result.mesh.uvs.reserve(vertexCount);
    result.mesh.skinWeights.reserve(vertexCount);

    // Straight glm -> gte::Vec3/Vec2 field copies, nothing more - see this
    // function's own doc comment (PmxLoader.h) for why no axis/winding
    // remapping happens here. Skin weights (BDEF1/BDEF2/BDEF4/SDEF/QDEF)
    // are extracted the same pass, index-aligned with positions/normals/
    // uvs - see ConvertVertexSkinWeights() above.
    for (const auto& vertex : pmxFile.m_vertices) {
        result.mesh.positions.emplace_back(vertex.m_position.x, vertex.m_position.y, vertex.m_position.z);
        result.mesh.normals.emplace_back(vertex.m_normal.x, vertex.m_normal.y, vertex.m_normal.z);
        result.mesh.uvs.emplace_back(vertex.m_uv.x, vertex.m_uv.y);
        result.mesh.skinWeights.push_back(ConvertVertexSkinWeights(vertex));
    }

    result.mesh.indices.reserve(pmxFile.m_faces.size() * 3);
    for (const auto& face : pmxFile.m_faces) {
        result.mesh.indices.push_back(face.m_vertices[0]);
        result.mesh.indices.push_back(face.m_vertices[1]);
        result.mesh.indices.push_back(face.m_vertices[2]);
    }

    result.skeleton.bones.reserve(pmxFile.m_bones.size());
    for (const auto& bone : pmxFile.m_bones) {
        result.skeleton.bones.push_back(ConvertBone(bone));
    }

    result.morphs.morphs.reserve(pmxFile.m_morphs.size());
    for (const auto& morph : pmxFile.m_morphs) {
        result.morphs.morphs.push_back(ConvertMorph(morph));
    }

    result.physics.rigidBodies.reserve(pmxFile.m_rigidbodies.size());
    for (const auto& body : pmxFile.m_rigidbodies) {
        result.physics.rigidBodies.push_back(ConvertRigidBody(body));
    }

    result.physics.joints.reserve(pmxFile.m_joints.size());
    for (const auto& joint : pmxFile.m_joints) {
        result.physics.joints.push_back(ConvertJoint(joint));
    }

    result.materials = ConvertMaterialData(pmxFile, filePath);

    result.success = true;
    result.message = "Loaded PMX file: " + filePath
        + " (" + std::to_string(vertexCount) + " vertices, "
        + std::to_string(pmxFile.m_faces.size()) + " faces, "
        + std::to_string(result.skeleton.bones.size()) + " bones, "
        + std::to_string(result.morphs.morphs.size()) + " morphs, "
        + std::to_string(result.physics.rigidBodies.size()) + " rigid bodies, "
        + std::to_string(result.physics.joints.size()) + " joints, "
        + std::to_string(result.materials.materials.size()) + " materials, "
        + std::to_string(result.materials.textures.size()) + " textures)";
    return result;
}

} // namespace gte
