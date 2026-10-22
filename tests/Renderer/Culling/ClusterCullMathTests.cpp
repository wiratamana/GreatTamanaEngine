// Tier-1 tests for src/Renderer/Culling/ClusterCullMath.h: capabilities, layouts, limits, ledger and the CPU cull oracle.

#include "Core/FeatureKit/PersistentBufferPolicy.h"
#include "Renderer/Culling/ClusterCullMath.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace gte {
namespace {

constexpr IndirectDrawCapsView kAllCaps{ true, true, true, 65536 };

MeshCluster MakeCluster(Vec3 center, float radius, Vec3 coneAxis, float cutoff)
{
    MeshCluster cluster{};
    cluster.sphere[0] = center.x;
    cluster.sphere[1] = center.y;
    cluster.sphere[2] = center.z;
    cluster.sphere[3] = radius;
    cluster.cone[0] = coneAxis.x;
    cluster.cone[1] = coneAxis.y;
    cluster.cone[2] = coneAxis.z;
    cluster.cone[3] = cutoff;
    cluster.aabbMin[0] = center.x - radius;
    cluster.aabbMin[1] = center.y - radius;
    cluster.aabbMin[2] = center.z - radius;
    cluster.aabbMax[0] = center.x + radius;
    cluster.aabbMax[1] = center.y + radius;
    cluster.aabbMax[2] = center.z + radius;
    cluster.firstIndex = 0;
    cluster.indexCount = 3;
    return cluster;
}

Mat4 PerspectiveFrom(Vec3 eye)
{
    const Mat4 view = Mat4::LookAtLH(eye, eye + Vec3(0.0f, 0.0f, 1.0f), Vec3::Up());
    return Mat4::PerspectiveFovLH_ZO(1.0f, 1.0f, 0.1f, 100.0f) * view;
}

Mat4 OrthographicLookingForward()
{
    const Mat4 view = Mat4::LookAtLH(Vec3(0.0f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 0.0f), Vec3::Up());
    return Mat4::OrthographicLH_ZO(-10.0f, 10.0f, -10.0f, 10.0f, 0.1f, 100.0f) * view;
}

// ---- capabilities ----

TEST(ClusterCullMath, ClusterPathNeedsEveryCapability)
{
    EXPECT_TRUE(ClusterPathSupported(kAllCaps));

    IndirectDrawCapsView caps = kAllCaps;
    caps.drawIndirectCount = false;
    EXPECT_FALSE(ClusterPathSupported(caps));

    caps = kAllCaps;
    caps.multiDrawIndirect = false;
    EXPECT_FALSE(ClusterPathSupported(caps));

    caps = kAllCaps;
    caps.drawIndirectFirstInstance = false;
    EXPECT_FALSE(ClusterPathSupported(caps));

    caps = kAllCaps;
    caps.maxDrawIndirectCount = 0;
    EXPECT_FALSE(ClusterPathSupported(caps));
}

TEST(ClusterCullMath, OldBatchPathNeedsThreeFeaturesButNoLimit)
{
    EXPECT_TRUE(OldBatchPathSupported(kAllCaps));

    IndirectDrawCapsView caps = kAllCaps;
    caps.drawIndirectCount = false;
    EXPECT_FALSE(OldBatchPathSupported(caps));

    caps = kAllCaps;
    caps.multiDrawIndirect = false;
    EXPECT_FALSE(OldBatchPathSupported(caps));

    caps = kAllCaps;
    caps.drawIndirectFirstInstance = false;
    EXPECT_FALSE(OldBatchPathSupported(caps));

    caps = kAllCaps;
    caps.maxDrawIndirectCount = 0;
    EXPECT_TRUE(OldBatchPathSupported(caps));
}

TEST(ClusterCullMath, DefaultCapsSupportNothing)
{
    constexpr IndirectDrawCapsView none{};
    EXPECT_FALSE(ClusterPathSupported(none));
    EXPECT_FALSE(OldBatchPathSupported(none));
}

// ---- layouts and constants ----

TEST(ClusterCullMath, RecordSizesAndKitConstantsAgree)
{
    EXPECT_EQ(sizeof(MeshCluster), 64u);
    EXPECT_EQ(sizeof(ClusterInstanceGpu), 80u);
    EXPECT_EQ(sizeof(ClusterCullParams) % 16, 0u);
    EXPECT_EQ(kClusterMaxBufferBytes, fx::kMaxPersistentBufferBytes);
    EXPECT_EQ(kClusterMinBufferBytes, fx::kMinPersistentBufferBytes);
    for (std::uint64_t bytes : { 0ull, 1ull, 256ull, 257ull, 5000ull, 1ull << 27, 1ull << 40 }) {
        EXPECT_EQ(ClusterBufferCapacity(bytes), fx::PersistentBufferCapacityFor(bytes)) << bytes;
    }
    EXPECT_EQ(kClusterFlagUniformScale | kClusterFlagPositiveHand | kClusterFlagMoved, 7u);
    EXPECT_EQ(kMaxMeshClusters, kClusterMaxBufferBytes / sizeof(MeshCluster));
}

TEST(ClusterCullMath, InstanceRecordKeepsWorldFlagsAndScale)
{
    const Mat4 world = Mat4::Translation(Vec3(1.0f, 2.0f, 3.0f));
    const ClusterInstanceGpu gpu = MakeClusterInstanceGpu(world, kClusterFlagPositiveHand, 2.5f);
    EXPECT_EQ(gpu.world[12], 1.0f);
    EXPECT_EQ(gpu.world[13], 2.0f);
    EXPECT_EQ(gpu.world[14], 3.0f);
    EXPECT_EQ(gpu.info[0], kClusterFlagPositiveHand);
    EXPECT_EQ(std::bit_cast<float>(gpu.info[1]), 2.5f);
}

TEST(ClusterCullMath, ParamsCarryPlanesCountsAndOrthographicBit)
{
    const Mat4 vp = PerspectiveFrom(Vec3(0.0f, 0.0f, -5.0f));
    const ClusterCullParams params =
        MakeClusterCullParams(vp, Vec3(0.0f, 0.0f, -5.0f), false, 7, 9, ClusterCullFlags::Frustum);
    const auto planes = ExtractFrustumPlanes(vp);
    for (std::size_t i = 0; i < 6; ++i) {
        EXPECT_EQ(params.planes[i][0], planes[i].normal.x);
        EXPECT_EQ(params.planes[i][3], planes[i].distance);
    }
    EXPECT_EQ(params.counts[0], 7u);
    EXPECT_EQ(params.counts[1], 9u);
    EXPECT_EQ(params.counts[2], 63u);
    EXPECT_EQ(params.counts[3], kClusterCullFlagFrustum);
    EXPECT_TRUE(ClusterCullParamsAreFinite(params));

    const ClusterCullParams ortho =
        MakeClusterCullParams(vp, Vec3(0.0f, 0.0f, 1.0f), true, 1, 1, ClusterCullFlags::Frustum);
    EXPECT_EQ(ortho.counts[3], kClusterCullFlagFrustum | kClusterCullFlagOrthographic);

    ClusterCullParams broken = params;
    broken.eyeOrDirection[1] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ClusterCullParamsAreFinite(broken));
    broken = params;
    broken.planes[3][2] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(ClusterCullParamsAreFinite(broken));
}

