// Tier-1 tests for src/Renderer/Culling/ClusterCullMath.h: capabilities, layouts, limits, ledger and the CPU cull oracle.

#include "Assets/MeshClusterValidation.h"
#include "Core/FeatureKit/PersistentBufferPolicy.h"
#include "Renderer/Culling/ClusterCullMath.h"
#include "Renderer/Culling/HizBuildPlan.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

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

TEST(ClusterCullMath, DefaultCapsSupportNothing)
{
    constexpr IndirectDrawCapsView none{};
    EXPECT_FALSE(ClusterPathSupported(none));
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
    EXPECT_EQ(kClusterFlagUniformScale | kClusterFlagPositiveHand, 3u);
    EXPECT_EQ(sizeof(ClusterCullParams), 224u);
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
    EXPECT_EQ(ClusterChainBytes(1, 1, false), 4u * 256u);
    // 800 -> 1024, 8000 -> 8192, 20000 -> 32768, 4 -> 256.
    EXPECT_EQ(ClusterChainBytes(10, 1000, false), 1024u + 8192u + 32768u + 256u);
    EXPECT_EQ(ClusterBufferCapacity(0), 256u);
    EXPECT_EQ(ClusterBufferCapacity(257), 512u);
    EXPECT_EQ(ClusterBufferCapacity(1ull << 40), kClusterMaxBufferBytes);
    EXPECT_LE(ClusterChainBytes(std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max(), false),
        4 * kClusterMaxBufferBytes);
}

