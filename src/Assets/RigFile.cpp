#include "RigFile.h"

#include <cstring>

namespace gte {

namespace {

// --- Binary writer ---------------------------------------------------------
// A small, local append-only byte writer - same "plain helper functions
// operating on a std::vector<uint8_t>&" shape as MeshFile.cpp's own
// AppendU32()/AppendArray(), just with a few more primitive types (strings,
// Vec3/Vec4/Quat, bool-as-u8) since RigFileData's shape is far less
// uniform than MeshData's tightly-packed float arrays.
class BinaryWriter {
public:
    explicit BinaryWriter(std::vector<std::uint8_t>& bytes) : m_bytes(bytes) { }

    void U8(std::uint8_t v) { m_bytes.push_back(v); }
    void Bool(bool v) { U8(v ? 1 : 0); }

    void U16(std::uint16_t v)
    {
        U8(static_cast<std::uint8_t>(v & 0xFF));
        U8(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }

    void U32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            U8(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }

    void F32(float v)
    {
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }

    void String(const std::string& s)
    {
        U32(static_cast<std::uint32_t>(s.size()));
        m_bytes.insert(m_bytes.end(), s.begin(), s.end());
    }

    void Vec3Val(const Vec3& v) { F32(v.x); F32(v.y); F32(v.z); }
    void Vec4Val(const Vec4& v) { F32(v.x); F32(v.y); F32(v.z); F32(v.w); }
    void QuatVal(const Quat& q) { F32(q.x); F32(q.y); F32(q.z); F32(q.w); }

private:
    std::vector<std::uint8_t>& m_bytes;
};

// --- Binary reader ---------------------------------------------------------
// Mirrors BinaryWriter above. "Sticky failure" convention: once any Read*
// call runs past the end of `m_bytes`, every subsequent Read* call becomes a
// no-op (leaving the output untouched) and Ok() latches false - this lets
// every decode function below just read fields in a straight line without
// checking a return value after every single call, then check Ok() once at
// the very end, same ergonomic shape as istream's own failbit.
class BinaryReader {
public:
    BinaryReader(const std::vector<std::uint8_t>& bytes, std::size_t cursor) : m_bytes(bytes), m_cursor(cursor) { }

    bool Ok() const noexcept { return m_ok; }
    std::size_t Cursor() const noexcept { return m_cursor; }

    std::uint8_t U8()
    {
        if (!EnsureAvailable(1)) {
            return 0;
        }
        return m_bytes[m_cursor++];
    }

    bool Bool() { return U8() != 0; }

    std::uint16_t U16()
    {
        const std::uint16_t lo = U8();
        const std::uint16_t hi = U8();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }

    std::uint32_t U32()
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            v |= static_cast<std::uint32_t>(U8()) << (8 * i);
        }
        return v;
    }

    std::int32_t I32() { return static_cast<std::int32_t>(U32()); }