// ---- frustum and cone oracle ----

TEST(ClusterCullMath, FrustumAcceptsVisibleAndRejectsOutsideAndBehind)
{
    const auto planes = ExtractFrustumPlanes(PerspectiveFrom(Vec3(0.0f, 0.0f, -5.0f)));
    const MeshCluster cluster = MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f, 0.0f, 1.0f), 1.0f);
    EXPECT_FALSE(ClusterFrustumCulled(cluster, Mat4::Identity(), planes));
    EXPECT_TRUE(ClusterFrustumCulled(cluster, Mat4::Translation(Vec3(1000.0f, 0.0f, 0.0f)), planes));
    EXPECT_TRUE(ClusterFrustumCulled(cluster, Mat4::Translation(Vec3(0.0f, 0.0f, -50.0f)), planes));
}

TEST(ClusterCullMath, FrustumKeepsNonFiniteBox)
{
    const auto planes = ExtractFrustumPlanes(PerspectiveFrom(Vec3(0.0f, 0.0f, -5.0f)));
    MeshCluster cluster = MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f, 0.0f, 1.0f), 1.0f);
    cluster.aabbMin[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(ClusterFrustumCulled(cluster, Mat4::Translation(Vec3(1000.0f, 0.0f, 0.0f)), planes));
}

