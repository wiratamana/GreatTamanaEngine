// Parity of the SIMD skin+pack kernels (VertexSkinningSimd.h) with the reference SkinVertexRange().
#include "Animation/VertexSkinning.h"
#include "Animation/VertexSkinningSimd.h"
#include "Jobs/JobDispatch.h"
#include "Jobs/JobSystem.h"
#include "Math/Quat.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace gte;

namespace {

// Small deterministic generator: the same numbers on every platform and run.
class Lcg {
public:
    explicit Lcg(std::uint32_t seed) : m_state(seed) {}
    std::uint32_t Next()
    {
        m_state = m_state * 1664525u + 1013904223u;
        return m_state >> 8;
    }
    float Range(float lo, float hi) { return lo + (hi - lo) * (static_cast<float>(Next() & 0xFFFF) / 65535.0f); }
    int Int(int lo, int hi) { return lo + static_cast<int>(Next() % static_cast<std::uint32_t>(hi - lo + 1)); }

private:
    std::uint32_t m_state;
};

struct TestModel {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<Vec2> uvs;
    std::vector<VertexSkinWeights> weights;
};

enum class WeightStyle { Mixed, Unnormalised, BadIndices, AllInvalid, NegativeSum, ZeroWeights };

TestModel MakeModel(std::size_t count, int boneCount, WeightStyle style, bool unitNormals, Lcg& rng)
{
    TestModel model;
    for (std::size_t i = 0; i < count; ++i) {
        model.positions.push_back(Vec3{ rng.Range(-30, 30), rng.Range(-30, 30), rng.Range(-30, 30) });
        Vec3 normal{ rng.Range(-1, 1), rng.Range(-1, 1), rng.Range(-1, 1) };
        if (unitNormals) {
            normal = Normalize(normal);
        } else {
            normal = i % 7 == 0 ? Vec3::Zero() : normal * rng.Range(0.1f, 3.0f);
        }
        model.normals.push_back(normal);
        model.uvs.push_back(Vec2{ rng.Range(0, 1), rng.Range(0, 1) });

        VertexSkinWeights w;
        const int influences = rng.Int(1, 4);
        float sum = 0.0f;
        for (int k = 0; k < influences; ++k) {
            w.boneIndices[k] = rng.Int(0, boneCount - 1);
            w.boneWeights[k] = rng.Range(0.05f, 1.0f);
            sum += w.boneWeights[k];
        }
        if (style != WeightStyle::Unnormalised) {
            for (int k = 0; k < influences; ++k) {
                w.boneWeights[k] /= sum;
            }
        }
        if (style == WeightStyle::BadIndices) {
            for (int k = 0; k < 4; ++k) {
                const int roll = rng.Int(0, 5);
                if (roll == 0) w.boneIndices[k] = -1;
                if (roll == 1) w.boneIndices[k] = boneCount + rng.Int(0, 5);
                if (roll == 2) w.boneWeights[k] = 0.0f;
            }
        } else if (style == WeightStyle::AllInvalid) {
            for (int k = 0; k < 4; ++k) {
                w.boneIndices[k] = k % 2 ? -1 : boneCount + 3;
                w.boneWeights[k] = 0.5f;
            }
        } else if (style == WeightStyle::NegativeSum) {
            w = VertexSkinWeights{};
            w.boneIndices[0] = rng.Int(0, boneCount - 1);
            w.boneWeights[0] = -0.5f;
        } else if (style == WeightStyle::ZeroWeights) {
            for (int k = 0; k < 4; ++k) w.boneWeights[k] = 0.0f;
            w.boneIndices[0] = 0;
        }
        model.weights.push_back(w);
    }
    return model;
}

std::vector<Mat4> MakeMatrices(int count, bool withScale, Lcg& rng)
{
    std::vector<Mat4> matrices(static_cast<std::size_t>(count));
    for (Mat4& m : matrices) {
        Quat q{ rng.Range(-1, 1), rng.Range(-1, 1), rng.Range(-1, 1), rng.Range(0.1f, 1) };
        const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        m = Mat4::FromQuat(Quat{ q.x / length, q.y / length, q.z / length, q.w / length });
        if (withScale) {
            m = Mat4::Scaling(Vec3{ rng.Range(0.5f, 2), rng.Range(0.5f, 2), rng.Range(0.5f, 2) }) * m;
        }
        m.columns[3] = Vec4(rng.Range(-50, 50), rng.Range(-50, 50), rng.Range(-50, 50), 1.0f);
    }
    return matrices;
}

// |a - b| relative to max(1, |b|): fair for the large values an ill-conditioned weight sum produces.
::testing::AssertionResult Near(float actual, float expected, double tolerance)
{
    const double scale = std::max(1.0, std::fabs(static_cast<double>(expected)));
    const double diff = std::fabs(static_cast<double>(actual) - expected) / scale;
    if (diff <= tolerance) {
        return ::testing::AssertionSuccess();
    }
    return ::testing::AssertionFailure() << actual << " vs " << expected << " (relative diff " << diff << ")";
}

constexpr double kTolerance = 5e-5;

// Runs both kernels over [begin, end) and compares every vertex with SkinVertexRange; vertices outside the range must
// keep their sentinel and the guard floats behind the last vertex must stay untouched.
void ExpectParity(const TestModel& model, const std::vector<Mat4>& matrices, std::uint32_t begin, std::uint32_t end,
    bool dropWeights = false, bool dropNormals = false)
{
    const std::vector<VertexSkinWeights> noWeights;
    const std::vector<Vec3> noNormals;
    const std::vector<VertexSkinWeights>& weights = dropWeights ? noWeights : model.weights;
    const std::vector<Vec3>& normals = dropNormals ? noNormals : model.normals;
    const std::size_t n = model.positions.size();

    std::vector<Vec3> refPositions(n, Vec3{ 777, 777, 777 });
    std::vector<Vec3> refNormals(n, Vec3{ 777, 777, 777 });
    SkinVertexRange(begin, end, model.positions, normals, weights, matrices, refPositions, refNormals);

    const CompactSkinData data = BuildCompactSkinData(model.positions, normals, model.uvs, weights, matrices.size());
    ASSERT_EQ(data.VertexCount(), n);

    constexpr std::size_t kGuard = 8;
    std::vector<float> textured(n * 8 + kGuard, 777.0f);
    std::vector<float> plain(n * 6 + kGuard, 777.0f);
    SkinPackTexturedRange(begin, end, data, matrices.data(), matrices.size(), textured.data());
    SkinPackUntexturedRange(begin, end, data, matrices.data(), matrices.size(), plain.data());

    const std::size_t clampedEnd = std::min<std::size_t>(end, n);
    for (std::size_t i = 0; i < n; ++i) {
        const float* t = &textured[i * 8];
        const float* p = &plain[i * 6];
        if (i < begin || i >= clampedEnd) {
            EXPECT_EQ(t[0], 777.0f) << "vertex " << i << " written outside the range";
            EXPECT_EQ(p[0], 777.0f) << "vertex " << i << " written outside the range";
            continue;
        }
        const float expected[6] = { refPositions[i].x, refPositions[i].y, refPositions[i].z, refNormals[i].x,
            refNormals[i].y, refNormals[i].z };
        for (int k = 0; k < 6; ++k) {
            EXPECT_TRUE(Near(t[k], expected[k], kTolerance)) << "textured vertex " << i << " component " << k;
            EXPECT_TRUE(Near(p[k], expected[k], kTolerance)) << "plain vertex " << i << " component " << k;
        }
        EXPECT_EQ(t[6], model.uvs[i].x) << "vertex " << i;
        EXPECT_EQ(t[7], model.uvs[i].y) << "vertex " << i;
    }
    for (std::size_t k = 0; k < kGuard; ++k) {
        EXPECT_EQ(textured[n * 8 + k], 777.0f) << "textured guard " << k;
        EXPECT_EQ(plain[n * 6 + k], 777.0f) << "plain guard " << k;
    }
}

} // namespace

