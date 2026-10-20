// Tests for tangent generation and cleanup (src/Assets/MeshTangents.h).

#include "Assets/MeshTangents.h"

#include <cmath>
#include <cstring>
#include <limits>

#include <gtest/gtest.h>

namespace gte {
namespace {

constexpr float kTol = 1e-5f;

MeshData Triangle(const Vec2& uv0, const Vec2& uv1, const Vec2& uv2)
{
    MeshData mesh;
    mesh.positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0) };
    mesh.normals = { Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1) };
    mesh.uvs = { uv0, uv1, uv2 };
    mesh.indices = { 0, 1, 2 };
    return mesh;
}

// Quad (0,0) (1,0) (1,1) (0,1) as triangles (0,1,2) and (0,2,3); v points down the image.
MeshData Quad(bool mirrorSecondTriangle)
{
    MeshData mesh;
    mesh.positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(1, 1, 0), Vec3(0, 1, 0) };
    mesh.normals = { Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1), Vec3(0, 0, 1) };
    const Vec2 lastUv = mirrorSecondTriangle ? Vec2(1, 1) : Vec2(0, 0);
    mesh.uvs = { Vec2(0, 1), Vec2(1, 1), Vec2(1, 0), lastUv };
    mesh.indices = { 0, 1, 2, 0, 2, 3 };
    return mesh;
}

void ExpectNear(const Vec3& a, const Vec3& b, float tolerance = kTol)
{
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}

void ExpectFallbackFrame(const Vec4& tangent, const Vec3& normal)
{
    EXPECT_NEAR(Length(tangent.XYZ()), 1.0f, kTol);
    EXPECT_NEAR(Dot(tangent.XYZ(), normal), 0.0f, kTol);
    EXPECT_EQ(tangent.w, 1.0f);
}

// "Up on the image" direction of one triangle: -dP/dv.
Vec3 UpOfTriangle(const MeshData& mesh, std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    const Vec3 e1 = mesh.positions[b] - mesh.positions[a];
    const Vec3 e2 = mesh.positions[c] - mesh.positions[a];
    const double du1 = mesh.uvs[b].x - mesh.uvs[a].x;
    const double dv1 = mesh.uvs[b].y - mesh.uvs[a].y;
    const double du2 = mesh.uvs[c].x - mesh.uvs[a].x;
    const double dv2 = mesh.uvs[c].y - mesh.uvs[a].y;
    const double det = du1 * dv2 - du2 * dv1;
    const Vec3 dPdv((e2.x * du1 - e1.x * du2) / det, (e2.y * du1 - e1.y * du2) / det, (e2.z * du1 - e1.z * du2) / det);
    return -dPdv;
}

// The rule every triangle corner must satisfy: cross(N, T) * w points along the triangle's "up" direction.
void ExpectConvention(const MeshData& mesh)
{
    ASSERT_EQ(mesh.tangents.size(), mesh.positions.size());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3 up = UpOfTriangle(mesh, mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]);
        for (std::size_t c = 0; c < 3; ++c) {
            const std::uint32_t v = mesh.indices[i + c];
            const Vec4& t = mesh.tangents[v];
            EXPECT_NEAR(Length(t.XYZ()), 1.0f, 1e-4f);
            EXPECT_NEAR(Dot(t.XYZ(), mesh.normals[v]), 0.0f, 1e-4f);
            EXPECT_GT(Dot(Cross(mesh.normals[v], t.XYZ()) * t.w, up), 0.0f) << "triangle " << i / 3 << " corner " << c;
        }
    }
}

TEST(MeshTangentsTest, UvDownTriangleGivesPositiveHandedness)
{
    MeshData mesh = Triangle(Vec2(0, 1), Vec2(1, 1), Vec2(0, 0));
    const TangentGenerationResult result = GenerateMeshTangents(mesh);
    EXPECT_TRUE(result.generated);
    EXPECT_EQ(result.splitVertexCount, 0u);
    ASSERT_EQ(mesh.tangents.size(), 3u);
    for (const Vec4& t : mesh.tangents) {
        ExpectNear(t.XYZ(), Vec3(1, 0, 0));
        EXPECT_EQ(t.w, 1.0f);
    }
}

