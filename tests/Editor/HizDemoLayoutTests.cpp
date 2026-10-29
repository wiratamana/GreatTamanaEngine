// Tests for the Hi-Z demo scene's pure layout, parameter clamps and reply keys. No device, no registry.

#include "Editor/HizDemoLayout.h"
#include "Editor/HizDemoSceneOpener.h"
#include "Editor/InstanceGridTestSpawner.h"
#include "Core/InstanceGridLimits.h"
#include "ECS/Components/Camera.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>

namespace gte {
namespace {

constexpr double kReferenceAspect = 16.0 / 9.0;
constexpr double kUltrawideAspect = 21.0 / 9.0;

HizDemoLayout LayoutFor(std::uint32_t count, float spacing)
{
    const Camera camera;
    return ComputeHizDemoLayout(count, spacing, Vec3{ 0.0f, 0.0f, kInstanceGridDefaultDepth }, camera.fovYDegrees, camera.farZ);
}

double TanHalfFov()
{
    return std::tan(static_cast<double>(DegToRad(Camera{}.fovYDegrees)) * 0.5);
}

bool InsideFrustum(const HizDemoLayout& layout, double x, double y, double z, double aspect)
{
    const double depth = z - layout.cameraPosition.z;
    if (depth <= Camera{}.nearZ || depth >= layout.cameraFarZ) {
        return false;
    }
    const double halfHeight = depth * TanHalfFov();
    return std::fabs(x - layout.cameraPosition.x) <= halfHeight * aspect && std::fabs(y - layout.cameraPosition.y) <= halfHeight;
}

// Calls fn(x, y, z) for the four corners of every instance of the field.
template <typename Fn>
void ForEachInstanceCorner(std::uint32_t count, float spacing, const HizDemoLayout& layout, Fn&& fn)
{
    const double originX = layout.fieldCentre.x - static_cast<double>(spacing) * (layout.columns - 1) * 0.5;
    const double originY = layout.fieldCentre.y - static_cast<double>(spacing) * (layout.rows - 1) * 0.5;
    for (std::uint32_t i = 0; i < count; ++i) {
        const double cx = originX + static_cast<double>(spacing) * (i % layout.columns);
        const double cy = originY + static_cast<double>(spacing) * (i / layout.columns);
        for (const double dx : { -0.5, 0.5 }) {
            for (const double dy : { -0.5, 0.5 }) {
                fn(cx + dx, cy + dy, static_cast<double>(layout.fieldCentre.z));
            }
        }
    }
}

TEST(HizDemoLayoutTest, EveryFieldInstanceIsInsideTheFrustumForWideAspects)
{
    for (const std::uint32_t count : { 1u, 2u, 50000u, kMaxSpawnGridInstances }) {
        for (const float spacing : { 0.1f, 2.0f, 1000.0f }) {
            const HizDemoLayout layout = LayoutFor(count, spacing);
            for (const double aspect : { kReferenceAspect, kUltrawideAspect }) {
                std::uint32_t outside = 0;
                ForEachInstanceCorner(count, spacing, layout, [&](double x, double y, double z) {
                    outside += InsideFrustum(layout, x, y, z, aspect) ? 0u : 1u;
                });
                EXPECT_EQ(outside, 0u) << "count " << count << " spacing " << spacing << " aspect " << aspect;
            }
        }
    }
}

TEST(HizDemoLayoutTest, TheWallCoversTheWholeFrustumCrossSection)
{
    for (const std::uint32_t count : { 1u, 2u, 50000u, kMaxSpawnGridInstances }) {
        const float spacing = 2.0f;
        const HizDemoLayout layout = LayoutFor(count, spacing);
        const double frontZ = static_cast<double>(layout.wallPosition.z) - layout.wallScale.z * 0.5;
        const double rearZ = static_cast<double>(layout.wallPosition.z) + layout.wallScale.z * 0.5;
        const double frontDepth = frontZ - layout.cameraPosition.z;

        EXPECT_GT(frontDepth, Camera{}.nearZ) << "the wall must stay clear of the near plane";
        EXPECT_LT(rearZ, layout.fieldCentre.z) << "the wall must sit between the camera and the field";

        // The widest aspect the wall promises to cover, at its front face.
        const double halfHeight = frontDepth * TanHalfFov();
        EXPECT_GE(layout.wallScale.y * 0.5, halfHeight);
        EXPECT_GE(layout.wallScale.x * 0.5, halfHeight * kHizDemoMaxCoveredAspect);

        // Every ray to a field corner crosses the wall's front face inside its rectangle.
        std::uint32_t uncovered = 0;
        const double fieldDepth = static_cast<double>(layout.fieldCentre.z) - layout.cameraPosition.z;
        ForEachInstanceCorner(count, spacing, layout, [&](double x, double y, double) {
            const double scale = frontDepth / fieldDepth;
            const bool covered = std::fabs((x - layout.cameraPosition.x) * scale) <= layout.wallScale.x * 0.5
                && std::fabs((y - layout.cameraPosition.y) * scale) <= layout.wallScale.y * 0.5;
            uncovered += covered ? 0u : 1u;
        });
        EXPECT_EQ(uncovered, 0u) << "count " << count;
    }
}

TEST(HizDemoLayoutTest, TheFarPlaneIsBeyondTheField)
{
    for (const std::uint32_t count : { 1u, 50000u, kMaxSpawnGridInstances }) {
        const HizDemoLayout layout = LayoutFor(count, 2.0f);
        EXPECT_GE(layout.cameraFarZ, Camera{}.farZ);
        EXPECT_GE(layout.cameraFarZ, layout.cameraDistance + kHizDemoFieldDepth);
    }
    const HizDemoLayout widest = LayoutFor(kMaxSpawnGridInstances, kMaxSpawnGridSpacing);
    EXPECT_GE(widest.cameraFarZ, widest.cameraDistance + kHizDemoFieldDepth);
}

TEST(HizDemoLayoutTest, TheCameraDistanceStaysInsideItsBounds)
{
    EXPECT_FLOAT_EQ(LayoutFor(1, 0.1f).cameraDistance, kHizDemoMinCameraDistance);
    const HizDemoLayout widest = LayoutFor(kMaxSpawnGridInstances, kMaxSpawnGridSpacing);
    EXPECT_LE(widest.cameraDistance, kHizDemoMaxCameraDistance);
    EXPECT_TRUE(std::isfinite(widest.cameraFarZ));
}

TEST(HizDemoLayoutTest, TheWholeFieldIsInViewAtTheReferenceAspect)
{
    for (const std::uint32_t count : { 1u, 2u, 50000u, kMaxSpawnGridInstances }) {
        EXPECT_NEAR(LayoutFor(count, 2.0f).fieldFractionInView, 1.0f, 1e-5f) << "count " << count;
    }
}

TEST(HizDemoLayoutTest, ColumnsAndRowsHoldEveryInstance)
{
    const HizDemoLayout layout = LayoutFor(50000, 2.0f);
    EXPECT_EQ(layout.columns, 224u);
    EXPECT_GE(static_cast<std::uint64_t>(layout.columns) * layout.rows, 50000u);
    EXPECT_LT(static_cast<std::uint64_t>(layout.columns) * (layout.rows - 1), 50000u);
}

TEST(HizDemoParametersTest, UnsetValuesTakeTheDefaults)
{
    const HizDemoParameters p = SanitizeHizDemoParameters(std::nullopt, std::nullopt);
    ASSERT_TRUE(p.valid);
    EXPECT_EQ(p.count, kHizDemoDefaultCount);
    EXPECT_FLOAT_EQ(p.spacing, kHizDemoDefaultSpacing);
    EXPECT_FALSE(p.countClamped);
    EXPECT_TRUE(p.message.empty());
}

TEST(HizDemoParametersTest, CountIsClampedAndSaysSo)
{
    const HizDemoParameters tooMany = SanitizeHizDemoParameters(kMaxSpawnGridInstances + 1, std::nullopt);
    ASSERT_TRUE(tooMany.valid);
    EXPECT_EQ(tooMany.count, kMaxSpawnGridInstances);
    EXPECT_TRUE(tooMany.countClamped);
    EXPECT_EQ(tooMany.message, "count clamped to " + std::to_string(kMaxSpawnGridInstances));

    const HizDemoParameters huge = SanitizeHizDemoParameters(std::numeric_limits<std::uint32_t>::max(), std::nullopt);
    EXPECT_EQ(huge.count, kMaxSpawnGridInstances);

    const HizDemoParameters none = SanitizeHizDemoParameters(0u, std::nullopt);
    ASSERT_TRUE(none.valid);
    EXPECT_EQ(none.count, 1u);
    EXPECT_TRUE(none.countClamped);

    const HizDemoParameters exact = SanitizeHizDemoParameters(kMaxSpawnGridInstances, std::nullopt);
    EXPECT_EQ(exact.count, kMaxSpawnGridInstances);
    EXPECT_FALSE(exact.countClamped);
}

TEST(HizDemoParametersTest, SpacingOutsideTheGridRangeOrNotFiniteIsRefused)
{
    for (const float bad : { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
             -std::numeric_limits<float>::infinity(), 0.05f, -1.0f, 1000.5f }) {
        EXPECT_FALSE(SanitizeHizDemoParameters(10u, bad).valid) << bad;
    }
    EXPECT_TRUE(SanitizeHizDemoParameters(10u, kMinSpawnGridSpacing).valid);
    EXPECT_TRUE(SanitizeHizDemoParameters(10u, kMaxSpawnGridSpacing).valid);
}

TEST(HizDemoFeatureListTest, MatchesByPrefix)
{
    EXPECT_TRUE(IsGeometryOnlyFeature("ShadowMask"));
    EXPECT_TRUE(IsGeometryOnlyFeature("ToneMapping"));
    EXPECT_TRUE(IsGeometryOnlyFeature("Ssgi"));
    EXPECT_FALSE(IsGeometryOnlyFeature("GpuDrivenScene"));
    EXPECT_FALSE(IsGeometryOnlyFeature("Gt"));
    EXPECT_FALSE(IsGeometryOnlyFeature(""));
}

TEST(HizDemoReplyTest, DetailsCarryEveryDocumentedKey)
{
    HizDemoReport report;
    report.success = true;
    report.profile = "geometry_only";
    report.instanceCount = 50000;
    report.countClamped = true;
    report.clustersPerInstance = 22;
    report.workItems = 1100000;
    report.camera = Entity{ 3, 1 };
    report.sun = Entity{ 4, 0 };
    report.wall = Entity{ 5, 2 };
    report.mode = "Frustum";
    report.collectStats = true;

    const nlohmann::json json = nlohmann::json::parse(BuildHizDemoDetailsJson(report));
    ASSERT_TRUE(json.is_object());
    EXPECT_EQ(json.size(), 8u);
    EXPECT_EQ(json["profile"], "geometry_only");
    EXPECT_EQ(json["instanceCount"], 50000);
    EXPECT_EQ(json["countClamped"], true);
    EXPECT_EQ(json["clustersPerInstance"], 22);
    EXPECT_EQ(json["workItems"], 1100000);
    EXPECT_EQ(json["entities"].size(), 3u);
    EXPECT_EQ(json["entities"]["camera"], "3/1");
    EXPECT_EQ(json["entities"]["sun"], "4/0");
    EXPECT_EQ(json["entities"]["wall"], "5/2");
    EXPECT_EQ(json["gpuDrivenScene"].size(), 2u);
    EXPECT_EQ(json["gpuDrivenScene"]["mode"], "Frustum");
    EXPECT_EQ(json["gpuDrivenScene"]["collectStats"], true);
    EXPECT_EQ(json["restoresOnClose"], true);
}

} // namespace
} // namespace gte