TEST(VertexSkinningSimdTests, MatchesReferenceForEveryWeightStyleAndVertexCount)
{
    Lcg rng(1234);
    const WeightStyle styles[] = { WeightStyle::Mixed, WeightStyle::Unnormalised, WeightStyle::BadIndices,
        WeightStyle::AllInvalid, WeightStyle::NegativeSum, WeightStyle::ZeroWeights };
    const std::size_t counts[] = { 0, 1, 3, 4, 5, 7, 255, 257, 1001 };
    constexpr int kBones = 40;

    for (const bool withScale : { false, true }) {
        const std::vector<Mat4> matrices = MakeMatrices(kBones, withScale, rng);
        for (const WeightStyle style : styles) {
            for (const bool unitNormals : { true, false }) {
                for (const std::size_t count : counts) {
                    const TestModel model = MakeModel(count, kBones, style, unitNormals, rng);
                    SCOPED_TRACE(::testing::Message() << "style " << static_cast<int>(style) << " scale " << withScale
                                                      << " unit " << unitNormals << " count " << count);
                    ExpectParity(model, matrices, 0, static_cast<std::uint32_t>(count));
                }
            }
        }
    }
}

TEST(VertexSkinningSimdTests, VerticesWithoutUsableInfluenceKeepTheirBindPoseExactly)
{
    Lcg rng(99);
    const std::vector<Mat4> matrices = MakeMatrices(10, false, rng);
    const TestModel model = MakeModel(64, 10, WeightStyle::AllInvalid, false, rng);
    const CompactSkinData data = BuildCompactSkinData(model.positions, model.normals, model.uvs, model.weights, 10);

    std::vector<float> out(64 * 8, 0.0f);
    SkinPackTexturedRange(0, 64, data, matrices.data(), matrices.size(), out.data());
    for (std::size_t i = 0; i < 64; ++i) {
        EXPECT_EQ(out[i * 8 + 0], model.positions[i].x);
        EXPECT_EQ(out[i * 8 + 1], model.positions[i].y);
        EXPECT_EQ(out[i * 8 + 2], model.positions[i].z);
        EXPECT_EQ(out[i * 8 + 3], model.normals[i].x); // not normalised, like the reference
        EXPECT_EQ(out[i * 8 + 4], model.normals[i].y);
        EXPECT_EQ(out[i * 8 + 5], model.normals[i].z);
    }
}