TEST(MeshTangentsTest, UvUpTriangleGivesNegativeHandedness)
{
    MeshData mesh = Triangle(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1));
    const TangentGenerationResult result = GenerateMeshTangents(mesh);
    EXPECT_TRUE(result.generated);
    EXPECT_EQ(result.splitVertexCount, 0u);
    ASSERT_EQ(mesh.tangents.size(), 3u);
    for (const Vec4& t : mesh.tangents) {
        ExpectNear(t.XYZ(), Vec3(1, 0, 0));
        EXPECT_EQ(t.w, -1.0f);
    }
}

TEST(MeshTangentsTest, DegenerateOrNanUvGivesTheFallback)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (const Vec2 uv : { Vec2(0.5f, 0.5f), Vec2(nan, nan) }) {
        MeshData mesh = Triangle(uv, uv, uv);
        GenerateMeshTangents(mesh);
        ASSERT_EQ(mesh.tangents.size(), 3u);
        for (std::size_t i = 0; i < 3; ++i) {
            ExpectFallbackFrame(mesh.tangents[i], mesh.normals[i]);
        }
    }
}

TEST(MeshTangentsTest, OrthonormalizeFixesSlantedZeroAndNanTangents)
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    MeshData mesh = Triangle(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1));
    mesh.tangents = { Vec4(0.6f, 0.0f, 0.8f, -1.0f), Vec4(0, 0, 0, 1), Vec4(nan, 0, 0, 1) };
    OrthonormalizeMeshTangents(mesh);

    ExpectNear(mesh.tangents[0].XYZ(), Vec3(1, 0, 0));
    EXPECT_EQ(mesh.tangents[0].w, -1.0f);
    ExpectFallbackFrame(mesh.tangents[1], mesh.normals[1]);
    ExpectFallbackFrame(mesh.tangents[2], mesh.normals[2]);
}

TEST(MeshTangentsTest, FallbackTangentIsPerpendicularAndSurvivesBadNormals)
{
    for (const Vec3 n : { Vec3(0, 0, 1), Vec3(0, 1, 0), Vec3(1, 0, 0), Vec3(1, 1, 1) * 0.57735f }) {
        ExpectFallbackFrame(FallbackTangent(n), n);
    }
    EXPECT_EQ(FallbackTangent(Vec3::Zero()), Vec4(1, 0, 0, 1));
    EXPECT_EQ(FallbackTangent(Vec3(std::numeric_limits<float>::infinity(), 0, 0)), Vec4(1, 0, 0, 1));
}

TEST(MeshTangentsTest, MissingUvsGiveFallbackTangents)
{
    MeshData mesh = Triangle(Vec2(0, 0), Vec2(1, 0), Vec2(0, 1));
    mesh.uvs.clear();
    const TangentGenerationResult result = GenerateMeshTangents(mesh);
    EXPECT_FALSE(result.generated);
    ASSERT_EQ(mesh.tangents.size(), 3u);
    for (std::size_t i = 0; i < 3; ++i) {
        ExpectFallbackFrame(mesh.tangents[i], mesh.normals[i]);
    }
}

TEST(MeshTangentsTest, OutOfRangeIndexIsSkippedAndChangesNothingElse)
{
    MeshData mesh = Triangle(Vec2(0, 1), Vec2(1, 1), Vec2(0, 0));
    mesh.positions.push_back(Vec3(1, 1, 0));
    mesh.normals.push_back(Vec3(0, 0, 1));
    mesh.uvs.push_back(Vec2(1, 0));
    mesh.indices = { 0, 1, 2, 0, 1, 99 };

    const TangentGenerationResult result = GenerateMeshTangents(mesh);
    EXPECT_TRUE(result.generated);
    EXPECT_EQ(result.splitVertexCount, 0u);
    ASSERT_EQ(mesh.positions.size(), 4u);
    ASSERT_EQ(mesh.tangents.size(), 4u);
    const std::vector<std::uint32_t> expectedIndices = { 0, 1, 2, 0, 1, 99 };
    EXPECT_EQ(mesh.indices, expectedIndices);
    for (std::size_t i = 0; i < 3; ++i) {
        ExpectNear(mesh.tangents[i].XYZ(), Vec3(1, 0, 0));
    }
    ExpectFallbackFrame(mesh.tangents[3], mesh.normals[3]);
}

