#include "Editor/ClusterViewerTruth.h"

#include <gtest/gtest.h>

#include <array>

namespace gte {
namespace {

CapturedPixels MakeIdImage(int width, int height, std::span<const std::uint32_t> ids, PixelFormat format)
{
    CapturedPixels image;
    image.width = width;
    image.height = height;
    image.format = format;
    const bool bgra = format == PixelFormat::BGRA8_UNorm;
    for (const std::uint32_t id : ids) {
        const std::uint8_t r = static_cast<std::uint8_t>(id & 255u);
        const std::uint8_t g = static_cast<std::uint8_t>((id >> 8) & 255u);
        const std::uint8_t b = static_cast<std::uint8_t>((id >> 16) & 255u);
        const std::array<std::uint8_t, 4> px = bgra ? std::array<std::uint8_t, 4>{ b, g, r, 255 }
                                                    : std::array<std::uint8_t, 4>{ r, g, b, 255 };
        image.pixels.insert(image.pixels.end(), px.begin(), px.end());
    }
    return image;
}

TEST(ClusterViewerTruthTest, DecodeCountsPixelsPerClusterAndInvalidIds)
{
    // ids: 0 = background, 1..3 = clusters 0..2, 4 is past the three clusters.
    const std::array<std::uint32_t, 8> ids{ 0, 1, 2, 2, 3, 4, 0, 1 };
    const ClusterIdDecode d = DecodeIdImage(MakeIdImage(4, 2, ids, PixelFormat::RGBA8_UNorm), 3);
    ASSERT_TRUE(d.ok) << d.error;
    ASSERT_EQ(d.pixelCount.size(), 3u);
    EXPECT_EQ(d.pixelCount[0], 2u);
    EXPECT_EQ(d.pixelCount[1], 2u);
    EXPECT_EQ(d.pixelCount[2], 1u);
    EXPECT_EQ(d.invalidIds, 1u);
    EXPECT_EQ(d.backgroundPixels, 2u);
    EXPECT_EQ(d.pixelsChecked, 8u);
}

TEST(ClusterViewerTruthTest, BothByteOrdersGiveTheSameIds)
{
    const std::array<std::uint32_t, 4> ids{ 0x010203u, 5u, 0u, 0x010203u };
    const ClusterIdDecode rgba = DecodeIdImage(MakeIdImage(2, 2, ids, PixelFormat::RGBA8_UNorm), 70000);
    const ClusterIdDecode bgra = DecodeIdImage(MakeIdImage(2, 2, ids, PixelFormat::BGRA8_UNorm), 70000);
    ASSERT_TRUE(rgba.ok);
    ASSERT_TRUE(bgra.ok);
    EXPECT_EQ(rgba.pixelCount, bgra.pixelCount);
    EXPECT_EQ(rgba.pixelCount[0x010203u - 1], 2u);
    EXPECT_EQ(rgba.pixelCount[4], 1u);
}

TEST(ClusterViewerTruthTest, BadImagesAreRefused)
{
    const std::array<std::uint32_t, 8> ids{ 0, 1, 2, 2, 3, 4, 0, 1 };
    CapturedPixels small = MakeIdImage(4, 2, ids, PixelFormat::RGBA8_UNorm);
    small.pixels.resize(31);
    EXPECT_FALSE(DecodeIdImage(small, 3).ok);

    CapturedPixels wrongFormat = MakeIdImage(4, 2, ids, PixelFormat::RGBA8_UNorm);
    wrongFormat.format = PixelFormat::RGBA16_Float;
    EXPECT_FALSE(DecodeIdImage(wrongFormat, 3).ok);

    EXPECT_FALSE(DecodeIdImage(CapturedPixels{}, 3).ok);
}

TEST(ClusterViewerTruthTest, CompareSplitsFalseCullsAndConservative)
{
    const std::array<std::uint32_t, 3> survivors{ 0, 1, 4 };
    const std::array<std::uint32_t, 5> pixels{ 5, 0, 3, 0, 7 };
    const ClusterTruthResult r = CompareClusterTruth(survivors, pixels);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.kept, 3u);
    EXPECT_EQ(r.seen, 3u);
    EXPECT_EQ(r.falseCulls, 1u);
    EXPECT_EQ(r.conservative, 1u);
    EXPECT_EQ(r.correct, 3u);
    ASSERT_EQ(r.firstFalseCullIds.size(), 1u);
    EXPECT_EQ(r.firstFalseCullIds[0], 2u);
    EXPECT_EQ(r.unseen, 2u);
    EXPECT_EQ(r.culledAndUnseen, 1u);
    EXPECT_DOUBLE_EQ(r.cullEfficiency, 0.5);
}

TEST(ClusterViewerTruthTest, FalseCullListIsCappedAtTwenty)
{
    const std::vector<std::uint32_t> pixels(100, 1u);
    const ClusterTruthResult r = CompareClusterTruth({}, pixels);
    ASSERT_TRUE(r.ok);
    EXPECT_EQ(r.falseCulls, 100u);
    ASSERT_EQ(r.firstFalseCullIds.size(), kTruthMaxListedIds);
    EXPECT_EQ(r.firstFalseCullIds.front(), 0u);
    EXPECT_EQ(r.firstFalseCullIds.back(), 19u);
}

TEST(ClusterViewerTruthTest, SurvivorPastClusterCountIsAnError)
{
    const std::array<std::uint32_t, 1> survivors{ 9 };
    const std::array<std::uint32_t, 2> pixels{ 1, 1 };
    EXPECT_FALSE(CompareClusterTruth(survivors, pixels).ok);
}

TEST(ClusterViewerTruthTest, KeyChangeMakesResultStale)
{
    DebugCameraState cam;
    const ClusterCullFlags flags = ClusterCullFlags::Frustum;
    const VerifyKey base = MakeVerifyKey(cam, flags, 3, 1280, 720, 1);
    EXPECT_FALSE(IsStale(base, MakeVerifyKey(cam, flags, 3, 1280, 720, 1)));

    DebugCameraState moved = cam;
    moved.transform.position.x += 1.0f;
    EXPECT_TRUE(IsStale(base, MakeVerifyKey(moved, flags, 3, 1280, 720, 1)));
    EXPECT_TRUE(IsStale(base, MakeVerifyKey(cam, flags | ClusterCullFlags::BackfaceCone, 3, 1280, 720, 1)));
    EXPECT_TRUE(IsStale(base, MakeVerifyKey(cam, flags, 4, 1280, 720, 1)));
    EXPECT_TRUE(IsStale(base, MakeVerifyKey(cam, flags, 3, 640, 360, 1)));
    EXPECT_TRUE(IsStale(base, MakeVerifyKey(cam, flags, 3, 1280, 720, 2)));
}

TEST(ClusterViewerTruthTest, FalseCullsWithStaleHizAreNotABug)
{
    ClusterViewHizInfo hiz;
    hiz.width = 1280;
    hiz.height = 720;
    const Mat4 vp = Mat4::Identity();
    const Vec3 eye = Vec3::Zero();

    EXPECT_EQ(ClassifyFalseCulls(0, true, hiz, vp, eye, 1280, 720), HizVerdictKind::Clean);
    EXPECT_EQ(ClassifyFalseCulls(3, true, hiz, vp, eye, 1280, 720), HizVerdictKind::Bug);
    EXPECT_EQ(ClassifyFalseCulls(3, true, hiz, vp, eye, 640, 360), HizVerdictKind::HizStale);
    EXPECT_EQ(ClassifyFalseCulls(3, true, hiz, vp, Vec3(1, 0, 0), 1280, 720), HizVerdictKind::HizStale);
    EXPECT_EQ(ClassifyFalseCulls(3, false, hiz, vp, Vec3(1, 0, 0), 640, 360), HizVerdictKind::Bug);
    EXPECT_EQ(ClassifyFalseCulls(3, true, std::nullopt, vp, eye, 1280, 720), HizVerdictKind::Bug);
}

} // namespace
} // namespace gte
