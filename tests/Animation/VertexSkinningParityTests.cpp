// Job System Phase 6 (First Production Consumer - Animation / Vertex
// Skinning - see task_manager/job_system/
// JOBSYSTEM_PHASE6_FIRST_PRODUCTION_CONSUMER_ANIMATION_SKINNING_v2.md and
// JOB_SYSTEM_PHASE6_COMPLETION_REPORT.md) parity proof. AnimationSystem::
// Update() itself needs a live Renderer/GPU device to test end-to-end
// (Mesh::UpdateVertexData() - see TESTING.md's own "Tier 2" bucket), so
// this test instead proves, at the exact Tier-1 pure-math level this whole
// module has always been tested at, the one new piece of logic Phase 6
// actually adds: skinning a large vertex array via several CONCURRENT
// gte::Jobs::Dispatch() batches (src/Animation/VertexSkinning.h's
// SkinVertexRange(), the same function AnimationSystem::Update() now calls)
// produces results IDENTICAL to the original, serial SkinVertices() call -
// no batch-boundary off-by-one, no aliasing between two batches' own output
// slices, no divergence introduced by running on more than one thread.
#include "Animation/VertexSkinning.h"
#include "Jobs/JobDispatch.h"
#include "Jobs/JobSystem.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace gte;

namespace {

// Mirrors AnimationSystem.cpp's own (anonymous-namespace, therefore
// un-reachable from here) SkinningBatchContext/RunSkinningBatch exactly -
// this is deliberately a SEPARATE copy for the test, not a shared header,
// since the real one is a private implementation detail of
// AnimationSystem.cpp with no reason to be exposed just for testing (the
// PURE function it calls, SkinVertexRange(), is the actual public,
// Tier-1-tested surface - see VertexSkinning.h).
struct SkinningBatchContext {
    const std::vector<Vec3>* bindPositions;
    const std::vector<Vec3>* bindNormals;
    const std::vector<VertexSkinWeights>* skinWeights;
    const std::vector<Mat4>* skinningMatrices;
    std::vector<Vec3>* outPositions;
    std::vector<Vec3>* outNormals;
};

void RunSkinningBatch(std::uint32_t beginIndex, std::uint32_t endIndex, void* payload)
{
    SkinningBatchContext* context = static_cast<SkinningBatchContext*>(payload);
    SkinVertexRange(beginIndex, endIndex, *context->bindPositions, *context->bindNormals, *context->skinWeights,
        *context->skinningMatrices, *context->outPositions, *context->outNormals);
}

// Builds a synthetic, deterministic "model" with `vertexCount` vertices,
// each bound (BDEF1, full weight) to one of `boneCount` bones in
// round-robin order - `boneCount` distinct, easily-distinguishable
// translation matrices mean a batch-splitting or write-aliasing bug would
// show up as a genuine, easy-to-spot positional mismatch, not just luck.
void BuildSyntheticSkinnedModel(std::size_t vertexCount, std::size_t boneCount, std::vector<Vec3>& outBindPositions,
    std::vector<Vec3>& outBindNormals, std::vector<VertexSkinWeights>& outSkinWeights,
    std::vector<Mat4>& outSkinningMatrices)
{
    outBindPositions.resize(vertexCount);
    outBindNormals.resize(vertexCount);
    outSkinWeights.resize(vertexCount);
    for (std::size_t i = 0; i < vertexCount; ++i) {
        outBindPositions[i] = Vec3{ static_cast<float>(i), 0.0f, 0.0f };
        outBindNormals[i] = Vec3::Up();

        VertexSkinWeights w;
        w.type = VertexWeightType::BDEF1;
        w.boneIndices[0] = static_cast<std::int32_t>(i % boneCount);
        w.boneWeights[0] = 1.0f;
        outSkinWeights[i] = w;
    }

    outSkinningMatrices.resize(boneCount);
    for (std::size_t b = 0; b < boneCount; ++b) {
        outSkinningMatrices[b] = Mat4::Translation(Vec3{ 0.0f, static_cast<float>(b) * 100.0f, 0.0f });
    }
}

} // namespace