TEST(MeshTangentsTest, MirroredUvIslandsSplitTheSharedVertices)
{
    MeshData mesh = Quad(true);
    const TangentGenerationResult result = GenerateMeshTangents(mesh);
    EXPECT_TRUE(result.generated);
    ASSERT_GT(result.splitVertexCount, 0u);
    EXPECT_EQ(mesh.positions.size(), 4u + result.splitVertexCount);
    ExpectConvention(mesh);

    // Every appended copy has the same position, normal and uv as an original vertex, and the opposite handedness.
    for (std::size_t copy = 4; copy < mesh.positions.size(); ++copy) {
        bool foundOriginal = false;
        for (std::size_t original = 0; original < 4; ++original) {
            if (mesh.positions[copy] == mesh.positions[original] && mesh.normals[copy] == mesh.normals[original]
                && mesh.uvs[copy] == mesh.uvs[original]) {
                foundOriginal = true;
                EXPECT_EQ(mesh.tangents[copy].w, -mesh.tangents[original].w);
            }
        }
        EXPECT_TRUE(foundOriginal) << "copy " << copy;
    }
}

TEST(MeshTangentsTest, SmoothQuadStripKeepsItsVertices)
{
    MeshData mesh;
    for (int x = 0; x < 4; ++x) {
        for (int y = 0; y < 2; ++y) {
            mesh.positions.push_back(Vec3(static_cast<float>(x), static_cast<float>(y), 0));
            mesh.normals.push_back(Vec3(0, 0, 1));
            mesh.uvs.push_back(Vec2(static_cast<float>(x) / 3.0f, 1.0f - static_cast<float>(y)));
        }
    }
    for (std::uint32_t quad = 0; quad < 3; ++quad) {
        const std::uint32_t a = quad * 2, b = quad * 2 + 1, c = quad * 2 + 2, d = quad * 2 + 3;
        mesh.indices.insert(mesh.indices.end(), { a, c, b, b, c, d });
    }

    const TangentGenerationResult result = GenerateMeshTangents(mesh);
    EXPECT_TRUE(result.generated);
    EXPECT_EQ(result.splitVertexCount, 0u);
    EXPECT_EQ(mesh.positions.size(), 8u);
    ExpectConvention(mesh);
}

TEST(MeshTangentsTest, SkinWeightsFollowAVertexSplit)
{
    MeshData mesh = Quad(true);
    for (std::int32_t i = 0; i < 4; ++i) {
        VertexSkinWeights weights;
        weights.boneIndices[0] = i;
        mesh.skinWeights.push_back(weights);
    }
    const TangentGenerationResult result = GenerateMeshTangents(mesh);
    ASSERT_GT(result.splitVertexCount, 0u);
    ASSERT_EQ(mesh.skinWeights.size(), mesh.positions.size());
    for (std::size_t i = 4; i < mesh.positions.size(); ++i) {
        const auto source = static_cast<std::size_t>(mesh.skinWeights[i].boneIndices[0]);
        ASSERT_LT(source, 4u);
        EXPECT_EQ(mesh.positions[i], mesh.positions[source]);
    }
}

TEST(MeshTangentsTest, GenerationIsDeterministic)
{
    MeshData a = Quad(true);
    MeshData b = Quad(true);
    GenerateMeshTangents(a);
    GenerateMeshTangents(b);
    ASSERT_EQ(a.tangents.size(), b.tangents.size());
    EXPECT_EQ(std::memcmp(a.tangents.data(), b.tangents.data(), a.tangents.size() * sizeof(Vec4)), 0);
    ASSERT_EQ(a.positions.size(), b.positions.size());
    EXPECT_EQ(std::memcmp(a.positions.data(), b.positions.data(), a.positions.size() * sizeof(Vec3)), 0);
    EXPECT_EQ(a.indices, b.indices);
}

TEST(MeshTangentsTest, TiltedTriangleFollowsTheConventionForBothUvWindings)
{
    for (const bool flipped : { false, true }) {
        MeshData mesh;
        mesh.positions = { Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.2f, 0.3f), Vec3(0.1f, 1.0f, 0.5f) };
        const Vec3 n = Normalize(Cross(mesh.positions[1] - mesh.positions[0], mesh.positions[2] - mesh.positions[0]));
        mesh.normals = { n, n, n };
        mesh.uvs = flipped ? std::vector<Vec2>{ Vec2(0.1f, 0.2f), Vec2(0.1f, 0.9f), Vec2(0.8f, 0.3f) }
                           : std::vector<Vec2>{ Vec2(0.1f, 0.2f), Vec2(0.7f, 0.1f), Vec2(0.2f, 0.9f) };
        mesh.indices = { 0, 1, 2 };
        const TangentGenerationResult result = GenerateMeshTangents(mesh);
        EXPECT_TRUE(result.generated);
        ExpectConvention(mesh);
    }
}

} // namespace
} // namespace gte