TEST(ClusterCullMath, ConeCullsBackFacingClusterInPerspective)
{
    const MeshCluster cluster = MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f, 0.0f, 1.0f), 0.5f);
    const std::uint32_t both = kClusterFlagUniformScale | kClusterFlagPositiveHand;
    // The axis points away from an eye at -Z: every face looks away, so the cluster is culled.
    EXPECT_TRUE(ClusterConeCulled(cluster, Mat4::Identity(), both, 1.0f, Vec3(0.0f, 0.0f, -10.0f), false));
    EXPECT_FALSE(ClusterConeCulled(cluster, Mat4::Identity(), both, 1.0f, Vec3(0.0f, 0.0f, 10.0f), false));
    // Close to the sphere the radius term keeps it.
    EXPECT_FALSE(ClusterConeCulled(cluster, Mat4::Identity(), both, 1.0f, Vec3(0.0f, 0.0f, -1.5f), false));
}

TEST(ClusterCullMath, ConeCullsInOrthographicByDirection)
{
    const MeshCluster cluster = MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f, 0.0f, 1.0f), 0.5f);
    const std::uint32_t both = kClusterFlagUniformScale | kClusterFlagPositiveHand;
    EXPECT_TRUE(ClusterConeCulled(cluster, Mat4::Identity(), both, 1.0f, Vec3(0.0f, 0.0f, 1.0f), true));
    EXPECT_FALSE(ClusterConeCulled(cluster, Mat4::Identity(), both, 1.0f, Vec3(0.0f, 0.0f, -1.0f), true));
}

TEST(ClusterCullMath, ConeIsOffForNonUniformMirroredOrConelessInstances)
{
    const MeshCluster cone = MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f, 0.0f, 1.0f), 0.5f);
    const MeshCluster noCone = MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    const Vec3 eye(0.0f, 0.0f, -10.0f);
    const std::uint32_t both = kClusterFlagUniformScale | kClusterFlagPositiveHand;
    EXPECT_FALSE(ClusterConeCulled(cone, Mat4::Identity(), kClusterFlagUniformScale, 1.0f, eye, false));
    EXPECT_FALSE(ClusterConeCulled(cone, Mat4::Identity(), kClusterFlagPositiveHand, 1.0f, eye, false));
    EXPECT_FALSE(ClusterConeCulled(cone, Mat4::Identity(), 0, 1.0f, eye, false));
    EXPECT_FALSE(ClusterConeCulled(noCone, Mat4::Identity(), both, 1.0f, eye, false));
}

TEST(ClusterCullMath, CombinedTestHonoursEnabledFlagsOnly)
{
    const auto planes = ExtractFrustumPlanes(PerspectiveFrom(Vec3(0.0f, 0.0f, -10.0f)));
    const MeshCluster cluster = MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 1.0f, Vec3(0.0f, 0.0f, 1.0f), 0.5f);
    const Vec3 eye(0.0f, 0.0f, -10.0f);
    const std::uint32_t both = kClusterFlagUniformScale | kClusterFlagPositiveHand;
    EXPECT_FALSE(ClusterCulled(cluster, Mat4::Identity(), both, 1.0f, ClusterCullFlags::Frustum, planes, eye, false));
    EXPECT_TRUE(ClusterCulled(cluster, Mat4::Identity(), both, 1.0f,
        ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone, planes, eye, false));
    EXPECT_TRUE(ClusterCulled(cluster, Mat4::Translation(Vec3(1000.0f, 0.0f, 0.0f)), both, 1.0f,
        ClusterCullFlags::Frustum, planes, eye, false));
    EXPECT_FALSE(ClusterCulled(cluster, Mat4::Translation(Vec3(1000.0f, 0.0f, 0.0f)), both, 1.0f,
        ClusterCullFlags::None, planes, eye, false));
}

// ---- instance flags ----

TEST(ClusterCullMath, InstanceFlagsFollowScaleAndHandedness)
{
    float maxScale = 0.0f;
    EXPECT_EQ(ClusterInstanceFlagsFor(Mat4::Identity(), &maxScale), kClusterFlagUniformScale | kClusterFlagPositiveHand);
    EXPECT_FLOAT_EQ(maxScale, 1.0f);

    EXPECT_EQ(ClusterInstanceFlagsFor(Mat4::Scaling(Vec3(2.0f, 2.0f, 2.0f)), &maxScale),
        kClusterFlagUniformScale | kClusterFlagPositiveHand);
    EXPECT_FLOAT_EQ(maxScale, 2.0f);

    EXPECT_EQ(ClusterInstanceFlagsFor(Mat4::Scaling(Vec3(1.0f, 2.0f, 1.0f)), &maxScale), kClusterFlagPositiveHand);
    EXPECT_FLOAT_EQ(maxScale, 2.0f);

    EXPECT_EQ(ClusterInstanceFlagsFor(Mat4::Scaling(Vec3(-1.0f, 1.0f, 1.0f)), &maxScale), kClusterFlagUniformScale);

    // Within 1e-3 relative counts as uniform.
    EXPECT_EQ(ClusterInstanceFlagsFor(Mat4::Scaling(Vec3(1.0f, 1.0005f, 1.0f)), &maxScale),
        kClusterFlagUniformScale | kClusterFlagPositiveHand);
}