TEST(VertexSkinningParityTests, SkinVertexRangeFullRangeMatchesSkinVertices)
{
    std::vector<Vec3> bindPositions;
    std::vector<Vec3> bindNormals;
    std::vector<VertexSkinWeights> skinWeights;
    std::vector<Mat4> skinningMatrices;
    constexpr std::size_t kVertexCount = 37;
    constexpr std::size_t kBoneCount = 3;
    BuildSyntheticSkinnedModel(kVertexCount, kBoneCount, bindPositions, bindNormals, skinWeights, skinningMatrices);

    std::vector<Vec3> viaSkinVerticesPositions;
    std::vector<Vec3> viaSkinVerticesNormals;
    SkinVertices(bindPositions, bindNormals, skinWeights, skinningMatrices, viaSkinVerticesPositions,
        viaSkinVerticesNormals);

    std::vector<Vec3> viaRangePositions(kVertexCount);
    std::vector<Vec3> viaRangeNormals(kVertexCount);
    SkinVertexRange(0, static_cast<std::uint32_t>(kVertexCount), bindPositions, bindNormals, skinWeights,
        skinningMatrices, viaRangePositions, viaRangeNormals);

    ASSERT_EQ(viaSkinVerticesPositions.size(), viaRangePositions.size());
    for (std::size_t i = 0; i < kVertexCount; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(viaSkinVerticesPositions[i], viaRangePositions[i])) << "vertex " << i;
        EXPECT_TRUE(ApproximatelyEqual(viaSkinVerticesNormals[i], viaRangeNormals[i])) << "vertex " << i;
    }
}

TEST(VertexSkinningParityTests, ParallelDispatchProducesIdenticalResultsToSerialSkinVertices)
{
    std::vector<Vec3> bindPositions;
    std::vector<Vec3> bindNormals;
    std::vector<VertexSkinWeights> skinWeights;
    std::vector<Mat4> skinningMatrices;
    // Deliberately a large, non-evenly-divisible vertex count (mirrors this
    // Job System campaign's own "prime-ish" edge-case testing convention -
    // see tests/Jobs/JobDispatchTests.cpp's own 1009-item case).
    constexpr std::size_t kVertexCount = 4001;
    constexpr std::size_t kBoneCount = 11;
    BuildSyntheticSkinnedModel(kVertexCount, kBoneCount, bindPositions, bindNormals, skinWeights, skinningMatrices);

    std::vector<Vec3> serialPositions;
    std::vector<Vec3> serialNormals;
    SkinVertices(bindPositions, bindNormals, skinWeights, skinningMatrices, serialPositions, serialNormals);

    std::vector<Vec3> parallelPositions(kVertexCount);
    std::vector<Vec3> parallelNormals(kVertexCount);
    SkinningBatchContext context{ &bindPositions, &bindNormals, &skinWeights, &skinningMatrices, &parallelPositions,
        &parallelNormals };
    Jobs::JobHandle handle;
    Jobs::Dispatch(&RunSkinningBatch, static_cast<std::uint32_t>(kVertexCount), &context, handle,
        /*minItemsPerBatch=*/256);
    Jobs::JobSystem::Instance().WaitForJobs(handle);

    ASSERT_EQ(serialPositions.size(), parallelPositions.size());
    ASSERT_EQ(serialNormals.size(), parallelNormals.size());
    for (std::size_t i = 0; i < kVertexCount; ++i) {
        EXPECT_TRUE(ApproximatelyEqual(serialPositions[i], parallelPositions[i])) << "vertex " << i;
        EXPECT_TRUE(ApproximatelyEqual(serialNormals[i], parallelNormals[i])) << "vertex " << i;
    }
}

TEST(VertexSkinningParityTests, RepeatedParallelDispatchesStayConsistentAcrossIterations)
{
    // Stress-repeat several independent Dispatch() calls in a row - same
    // "never trust a single passing run for genuinely concurrent code"
    // discipline this campaign already applies elsewhere (see AGENTS.md,
    // "Job System").
    std::vector<Vec3> bindPositions;
    std::vector<Vec3> bindNormals;
    std::vector<VertexSkinWeights> skinWeights;
    std::vector<Mat4> skinningMatrices;
    constexpr std::size_t kVertexCount = 2003;
    constexpr std::size_t kBoneCount = 7;
    BuildSyntheticSkinnedModel(kVertexCount, kBoneCount, bindPositions, bindNormals, skinWeights, skinningMatrices);

    std::vector<Vec3> serialPositions;
    std::vector<Vec3> serialNormals;
    SkinVertices(bindPositions, bindNormals, skinWeights, skinningMatrices, serialPositions, serialNormals);

    constexpr int kIterations = 25;
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        std::vector<Vec3> parallelPositions(kVertexCount);
        std::vector<Vec3> parallelNormals(kVertexCount);
        SkinningBatchContext context{ &bindPositions, &bindNormals, &skinWeights, &skinningMatrices,
            &parallelPositions, &parallelNormals };
        Jobs::JobHandle handle;
        Jobs::Dispatch(&RunSkinningBatch, static_cast<std::uint32_t>(kVertexCount), &context, handle,
            /*minItemsPerBatch=*/128);
        Jobs::JobSystem::Instance().WaitForJobs(handle);

        for (std::size_t i = 0; i < kVertexCount; ++i) {
            ASSERT_TRUE(ApproximatelyEqual(serialPositions[i], parallelPositions[i]))
                << "iteration " << iteration << " vertex " << i;
        }
    }
}
