// CPU checks of the shared sun shadow receiver record.

#include "Game/Lighting/SunShadowReceiver.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>

namespace gte {
namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();

SunShadowReceiver MakeReceiver(std::uint32_t count)
{
    SunShadowReceiver r;
    r.cascadeCount = count;
    r.resolution = 1024;
    for (std::uint32_t c = 0; c < std::min(count, kSunShadowReceiverMaxCascades); ++c) {
        r.maps[c] = fx::Texture{ rg::TextureHandle{ c, 1 }, 3 };
        r.viewProjection[c] = Mat4::Identity();
        r.splitFar[c] = 10.0f * static_cast<float>(c + 1);
        r.blendWidth[c] = c + 1 < count ? 2.0f : 0.0f;
        r.texelWorld[c] = 0.05f;
        r.depthRange[c] = 100.0f;
    }
    r.cameraPosition = Vec3{ 1.0f, 2.0f, 3.0f };
    r.cameraForward = Vec3::Forward();
    return r;
}

TEST(SunShadowReceiverTest, UsableRequiresAValidCascadeCount)
{
    EXPECT_TRUE(SunShadowReceiverUsable(MakeReceiver(1)));
    EXPECT_TRUE(SunShadowReceiverUsable(MakeReceiver(4)));
    EXPECT_FALSE(SunShadowReceiverUsable(MakeReceiver(0)));
    EXPECT_FALSE(SunShadowReceiverUsable(MakeReceiver(5)));
}

TEST(SunShadowReceiverTest, UsableRejectsEveryBrokenField)
{
    SunShadowReceiver r = MakeReceiver(3);
    r.resolution = 0;
    EXPECT_FALSE(SunShadowReceiverUsable(r));

    r = MakeReceiver(3);
    r.maps[1] = fx::Texture{};
    EXPECT_FALSE(SunShadowReceiverUsable(r));

    r = MakeReceiver(3);
    r.viewProjection[2](1, 1) = kNaN;
    EXPECT_FALSE(SunShadowReceiverUsable(r));

    r = MakeReceiver(3);
    r.splitFar[1] = 0.0f;
    EXPECT_FALSE(SunShadowReceiverUsable(r));

    r = MakeReceiver(3);
    r.splitFar[2] = 5.0f; // Decreasing.
    EXPECT_FALSE(SunShadowReceiverUsable(r));

    r = MakeReceiver(3);
    r.texelWorld[0] = 0.0f;
    EXPECT_FALSE(SunShadowReceiverUsable(r));

    r = MakeReceiver(3);
    r.cameraForward = Vec3{ 0.0f, 0.0f, 2.0f };
    EXPECT_FALSE(SunShadowReceiverUsable(r));
}

TEST(SunShadowReceiverTest, DepthFittedDefaultsToFalseAndDoesNotAffectUsability)
{
    SunShadowReceiver r = MakeReceiver(2);
    EXPECT_FALSE(r.depthFitted);
    r.depthFitted = true;
    EXPECT_TRUE(SunShadowReceiverUsable(r));
}

} // namespace
} // namespace gte