    float F32()
    {
        const std::uint32_t bits = U32();
        float v;
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    std::string String()
    {
        const std::uint32_t length = U32();
        if (!EnsureAvailable(length)) {
            return std::string();
        }
        std::string s(reinterpret_cast<const char*>(m_bytes.data() + m_cursor), length);
        m_cursor += length;
        return s;
    }

    Vec3 Vec3Val()
    {
        const float x = F32();
        const float y = F32();
        const float z = F32();
        return Vec3(x, y, z);
    }

    Vec4 Vec4Val()
    {
        const float x = F32();
        const float y = F32();
        const float z = F32();
        const float w = F32();
        return Vec4(x, y, z, w);
    }

    Quat QuatVal()
    {
        const float x = F32();
        const float y = F32();
        const float z = F32();
        const float w = F32();
        return Quat(x, y, z, w);
    }

private:
    bool EnsureAvailable(std::size_t count)
    {
        if (!m_ok || m_cursor + count > m_bytes.size()) {
            m_ok = false;
            return false;
        }
        return true;
    }

    const std::vector<std::uint8_t>& m_bytes;
    std::size_t m_cursor;
    bool m_ok = true;
};

void WriteSkinWeights(BinaryWriter& w, const std::vector<VertexSkinWeights>& skinWeights)
{
    w.U32(static_cast<std::uint32_t>(skinWeights.size()));
    for (const auto& sw : skinWeights) {
        w.U8(static_cast<std::uint8_t>(sw.type));
        for (int i = 0; i < 4; ++i) {
            w.I32(sw.boneIndices[i]);
        }
        for (int i = 0; i < 4; ++i) {
            w.F32(sw.boneWeights[i]);
        }
        w.Vec3Val(sw.sdefC);
        w.Vec3Val(sw.sdefR0);
        w.Vec3Val(sw.sdefR1);
    }
}

bool ReadSkinWeights(BinaryReader& r, std::vector<VertexSkinWeights>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        VertexSkinWeights sw;
        sw.type = static_cast<VertexWeightType>(r.U8());
        for (int j = 0; j < 4; ++j) {
            sw.boneIndices[j] = r.I32();
        }
        for (int j = 0; j < 4; ++j) {
            sw.boneWeights[j] = r.F32();
        }
        sw.sdefC = r.Vec3Val();
        sw.sdefR0 = r.Vec3Val();
        sw.sdefR1 = r.Vec3Val();
        out->push_back(sw);
    }
    return r.Ok();
}

void WriteBones(BinaryWriter& w, const std::vector<Bone>& bones)
{
    w.U32(static_cast<std::uint32_t>(bones.size()));
    for (const auto& bone : bones) {
        w.String(bone.name);
        w.String(bone.englishName);
        w.Vec3Val(bone.position);
        w.I32(bone.parentBoneIndex);
        w.I32(bone.deformDepth);

        w.Bool(bone.rotatable);
        w.Bool(bone.translatable);
        w.Bool(bone.visible);
        w.Bool(bone.controllable);
        w.Bool(bone.isIk);
        w.Bool(bone.deformAfterPhysics);

        w.Bool(bone.tailIsBone);
        w.Vec3Val(bone.tailOffset);
        w.I32(bone.tailBoneIndex);

        w.Bool(bone.appendRotate);
        w.Bool(bone.appendTranslate);
        w.Bool(bone.appendLocal);
        w.I32(bone.appendBoneIndex);
        w.F32(bone.appendWeight);

        w.Bool(bone.hasFixedAxis);
        w.Vec3Val(bone.fixedAxis);

        w.Bool(bone.hasLocalAxis);
        w.Vec3Val(bone.localXAxis);
        w.Vec3Val(bone.localZAxis);

        w.Bool(bone.hasExternalParent);
        w.I32(bone.externalParentKey);

        w.I32(bone.ikTargetBoneIndex);
        w.I32(bone.ikIterationCount);
        w.F32(bone.ikAngleLimitRadians);
        w.U32(static_cast<std::uint32_t>(bone.ikLinks.size()));
        for (const auto& link : bone.ikLinks) {
            w.I32(link.boneIndex);
            w.Bool(link.hasAngleLimit);
            w.Vec3Val(link.angleLimitMin);
            w.Vec3Val(link.angleLimitMax);
        }
    }
}

bool ReadBones(BinaryReader& r, std::vector<Bone>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        Bone bone;
        bone.name = r.String();
        bone.englishName = r.String();
        bone.position = r.Vec3Val();
        bone.parentBoneIndex = r.I32();
        bone.deformDepth = r.I32();

        bone.rotatable = r.Bool();
        bone.translatable = r.Bool();
        bone.visible = r.Bool();
        bone.controllable = r.Bool();
        bone.isIk = r.Bool();
        bone.deformAfterPhysics = r.Bool();

        bone.tailIsBone = r.Bool();
        bone.tailOffset = r.Vec3Val();
        bone.tailBoneIndex = r.I32();

        bone.appendRotate = r.Bool();
        bone.appendTranslate = r.Bool();
        bone.appendLocal = r.Bool();
        bone.appendBoneIndex = r.I32();
        bone.appendWeight = r.F32();

        bone.hasFixedAxis = r.Bool();
        bone.fixedAxis = r.Vec3Val();

        bone.hasLocalAxis = r.Bool();
        bone.localXAxis = r.Vec3Val();
        bone.localZAxis = r.Vec3Val();

        bone.hasExternalParent = r.Bool();
        bone.externalParentKey = r.I32();

        bone.ikTargetBoneIndex = r.I32();
        bone.ikIterationCount = r.I32();
        bone.ikAngleLimitRadians = r.F32();
        const std::uint32_t ikLinkCount = r.U32();
        bone.ikLinks.reserve(ikLinkCount);
        for (std::uint32_t j = 0; j < ikLinkCount && r.Ok(); ++j) {
            Bone::IkLink link;
            link.boneIndex = r.I32();
            link.hasAngleLimit = r.Bool();
            link.angleLimitMin = r.Vec3Val();
            link.angleLimitMax = r.Vec3Val();
            bone.ikLinks.push_back(link);
        }

        out->push_back(std::move(bone));
    }
    return r.Ok();
}