TEST(ClusterCullMath, InstanceFlagsAreZeroForDegenerateOrNonFiniteMatrices)
{
    float maxScale = 1.0f;
    EXPECT_EQ(ClusterInstanceFlagsFor(Mat4::Zero(), &maxScale), 0u);
    EXPECT_EQ(maxScale, 0.0f);

    Mat4 broken = Mat4::Identity();
    broken(1, 1) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(ClusterInstanceFlagsFor(broken, nullptr), 0u);
}

// ---- id mapping ----

TEST(ClusterCullMath, IdMappingCoversFirstLastAndGuard)
{
    auto ref = ClusterIdToInstanceCluster(0, 5, 15);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->instance, 0u);
    EXPECT_EQ(ref->cluster, 0u);

    ref = ClusterIdToInstanceCluster(14, 5, 15);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->instance, 2u);
    EXPECT_EQ(ref->cluster, 4u);

    EXPECT_FALSE(ClusterIdToInstanceCluster(15, 5, 15).has_value());
    EXPECT_FALSE(ClusterIdToInstanceCluster(0, 0, 0).has_value());
    EXPECT_FALSE(ClusterIdToInstanceCluster(0, 0, 10).has_value());
}

TEST(ClusterCullMath, IdMappingHandlesOneClusterAndOneInstance)
{
    auto ref = ClusterIdToInstanceCluster(7, 1, 10);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->instance, 7u);
    EXPECT_EQ(ref->cluster, 0u);

    ref = ClusterIdToInstanceCluster(3, 4, 4);
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->instance, 0u);
    EXPECT_EQ(ref->cluster, 3u);
}

// ---- limits ----

TEST(ClusterCullMath, LimitsComeFromConstantsAndDeviceCount)
{
    IndirectDrawCapsView caps = kAllCaps;
    caps.maxDrawIndirectCount = std::numeric_limits<std::uint32_t>::max();
    ClusterLimits limits = ClusterLimitsFor(caps);
    EXPECT_EQ(limits.maxWorkItems, 65535ull * 64ull);
    EXPECT_EQ(limits.maxInstances, kClusterMaxBufferBytes / 80);
    EXPECT_EQ(limits.maxClusters, kMaxMeshClusters);

    caps.maxDrawIndirectCount = 1000;
    EXPECT_EQ(ClusterLimitsFor(caps).maxWorkItems, 1000ull);
}

TEST(ClusterCullMath, PartVerdictsCoverEveryLimit)
{
    IndirectDrawCapsView caps = kAllCaps;
    caps.maxDrawIndirectCount = std::numeric_limits<std::uint32_t>::max();
    const ClusterLimits limits = ClusterLimitsFor(caps);

    EXPECT_EQ(EvaluateClusterPart(10, 100, limits), ClusterPartVerdict::Ok);
    EXPECT_EQ(EvaluateClusterPart(1, limits.maxClusters + 1, limits), ClusterPartVerdict::TooManyClusters);
    EXPECT_EQ(EvaluateClusterPart(limits.maxInstances + 1, 1, limits), ClusterPartVerdict::TooManyInstances);
    EXPECT_EQ(EvaluateClusterPart(100000, 100, limits), ClusterPartVerdict::TooManyWorkItems);
    // 1e6 x 5000 = 5e9 overflows 32 bits and must still be refused.
    EXPECT_EQ(EvaluateClusterPart(1000000, 5000, limits), ClusterPartVerdict::TooManyWorkItems);
    EXPECT_EQ(EvaluateClusterPart(limits.maxInstances, limits.maxClusters, limits), ClusterPartVerdict::TooManyWorkItems);
    // 65535 x 64 is exactly the dispatch limit; one more cluster crosses it.
    EXPECT_EQ(EvaluateClusterPart(65535, 64, limits), ClusterPartVerdict::Ok);
    EXPECT_EQ(EvaluateClusterPart(65535, 65, limits), ClusterPartVerdict::TooManyWorkItems);
}

