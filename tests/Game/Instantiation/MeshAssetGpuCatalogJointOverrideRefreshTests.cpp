// task_manager/verlet-integration-11, PHASE3 - see
// PHASE3_SAME_SESSION_CACHE_FRESHNESS_INFRASTRUCTURE.md, Step 3.5. Only the
// "never cached this session" branch is genuinely Tier-1-testable without a
// live Renderer/GPU device (MeshAssetGpuCatalog is documented as "Tier 2, no
// automated coverage yet" for its real GPU-touching job - see its own class
// comment) - the "already cached, refresh actually replaces the field"
// branch is verified manually instead (see that phase document's own
// "Manual Editor verification checklist").

#include "Game/Instantiation/MeshAssetGpuCatalog.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(MeshAssetGpuCatalogJointOverrideRefreshTest, RefreshingAPathNeverCachedThisSessionIsANoOpThatReturnsFalse)
{
    MeshAssetGpuCatalog catalog;
    // No EnsureMeshAsset()/Resolve() call has ever been made against this
    // path on this catalog instance - m_skinnedMeshCache is genuinely empty
    // for it, so this needs no Renderer/GPU/real *.gta file at all.
    EXPECT_FALSE(catalog.RefreshCachedJointPhysicsOverridesFromDisk("C:/does/not/matter/NeverSpawned.gta"));
}

} // namespace
} // namespace gte
