// Unit tests for src/Assets/VmdLoader.h - LoadVmdMotion()'s extraction of
// bone/morph/camera/light/shadow/IK keyframe tracks out of a MikuMikuDance
// .vmd motion file. Touches a real temp directory (created/torn down by the
// fixture below, same convention as PmxLoaderTests.cpp/AssetImporterTests.cpp)
// but no GPU/SDL/ImGui at all - "Tier 1" per tests/CMakeLists.txt's own
// taxonomy. Always built - src/Assets/ has no GTE_ENABLE_EDITOR/
// GTE_ENABLE_PROJECT_PANEL dependency.

#include "Assets/VmdLoader.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include <gtest/gtest.h>

namespace gte {
namespace {

// Hand-built, byte-precise minimal/rich .vmd files: just enough of the
// binary format (see third_party/saba/src/Saba/Model/MMD/VMDFile.cpp's
// ReadVMDFile for the exact field-by-field read order this must match) to
// exercise LoadVmdMotion()'s real parsing path without depending on a large
// external motion file. Same "construct the exact binary format by hand"
// approach as PmxLoaderTests.cpp's BuildMinimalTrianglePmx()/
// GtaFileTests.cpp.
class VmdByteWriter {
public:
    void U8(std::uint8_t v) { m_bytes.push_back(v); }

    void U32(std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            m_bytes.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
        }
    }

    void F32(float v)
    {
        static_assert(sizeof(float) == 4, "expected 32-bit float");
        std::uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }

    void Vec3F(float x, float y, float z)
    {
        F32(x);
        F32(y);
        F32(z);
    }

    // A glm::quat-shaped field, x/y/z/w order (matches saba's raw
    // Read(&q, file) byte-copy - see PmxLoaderTests.cpp's own QuatF() for
    // the identical reasoning).
    void QuatF(float x, float y, float z, float w)
    {
        F32(x);
        F32(y);
        F32(z);
        F32(w);
    }

    // A VMD/MMDFileString<Size>-shaped field: exactly `length` raw bytes on
    // disk (NOT length-prefixed, unlike a PMX string) - `s`'s bytes, then
    // zero-padded out to `length`. Plain ASCII round-trips through saba's
    // Shift-JIS -> UTF-16 -> UTF-8 conversion unchanged (ASCII is a valid
    // Shift-JIS subset), so test names are kept plain ASCII deliberately.
    void FixedStr(const std::string& s, std::size_t length)
    {
        for (std::size_t i = 0; i < length; ++i) {
            m_bytes.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
        }
    }

    void RawBytes(std::size_t count, std::uint8_t fill = 0)
    {
        for (std::size_t i = 0; i < count; ++i) {
            m_bytes.push_back(fill);
        }
    }

