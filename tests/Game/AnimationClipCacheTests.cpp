// Unit tests for AnimationClipCache.h - replaces Game's old
// EnsureAnimationClip()/m_animationClipCache private method+member pair (see
// GameInstantiationRefactorProposal.txt, Step 3.5). Touches a real temp
// directory (same convention as Assets/AssetDatabaseTests.cpp) but no GPU/
// SDL/ImGui - Tier 1.

#include "Game/Animation/AnimationClipCache.h"

#include "Assets/AssetTypes.h"
#include "Assets/GtaFile.h"
#include "Assets/MotionFile.h"

#include <fstream>

#include <gtest/gtest.h>

namespace gte {
namespace {

class AnimationClipCacheTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
        m_root = std::filesystem::temp_directory_path()
            / (std::string("GteAnimationClipCacheTest_") + info->test_suite_name() + "_" + info->name());

        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
        std::filesystem::create_directories(m_root, ec);
    }

    void TearDown() override
    {
        std::error_code ec;
        std::filesystem::remove_all(m_root, ec);
    }

    std::filesystem::path m_root;
    AnimationClipCache m_cache;
};

TEST_F(AnimationClipCacheTest, GetOrLoadReturnsNullptrForAMissingFile)
{
    EXPECT_EQ(m_cache.GetOrLoad((m_root / "DoesNotExist.gta").string()), nullptr);
}

TEST_F(AnimationClipCacheTest, GetOrLoadReturnsNullptrForTheWrongAssetType)
{
    const std::filesystem::path gtaPath = m_root / "not-a-motion.gta";
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Texture, Guid::Generate(), AssetFlags::None, {}, {}));

    EXPECT_EQ(m_cache.GetOrLoad(gtaPath.string()), nullptr);
}

TEST_F(AnimationClipCacheTest, TryGetReturnsNullptrBeforeGetOrLoadHasBeenCalled)
{
    EXPECT_EQ(m_cache.TryGet("C:/Project/Never.gta"), nullptr);
}

TEST_F(AnimationClipCacheTest, GetOrLoadDecodesAValidMotionAssetAndCachesIt)
{
    MotionData motion;
    motion.modelName = "TestModel";
    BoneKeyframe keyframe;
    keyframe.boneName = "Hips";
    keyframe.frame = 10;
    motion.boneKeyframes.push_back(keyframe);

    const std::vector<std::uint8_t> payload = EncodeMotionDataToBytes(motion);
    const std::filesystem::path gtaPath = m_root / "motion.gta";
    ASSERT_TRUE(WriteGtaFile(gtaPath, AssetType::Animation, Guid::Generate(), AssetFlags::None, {}, payload));

    const MotionData* loaded = m_cache.GetOrLoad(gtaPath.string());
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->modelName, "TestModel");
    ASSERT_EQ(loaded->boneKeyframes.size(), 1u);
    EXPECT_EQ(loaded->boneKeyframes[0].boneName, "Hips");

    // Second call should hit the cache and return the exact same record.
    const MotionData* loadedAgain = m_cache.GetOrLoad(gtaPath.string());
    EXPECT_EQ(loaded, loadedAgain);

    // TryGet() should now also find it without loading anything.
    EXPECT_EQ(m_cache.TryGet(gtaPath.string()), loaded);
}

} // namespace
} // namespace gte
