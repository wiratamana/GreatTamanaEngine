// Unit tests for src/Assets/PmxLoader.h - LoadPmxModel()'s extraction of
// vertex positions/normals/UVs/triangle indices out of a MikuMikuDance .pmx
// model file. Touches a real temp directory (created/torn down by the
// fixture below, same convention as AssetImporterTests.cpp) but no GPU/SDL/
// ImGui at all - "Tier 1" per tests/CMakeLists.txt's own taxonomy. Always
// built - src/Assets/ has no GTE_ENABLE_EDITOR/GTE_ENABLE_PROJECT_PANEL
// dependency.

#include "Assets/PmxLoader.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Hand-built, byte-precise minimal .pmx file: one triangle, no bones/
// morphs/materials/textures/rigid bodies/joints - just enough of the binary
// format (see third_party/saba/src/Saba/Model/MMD/PMXFile.cpp's ReadPMXFile
// for the exact field-by-field read order this must match) to exercise
// LoadPmxModel()'s real parsing path without depending on a large external
// model file. Same "construct the exact binary format by hand" approach as
// GtaFileTests.cpp/Ktx2EncoderTests.cpp's BuildMinimal2x2Bmp().
class PmxByteWriter {
public:
    void U8(std::uint8_t v) { m_bytes.push_back(v); }

    void U32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            m_bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void I32(std::int32_t v) { U32(static_cast<std::uint32_t>(v)); }

    void F32(float v)
    {
        static_assert(sizeof(float) == 4, "expected 32-bit float");
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }

    void U16(std::uint16_t v)
    {
        m_bytes.push_back(static_cast<std::uint8_t>(v & 0xFF));
        m_bytes.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    }

    // PMX string: a uint32_t byte length followed by that many raw bytes -
    // written here always as plain UTF-8 (matches BuildMinimalTrianglePmx()'s
    // own header encode == 1 byte below - see ReadString() in
    // third_party/saba/src/Saba/Model/MMD/PMXFile.cpp).
    void Str(const std::string& s)
    {
        U32(static_cast<std::uint32_t>(s.size()));
        m_bytes.insert(m_bytes.end(), s.begin(), s.end());
    }

    // A glm::vec3-shaped field, in x/y/z order.
    void Vec3F(float x, float y, float z) { F32(x); F32(y); F32(z); }

    // A glm::quat-shaped field. GLM's default (non-GLM_FORCE_QUAT_DATA_WXYZ)
    // in-memory layout is x/y/z/w (see third_party glm's own
    // detail/type_quat.hpp) - matches saba's raw Read(&q, file) byte-copy.
    void QuatF(float x, float y, float z, float w) { F32(x); F32(y); F32(z); F32(w); }

    const std::vector<std::uint8_t>& Bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
};

// Builds one BDEF1-weighted vertex: position, normal, uv, (zero addUV
// entries), weight type (0 == BDEF1), one 1-byte bone index, edge magnitude.
void WriteVertex(PmxByteWriter& w, float px, float py, float pz, float nx, float ny, float nz, float u, float v)
{
    w.F32(px); w.F32(py); w.F32(pz);
    w.F32(nx); w.F32(ny); w.F32(nz);
    w.F32(u); w.F32(v);
    w.U8(0); // PMXVertexWeight::BDEF1
    w.U8(0); // bone index (boneIndexSize == 1 below), unused by LoadPmxModel()
    w.F32(0.0f); // edge magnitude
}

