// Unit tests for PrimitiveMeshGenerator (src/Renderer/Primitives/
// PrimitiveMeshGenerator.h) - pure CPU-side geometry generation, no GPU
// device/Renderer/ECS of any kind involved (see the class comment), so
// these are exercised exactly like Math/ECS: hand-verified geometric
// invariants against the actual generated Vertex data, never a live mesh/
// pipeline.

#include "Renderer/Primitives/PrimitiveMeshGenerator.h"

#include "Math/MathTypes.h"
#include "Math/Vec3.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace gte {
namespace {

Vec3 PositionOf(const Vertex& v) noexcept
{
    return Vec3{ v.position[0], v.position[1], v.position[2] };
}

Vec3 ColorOf(const Vertex& v) noexcept
{
    return Vec3{ v.color[0], v.color[1], v.color[2] };
}

// Every generated vertex's baked color must be a finite, non-negative value
// no brighter than the shape's own base color (see PrimitiveMeshGenerator.cpp's
// kBaseColor/kAmbient) - a cheap, shape-agnostic sanity check that shading
// never produced NaN/negative/blown-out values, run against all five shapes.
void ExpectPlausibleColors(const std::vector<Vertex>& vertices)
{
    for (const Vertex& v : vertices) {
        const Vec3 color = ColorOf(v);
        EXPECT_TRUE(std::isfinite(color.x));
        EXPECT_TRUE(std::isfinite(color.y));
        EXPECT_TRUE(std::isfinite(color.z));
        EXPECT_GE(color.x, 0.0f);
        EXPECT_LE(color.x, 1.0f);
        EXPECT_GE(color.y, 0.0f);
        EXPECT_LE(color.y, 1.0f);
        EXPECT_GE(color.z, 0.0f);
        EXPECT_LE(color.z, 1.0f);
    }
}

TEST(PrimitiveMeshGeneratorTest, ToString_ReturnsDistinctNonNullNameForEveryType)
{
    EXPECT_STREQ(ToString(PrimitiveType::Cube), "Cube");
    EXPECT_STREQ(ToString(PrimitiveType::Sphere), "Sphere");
    EXPECT_STREQ(ToString(PrimitiveType::Capsule), "Capsule");
    EXPECT_STREQ(ToString(PrimitiveType::Cone), "Cone");
    EXPECT_STREQ(ToString(PrimitiveType::Plane), "Plane");
}

TEST(PrimitiveMeshGeneratorTest, Generate_DispatchesToTheMatchingShape)
{
    // Generate() is just a switch over the same private GenerateX() methods
    // exercised individually below - this only checks the dispatch itself
    // picks the right one (same vertex count), not the geometry again.
    EXPECT_EQ(PrimitiveMeshGenerator::Generate(PrimitiveType::Cube).size(), 36u);
    EXPECT_EQ(PrimitiveMeshGenerator::Generate(PrimitiveType::Plane).size(), 6u);
}

TEST(PrimitiveMeshGeneratorTest, Cube_HasExactlySixFacesOfTwoTrianglesEach)
{
    const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(PrimitiveType::Cube);

    // 6 faces * 2 triangles * 3 vertices, non-indexed (see Mesh.h/TODO.md).
    ASSERT_EQ(vertices.size(), 36u);
    ExpectPlausibleColors(vertices);
}

TEST(PrimitiveMeshGeneratorTest, Cube_EveryVertexLiesExactlyOnTheUnitCubesSurface)
{
    const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(PrimitiveType::Cube);

    float minX = 0.0f, maxX = 0.0f, minY = 0.0f, maxY = 0.0f, minZ = 0.0f, maxZ = 0.0f;
    for (const Vertex& v : vertices) {
        const Vec3 p = PositionOf(v);
        // A point on a unit cube's surface always has at least one
        // component at exactly +-0.5, and none beyond it.
        const float maxAbs = std::max({ std::fabs(p.x), std::fabs(p.y), std::fabs(p.z) });
        EXPECT_TRUE(ApproximatelyEqual(maxAbs, 0.5f));

        minX = std::min(minX, p.x); maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y); maxY = std::max(maxY, p.y);
        minZ = std::min(minZ, p.z); maxZ = std::max(maxZ, p.z);
    }