TEST(ClusterCullMath, TwoPhaseChainDoublesLanesAndAddsTheHistory)
{
    EXPECT_EQ(ClusterHistoryCapacity(0), 256u);
    EXPECT_EQ(ClusterHistoryCapacity(1000), 4096u);
    EXPECT_EQ(ClusterChainBytes(1, 1, true), 256u + 2u * 3u * 256u + 256u);
    // Lane: visible 8192 + commands 32768 + count 256. History: 4000 -> 4096.
    EXPECT_EQ(ClusterChainBytes(10, 1000, true), 1024u + 2u * (8192u + 32768u + 256u) + 4096u);
    EXPECT_GT(ClusterChainBytes(10, 1000, true), ClusterChainBytes(10, 1000, false));
    // Saturating buffers: instances, two lanes of visible and commands, history.
    EXPECT_LE(ClusterChainBytes(std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max(), true),
        6 * kClusterMaxBufferBytes + 4096u);
    // One million items per chain fit the draw budget; the dispatch limit (4.2M items) does not.
    EXPECT_LE(ClusterChainBytes(1, 1000000, true), kClusterDrawBudgetBytes);
    EXPECT_GT(ClusterChainBytes(1, kMaxClusterDispatchGroups * kClusterCullGroupSize, true), kClusterDrawBudgetBytes);
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

// ---- pipeline cache index ----

TEST(ClusterCullMath, PipelineCacheIndexIsDenseAndUnique)
{
    std::array<bool, kClusterPipelineKeyCount> seen{};
    for (const ClusterPipelineVariant variant :
        { ClusterPipelineVariant::Lit, ClusterPipelineVariant::IdTint, ClusterPipelineVariant::DepthOnly }) {
        for (const bool cullBackFaces : { false, true }) {
            const std::size_t index = ClusterPipelineCacheIndex(ClusterPipelineKey{ variant, cullBackFaces });
            ASSERT_LT(index, kClusterPipelineKeyCount);
            EXPECT_FALSE(seen[index]);
            seen[index] = true;
        }
    }
    EXPECT_TRUE(std::all_of(seen.begin(), seen.end(), [](bool b) { return b; }));
}

// ---- Hi-Z occlusion ----

std::vector<float> UniformAtlas(const HizLayout& layout, float value)
{
    return std::vector<float>(static_cast<std::size_t>(layout.atlasWidth) * layout.atlasHeight, value);
}

// Perspective view-projection with the near plane at 0.1 and the given far plane, looking along +Z.
Mat4 PerspectiveWithFar(float farZ)
{
    return Mat4::PerspectiveFovLH_ZO(1.0f, 1.0f, 0.1f, farZ) * Mat4::LookAtLH(Vec3(0.0f, 0.0f, 0.0f),
               Vec3(0.0f, 0.0f, 1.0f), Vec3::Up());
}

TEST(ClusterCullMath, HizLayoutPacksLevelsRightOfLevelZero)
{
    const HizLayout layout = HizLayoutFor(256, 256);
    ASSERT_TRUE(layout.IsValid());
    EXPECT_EQ(layout.baseWidth, 128u);
    EXPECT_EQ(layout.baseHeight, 128u);
    EXPECT_EQ(layout.levelCount, 8u);
    EXPECT_EQ(layout.atlasWidth, 192u);
    EXPECT_EQ(layout.atlasHeight, 128u);

    EXPECT_EQ(layout.levels[0].originX, 0u);
    EXPECT_EQ(layout.levels[0].originY, 0u);
    const std::uint32_t expectedY[] = { 0, 0, 64, 96, 112, 120, 124, 126 };
    const std::uint32_t expectedSize[] = { 128, 64, 32, 16, 8, 4, 2, 1 };
    for (std::uint32_t k = 1; k < layout.levelCount; ++k) {
        EXPECT_EQ(layout.levels[k].originX, 128u) << k;
        EXPECT_EQ(layout.levels[k].originY, expectedY[k]) << k;
        EXPECT_EQ(layout.levels[k].width, expectedSize[k]) << k;
        EXPECT_EQ(layout.levels[k].height, expectedSize[k]) << k;
    }
}

TEST(ClusterCullMath, HizLayoutLevelCountReachesOneTexelForEverySize)
{
    struct Case {
        std::uint32_t scene;
        std::uint32_t sceneHeight;
        std::uint32_t base;
        std::uint32_t baseHeight;
        std::uint32_t levels;
    };
    const Case cases[] = {
        { 1, 1, 1, 1, 1 },
        { 2, 2, 1, 1, 1 },
        { 350, 447, 175, 224, 9 },
        { 1920, 1080, 960, 540, 11 },
        { 3840, 2160, 1920, 1080, 12 },
        { 8192, 4320, 4096, 2160, 13 },
        { 16384, 16384, 8192, 8192, 14 },
    };
    for (const Case& c : cases) {
        const HizLayout layout = HizLayoutFor(c.scene, c.sceneHeight);
        ASSERT_TRUE(layout.IsValid()) << c.scene << "x" << c.sceneHeight;
        EXPECT_EQ(layout.baseWidth, c.base);
        EXPECT_EQ(layout.baseHeight, c.baseHeight);
        EXPECT_EQ(layout.levelCount, c.levels) << c.scene << "x" << c.sceneHeight;
        const HizLevel& top = layout.levels[layout.levelCount - 1];
        EXPECT_EQ(top.width, 1u);
        EXPECT_EQ(top.height, 1u);
        for (std::uint32_t k = 0; k < layout.levelCount; ++k) {
            EXPECT_EQ(layout.levels[k].width, std::max(1u, (c.base + (1u << k) - 1u) >> k)) << k;
            EXPECT_EQ(layout.levels[k].height, std::max(1u, (c.baseHeight + (1u << k) - 1u) >> k)) << k;
        }
    }
}

TEST(ClusterCullMath, HizLayoutRefusesZeroAndOversizedScenes)
{
    EXPECT_FALSE(HizLayoutFor(0, 100).IsValid());
    EXPECT_FALSE(HizLayoutFor(100, 0).IsValid());
    EXPECT_TRUE(HizLayoutFor(16384, 16384).IsValid());
    EXPECT_FALSE(HizLayoutFor(16385, 100).IsValid());
    EXPECT_FALSE(HizLayoutFor(100, 16385).IsValid());
    EXPECT_FALSE(HizLayoutFor(0xFFFFFFFFu, 0xFFFFFFFFu).IsValid());
}

TEST(ClusterCullMath, HizLevelsNeverOverlapAndFitTheAtlas)
{
    for (const auto& [width, height] : { std::pair{ 5u, 3u }, std::pair{ 350u, 447u }, std::pair{ 1921u, 1081u } }) {
        const HizLayout layout = HizLayoutFor(width, height);
        ASSERT_TRUE(layout.IsValid());
        std::vector<std::uint8_t> used(static_cast<std::size_t>(layout.atlasWidth) * layout.atlasHeight, 0);
        for (std::uint32_t k = 0; k < layout.levelCount; ++k) {
            const HizLevel& level = layout.levels[k];
            ASSERT_LE(level.originX + level.width, layout.atlasWidth) << k;
            ASSERT_LE(level.originY + level.height, layout.atlasHeight) << k;
            for (std::uint32_t y = 0; y < level.height; ++y) {
                for (std::uint32_t x = 0; x < level.width; ++x) {
                    std::uint8_t& cell = used[static_cast<std::size_t>(level.originY + y) * layout.atlasWidth
                        + level.originX + x];
                    EXPECT_EQ(cell, 0u) << "level " << k;
                    cell = 1;
                }
            }
        }
    }
}

TEST(ClusterCullMath, HizLevelForExtentIsTheSmallestCoveringLevel)
{
    const std::pair<float, std::uint32_t> cases[] = {
        { 0.0f, 0 }, { 1.0f, 0 }, { 2.0f, 1 }, { 3.0f, 2 }, { 33.0f, 6 }, { 64.0f, 6 }, { 500.0f, 9 }, { 4000.0f, 12 },
    };
    for (const auto& [extent, level] : cases) {
        EXPECT_EQ(HizLevelForExtent(extent, kHizMaxLevels), level) << extent;
    }
    EXPECT_EQ(HizLevelForExtent(4000.0f, 3), 2u); // Capped at the top level.
    EXPECT_EQ(HizLevelForExtent(std::nanf(""), kHizMaxLevels), 0u);
    EXPECT_EQ(HizLevelForExtent(100.0f, 0), 0u);
}

TEST(ClusterCullMath, HizChooseRectPicksTheSmallestLevelThatCoversTwoTexels)
{
    const HizLayout layout = HizLayoutFor(256, 256);
    const std::optional<HizRect> small = HizChooseRect(10.0f, 10.0f, 10.2f, 10.2f, layout);
    ASSERT_TRUE(small.has_value());
    EXPECT_EQ(small->level, 2u); // 0.2 texels plus a margin of one on each side is 2.2 wide.
    EXPECT_EQ(small->x0, 2u);
    EXPECT_EQ(small->x1, 2u);

    // A rect that covers the whole screen reads the 1 x 1 top level: no size rule keeps it.
    const std::optional<HizRect> whole = HizChooseRect(0.0f, 0.0f, 128.0f, 128.0f, layout);
    ASSERT_TRUE(whole.has_value());
    EXPECT_EQ(whole->level, layout.levelCount - 1);
    EXPECT_EQ(whole->x0, 0u);
    EXPECT_EQ(whole->x1, 0u);

    EXPECT_FALSE(HizChooseRect(std::nanf(""), 0.0f, 1.0f, 1.0f, layout).has_value());
    EXPECT_FALSE(HizChooseRect(0.0f, 0.0f, 1.0f, 1.0f, HizLayout{}).has_value());
}

TEST(ClusterCullMath, HizRectNeverSpansMoreThanTwoTexelsAtItsLevel)
{
    const HizLayout layout = HizLayoutFor(1024, 1024);
    for (float width : { 0.3f, 1.0f, 2.5f, 7.7f, 33.0f, 100.0f, 255.5f, 400.0f }) {
        for (float start = 0.0f; start < 100.0f; start += 6.37f) {
            const std::optional<HizRect> rect = HizChooseRect(start, start * 0.5f, start + width, start * 0.5f + width, layout);
            ASSERT_TRUE(rect.has_value());
            EXPECT_LE(rect->x1 - rect->x0, 1u) << start << " " << width;
            EXPECT_LE(rect->y1 - rect->y0, 1u) << start << " " << width;
        }
    }
}

TEST(ClusterCullMath, HizFarthestDepthTakesTheMaxOfFourTexels)
{
    const HizLayout layout = HizLayoutFor(256, 256);
    std::vector<float> atlas = UniformAtlas(layout, 0.25f);
    const HizRect rect{ 0, 4, 4, 5, 5 };
    atlas[static_cast<std::size_t>(5) * layout.atlasWidth + 4] = 0.75f;
    EXPECT_FLOAT_EQ(HizFarthestDepth(atlas, layout, rect), 0.75f);

    const std::vector<float> tooSmall(4, 0.0f);
    EXPECT_FLOAT_EQ(HizFarthestDepth(tooSmall, layout, rect), 1.0f); // No data: nothing is occluded.
}

TEST(ClusterCullMath, HizPyramidReferenceIsTheMaxOverEveryBlock)
{
    constexpr std::uint32_t kWidth = 33;
    constexpr std::uint32_t kHeight = 17;
    std::vector<float> depth(static_cast<std::size_t>(kWidth) * kHeight);
    std::uint32_t state = 12345u;
    for (float& d : depth) {
        state = state * 1664525u + 1013904223u;
        d = static_cast<float>(state >> 8) / static_cast<float>(1u << 24);
    }
    const HizLayout layout = HizLayoutFor(kWidth, kHeight);
    const std::vector<float> atlas = BuildHizPyramidReference(depth, kWidth, kHeight, layout);
    ASSERT_EQ(atlas.size(), static_cast<std::size_t>(layout.atlasWidth) * layout.atlasHeight);

    for (std::uint32_t k = 0; k < layout.levelCount; ++k) {
        const HizLevel& level = layout.levels[k];
        const std::uint32_t block = 2u << k;
        for (std::uint32_t y = 0; y < level.height; ++y) {
            for (std::uint32_t x = 0; x < level.width; ++x) {
                float expected = 0.0f;
                for (std::uint32_t sy = y * block; sy < std::min((y + 1) * block, kHeight); ++sy) {
                    for (std::uint32_t sx = x * block; sx < std::min((x + 1) * block, kWidth); ++sx) {
                        expected = std::max(expected, depth[static_cast<std::size_t>(sy) * kWidth + sx]);
                    }
                }
                const std::size_t index = static_cast<std::size_t>(level.originY + y) * layout.atlasWidth + level.originX + x;
                EXPECT_EQ(atlas[index], expected) << "level " << k << " texel " << x << "," << y;
            }
        }
    }
    const HizLevel& top = layout.levels[layout.levelCount - 1];
    EXPECT_EQ(atlas[static_cast<std::size_t>(top.originY) * layout.atlasWidth + top.originX],
        *std::max_element(depth.begin(), depth.end()));

    EXPECT_TRUE(BuildHizPyramidReference(depth, kWidth, kHeight, HizLayout{}).empty());
    EXPECT_TRUE(BuildHizPyramidReference(std::span<const float>(depth.data(), 4), kWidth, kHeight, layout).empty());
}

TEST(ClusterCullMath, HizDepthMapMatchesThePerspectiveProjection)
{
    const HizDepthMap map = HizDepthMapOf(PerspectiveWithFar(1000.0f));
    ASSERT_FALSE(map.raw);
    EXPECT_NEAR(map.k, 1000.0f / 999.9f, 1e-5f);
    EXPECT_NEAR(map.b, -0.1f * 1000.0f / 999.9f, 1e-5f);

    // A moved camera keeps the same map: only the near and far planes matter.
    const Mat4 moved = Mat4::PerspectiveFovLH_ZO(1.0f, 1.0f, 0.1f, 1000.0f)
        * Mat4::LookAtLH(Vec3(3.0f, 4.0f, -5.0f), Vec3(1.0f, 0.0f, 2.0f), Vec3::Up());
    const HizDepthMap movedMap = HizDepthMapOf(moved);
    ASSERT_FALSE(movedMap.raw);
    EXPECT_NEAR(movedMap.k, map.k, 1e-4f);
    EXPECT_NEAR(movedMap.b, map.b, 1e-4f);
}

TEST(ClusterCullMath, HizDepthMapIsRawForOrthographicAndSkewedMatrices)
{
    EXPECT_TRUE(HizDepthMapOf(OrthographicLookingForward()).raw);
    Mat4 skewed = PerspectiveWithFar(1000.0f);
    skewed(2, 0) += 0.5f;
    EXPECT_TRUE(HizDepthMapOf(skewed).raw);
    Mat4 broken = PerspectiveWithFar(1000.0f);
    broken(2, 2) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(HizDepthMapOf(broken).raw);
}

TEST(ClusterCullMath, HizBehindUsesARelativeMarginInViewDepth)
{
    const HizDepthMap map = HizDepthMapOf(PerspectiveWithFar(1000.0f));
    ASSERT_FALSE(map.raw);
    for (const float occluderW : { 5.0f, 100.0f, 900.0f }) {
        const float depth = map.k + map.b / occluderW;
        EXPECT_FALSE(HizBehind(map, occluderW * 1.001f, 0.5f, depth)) << occluderW; // 0.1% behind: kept.
        EXPECT_TRUE(HizBehind(map, occluderW * 1.01f, 0.5f, depth)) << occluderW;   // 1% behind: culled.
        EXPECT_FALSE(HizBehind(map, occluderW * 0.9f, 0.5f, depth)) << occluderW;   // In front: kept.
    }
}

TEST(ClusterCullMath, HizBehindNeverCullsForClearOrNonFiniteDepth)
{
    const HizDepthMap map = HizDepthMapOf(PerspectiveWithFar(1000.0f));
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(HizBehind(map, 5000.0f, 1.0f, 1.0f)); // Clear depth: nothing was drawn there.
    EXPECT_FALSE(HizBehind(map, 5000.0f, 1.0f, 1.5f));
    EXPECT_FALSE(HizBehind(map, nan, 0.5f, 0.5f));
    EXPECT_FALSE(HizBehind(map, 50.0f, nan, 0.5f));
    EXPECT_FALSE(HizBehind(map, 50.0f, 0.5f, nan));
    EXPECT_FALSE(HizBehind(map, inf, 0.5f, 0.5f));
    EXPECT_FALSE(HizBehind(HizDepthMap{}, nan, 0.5f, 0.5f));
}

TEST(ClusterCullMath, HizBehindComparesRawDepthWithAnAbsoluteMargin)
{
    const HizDepthMap raw{};
    ASSERT_TRUE(raw.raw);
    EXPECT_TRUE(HizBehind(raw, 0.0f, 0.6f, 0.5f));
    EXPECT_FALSE(HizBehind(raw, 0.0f, 0.5005f, 0.5f));
    EXPECT_FALSE(HizBehind(raw, 0.0f, 0.4f, 0.5f));
}

TEST(ClusterCullMath, OccludedOnlyWhenBoxIsBehindThePyramidDepth)
{
    const HizLayout layout = HizLayoutFor(256, 256);
    const Mat4 viewProjection = PerspectiveFrom(Vec3(0.0f, 0.0f, 0.0f));
    const MeshCluster cluster = MakeCluster(Vec3(0.0f, 0.0f, 10.0f), 0.5f, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    const Mat4 world = Mat4::Identity();

    // The nearest face is at view distance 9.5.
    EXPECT_TRUE(ClusterOccluded(cluster, world, viewProjection, layout, UniformAtlas(layout, 0.5f)));
    EXPECT_TRUE(ClusterOccluded(cluster, world, viewProjection, layout, UniformAtlas(layout, 0.98f)));
    EXPECT_FALSE(ClusterOccluded(cluster, world, viewProjection, layout, UniformAtlas(layout, 0.9999f)));
    EXPECT_FALSE(ClusterOccluded(cluster, world, viewProjection, layout, UniformAtlas(layout, 1.0f)));
}

TEST(ClusterCullMath, OcclusionHasNoSizeRule)
{
    // A box that covers most of a large view is culled by a near pyramid.
    const HizLayout big = HizLayoutFor(1024, 1024);
    const Mat4 viewProjection = PerspectiveFrom(Vec3(0.0f, 0.0f, 0.0f));
    const MeshCluster wide = MakeCluster(Vec3(0.0f, 0.0f, 10.0f), 3.0f, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_TRUE(ClusterOccluded(wide, Mat4::Identity(), viewProjection, big, UniformAtlas(big, 0.0f)));
}

TEST(ClusterCullMath, OcclusionKeepsWhatItCannotJudge)
{
    const HizLayout layout = HizLayoutFor(256, 256);
    const std::vector<float> nearEverywhere = UniformAtlas(layout, 0.0f);
    const Mat4 viewProjection = PerspectiveFrom(Vec3(0.0f, 0.0f, 0.0f));
    const Mat4 world = Mat4::Identity();

    // Behind the eye.
    EXPECT_FALSE(ClusterOccluded(MakeCluster(Vec3(0.0f, 0.0f, -10.0f), 0.5f, Vec3(0.0f, 0.0f, 0.0f), 1.0f), world,
        viewProjection, layout, nearEverywhere));
    // Fully off screen: the clamped rect is empty.
    EXPECT_FALSE(ClusterOccluded(MakeCluster(Vec3(100.0f, 0.0f, 10.0f), 0.5f, Vec3(0.0f, 0.0f, 0.0f), 1.0f), world,
        viewProjection, layout, nearEverywhere));
    // A corner behind the eye (the box straddles the eye plane).
    EXPECT_FALSE(ClusterOccluded(MakeCluster(Vec3(0.0f, 0.0f, 0.0f), 2.0f, Vec3(0.0f, 0.0f, 0.0f), 1.0f), world,
        viewProjection, layout, nearEverywhere));
    // An invalid layout.
    EXPECT_FALSE(ClusterOccluded(MakeCluster(Vec3(0.0f, 0.0f, 10.0f), 0.5f, Vec3(0.0f, 0.0f, 0.0f), 1.0f), world,
        viewProjection, HizLayout{}, nearEverywhere));
}

TEST(ClusterCullMath, BoxPartlyOffScreenIsTestedOnItsVisiblePart)
{
    const HizLayout layout = HizLayoutFor(256, 256);
    const Mat4 viewProjection = PerspectiveFrom(Vec3(0.0f, 0.0f, 0.0f));
    // Straddles the right screen edge at view distance 9.5 .. 10.5.
    const MeshCluster edge = MakeCluster(Vec3(5.5f, 0.0f, 10.0f), 0.5f, Vec3(0.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_TRUE(ClusterOccluded(edge, Mat4::Identity(), viewProjection, layout, UniformAtlas(layout, 0.5f)));
    // Where nothing is drawn (clear depth) the box stays.
    EXPECT_FALSE(ClusterOccluded(edge, Mat4::Identity(), viewProjection, layout, UniformAtlas(layout, 1.0f)));
}

TEST(ClusterCullMath, CullParamsCarryTheCullCameraPyramidSizeAndDepthMap)
{
    const Mat4 vp = PerspectiveFrom(Vec3(1.0f, 2.0f, 3.0f));
    ClusterCullParams params =
        MakeClusterCullParams(vp, Vec3(1.0f, 2.0f, 3.0f), false, 1, 1, ClusterCullFlags::Frustum);
    EXPECT_EQ(std::memcmp(params.viewProjection, vp.Data(), sizeof(params.viewProjection)), 0);

    const HizLayout layout = HizLayoutFor(256, 128);
    SetClusterCullHiz(params, layout);
    EXPECT_FLOAT_EQ(params.hiz[0], static_cast<float>(layout.atlasWidth));
    EXPECT_FLOAT_EQ(params.hiz[1], static_cast<float>(layout.atlasHeight));
    EXPECT_FLOAT_EQ(params.hiz[2], 128.0f);
    EXPECT_FLOAT_EQ(params.hiz[3], 64.0f);

    const HizDepthMap map = HizDepthMapOf(vp);
    EXPECT_FLOAT_EQ(params.depthMap[0], map.k);
    EXPECT_FLOAT_EQ(params.depthMap[1], map.b);
    EXPECT_FLOAT_EQ(params.depthMap[2], static_cast<float>(layout.levelCount));
    EXPECT_EQ(params.counts[3] & kClusterCullFlagRawDepth, 0u);

    ClusterCullParams ortho = MakeClusterCullParams(
        OrthographicLookingForward(), Vec3(0.0f, 0.0f, 1.0f), true, 1, 1, ClusterCullFlags::Frustum);
    SetClusterCullHiz(ortho, layout);
    EXPECT_NE(ortho.counts[3] & kClusterCullFlagRawDepth, 0u);
    EXPECT_EQ(ortho.counts[3] & kClusterCullFlagFrustum, kClusterCullFlagFrustum);
}

TEST(ClusterCullMath, ParamBlockLayoutIsFixed)
{
    EXPECT_EQ(sizeof(ClusterCullParams), 224u);
    EXPECT_EQ(offsetof(ClusterCullParams, viewProjection), 112u);
    EXPECT_EQ(offsetof(ClusterCullParams, hiz), 192u);
    EXPECT_EQ(offsetof(ClusterCullParams, depthMap), 208u);
    EXPECT_EQ(kClusterCullFlagPhaseFirst, 32u);
    EXPECT_EQ(kClusterCullFlagPhaseSecond, 64u);
    EXPECT_EQ(kClusterCullFlagHistoryValid, 128u);
    EXPECT_EQ(kClusterCullFlagRawDepth, 256u);
    EXPECT_EQ(static_cast<std::uint32_t>(ClusterCullFlags::PhaseFirst), kClusterCullFlagPhaseFirst);
}

TEST(ClusterCullMath, HizBuildPlanRunsTheTopPassOnlyAboveLevelFour)
{
    const HizBuildPlan large = HizBuildPlanFor(HizLayoutFor(350, 447));
    EXPECT_EQ(large.baseThreadsX, 175u);
    EXPECT_EQ(large.baseThreadsY, 224u);
    EXPECT_EQ(large.topThreadsX, kHizTopThreads);
    EXPECT_TRUE(large.runTop);

    EXPECT_FALSE(HizBuildPlanFor(HizLayoutFor(32, 32)).runTop); // Five levels: the base pass writes them all.
    EXPECT_TRUE(HizBuildPlanFor(HizLayoutFor(64, 64)).runTop);

    const HizBuildPush push = MakeHizBuildPush(350, 447, HizLayoutFor(350, 447));
    EXPECT_EQ(push.sizes[0], 350u);
    EXPECT_EQ(push.sizes[1], 447u);
    EXPECT_EQ(push.sizes[2], 175u);
    EXPECT_EQ(push.sizes[3], 224u);
    EXPECT_EQ(push.levels[0], 9u);
    EXPECT_EQ(kHizPassCount, 2u);
}

// ---- two-phase history ----

TEST(ClusterCullMath, FirstPhaseDrawsLastFramesSurvivorsAndRewritesEveryWord)
{
    // Invalid history reads as zero: nothing is drawn and the word is cleared.
    ClusterPhaseResult r = ClusterPhaseFirst(3, false, true);
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, 0u);

    r = ClusterPhaseFirst(kClusterHistoryVisible, true, true);
    EXPECT_TRUE(r.emit);
    EXPECT_EQ(r.word, kClusterHistoryVisible | kClusterHistoryDrawnFirst);

    r = ClusterPhaseFirst(kClusterHistoryVisible, true, false); // Visible before, out of view now.
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, kClusterHistoryVisible);

    r = ClusterPhaseFirst(0, true, true); // Not visible before: left to the second phase.
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, 0u);

    r = ClusterPhaseFirst(kClusterHistoryDrawnFirst, true, true); // A stale phase bit never survives.
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, 0u);

    r = ClusterPhaseFirst(kClusterHistoryVisible | kClusterHistoryDrawnFirst, true, true);
    EXPECT_TRUE(r.emit);
    EXPECT_EQ(r.word, kClusterHistoryVisible | kClusterHistoryDrawnFirst);
}

TEST(ClusterCullMath, SecondPhaseEmitsWhatTheFirstPhaseDidNotAndWritesEveryWord)
{
    ClusterPhaseResult r = ClusterPhaseSecond(0, true, false);
    EXPECT_TRUE(r.emit);
    EXPECT_EQ(r.word, kClusterHistoryVisible);

    r = ClusterPhaseSecond(kClusterHistoryDrawnFirst, true, false); // Drawn in the first phase: not again.
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, kClusterHistoryVisible);

    r = ClusterPhaseSecond(kClusterHistoryDrawnFirst, true, true); // Drawn early, occluded now: leaves the history.
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, 0u);

    r = ClusterPhaseSecond(0, false, false);
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, 0u);

    r = ClusterPhaseSecond(0, true, true);
    EXPECT_FALSE(r.emit);
    EXPECT_EQ(r.word, 0u);
}