std::vector<std::uint8_t> BuildMinimalTrianglePmx()
{
    PmxByteWriter w;

    // --- Header ---
    w.U8('P'); w.U8('M'); w.U8('X'); w.U8(' '); // magic (4 bytes, MMDFileString<4> only reads Size bytes)
    w.F32(2.0f); // version
    w.U8(8); // dataSize (read but never used by the parser)
    w.U8(1); // encode: 1 == UTF-8
    w.U8(0); // addUVNum
    w.U8(1); // vertexIndexSize
    w.U8(1); // textureIndexSize
    w.U8(1); // materialIndexSize
    w.U8(1); // boneIndexSize
    w.U8(1); // morphIndexSize
    w.U8(1); // rigidbodyIndexSize

    // --- Info --- (modelName/englishModelName/comment/englishComment, all empty)
    w.U32(0); w.U32(0); w.U32(0); w.U32(0);

    // --- Vertices ---
    w.I32(3);
    WriteVertex(w, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    WriteVertex(w, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f);
    WriteVertex(w, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f);

    // --- Faces --- (raw index count, ReadFace() divides by 3 for face count)
    w.I32(3);
    w.U8(0); w.U8(1); w.U8(2);

    // --- Textures / Materials / Bones / Morphs / DisplayFrames / Rigidbodies / Joints ---
    // (all empty - the file ends exactly here, so ReadPMXFile()'s trailing
    // "is there a softbody section?" Tell() < GetSize() check is false and
    // ReadSoftbody() is correctly skipped)
    w.I32(0); // textures
    w.I32(0); // materials
    w.I32(0); // bones
    w.I32(0); // morphs
    w.I32(0); // display frames
    w.I32(0); // rigidbodies
    w.I32(0); // joints

    return w.Bytes();
}

// --- A richer fixture: bones (with IK/flags), morphs, physics, and all ---
// --- four requested vertex weight types (BDEF1/BDEF2/BDEF4/SDEF).      ---
// Same "construct the exact binary format by hand" approach as
// BuildMinimalTrianglePmx() above - see that function's own comment - just
// exercising the sections it deliberately leaves empty. Field-by-field
// order mirrors third_party/saba/src/Saba/Model/MMD/PMXFile.cpp's
// ReadBone()/ReadMorph()/ReadRigidbody()/ReadJoint() exactly.

void WriteVertexBdef1(PmxByteWriter& w, float px, float py, float pz, std::uint8_t bone0)
{
    w.Vec3F(px, py, pz);
    w.Vec3F(0.0f, 0.0f, 1.0f); // normal
    w.F32(0.0f); w.F32(0.0f); // uv
    w.U8(0); // PMXVertexWeight::BDEF1
    w.U8(bone0);
    w.F32(0.0f); // edge magnitude
}

void WriteVertexBdef2(PmxByteWriter& w, float px, float py, float pz, std::uint8_t bone0, std::uint8_t bone1, float weight0)
{
    w.Vec3F(px, py, pz);
    w.Vec3F(0.0f, 0.0f, 1.0f);
    w.F32(0.0f); w.F32(0.0f);
    w.U8(1); // PMXVertexWeight::BDEF2
    w.U8(bone0); w.U8(bone1);
    w.F32(weight0);
    w.F32(0.0f);
}

void WriteVertexBdef4(PmxByteWriter& w, float px, float py, float pz, const std::uint8_t bones[4], const float weights[4])
{
    w.Vec3F(px, py, pz);
    w.Vec3F(0.0f, 0.0f, 1.0f);
    w.F32(0.0f); w.F32(0.0f);
    w.U8(2); // PMXVertexWeight::BDEF4
    for (int i = 0; i < 4; ++i) {
        w.U8(bones[i]);
    }
    for (int i = 0; i < 4; ++i) {
        w.F32(weights[i]);
    }
    w.F32(0.0f);
}

void WriteVertexSdef(PmxByteWriter& w, float px, float py, float pz, std::uint8_t bone0, std::uint8_t bone1, float weight0)
{
    w.Vec3F(px, py, pz);
    w.Vec3F(0.0f, 0.0f, 1.0f);
    w.F32(0.0f); w.F32(0.0f);
    w.U8(3); // PMXVertexWeight::SDEF
    w.U8(bone0); w.U8(bone1);
    w.F32(weight0);
    w.Vec3F(0.1f, 0.2f, 0.3f); // sdefC
    w.Vec3F(0.4f, 0.5f, 0.6f); // sdefR0
    w.Vec3F(0.7f, 0.8f, 0.9f); // sdefR1
    w.F32(0.0f);
}

std::vector<std::uint8_t> BuildRiggedPmx()
{
    PmxByteWriter w;

    // --- Header ---
    w.U8('P'); w.U8('M'); w.U8('X'); w.U8(' ');
    w.F32(2.0f);
    w.U8(8);
    w.U8(1); // encode: UTF-8
    w.U8(0); // addUVNum
    w.U8(1); // vertexIndexSize
    w.U8(1); // textureIndexSize
    w.U8(1); // materialIndexSize
    w.U8(1); // boneIndexSize
    w.U8(1); // morphIndexSize
    w.U8(1); // rigidbodyIndexSize

    // --- Info ---
    w.U32(0); w.U32(0); w.U32(0); w.U32(0);

    // --- Vertices --- (one of each requested weight type)
    w.I32(4);
    WriteVertexBdef1(w, 0.0f, 0.0f, 0.0f, 0); // v0: BDEF1, bone 0
    WriteVertexBdef2(w, 1.0f, 0.0f, 0.0f, 0, 1, 0.7f); // v1: BDEF2, bones 0/1
    {
        const std::uint8_t bones[4] = { 0, 1, 0, 1 };
        const float weights[4] = { 0.4f, 0.3f, 0.2f, 0.1f };
        WriteVertexBdef4(w, 0.0f, 1.0f, 0.0f, bones, weights); // v2: BDEF4
    }
    WriteVertexSdef(w, 1.0f, 1.0f, 0.0f, 0, 1, 0.6f); // v3: SDEF, bones 0/1

    // --- Faces --- (two triangles covering all 4 vertices)
    w.I32(6);
    w.U8(0); w.U8(1); w.U8(2);
    w.U8(1); w.U8(3); w.U8(2);

    // --- Textures / Materials --- (both empty)
    w.I32(0);
    w.I32(0);

    // --- Bones --- (Root -> Tip, Tip has an IK chain back to Root)
    w.I32(2);
    {
        // Bone 0: "Root" - AllowRotate|AllowTranslate|Visible|AllowControl (0x1E), no IK.
        w.Str("Root");
        w.Str("Root_en");
        w.Vec3F(0.0f, 0.0f, 0.0f); // position
        w.U8(0xFF); // parentBoneIndex == -1 (boneIndexSize == 1 sentinel)
        w.I32(0); // deformDepth
        w.U16(0x001E); // boneFlag
        w.Vec3F(0.0f, 1.0f, 0.0f); // positionOffset (TargetShowMode bit unset)
    }
    {
        // Bone 1: "Tip" - same flags plus IK (0x3E), parented to bone 0, one
        // IK link back to bone 0 with an angle limit.
        w.Str("Tip");
        w.Str("Tip_en");
        w.Vec3F(0.0f, 1.0f, 0.0f);
        w.U8(0); // parentBoneIndex == 0
        w.I32(1); // deformDepth
        w.U16(0x003E); // boneFlag (AllowRotate|AllowTranslate|Visible|AllowControl|IK)
        w.Vec3F(0.0f, 0.5f, 0.0f); // positionOffset
        // IK block:
        w.U8(0); // ikTargetBoneIndex == 0
        w.I32(10); // ikIterationCount
        w.F32(0.5f); // ikLimit (radians)
        w.I32(1); // ikLinks count
        w.U8(0); // link boneIndex == 0
        w.U8(1); // enableLimit == true
        w.Vec3F(-1.0f, -1.0f, -1.0f); // limitMin
        w.Vec3F(1.0f, 1.0f, 1.0f); // limitMax
    }

    // --- Morphs --- (one Position/"blend shape" morph, one Bone morph)
    w.I32(2);
    {
        // Morph 0: Position ("Brow" panel), 2 vertex offsets.
        w.Str("Brow");
        w.Str("Brow_en");
        w.U8(1); // controlPanel: 1 == eyebrow
        w.U8(1); // PMXMorphType::Position
        w.I32(2); // dataCount
        w.U8(0); w.Vec3F(0.1f, 0.0f, 0.0f); // vertexIndex 0
        w.U8(1); w.Vec3F(0.0f, 0.1f, 0.0f); // vertexIndex 1
    }
    {
        // Morph 1: Bone, 1 offset onto bone 1.
        w.Str("BoneMorph");
        w.Str("BoneMorph_en");
        w.U8(0); // controlPanel: 0 == system-reserved
        w.U8(2); // PMXMorphType::Bone
        w.I32(1); // dataCount
        w.U8(1); // boneIndex == 1
        w.Vec3F(0.0f, 0.05f, 0.0f); // translation
        w.QuatF(0.0f, 0.0f, 0.0f, 1.0f); // rotation (identity)
    }

    // --- Display frames --- (empty)
    w.I32(0);

    // --- Rigid bodies --- (one Box, Dynamic, attached to bone 1)
    w.I32(1);
    {
        w.Str("RB0");
        w.Str("RB0_en");
        w.U8(1); // boneIndex == 1
        w.U8(0); // group
        w.U16(0xFFFF); // collisionGroup mask
        w.U8(1); // Shape::Box
        w.Vec3F(0.5f, 0.5f, 0.5f); // shapeSize
        w.Vec3F(0.0f, 1.0f, 0.0f); // translate
        w.Vec3F(0.0f, 0.0f, 0.0f); // rotate
        w.F32(1.0f); // mass
        w.F32(0.5f); // translateDimmer
        w.F32(0.5f); // rotateDimmer
        w.F32(0.1f); // repulsion
        w.F32(0.2f); // friction
        w.U8(1); // Operation::Dynamic
    }

    // --- Joints --- (one Hinge, connecting rigid body 0 to "nothing")
    w.I32(1);
    {
        w.Str("J0");
        w.Str("J0_en");
        w.U8(5); // JointType::Hinge
        w.U8(0); // rigidbodyAIndex == 0
        w.U8(0xFF); // rigidbodyBIndex == -1 (rigidbodyIndexSize == 1 sentinel)
        w.Vec3F(0.0f, 1.0f, 0.0f); // translate
        w.Vec3F(0.0f, 0.0f, 0.0f); // rotate
        w.Vec3F(0.0f, 0.0f, 0.0f); // translateLowerLimit
        w.Vec3F(0.0f, 0.0f, 0.0f); // translateUpperLimit
        w.Vec3F(-1.0f, 0.0f, 0.0f); // rotateLowerLimit
        w.Vec3F(1.0f, 0.0f, 0.0f); // rotateUpperLimit
        w.Vec3F(0.0f, 0.0f, 0.0f); // springTranslateFactor
        w.Vec3F(0.0f, 0.0f, 0.0f); // springRotateFactor
    }

    return w.Bytes();
}

class PmxLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GtePmxLoaderTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    static void WriteBinaryFile(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    std::filesystem::path m_root;
};

TEST_F(PmxLoaderTest, ExtractsPositionsNormalsUvsAndIndicesFromAMinimalTriangle)
{
    const std::filesystem::path path = m_root / "triangle.pmx";
    WriteBinaryFile(path, BuildMinimalTrianglePmx());

    const PmxLoadResult result = LoadPmxModel(path.string());

    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.positions.size(), 3u);
    ASSERT_EQ(result.mesh.normals.size(), 3u);
    ASSERT_EQ(result.mesh.uvs.size(), 3u);
    ASSERT_EQ(result.mesh.indices.size(), 3u);

    EXPECT_EQ(result.mesh.positions[0], Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[1], Vec3(1.0f, 0.0f, 0.0f));
    EXPECT_EQ(result.mesh.positions[2], Vec3(0.0f, 1.0f, 0.0f));

    for (const auto& normal : result.mesh.normals) {
        EXPECT_EQ(normal, Vec3(0.0f, 0.0f, 1.0f));
    }

    EXPECT_EQ(result.mesh.uvs[0], Vec2(0.0f, 0.0f));
    EXPECT_EQ(result.mesh.uvs[1], Vec2(1.0f, 0.0f));
    EXPECT_EQ(result.mesh.uvs[2], Vec2(0.0f, 1.0f));

    EXPECT_EQ(result.mesh.indices[0], 0u);
    EXPECT_EQ(result.mesh.indices[1], 1u);
    EXPECT_EQ(result.mesh.indices[2], 2u);

    EXPECT_FALSE(result.message.empty());
}