    // All six faces were actually emitted (the bounding box is the full
    // [-0.5, 0.5]^3 cube, not just some of its faces).
    EXPECT_TRUE(ApproximatelyEqual(minX, -0.5f));
    EXPECT_TRUE(ApproximatelyEqual(maxX, 0.5f));
    EXPECT_TRUE(ApproximatelyEqual(minY, -0.5f));
    EXPECT_TRUE(ApproximatelyEqual(maxY, 0.5f));
    EXPECT_TRUE(ApproximatelyEqual(minZ, -0.5f));
    EXPECT_TRUE(ApproximatelyEqual(maxZ, 0.5f));
}

TEST(PrimitiveMeshGeneratorTest, Plane_IsASingleUnitQuadOnTheXZPlane)
{
    const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(PrimitiveType::Plane);

    // 1 quad * 2 triangles * 3 vertices.
    ASSERT_EQ(vertices.size(), 6u);
    ExpectPlausibleColors(vertices);

    for (const Vertex& v : vertices) {
        const Vec3 p = PositionOf(v);
        EXPECT_TRUE(ApproximatelyEqual(p.y, 0.0f)); // Flat on the XZ plane.
        EXPECT_LE(std::fabs(p.x), 0.5f + kEpsilon);
        EXPECT_LE(std::fabs(p.z), 0.5f + kEpsilon);
    }
}

TEST(PrimitiveMeshGeneratorTest, Sphere_IsANonEmptyTriangleListWithEveryVertexOnTheUnitSphere)
{
    const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(PrimitiveType::Sphere);

    ASSERT_GT(vertices.size(), 0u);
    ASSERT_EQ(vertices.size() % 3u, 0u); // A valid (non-indexed) triangle list.
    ExpectPlausibleColors(vertices);

    for (const Vertex& v : vertices) {
        const Vec3 p = PositionOf(v);
        EXPECT_NEAR(Length(p), 0.5f, 1e-4f); // Radius 0.5, centered on the origin.
    }
}

TEST(PrimitiveMeshGeneratorTest, Cone_EveryVertexIsAtTheApexOrOnTheBasePlane)
{
    const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(PrimitiveType::Cone);

    ASSERT_GT(vertices.size(), 0u);
    ASSERT_EQ(vertices.size() % 3u, 0u);
    ExpectPlausibleColors(vertices);

    bool sawApex = false;
    bool sawBaseCenter = false;
    bool sawBaseRim = false;

    for (const Vertex& v : vertices) {
        const Vec3 p = PositionOf(v);
        const float radial = std::sqrt(p.x * p.x + p.z * p.z);

        if (ApproximatelyEqual(p.y, 0.5f) && ApproximatelyEqual(radial, 0.0f)) {
            sawApex = true;
        } else if (ApproximatelyEqual(p.y, -0.5f) && ApproximatelyEqual(radial, 0.0f)) {
            sawBaseCenter = true;
        } else if (ApproximatelyEqual(p.y, -0.5f) && ApproximatelyEqual(radial, 0.5f)) {
            sawBaseRim = true;
        } else {
            ADD_FAILURE() << "Cone vertex not at apex/base-center/base-rim: (" << p.x << ", " << p.y << ", " << p.z
                           << ")";
        }
    }

    EXPECT_TRUE(sawApex);
    EXPECT_TRUE(sawBaseCenter);
    EXPECT_TRUE(sawBaseRim);
}

TEST(PrimitiveMeshGeneratorTest, Capsule_EveryVertexIsExactlyRadiusAwayFromItsNearestCoreSegmentPoint)
{
    const std::vector<Vertex> vertices = PrimitiveMeshGenerator::Generate(PrimitiveType::Capsule);

    ASSERT_GT(vertices.size(), 0u);
    ASSERT_EQ(vertices.size() % 3u, 0u);
    ExpectPlausibleColors(vertices);

    // A capsule is the set of points at exactly `radius` distance from SOME
    // point on a straight core segment - here the segment from
    // (0,-cylinderHalfHeight,0) to (0,+cylinderHalfHeight,0). Clamping a
    // vertex's own y to that range and measuring distance to the resulting
    // point on the core is the standard capsule signed-distance check, and
    // holds equally for the cylindrical band AND both hemisphere caps.
    constexpr float radius = 0.5f;
    constexpr float cylinderHalfHeight = 0.5f; // matches GenerateCapsule()'s own constants.

    for (const Vertex& v : vertices) {
        const Vec3 p = PositionOf(v);
        const float clampedY = Clamp(p.y, -cylinderHalfHeight, cylinderHalfHeight);
        const Vec3 nearestCorePoint{ 0.0f, clampedY, 0.0f };
        EXPECT_NEAR(Length(p - nearestCorePoint), radius, 1e-4f);
    }
}

} // namespace
} // namespace gte