void WriteMorphs(BinaryWriter& w, const std::vector<Morph>& morphs)
{
    w.U32(static_cast<std::uint32_t>(morphs.size()));
    for (const auto& morph : morphs) {
        w.String(morph.name);
        w.String(morph.englishName);
        w.U8(morph.controlPanel);
        w.U8(static_cast<std::uint8_t>(morph.type));

        w.U32(static_cast<std::uint32_t>(morph.positionOffsets.size()));
        for (const auto& o : morph.positionOffsets) {
            w.I32(o.vertexIndex);
            w.Vec3Val(o.offset);
        }

        w.U32(static_cast<std::uint32_t>(morph.uvOffsets.size()));
        for (const auto& o : morph.uvOffsets) {
            w.I32(o.vertexIndex);
            w.Vec4Val(o.offset);
        }

        w.U32(static_cast<std::uint32_t>(morph.boneOffsets.size()));
        for (const auto& o : morph.boneOffsets) {
            w.I32(o.boneIndex);
            w.Vec3Val(o.translation);
            w.QuatVal(o.rotation);
        }

        w.U32(static_cast<std::uint32_t>(morph.materialOffsets.size()));
        for (const auto& o : morph.materialOffsets) {
            w.I32(o.materialIndex);
            w.U8(static_cast<std::uint8_t>(o.op));
            w.Vec4Val(o.diffuse);
            w.Vec3Val(o.specular);
            w.F32(o.specularPower);
            w.Vec3Val(o.ambient);
            w.Vec4Val(o.edgeColor);
            w.F32(o.edgeSize);
            w.Vec4Val(o.textureFactor);
            w.Vec4Val(o.sphereTextureFactor);
            w.Vec4Val(o.toonTextureFactor);
        }

        w.U32(static_cast<std::uint32_t>(morph.groupOffsets.size()));
        for (const auto& o : morph.groupOffsets) {
            w.I32(o.morphIndex);
            w.F32(o.weight);
        }

        w.U32(static_cast<std::uint32_t>(morph.flipOffsets.size()));
        for (const auto& o : morph.flipOffsets) {
            w.I32(o.morphIndex);
            w.F32(o.weight);
        }

        w.U32(static_cast<std::uint32_t>(morph.impulseOffsets.size()));
        for (const auto& o : morph.impulseOffsets) {
            w.I32(o.rigidBodyIndex);
            w.Bool(o.isLocal);
            w.Vec3Val(o.velocity);
            w.Vec3Val(o.torque);
        }
    }
}