TEST_F(PmxLoaderTest, FailsGracefullyWhenFileDoesNotExist)
{
    const PmxLoadResult result = LoadPmxModel((m_root / "DoesNotExist.pmx").string());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.mesh.positions.empty());
    EXPECT_TRUE(result.mesh.normals.empty());
    EXPECT_TRUE(result.mesh.uvs.empty());
    EXPECT_TRUE(result.mesh.indices.empty());
    EXPECT_FALSE(result.message.empty());
}

TEST_F(PmxLoaderTest, FailsGracefullyOnATruncatedFile)
{
    std::vector<std::uint8_t> bytes = BuildMinimalTrianglePmx();
    bytes.resize(bytes.size() / 2); // Cut off mid-way through the vertex section.

    const std::filesystem::path path = m_root / "truncated.pmx";
    WriteBinaryFile(path, bytes);

    const PmxLoadResult result = LoadPmxModel(path.string());
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.message.empty());
}

// --- Skin weights (BDEF1/BDEF2/BDEF4/SDEF) ----------------------------------

TEST_F(PmxLoaderTest, ExtractsSkinWeightsForBdef1Bdef2Bdef4AndSdef)
{
    const std::filesystem::path path = m_root / "rigged.pmx";
    WriteBinaryFile(path, BuildRiggedPmx());

    const PmxLoadResult result = LoadPmxModel(path.string());
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.mesh.skinWeights.size(), 4u);

    const VertexSkinWeights& v0 = result.mesh.skinWeights[0];
    EXPECT_EQ(v0.type, VertexWeightType::BDEF1);
    EXPECT_EQ(v0.boneIndices[0], 0);
    EXPECT_FLOAT_EQ(v0.boneWeights[0], 1.0f);
    EXPECT_EQ(v0.boneIndices[1], -1);

    const VertexSkinWeights& v1 = result.mesh.skinWeights[1];
    EXPECT_EQ(v1.type, VertexWeightType::BDEF2);
    EXPECT_EQ(v1.boneIndices[0], 0);
    EXPECT_EQ(v1.boneIndices[1], 1);
    EXPECT_FLOAT_EQ(v1.boneWeights[0], 0.7f);
    EXPECT_FLOAT_EQ(v1.boneWeights[1], 0.3f); // 1 - 0.7, derived by LoadPmxModel()

    const VertexSkinWeights& v2 = result.mesh.skinWeights[2];
    EXPECT_EQ(v2.type, VertexWeightType::BDEF4);
    EXPECT_EQ(v2.boneIndices[0], 0);
    EXPECT_EQ(v2.boneIndices[1], 1);
    EXPECT_EQ(v2.boneIndices[2], 0);
    EXPECT_EQ(v2.boneIndices[3], 1);
    EXPECT_FLOAT_EQ(v2.boneWeights[0], 0.4f);
    EXPECT_FLOAT_EQ(v2.boneWeights[1], 0.3f);
    EXPECT_FLOAT_EQ(v2.boneWeights[2], 0.2f);
    EXPECT_FLOAT_EQ(v2.boneWeights[3], 0.1f);

    const VertexSkinWeights& v3 = result.mesh.skinWeights[3];
    EXPECT_EQ(v3.type, VertexWeightType::SDEF);
    EXPECT_EQ(v3.boneIndices[0], 0);
    EXPECT_EQ(v3.boneIndices[1], 1);
    EXPECT_FLOAT_EQ(v3.boneWeights[0], 0.6f);
    EXPECT_FLOAT_EQ(v3.boneWeights[1], 0.4f);
    EXPECT_EQ(v3.sdefC, Vec3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(v3.sdefR0, Vec3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(v3.sdefR1, Vec3(0.7f, 0.8f, 0.9f));

    // Non-SDEF vertices must never carry stray SDEF correction data.
    EXPECT_EQ(v0.sdefC, Vec3::Zero());
    EXPECT_EQ(v1.sdefC, Vec3::Zero());
    EXPECT_EQ(v2.sdefC, Vec3::Zero());
}

// --- Bones (hierarchy, flags, IK) -------------------------------------------

TEST_F(PmxLoaderTest, ExtractsBoneHierarchyWithFlagsAndIk)
{
    const std::filesystem::path path = m_root / "rigged.pmx";
    WriteBinaryFile(path, BuildRiggedPmx());

    const PmxLoadResult result = LoadPmxModel(path.string());
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.skeleton.bones.size(), 2u);

    const Bone& root = result.skeleton.bones[0];
    EXPECT_EQ(root.name, "Root");
    EXPECT_EQ(root.englishName, "Root_en");
    EXPECT_EQ(root.position, Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_EQ(root.parentBoneIndex, -1);
    EXPECT_EQ(root.deformDepth, 0);
    EXPECT_TRUE(root.rotatable);
    EXPECT_TRUE(root.translatable);
    EXPECT_TRUE(root.visible);
    EXPECT_TRUE(root.controllable);
    EXPECT_FALSE(root.isIk);
    EXPECT_FALSE(root.tailIsBone);
    EXPECT_EQ(root.tailOffset, Vec3(0.0f, 1.0f, 0.0f));

    const Bone& tip = result.skeleton.bones[1];
    EXPECT_EQ(tip.name, "Tip");
    EXPECT_EQ(tip.parentBoneIndex, 0);
    EXPECT_EQ(tip.deformDepth, 1);
    EXPECT_TRUE(tip.isIk);
    EXPECT_EQ(tip.ikTargetBoneIndex, 0);
    EXPECT_EQ(tip.ikIterationCount, 10);
    EXPECT_FLOAT_EQ(tip.ikAngleLimitRadians, 0.5f);
    ASSERT_EQ(tip.ikLinks.size(), 1u);
    EXPECT_EQ(tip.ikLinks[0].boneIndex, 0);
    EXPECT_TRUE(tip.ikLinks[0].hasAngleLimit);
    EXPECT_EQ(tip.ikLinks[0].angleLimitMin, Vec3(-1.0f, -1.0f, -1.0f));
    EXPECT_EQ(tip.ikLinks[0].angleLimitMax, Vec3(1.0f, 1.0f, 1.0f));
}

// --- Morphs (blend shapes) --------------------------------------------------

TEST_F(PmxLoaderTest, ExtractsPositionAndBoneMorphs)
{
    const std::filesystem::path path = m_root / "rigged.pmx";
    WriteBinaryFile(path, BuildRiggedPmx());

    const PmxLoadResult result = LoadPmxModel(path.string());
    ASSERT_TRUE(result.success) << result.message;
    ASSERT_EQ(result.morphs.morphs.size(), 2u);

    const Morph& brow = result.morphs.morphs[0];
    EXPECT_EQ(brow.name, "Brow");
    EXPECT_EQ(brow.controlPanel, 1);
    EXPECT_EQ(brow.type, MorphType::Position);
    ASSERT_EQ(brow.positionOffsets.size(), 2u);
    EXPECT_EQ(brow.positionOffsets[0].vertexIndex, 0);
    EXPECT_EQ(brow.positionOffsets[0].offset, Vec3(0.1f, 0.0f, 0.0f));
    EXPECT_EQ(brow.positionOffsets[1].vertexIndex, 1);
    EXPECT_EQ(brow.positionOffsets[1].offset, Vec3(0.0f, 0.1f, 0.0f));

    const Morph& boneMorph = result.morphs.morphs[1];
    EXPECT_EQ(boneMorph.name, "BoneMorph");
    EXPECT_EQ(boneMorph.type, MorphType::Bone);
    ASSERT_EQ(boneMorph.boneOffsets.size(), 1u);
    EXPECT_EQ(boneMorph.boneOffsets[0].boneIndex, 1);
    EXPECT_EQ(boneMorph.boneOffsets[0].translation, Vec3(0.0f, 0.05f, 0.0f));
    EXPECT_TRUE(RepresentSameRotation(boneMorph.boneOffsets[0].rotation, Quat::Identity()));
}

// --- Physics: rigid bodies and joints ---------------------------------------

TEST_F(PmxLoaderTest, ExtractsRigidBodiesAndJoints)
{
    const std::filesystem::path path = m_root / "rigged.pmx";
    WriteBinaryFile(path, BuildRiggedPmx());

    const PmxLoadResult result = LoadPmxModel(path.string());
    ASSERT_TRUE(result.success) << result.message;

    ASSERT_EQ(result.physics.rigidBodies.size(), 1u);
    const RigidBody& body = result.physics.rigidBodies[0];
    EXPECT_EQ(body.name, "RB0");
    EXPECT_EQ(body.boneIndex, 1);
    EXPECT_EQ(body.shape, RigidBodyShape::Box);
    EXPECT_EQ(body.shapeSize, Vec3(0.5f, 0.5f, 0.5f));
    EXPECT_EQ(body.translate, Vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(body.mass, 1.0f);
    EXPECT_FLOAT_EQ(body.restitution, 0.1f);
    EXPECT_FLOAT_EQ(body.friction, 0.2f);
    EXPECT_EQ(body.motionType, RigidBodyMotionType::Dynamic);
    EXPECT_EQ(body.collisionGroupMask, 0xFFFFu);

    ASSERT_EQ(result.physics.joints.size(), 1u);
    const Joint& joint = result.physics.joints[0];
    EXPECT_EQ(joint.name, "J0");
    EXPECT_EQ(joint.type, JointType::Hinge);
    EXPECT_EQ(joint.rigidBodyAIndex, 0);
    EXPECT_EQ(joint.rigidBodyBIndex, -1);
    EXPECT_EQ(joint.rotateLowerLimit, Vec3(-1.0f, 0.0f, 0.0f));
    EXPECT_EQ(joint.rotateUpperLimit, Vec3(1.0f, 0.0f, 0.0f));
}

// Optional real-world smoke test: if the MMD test model this integration was
// verified against (see AGENTS.md-style task notes for this session) is
// present on THIS machine, actually parse it end-to-end and sanity-check the
// extracted counts - a genuine, non-synthetic .pmx file exercising every
// section BuildMinimalTrianglePmx() above deliberately leaves empty
// (materials/bones/morphs/textures/...). Never a hard requirement: the
// model is a large third-party asset that isn't (and shouldn't be) vendored
// into this repo, so this GTEST_SKIP()s cleanly on every other machine/CI
// runner instead of failing. Finds any *.pmx file inside the fixture
// directory via directory_iterator (rather than a hardcoded exact filename)
// deliberately - the real fixture's filename contains non-ASCII (Japanese)
// characters, and matching by extension sidesteps any narrow-string-literal
// source-encoding pitfalls entirely.
TEST(PmxLoaderRealModelSmokeTest, LoadsAnMmdModelIfPresentOnThisMachine)
{
    const std::filesystem::path directory = "C:\\Users\\F5954\\Documents\\TAMANA\\MMD_Model_Furina";

    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec)) {
        GTEST_SKIP() << "MMD test model directory not present on this machine - skipping.";
    }

    std::filesystem::path pmxPath;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".pmx") {
            pmxPath = entry.path();
            break;
        }
    }

    if (pmxPath.empty()) {
        GTEST_SKIP() << "No .pmx file found in the MMD test model directory - skipping.";
    }

    const PmxLoadResult result = LoadPmxModel(pmxPath.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_GT(result.mesh.positions.size(), 0u);
    EXPECT_EQ(result.mesh.positions.size(), result.mesh.normals.size());
    EXPECT_EQ(result.mesh.positions.size(), result.mesh.uvs.size());
    EXPECT_GT(result.mesh.indices.size(), 0u);
    EXPECT_EQ(result.mesh.indices.size() % 3, 0u); // Always a whole number of triangles.

    // Skinning/bones/morphs/physics (this session's additions) - a real MMD
    // model always carries per-vertex skin weights (mesh.skinWeights is
    // always 1:1 with mesh.positions - see PmxLoadResult's own doc comment)
    // and, for a typical rigged/jiggle-physics character model like this
    // fixture, at least one bone/morph/rigid body/joint too.
    EXPECT_EQ(result.mesh.skinWeights.size(), result.mesh.positions.size());
    EXPECT_GT(result.skeleton.bones.size(), 0u);
    EXPECT_GT(result.morphs.morphs.size(), 0u);
    EXPECT_GT(result.physics.rigidBodies.size(), 0u);
    EXPECT_GT(result.physics.joints.size(), 0u);

    // Every skin weight's bone indices must reference a real bone (or -1 for
    // an unused influence slot) - never an out-of-range index.
    for (const auto& sw : result.mesh.skinWeights) {
        for (int i = 0; i < 4; ++i) {
            EXPECT_GE(sw.boneIndices[i], -1);
            EXPECT_LT(sw.boneIndices[i], static_cast<std::int32_t>(result.skeleton.bones.size()));
        }
    }

    // Informational only (not an assertion) - makes the real vertex/
    // triangle/bone/morph/physics counts visible in the test log when this
    // actually runs.
    std::cout << "[PmxLoaderRealModelSmokeTest] " << pmxPath.string() << ": "
              << result.mesh.positions.size() << " vertices, "
              << (result.mesh.indices.size() / 3) << " triangles, "
              << result.skeleton.bones.size() << " bones, "
              << result.morphs.morphs.size() << " morphs, "
              << result.physics.rigidBodies.size() << " rigid bodies, "
              << result.physics.joints.size() << " joints\n";
}


} // namespace
} // namespace gte