TEST(ClusterCullMath, SmallPartsAreNotWorthAChain)
{
    const ClusterLimits limits = ClusterLimitsFor(kAllCaps);
    EXPECT_EQ(EvaluateClusterPart(3, 1, limits), ClusterPartVerdict::NotWorthIt);
    EXPECT_EQ(EvaluateClusterPart(4, 1, limits), ClusterPartVerdict::Ok);
    EXPECT_EQ(EvaluateClusterPart(1, 2, limits), ClusterPartVerdict::Ok);
    EXPECT_EQ(EvaluateClusterPart(0, 100, limits), ClusterPartVerdict::NotWorthIt);
    EXPECT_EQ(EvaluateClusterPart(100, 0, limits), ClusterPartVerdict::NotWorthIt);
}

// ---- budget and ledger ----

TEST(ClusterCullMath, ChainBytesRoundEachBufferToItsCapacity)
{
    EXPECT_EQ(ClusterChainBytes(1, 1), 4u * 256u);
    // 800 -> 1024, 8000 -> 8192, 20000 -> 32768, 4 -> 256.
    EXPECT_EQ(ClusterChainBytes(10, 1000), 1024u + 8192u + 32768u + 256u);
    EXPECT_EQ(ClusterBufferCapacity(0), 256u);
    EXPECT_EQ(ClusterBufferCapacity(257), 512u);
    EXPECT_EQ(ClusterBufferCapacity(1ull << 40), kClusterMaxBufferBytes);
    EXPECT_LE(ClusterChainBytes(std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max()),
        4 * kClusterMaxBufferBytes);
}

TEST(ClusterCullMath, BudgetDecisionNeverOverflows)
{
    constexpr std::uint64_t ceiling = kClusterDrawBudgetBytes;
    EXPECT_TRUE(FitsClusterBudget(ceiling - 1, 1, ceiling));
    EXPECT_TRUE(FitsClusterBudget(ceiling, 0, ceiling));
    EXPECT_FALSE(FitsClusterBudget(ceiling, 1, ceiling));
    EXPECT_FALSE(FitsClusterBudget(ceiling + 1, 0, ceiling));
    constexpr std::uint64_t threeGiB = 3ull << 30;
    EXPECT_TRUE(FitsClusterBudget(threeGiB, threeGiB, 8ull << 30));
    EXPECT_FALSE(FitsClusterBudget(threeGiB, threeGiB, 5ull << 30));
    EXPECT_FALSE(FitsClusterBudget(std::numeric_limits<std::uint64_t>::max(), 1, std::numeric_limits<std::uint64_t>::max()));
}

TEST(ClusterCullMath, LedgerRefusesPastTheCeilingByOneByteAndAcceptsAfterRelease)
{
    constexpr std::uint64_t ceiling = 500;
    ClusterDrawLedger ledger;
    for (std::uint32_t scope = 0; scope < 3; ++scope) {
        EXPECT_TRUE(ledger.TryReserve(1, scope, 100, ceiling));
    }
    EXPECT_TRUE(ledger.TryReserve(2, 0, 100, ceiling));
    EXPECT_TRUE(ledger.TryReserve(2, 1, 100, ceiling));
    EXPECT_EQ(ledger.Total(), ceiling);

    EXPECT_FALSE(ledger.TryReserve(2, 2, 1, ceiling));
    EXPECT_EQ(ledger.Total(), ceiling);

    // An idle scope returns its bytes; the same request then fits.
    ledger.Release(1, 2);
    EXPECT_EQ(ledger.HeldBy(1, 2), 0u);
    EXPECT_TRUE(ledger.TryReserve(2, 2, 100, ceiling));
    EXPECT_EQ(ledger.Total(), ceiling);

    ledger.ReleaseOwner(2);
    EXPECT_EQ(ledger.Total(), 200u);
}

TEST(ClusterCullMath, LedgerGrowsAnEntryByItsDeltaOnly)
{
    ClusterDrawLedger ledger;
    EXPECT_TRUE(ledger.TryReserve(1, 0, 300, 400));
    EXPECT_TRUE(ledger.TryReserve(1, 0, 400, 400));
    EXPECT_EQ(ledger.Total(), 400u);
    EXPECT_FALSE(ledger.TryReserve(1, 0, 401, 400));
    EXPECT_EQ(ledger.HeldBy(1, 0), 400u);
    // A smaller request never shrinks the entry.
    EXPECT_TRUE(ledger.TryReserve(1, 0, 10, 400));
    EXPECT_EQ(ledger.HeldBy(1, 0), 400u);
}