bool ReadMorphs(BinaryReader& r, std::vector<Morph>* out)
{
    const std::uint32_t count = r.U32();
    out->clear();
    out->reserve(count);
    for (std::uint32_t i = 0; i < count && r.Ok(); ++i) {
        Morph morph;
        morph.name = r.String();
        morph.englishName = r.String();
        morph.controlPanel = r.U8();
        morph.type = static_cast<MorphType>(r.U8());

        const std::uint32_t positionCount = r.U32();
        morph.positionOffsets.reserve(positionCount);
        for (std::uint32_t j = 0; j < positionCount && r.Ok(); ++j) {
            Morph::PositionOffset o;
            o.vertexIndex = r.I32();
            o.offset = r.Vec3Val();
            morph.positionOffsets.push_back(o);
        }

        const std::uint32_t uvCount = r.U32();
        morph.uvOffsets.reserve(uvCount);
        for (std::uint32_t j = 0; j < uvCount && r.Ok(); ++j) {
            Morph::UvOffset o;
            o.vertexIndex = r.I32();
            o.offset = r.Vec4Val();
            morph.uvOffsets.push_back(o);
        }

        const std::uint32_t boneCount = r.U32();
        morph.boneOffsets.reserve(boneCount);
        for (std::uint32_t j = 0; j < boneCount && r.Ok(); ++j) {
            Morph::BoneOffset o;
            o.boneIndex = r.I32();
            o.translation = r.Vec3Val();
            o.rotation = r.QuatVal();
            morph.boneOffsets.push_back(o);
        }

        const std::uint32_t materialCount = r.U32();
        morph.materialOffsets.reserve(materialCount);
        for (std::uint32_t j = 0; j < materialCount && r.Ok(); ++j) {
            Morph::MaterialOffset o;
            o.materialIndex = r.I32();
            o.op = static_cast<Morph::MaterialOffset::OpType>(r.U8());
            o.diffuse = r.Vec4Val();
            o.specular = r.Vec3Val();
            o.specularPower = r.F32();
            o.ambient = r.Vec3Val();
            o.edgeColor = r.Vec4Val();
            o.edgeSize = r.F32();
            o.textureFactor = r.Vec4Val();
            o.sphereTextureFactor = r.Vec4Val();
            o.toonTextureFactor = r.Vec4Val();
            morph.materialOffsets.push_back(o);
        }

        const std::uint32_t groupCount = r.U32();
        morph.groupOffsets.reserve(groupCount);
        for (std::uint32_t j = 0; j < groupCount && r.Ok(); ++j) {
            Morph::GroupOffset o;
            o.morphIndex = r.I32();
            o.weight = r.F32();
            morph.groupOffsets.push_back(o);
        }

        const std::uint32_t flipCount = r.U32();
        morph.flipOffsets.reserve(flipCount);
        for (std::uint32_t j = 0; j < flipCount && r.Ok(); ++j) {
            Morph::FlipOffset o;
            o.morphIndex = r.I32();
            o.weight = r.F32();
            morph.flipOffsets.push_back(o);
        }

        const std::uint32_t impulseCount = r.U32();
        morph.impulseOffsets.reserve(impulseCount);
        for (std::uint32_t j = 0; j < impulseCount && r.Ok(); ++j) {
            Morph::ImpulseOffset o;
            o.rigidBodyIndex = r.I32();
            o.isLocal = r.Bool();
            o.velocity = r.Vec3Val();
            o.torque = r.Vec3Val();
            morph.impulseOffsets.push_back(o);
        }

        out->push_back(std::move(morph));
    }
    return r.Ok();
}

void WritePhysics(BinaryWriter& w, const PhysicsData& physics)
{
    w.U32(static_cast<std::uint32_t>(physics.rigidBodies.size()));
    for (const auto& body : physics.rigidBodies) {
        w.String(body.name);
        w.String(body.englishName);
        w.I32(body.boneIndex);
        w.U8(body.group);
        w.U16(body.collisionGroupMask);
        w.U8(static_cast<std::uint8_t>(body.shape));
        w.Vec3Val(body.shapeSize);
        w.Vec3Val(body.translate);
        w.Vec3Val(body.rotateRadians);
        w.F32(body.mass);
        w.F32(body.linearDamping);
        w.F32(body.angularDamping);
        w.F32(body.restitution);
        w.F32(body.friction);
        w.U8(static_cast<std::uint8_t>(body.motionType));
    }

    w.U32(static_cast<std::uint32_t>(physics.joints.size()));
    for (const auto& joint : physics.joints) {
        w.String(joint.name);
        w.String(joint.englishName);
        w.U8(static_cast<std::uint8_t>(joint.type));
        w.I32(joint.rigidBodyAIndex);
        w.I32(joint.rigidBodyBIndex);
        w.Vec3Val(joint.translate);
        w.Vec3Val(joint.rotateRadians);
        w.Vec3Val(joint.translateLowerLimit);
        w.Vec3Val(joint.translateUpperLimit);
        w.Vec3Val(joint.rotateLowerLimit);
        w.Vec3Val(joint.rotateUpperLimit);
        w.Vec3Val(joint.springTranslateFactor);
        w.Vec3Val(joint.springRotateFactor);
    }
}