TEST(ClusterCullMath, BothPhasesDrawAVisibleItemExactlyOnceForAnyHistory)
{
    for (const std::uint32_t word : { 0u, 1u, 2u, 3u }) {
        for (const bool valid : { false, true }) {
            for (const bool inView : { false, true }) {
                for (const bool occluded : { false, true }) {
                    const ClusterPhaseResult first = ClusterPhaseFirst(word, valid, inView);
                    const ClusterPhaseResult second = ClusterPhaseSecond(first.word, inView, occluded);
                    const int drawn = (first.emit ? 1 : 0) + (second.emit ? 1 : 0);
                    const bool visible = inView && !occluded;
                    EXPECT_LE(drawn, 1);
                    if (visible) {
                        EXPECT_EQ(drawn, 1) << word << valid << inView << occluded;
                    }
                    if (!inView) {
                        EXPECT_EQ(drawn, 0);
                    }
                    EXPECT_EQ(second.word, visible ? kClusterHistoryVisible : 0u);
                    if (!valid) {
                        EXPECT_FALSE(first.emit);
                    }
                }
            }
        }
    }
}

TEST(ClusterCullMath, HistoryKeyIsOrderAndGenerationSensitive)
{
    const std::array<std::uint32_t, 3> ids{ 7, 8, 9 };
    const std::array<std::uint32_t, 3> swapped{ 8, 7, 9 };
    const std::uint64_t key = ClusterHistoryKey(1, 64, ids, 1);
    EXPECT_EQ(key, ClusterHistoryKey(1, 64, ids, 1));
    EXPECT_NE(key, ClusterHistoryKey(1, 64, swapped, 1));
    EXPECT_NE(key, ClusterHistoryKey(1, 64, ids, 2));
    EXPECT_NE(key, ClusterHistoryKey(2, 64, ids, 1));
    EXPECT_NE(key, ClusterHistoryKey(1, 65, ids, 1));
    EXPECT_NE(key, ClusterHistoryKey(1, 64, std::span<const std::uint32_t>(ids).first(2), 1));
    EXPECT_NE(ClusterHistoryKey(1, 64, {}, 1), ClusterHistoryKey(1, 64, ids, 1));
}