TEST(VertexSkinningSimdTests, RangesThatDoNotStartAtZeroOrPassTheEndMatchTheReference)
{
    Lcg rng(7);
    const std::vector<Mat4> matrices = MakeMatrices(30, true, rng);
    const TestModel model = MakeModel(1000, 30, WeightStyle::Mixed, true, rng);
    ExpectParity(model, matrices, 1, 999);
    ExpectParity(model, matrices, 333, 5000);
    ExpectParity(model, matrices, 500, 500);
}

TEST(VertexSkinningSimdTests, MissingWeightOrNormalArraysFollowTheReference)
{
    Lcg rng(3);
    const std::vector<Mat4> matrices = MakeMatrices(30, false, rng);
    const TestModel model = MakeModel(300, 30, WeightStyle::Mixed, true, rng);
    ExpectParity(model, matrices, 0, 300, /*dropWeights=*/true, false);
    ExpectParity(model, matrices, 0, 300, false, /*dropNormals=*/true);
    ExpectParity(model, matrices, 0, 300, true, true);
}

TEST(VertexSkinningSimdTests, AMatrixCountThatDiffersFromTheBuiltDataWritesNothing)
{
    Lcg rng(5);
    const TestModel model = MakeModel(10, 8, WeightStyle::Mixed, true, rng);
    const CompactSkinData data = BuildCompactSkinData(model.positions, model.normals, model.uvs, model.weights, 8);
    const std::vector<Mat4> fewer = MakeMatrices(7, false, rng);

    std::vector<float> out(10 * 8, 5.0f);
    SkinPackTexturedRange(0, 10, data, fewer.data(), fewer.size(), out.data());
    for (const float value : out) {
        EXPECT_EQ(value, 5.0f);
    }
}