bool ReadPhysics(BinaryReader& r, PhysicsData* out)
{
    const std::uint32_t bodyCount = r.U32();
    out->rigidBodies.clear();
    out->rigidBodies.reserve(bodyCount);
    for (std::uint32_t i = 0; i < bodyCount && r.Ok(); ++i) {
        RigidBody body;
        body.name = r.String();
        body.englishName = r.String();
        body.boneIndex = r.I32();
        body.group = r.U8();
        body.collisionGroupMask = r.U16();
        body.shape = static_cast<RigidBodyShape>(r.U8());
        body.shapeSize = r.Vec3Val();
        body.translate = r.Vec3Val();
        body.rotateRadians = r.Vec3Val();
        body.mass = r.F32();
        body.linearDamping = r.F32();
        body.angularDamping = r.F32();
        body.restitution = r.F32();
        body.friction = r.F32();
        body.motionType = static_cast<RigidBodyMotionType>(r.U8());
        out->rigidBodies.push_back(std::move(body));
    }

    const std::uint32_t jointCount = r.U32();
    out->joints.clear();
    out->joints.reserve(jointCount);
    for (std::uint32_t i = 0; i < jointCount && r.Ok(); ++i) {
        Joint joint;
        joint.name = r.String();
        joint.englishName = r.String();
        joint.type = static_cast<JointType>(r.U8());
        joint.rigidBodyAIndex = r.I32();
        joint.rigidBodyBIndex = r.I32();
        joint.translate = r.Vec3Val();
        joint.rotateRadians = r.Vec3Val();
        joint.translateLowerLimit = r.Vec3Val();
        joint.translateUpperLimit = r.Vec3Val();
        joint.rotateLowerLimit = r.Vec3Val();
        joint.rotateUpperLimit = r.Vec3Val();
        joint.springTranslateFactor = r.Vec3Val();
        joint.springRotateFactor = r.Vec3Val();
        out->joints.push_back(std::move(joint));
    }

    return r.Ok();
}

} // namespace

std::vector<std::uint8_t> EncodeRigDataToBytes(const RigFileData& rig)
{
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), kRigFileMagic, kRigFileMagic + sizeof(kRigFileMagic));

    BinaryWriter w(bytes);
    WriteSkinWeights(w, rig.skinWeights);
    WriteBones(w, rig.skeleton.bones);
    WriteMorphs(w, rig.morphs.morphs);
    WritePhysics(w, rig.physics);

    return bytes;
}

std::optional<RigFileData> DecodeRigDataFromBytes(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < sizeof(kRigFileMagic)) {
        return std::nullopt;
    }
    if (std::memcmp(bytes.data(), kRigFileMagic, sizeof(kRigFileMagic)) != 0) {
        return std::nullopt;
    }

    BinaryReader r(bytes, sizeof(kRigFileMagic));

    RigFileData rig;
    if (!ReadSkinWeights(r, &rig.skinWeights)) {
        return std::nullopt;
    }
    if (!ReadBones(r, &rig.skeleton.bones)) {
        return std::nullopt;
    }
    if (!ReadMorphs(r, &rig.morphs.morphs)) {
        return std::nullopt;
    }
    if (!ReadPhysics(r, &rig.physics)) {
        return std::nullopt;
    }

    return rig;
}

} // namespace gte