    const std::vector<std::uint8_t>& Bytes() const { return m_bytes; }

private:
    std::vector<std::uint8_t> m_bytes;
};

void WriteVmdHeader(VmdByteWriter& w, const std::string& modelName)
{
    w.FixedStr("Vocaloid Motion Data 0002", 30);
    w.FixedStr(modelName, 20);
}

// Header + exactly one bone keyframe, nothing else - the file ends right
// after the motion section, matching ReadVMDFile()'s own "file.Tell() <
// file.GetSize()" gate for every trailing (optional) section, exactly like
// a real legacy (pre-blend-shape) VMD.
std::vector<std::uint8_t> BuildMinimalOneBoneKeyframeVmd()
{
    VmdByteWriter w;
    WriteVmdHeader(w, "TestModel");

    w.U32(1); // motion count
    w.FixedStr("Bone1", 15);
    w.U32(5); // frame
    w.Vec3F(1.0f, 2.0f, 3.0f); // translate
    w.QuatF(0.0f, 0.0f, 0.0f, 1.0f); // quaternion (identity)
    w.RawBytes(64); // interpolation, all zero

    return w.Bytes();
}

// Exercises every track: bones, morphs, camera, light, shadow, IK.
std::vector<std::uint8_t> BuildRichVmd()
{
    VmdByteWriter w;
    WriteVmdHeader(w, "RichModel");

    // --- Bone motions (2) ---
    w.U32(2);
    w.FixedStr("Bone1", 15);
    w.U32(0);
    w.Vec3F(0.0f, 0.0f, 0.0f);
    w.QuatF(0.0f, 0.0f, 0.0f, 1.0f);
    w.RawBytes(64);
    w.FixedStr("Bone2", 15);
    w.U32(10);
    w.Vec3F(1.5f, -2.5f, 0.5f);
    w.QuatF(0.0f, 0.70710678f, 0.0f, 0.70710678f);
    w.RawBytes(64, 20);

    // --- Morphs (1) ---
    w.U32(1);
    w.FixedStr("MorphA", 15);
    w.U32(3);
    w.F32(0.75f);

    // --- Camera (1) ---
    w.U32(1);
    w.U32(7); // frame
    w.F32(-45.0f); // distance
    w.Vec3F(0.0f, 10.0f, 0.0f); // interest
    w.Vec3F(0.1f, 0.2f, 0.3f); // rotate
    w.RawBytes(24, 5); // interpolation
    w.U32(30); // view angle (degrees)
    w.U8(1); // isPerspective

    // --- Light (1) ---
    w.U32(1);
    w.U32(8); // frame
    w.Vec3F(1.0f, 1.0f, 1.0f); // color
    w.Vec3F(0.0f, -1.0f, 0.0f); // direction

    // --- Shadow (1) ---
    w.U32(1);
    w.U32(9); // frame
    w.U8(1); // shadow type
    w.F32(6.5f); // distance

    // --- IK (1, with 2 ik-bone states) ---
    w.U32(1);
    w.U32(11); // frame
    w.U8(1); // show
    w.U32(2); // ik info count
    w.FixedStr("LeftLegIK", 20);
    w.U8(1); // enabled
    w.FixedStr("RightLegIK", 20);
    w.U8(0); // disabled

    return w.Bytes();
}

class VmdLoaderTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteVmdLoaderTest_") + info->test_suite_name() + "_" + info->name());

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

TEST_F(VmdLoaderTest, ExtractsASingleBoneKeyframeFromAMinimalMotion)
{
    const std::filesystem::path path = m_root / "one_bone.vmd";
    WriteBinaryFile(path, BuildMinimalOneBoneKeyframeVmd());

    const VmdLoadResult result = LoadVmdMotion(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.motion.modelName, "TestModel");
    ASSERT_EQ(result.motion.boneKeyframes.size(), 1u);

    const BoneKeyframe& kf = result.motion.boneKeyframes[0];
    EXPECT_EQ(kf.boneName, "Bone1");
    EXPECT_EQ(kf.frame, 5u);
    EXPECT_EQ(kf.translation, Vec3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(kf.rotation.w, 1.0f);

    EXPECT_TRUE(result.motion.morphKeyframes.empty());
    EXPECT_TRUE(result.motion.cameraKeyframes.empty());
    EXPECT_TRUE(result.motion.lightKeyframes.empty());
    EXPECT_TRUE(result.motion.shadowKeyframes.empty());
    EXPECT_TRUE(result.motion.ikKeyframes.empty());
    EXPECT_FALSE(result.message.empty());
}

TEST_F(VmdLoaderTest, FailsGracefullyWhenFileDoesNotExist)
{
    const VmdLoadResult result = LoadVmdMotion((m_root / "DoesNotExist.vmd").string());

    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.motion.boneKeyframes.empty());
    EXPECT_FALSE(result.message.empty());
}

TEST_F(VmdLoaderTest, FailsGracefullyOnATruncatedOrGarbageHeader)
{
    const std::filesystem::path path = m_root / "garbage.vmd";
    const std::vector<std::uint8_t> bytes(10, 0xAB); // Too short for even the 50-byte header.
    WriteBinaryFile(path, bytes);

    const VmdLoadResult result = LoadVmdMotion(path.string());
    EXPECT_FALSE(result.success);
}

TEST_F(VmdLoaderTest, ExtractsEveryTrackFromARichMotion)
{
    const std::filesystem::path path = m_root / "rich.vmd";
    WriteBinaryFile(path, BuildRichVmd());

    const VmdLoadResult result = LoadVmdMotion(path.string());

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.motion.modelName, "RichModel");

    ASSERT_EQ(result.motion.boneKeyframes.size(), 2u);
    EXPECT_EQ(result.motion.boneKeyframes[0].boneName, "Bone1");
    EXPECT_EQ(result.motion.boneKeyframes[1].boneName, "Bone2");
    EXPECT_EQ(result.motion.boneKeyframes[1].frame, 10u);
    EXPECT_EQ(result.motion.boneKeyframes[1].translation, Vec3(1.5f, -2.5f, 0.5f));
    EXPECT_EQ(result.motion.boneKeyframes[1].interpolation[0], 20);

    ASSERT_EQ(result.motion.morphKeyframes.size(), 1u);
    EXPECT_EQ(result.motion.morphKeyframes[0].morphName, "MorphA");
    EXPECT_EQ(result.motion.morphKeyframes[0].frame, 3u);
    EXPECT_FLOAT_EQ(result.motion.morphKeyframes[0].weight, 0.75f);

