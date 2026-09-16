// Unit tests for RecomputeMeshNormalsFromGeometry()
// (src/Assets/MeshNormalRecompute.h) - task_manager/stl-parser-1, PHASE3.
// Pure, Tier-1, no file I/O at all - plain MeshData construction + assertion,
// following tests/Math/Vec3Tests.cpp's own hand-computed-value style.

#include "Assets/MeshNormalRecompute.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

TEST(MeshNormalRecomputeTests, RecomputesAFlatNormalForASingleNonSharedTriangle)
{
    MeshData mesh;
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f) };
    // Deliberately wrong starting normals.
    mesh.normals = { Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f) };
    mesh.indices = { 0, 1, 2 };

    RecomputeMeshNormalsFromGeometry(mesh);

    const Vec3 expected = Normalize(Cross(mesh.positions[1] - mesh.positions[0], mesh.positions[2] - mesh.positions[0]));
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(mesh.normals[i], expected)) << "vertex " << i;
    }
}

TEST(MeshNormalRecomputeTests, ProducesFlatDistinctNormalsForTwoNonSharedCoplanarTriangles)
{
    // Two triangles sharing an edge by POSITION but using 6 independent
    // vertex slots (matching PHASE1's own non-welding contract), all lying
    // in the Z=0 plane.
    MeshData mesh;
    mesh.positions = {
        Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f), // Triangle A
        Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), // Triangle B
    };
    mesh.normals.resize(6);
    mesh.indices = { 0, 1, 2, 3, 4, 5 };

    RecomputeMeshNormalsFromGeometry(mesh);

    const Vec3 expectedA = Normalize(Cross(mesh.positions[1] - mesh.positions[0], mesh.positions[2] - mesh.positions[0]));
    const Vec3 expectedB = Normalize(Cross(mesh.positions[4] - mesh.positions[3], mesh.positions[5] - mesh.positions[3]));

    EXPECT_TRUE(ApproximatelyEqual(expectedA, expectedB)); // Same plane -> same flat normal.
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(mesh.normals[i], expectedA)) << "triangle A vertex " << i;
    }
    for (int i = 3; i < 6; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(mesh.normals[i], expectedB)) << "triangle B vertex " << i;
    }
}

TEST(MeshNormalRecomputeTests, AveragesNormalsAcrossATrueSharedVertex)
{
    // A small fan of 4 triangles sharing ONE central vertex index (0), each
    // triangle in a different plane/orientation - the PMX-shaped regression
    // case.
    MeshData mesh;
    mesh.positions = {
        Vec3(0.0f, 0.0f, 0.0f), // 0: shared/central vertex
        Vec3(1.0f, 0.0f, 0.0f), // 1
        Vec3(0.0f, 1.0f, 0.0f), // 2
        Vec3(0.0f, 0.0f, 1.0f), // 3
        Vec3(-1.0f, 0.0f, 0.0f), // 4
        Vec3(0.0f, -1.0f, 1.0f), // 5
    };
    mesh.normals.resize(6);
    // 4 triangles, each touching vertex 0.
    mesh.indices = {
        0, 1, 2,
        0, 2, 3,
        0, 3, 4,
        0, 4, 5,
    };

    RecomputeMeshNormalsFromGeometry(mesh);

    Vec3 accumulated = Vec3::Zero();
    for (std::size_t t = 0; t < mesh.indices.size() / 3; ++t) {
        const std::uint32_t i0 = mesh.indices[t * 3 + 0];
        const std::uint32_t i1 = mesh.indices[t * 3 + 1];
        const std::uint32_t i2 = mesh.indices[t * 3 + 2];
        accumulated += Cross(mesh.positions[i1] - mesh.positions[i0], mesh.positions[i2] - mesh.positions[i0]);
    }
    const Vec3 expectedShared = Normalize(accumulated);

    EXPECT_TRUE(ApproximatelyEqual(mesh.normals[0], expectedShared));
}

TEST(MeshNormalRecomputeTests, ZeroAreaTriangleContributesNothingAndNeverProducesNaN)
{
    MeshData mesh;
    // Two identical vertex positions -> zero-area triangle.
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f) };
    mesh.normals.resize(3);
    mesh.indices = { 0, 1, 2 };

    RecomputeMeshNormalsFromGeometry(mesh);

    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(std::isfinite(mesh.normals[i].x));
        EXPECT_TRUE(std::isfinite(mesh.normals[i].y));
        EXPECT_TRUE(std::isfinite(mesh.normals[i].z));
    }

    // A vertex all of whose triangles are degenerate ends up as exactly Zero().
    MeshData allDegenerate;
    allDegenerate.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 0.0f) };
    allDegenerate.normals.resize(3);
    allDegenerate.indices = { 0, 1, 2 };
    RecomputeMeshNormalsFromGeometry(allDegenerate);
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(allDegenerate.normals[i], Vec3::Zero()));
    }
}

TEST(MeshNormalRecomputeTests, ResizesNormalsArrayIfItDoesNotAlreadyMatchPositions)
{
    MeshData mesh;
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f) };
    mesh.normals.clear(); // Deliberately empty/mismatched.
    mesh.indices = { 0, 1, 2 };

    RecomputeMeshNormalsFromGeometry(mesh);

    ASSERT_EQ(mesh.normals.size(), mesh.positions.size());
}

TEST(MeshNormalRecomputeTests, IgnoresATrailingPartialTriangleWithoutCrashing)
{
    MeshData mesh;
    mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(5.0f, 5.0f, 5.0f) };
    mesh.normals.resize(4);
    mesh.indices = { 0, 1, 2, 3 }; // One dangling extra index at the end (not a multiple of 3).

    RecomputeMeshNormalsFromGeometry(mesh);

    const Vec3 expected = Normalize(Cross(mesh.positions[1] - mesh.positions[0], mesh.positions[2] - mesh.positions[0]));
    for (int i = 0; i < 3; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(mesh.normals[i], expected)) << "vertex " << i;
    }
    // The dangling 4th vertex was never touched by any well-formed triangle.
    EXPECT_TRUE(ApproximatelyEqual(mesh.normals[3], Vec3::Zero()));
}

} // namespace
} // namespace gte