TEST(ClusterCullMath, PoseEqualityIsExact)
{
    const Vec3 eye(1.0f, 2.0f, 3.0f);
    const Mat4 vp = PerspectiveFrom(eye);
    EXPECT_TRUE(CameraPoseEqual(vp, eye, vp, eye));
    EXPECT_FALSE(CameraPoseEqual(vp, eye, PerspectiveFrom(Vec3(1.0f, 2.0f, 3.001f)), Vec3(1.0f, 2.0f, 3.001f)));
    EXPECT_FALSE(CameraPoseEqual(vp, eye, vp, Vec3(1.0f, 2.0f, 3.5f)));
    Mat4 broken = vp;
    broken.columns[0].x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(CameraPoseEqual(broken, eye, broken, eye));
}

LocalBoundsView UnitBounds()
{
    LocalBoundsView bounds;
    bounds.valid = true;
    bounds.min[0] = -1.0f;
    bounds.min[1] = -2.0f;
    bounds.min[2] = -3.0f;
    bounds.max[0] = 1.0f;
    bounds.max[1] = 2.0f;
    bounds.max[2] = 3.0f;
    return bounds;
}

TEST(ClusterSynthesisTests, SingleClusterCoversTheWholeMeshAndValidates)
{
    std::string why;
    const std::optional<MeshCluster> cluster = SynthesizeSingleCluster(UnitBounds(), 36, &why);
    ASSERT_TRUE(cluster.has_value()) << why;
    EXPECT_EQ(cluster->firstIndex, 0u);
    EXPECT_EQ(cluster->indexCount, 36u);
    EXPECT_FLOAT_EQ(cluster->cone[3], 1.0f);
    EXPECT_FLOAT_EQ(cluster->cone[0], 0.0f);
    EXPECT_FLOAT_EQ(cluster->aabbMin[2], -3.0f);
    EXPECT_FLOAT_EQ(cluster->aabbMax[1], 2.0f);
    // The sphere holds every corner of the box.
    EXPECT_GE(cluster->sphere[3], std::sqrt(1.0f + 4.0f + 9.0f));
    EXPECT_TRUE(ValidateClusterTable(std::span<const MeshCluster>(&*cluster, 1), 36, &why)) << why;
}