    ASSERT_EQ(result.motion.cameraKeyframes.size(), 1u);
    const CameraKeyframe& cam = result.motion.cameraKeyframes[0];
    EXPECT_EQ(cam.frame, 7u);
    EXPECT_FLOAT_EQ(cam.distance, -45.0f);
    EXPECT_EQ(cam.interest, Vec3(0.0f, 10.0f, 0.0f));
    EXPECT_EQ(cam.fieldOfViewDegrees, 30u);
    EXPECT_TRUE(cam.isPerspective);
    EXPECT_EQ(cam.interpolation[0], 5);

    ASSERT_EQ(result.motion.lightKeyframes.size(), 1u);
    EXPECT_EQ(result.motion.lightKeyframes[0].frame, 8u);
    EXPECT_EQ(result.motion.lightKeyframes[0].color, Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_EQ(result.motion.lightKeyframes[0].direction, Vec3(0.0f, -1.0f, 0.0f));

    ASSERT_EQ(result.motion.shadowKeyframes.size(), 1u);
    EXPECT_EQ(result.motion.shadowKeyframes[0].frame, 9u);
    EXPECT_EQ(result.motion.shadowKeyframes[0].shadowType, 1);
    EXPECT_FLOAT_EQ(result.motion.shadowKeyframes[0].distance, 6.5f);

    ASSERT_EQ(result.motion.ikKeyframes.size(), 1u);
    const IkKeyframe& ik = result.motion.ikKeyframes[0];
    EXPECT_EQ(ik.frame, 11u);
    EXPECT_TRUE(ik.visible);
    ASSERT_EQ(ik.states.size(), 2u);
    EXPECT_EQ(ik.states[0].ikBoneName, "LeftLegIK");
    EXPECT_TRUE(ik.states[0].enabled);
    EXPECT_EQ(ik.states[1].ikBoneName, "RightLegIK");
    EXPECT_FALSE(ik.states[1].enabled);
}

// Optional real-world smoke test: if the MMD test motion this integration
// was verified against is present on THIS machine, actually parse it
// end-to-end and sanity-check the extracted counts - a genuine, non-
// synthetic .vmd file exercising real-world bone/morph keyframe data
// BuildRichVmd() above only approximates by hand. Never a hard requirement:
// the motion is a large third-party asset that isn't (and shouldn't be)
// vendored into this repo, so this GTEST_SKIP()s cleanly on every other
// machine/CI runner instead of failing. Finds any *.vmd file inside the
// fixture directory via directory_iterator (rather than a hardcoded exact
// filename) deliberately - same reasoning as
// PmxLoaderRealModelSmokeTest's own directory scan (non-ASCII/Japanese
// filenames are common for real MMD assets).
TEST(VmdLoaderRealMotionSmokeTest, LoadsAnMmdMotionIfPresentOnThisMachine)
{
    const std::filesystem::path directory = "C:\\Users\\F5954\\Documents\\TAMANA\\MMD_Motion_A";

    std::error_code ec;
    if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec)) {
        GTEST_SKIP() << "MMD test motion directory not present on this machine - skipping.";
    }

    std::filesystem::path vmdPath;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".vmd") {
            vmdPath = entry.path();
            break;
        }
    }

    if (vmdPath.empty()) {
        GTEST_SKIP() << "No .vmd file found in the MMD test motion directory - skipping.";
    }

    const VmdLoadResult result = LoadVmdMotion(vmdPath.string());

    ASSERT_TRUE(result.success) << result.message;
    // A real character-motion VMD always carries at least one bone
    // keyframe; every other track (morph/camera/light/shadow/IK) is
    // legitimately optional (see MotionData's own doc comment).
    EXPECT_GT(result.motion.boneKeyframes.size(), 0u);
    for (const auto& kf : result.motion.boneKeyframes) {
        EXPECT_FALSE(kf.boneName.empty());
    }

    // Informational only (not an assertion) - makes the real keyframe
    // counts visible in the test log when this actually runs.
    std::cout << "[VmdLoaderRealMotionSmokeTest] " << vmdPath.string() << ": "
              << result.motion.boneKeyframes.size() << " bone keyframes, "
              << result.motion.morphKeyframes.size() << " morph keyframes, "
              << result.motion.cameraKeyframes.size() << " camera keyframes, "
              << result.motion.lightKeyframes.size() << " light keyframes, "
              << result.motion.shadowKeyframes.size() << " shadow keyframes, "
              << result.motion.ikKeyframes.size() << " IK keyframes\n";
}

} // namespace
} // namespace gte