TEST(ClusterCullMath, IdleReleaseStartsAtTheNamedFrameCount)
{
    EXPECT_FALSE(ShouldReleaseIdleScope(kClusterIdleReleaseFrames - 1));
    EXPECT_TRUE(ShouldReleaseIdleScope(kClusterIdleReleaseFrames));
}

// ---- pipeline key ----

TEST(ClusterCullMath, ConeFlagAndBackFaceCullingAreOneDecision)
{
    for (bool cone : { false, true }) {
        for (bool depthOnly : { false, true }) {
            const ClusterCullFlags flags =
                cone ? ClusterCullFlags::Frustum | ClusterCullFlags::BackfaceCone : ClusterCullFlags::Frustum;
            const ClusterPipelineKey key = ClusterPipelineKeyFor(flags, depthOnly, false);
            EXPECT_EQ(key.cullBackFaces, cone && !depthOnly);
            EXPECT_EQ(HasClusterCullFlag(EffectiveClusterCullFlags(flags, depthOnly), ClusterCullFlags::BackfaceCone),
                cone && !depthOnly);
            EXPECT_EQ(key.variant == ClusterPipelineVariant::DepthOnly, depthOnly);
        }
    }
}

TEST(ClusterCullMath, TintVariantIsChosenForColorLanesOnly)
{
    EXPECT_EQ(ClusterPipelineKeyFor(ClusterCullFlags::Frustum, false, true).variant, ClusterPipelineVariant::IdTint);
    EXPECT_EQ(ClusterPipelineKeyFor(ClusterCullFlags::Frustum, false, false).variant, ClusterPipelineVariant::Lit);
    EXPECT_EQ(ClusterPipelineKeyFor(ClusterCullFlags::Frustum, true, true).variant, ClusterPipelineVariant::DepthOnly);
}

// ---- camera ----

TEST(ClusterCullMath, OrthographicDetectionAndFiniteness)
{
    const Mat4 perspective = PerspectiveFrom(Vec3(0.0f, 0.0f, -5.0f));
    const Mat4 ortho = OrthographicLookingForward();
    EXPECT_FALSE(IsOrthographicProjection(perspective));
    EXPECT_TRUE(IsOrthographicProjection(ortho));

    Mat4 broken = ortho;
    broken(0, 0) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(CameraIsFinite(broken, Vec3(0.0f, 0.0f, 1.0f)));
    EXPECT_FALSE(CameraIsFinite(ortho, Vec3(std::numeric_limits<float>::infinity(), 0.0f, 0.0f)));
    EXPECT_TRUE(CameraIsFinite(ortho, Vec3(0.0f, 0.0f, 1.0f)));

    const Vec3 direction = OrthographicViewDirection(ortho);
    EXPECT_NEAR(direction.z, 1.0f, 1e-4f);
    EXPECT_NEAR(Length(direction), 1.0f, 1e-4f);
}

// ---- coincident triangles ----

TEST(ClusterCullMath, CoincidentTriangleHelperFindsRepeatsInAnyOrder)
{
    const std::vector<Vec3> positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(1, 1, 0) };
    const std::vector<std::uint32_t> repeated = { 0, 1, 2, 2, 1, 0 };
    const std::vector<std::uint32_t> distinct = { 0, 1, 2, 1, 3, 2 };
    EXPECT_TRUE(MeshHasCoincidentTriangles(positions, repeated));
    EXPECT_FALSE(MeshHasCoincidentTriangles(positions, distinct));
}

TEST(ClusterCullMath, CoincidentTriangleHelperComparesPositionsNotIndices)
{
    // Vertex 3 duplicates vertex 0 (with a negative zero) at another index.
    const std::vector<Vec3> positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0), Vec3(-0.0f, 0, 0) };
    const std::vector<std::uint32_t> indices = { 0, 1, 2, 3, 1, 2 };
    EXPECT_TRUE(MeshHasCoincidentTriangles(positions, indices));
}

TEST(ClusterCullMath, CoincidentTriangleHelperIgnoresBadIndicesAndEmptyInput)
{
    const std::vector<Vec3> positions = { Vec3(0, 0, 0), Vec3(1, 0, 0), Vec3(0, 1, 0) };
    const std::vector<std::uint32_t> bad = { 0, 1, 9, 0, 1, 9 };
    EXPECT_FALSE(MeshHasCoincidentTriangles(positions, bad));
    EXPECT_FALSE(MeshHasCoincidentTriangles({}, {}));
}

} // namespace
} // namespace gte