TEST(ClusterSynthesisTests, RefusesEachBadInput)
{
    std::string why;
    EXPECT_FALSE(SynthesizeSingleCluster(UnitBounds(), 0, &why).has_value());
    EXPECT_FALSE(SynthesizeSingleCluster(UnitBounds(), 4, &why).has_value());
    EXPECT_FALSE(SynthesizeSingleCluster(UnitBounds(), std::size_t{ 0xFFFFFFFFull } + 3, &why).has_value());
    EXPECT_FALSE(SynthesizeSingleCluster(LocalBoundsView{}, 3, &why).has_value());

    LocalBoundsView nan = UnitBounds();
    nan.max[0] = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SynthesizeSingleCluster(nan, 3, &why).has_value());
    LocalBoundsView inverted = UnitBounds();
    inverted.min[1] = 5.0f;
    EXPECT_FALSE(SynthesizeSingleCluster(inverted, 3, &why).has_value());
    EXPECT_FALSE(why.empty());
    EXPECT_TRUE(SynthesizeSingleCluster(UnitBounds(), 3, nullptr).has_value()); // A null reason pointer is fine.
}

TEST(ClusterSynthesisTests, RetryBackoffDoublesAndIsNotDueEarly)
{
    SynthesisBackoff state;
    EXPECT_TRUE(SynthesisDue(state, 0));

    state = NoteSynthesisFailure(state, 1000);
    EXPECT_EQ(state.failures, 1u);
    EXPECT_FALSE(SynthesisDue(state, 1000 + kSynthesisRetryBaseFrames - 1));
    EXPECT_TRUE(SynthesisDue(state, 1000 + kSynthesisRetryBaseFrames));

    state = NoteSynthesisFailure(state, 2000);
    EXPECT_FALSE(SynthesisDue(state, 2000 + 2 * kSynthesisRetryBaseFrames - 1));
    EXPECT_TRUE(SynthesisDue(state, 2000 + 2 * kSynthesisRetryBaseFrames));

    for (int i = 0; i < 40; ++i) {
        state = NoteSynthesisFailure(state, 0);
    }
    EXPECT_EQ(state.retryAtFrame, kSynthesisRetryMaxFrames);
}

} // namespace
} // namespace gte