TEST(VertexSkinningSimdTests, BuildDropsInvalidSlotsAndNormalisesTheRest)
{
    TestModel model;
    model.positions = { Vec3{ 1, 2, 3 } };
    model.normals = { Vec3::Up() };
    model.uvs = { Vec2{ 0.25f, 0.75f } };
    VertexSkinWeights w;
    w.boneIndices[0] = -1;     // dropped
    w.boneWeights[0] = 0.5f;
    w.boneIndices[1] = 2;      // kept
    w.boneWeights[1] = 1.0f;
    w.boneIndices[2] = 99;     // out of range, dropped
    w.boneWeights[2] = 0.5f;
    w.boneIndices[3] = 1;      // kept
    w.boneWeights[3] = 3.0f;
    model.weights = { w };

    const CompactSkinData data = BuildCompactSkinData(model.positions, model.normals, model.uvs, model.weights, 4);
    ASSERT_EQ(data.skin.size(), 1u);
    EXPECT_EQ(data.boneCount, 4u);
    EXPECT_EQ(data.skin[0].boneIndices[0], 2u);
    EXPECT_EQ(data.skin[0].boneIndices[1], 1u);
    EXPECT_FLOAT_EQ(data.skin[0].weights[0], 0.25f);
    EXPECT_FLOAT_EQ(data.skin[0].weights[1], 0.75f);
    EXPECT_EQ(data.skin[0].weights[2], 0.0f);
    EXPECT_EQ(data.skin[0].weights[3], 0.0f);
    EXPECT_EQ(data.bind[0].uv[0], 0.25f);
}

namespace {

struct SimdBatchContext {
    const CompactSkinData* data;
    const std::vector<Mat4>* matrices;
    float* out;
};

void RunSimdBatch(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    const SimdBatchContext* context = static_cast<const SimdBatchContext*>(payload);
    SkinPackTexturedRange(beginIndex, endIndex, *context->data, context->matrices->data(), context->matrices->size(),
        context->out);
}

} // namespace

TEST(VertexSkinningSimdTests, ParallelBatchesProduceBitIdenticalResultsToOneSerialCall)
{
    Lcg rng(42);
    constexpr std::size_t kCount = 4001;
    const std::vector<Mat4> matrices = MakeMatrices(60, true, rng);
    const TestModel model = MakeModel(kCount, 60, WeightStyle::BadIndices, false, rng);
    const CompactSkinData data = BuildCompactSkinData(model.positions, model.normals, model.uvs, model.weights, 60);

    std::vector<float> serial(kCount * 8, 0.0f);
    SkinPackTexturedRange(0, static_cast<std::uint32_t>(kCount), data, matrices.data(), matrices.size(), serial.data());

    for (int iteration = 0; iteration < 10; ++iteration) {
        std::vector<float> parallel(kCount * 8, 0.0f);
        SimdBatchContext context{ &data, &matrices, parallel.data() };
        Jobs::JobHandle handle;
        Jobs::Dispatch(&RunSimdBatch, static_cast<std::uint32_t>(kCount), &context, handle, /*minItemsPerBatch=*/256);
        Jobs::JobSystem::Instance().WaitForJobs(handle);
        EXPECT_EQ(0, std::memcmp(serial.data(), parallel.data(), serial.size() * sizeof(float)))
            << "iteration " << iteration;
    }
}
