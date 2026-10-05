// Tier-1 tests for RenderFeatureCompositor::RegisterProjectFeature()/
// UnregisterProjectFeature() and the bounded GPU-state slot pool
// (editor-core-separation-23 campaign, PHASE2 -
// PHASE2_REGISTER_PROJECT_FEATURE_AND_SLOT_POOL.md, section 3.6).
//
// Reuses the EXACT HeadlessSurfaceProvider + NoopHostServices + real Core
// fixture pattern already proven in ProjectAssemblyRegistrationLedgerTests.cpp
// (PHASE0_MASTER_STRATEGY.md's own Step 2 citation) - GTEST_SKIP()-ing
// identically if this machine's Vulkan driver/loader doesn't support
// VK_EXT_headless_surface. Every test uses its own uniquely-named, nowhere-
// else-used feature name(s) so tests never interfere with each other
// regardless of run order - RenderFeatureCompositor is a per-Core instance
// (not a process-wide singleton like ProjectAssemblyRegistrationLedger), but
// since every test constructs its OWN fresh Core, this is purely a defensive
// convention here, not a strict requirement.

#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Plugins/RenderFeatureDebugEntry.h"
#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "../../Fakes/HeadlessSurfaceProvider.h"

#include "Core/Plugins/RenderFeatureDescriptor.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace gte {
namespace {

// A trivial, no-op IHostServices - mirrors
// ProjectAssemblyRegistrationLedgerTests.cpp's own identical NoopHostServices
// exactly (Core's constructor requires a real IHostServices&, but these
// tests never need to observe anything logged through it).
class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel /*level*/, std::string_view /*message*/) override { }
};

// Constructs a real, headless gte::Core, or returns nullptr (the caller must
// GTEST_SKIP()) if this machine's Vulkan driver/loader doesn't support
// VK_EXT_headless_surface. `surfaceProvider`/`hostServices` must outlive the
// returned Core.
std::unique_ptr<Core> TryMakeHeadlessCore(HeadlessSurfaceProvider& surfaceProvider, NoopHostServices& hostServices)
{
    try {
        return std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception&) {
        return nullptr;
    }
}

// Finds `name` inside a DebugSnapshot() result, or nullptr if absent.
const RenderFeatureDebugEntry* FindByName(const std::vector<RenderFeatureDebugEntry>& snapshot, const std::string& name)
{
    for (const RenderFeatureDebugEntry& entry : snapshot) {
        if (entry.name == name) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

#define GTE_SKIP_IF_NO_HEADLESS_CORE(core)                                                                           \
    HeadlessSurfaceProvider surfaceProvider;                                                                         \
    NoopHostServices hostServices;                                                                                   \
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surfaceProvider, hostServices);                                 \
    if (core == nullptr) {                                                                                           \
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "                \
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "                    \
                        "HeadlessSurfaceProvider.h's own top-of-file comment).";                                     \
    }

// 1. RegisterProjectFeature() succeeds for a fresh, unique name; the claimed
// slot is observable indirectly (isProjectFeature == true in DebugSnapshot()).
TEST(RenderFeatureCompositorProjectFeatureTest, RegisterProjectFeatureSucceedsForAFreshUniqueName)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor descriptor = MakeRenderFeatureDescriptor(
        "PF_Test_FreshUniqueName", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    const bool registered = compositor->RegisterProjectFeature(
        descriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { });
    EXPECT_TRUE(registered);

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* found = FindByName(snapshot, "PF_Test_FreshUniqueName");
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->isProjectFeature);
    // better-render-pass-2 campaign, PHASE4 - isV3 assertion removed, the
    // field itself is deleted (RenderFeatureDebugEntry.h).
}

// 2. Registering a duplicate name fails and does not consume an extra slot -
// confirmed by then registering kMaxConcurrentProjectRenderFeatures (16)
// OTHER distinct names and observing all of them still succeed.
TEST(RenderFeatureCompositorProjectFeatureTest, RegisteringADuplicateNameFailsWithoutConsumingAnExtraSlot)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor descriptor = MakeRenderFeatureDescriptor(
        "PF_Test_DuplicateOriginal", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    ASSERT_TRUE(compositor->RegisterProjectFeature(
        descriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));

    const bool duplicateRegistered = compositor->RegisterProjectFeature(
        descriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { });
    EXPECT_FALSE(duplicateRegistered);

    // The duplicate attempt must not have consumed a slot - 16 OTHER,
    // distinct names must all still fit (1 already claimed above + 16 more
    // == exactly kMaxConcurrentProjectRenderFeatures if the duplicate leaked
    // a slot it would only fit 15).
    for (int i = 0; i < 15; ++i) {
        const std::string name = "PF_Test_DuplicateFiller_" + std::to_string(i);
        const GtePluginRenderFeatureDescriptor fillerDescriptor =
            MakeRenderFeatureDescriptor(name.c_str(), RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
        EXPECT_TRUE(compositor->RegisterProjectFeature(
        fillerDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }))
            << "filler #" << i;
    }
}

// 3. UnregisterProjectFeature() on an existing name succeeds; the freed slot
// is reused by the NEXT RegisterProjectFeature() call with a DIFFERENT name -
// proven by exhausting every slot, unregistering exactly ONE, then
// registering one more distinct name and confirming it succeeds where it
// would otherwise have failed.
TEST(RenderFeatureCompositorProjectFeatureTest, UnregisterFreesItsSlotForReuseByADifferentName)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    // Exhaust all 16 slots.
    for (int i = 0; i < 16; ++i) {
        const std::string name = "PF_Test_ReuseExhaust_" + std::to_string(i);
        const GtePluginRenderFeatureDescriptor descriptor =
            MakeRenderFeatureDescriptor(name.c_str(), RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
        ASSERT_TRUE(compositor->RegisterProjectFeature(
        descriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }))
            << "exhaust #" << i;
    }

    // Every slot is now claimed - one more distinct name must fail.
    const GtePluginRenderFeatureDescriptor overflowDescriptor = MakeRenderFeatureDescriptor(
        "PF_Test_ReuseOverflowBeforeFree", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    EXPECT_FALSE(compositor->RegisterProjectFeature(
        overflowDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));

    // Unregister exactly ONE of the 16.
    EXPECT_TRUE(compositor->UnregisterProjectFeature("PF_Test_ReuseExhaust_0"));

    // NOW a new, different name must succeed - proving the freed slot is
    // genuinely reusable.
    const GtePluginRenderFeatureDescriptor reuseDescriptor = MakeRenderFeatureDescriptor(
        "PF_Test_ReuseAfterFree", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    EXPECT_TRUE(compositor->RegisterProjectFeature(
        reuseDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));
}

// 4. Registering kMaxConcurrentProjectRenderFeatures + 1 distinctly-named
// features with NONE ever unregistered: the first 16 succeed, the 17th
// fails, and all previous ones remain registered.
TEST(RenderFeatureCompositorProjectFeatureTest, TheSeventeenthRegistrationFailsAndAllPriorSixteenRemainRegistered)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    for (int i = 0; i < 16; ++i) {
        const std::string name = "PF_Test_SeventeenthCheck_" + std::to_string(i);
        const GtePluginRenderFeatureDescriptor descriptor =
            MakeRenderFeatureDescriptor(name.c_str(), RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
        ASSERT_TRUE(compositor->RegisterProjectFeature(
        descriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }))
            << "registration #" << i;
    }

    const GtePluginRenderFeatureDescriptor seventeenthDescriptor = MakeRenderFeatureDescriptor(
        "PF_Test_SeventeenthCheck_Seventeenth", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    EXPECT_FALSE(compositor->RegisterProjectFeature(
        seventeenthDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    for (int i = 0; i < 16; ++i) {
        const std::string name = "PF_Test_SeventeenthCheck_" + std::to_string(i);
        EXPECT_NE(FindByName(snapshot, name), nullptr) << "registration #" << i << " should still be registered";
    }
    EXPECT_EQ(FindByName(snapshot, "PF_Test_SeventeenthCheck_Seventeenth"), nullptr);
}

// 5. UnregisterProjectFeature() on an unknown name fails harmlessly (no
// crash).
TEST(RenderFeatureCompositorProjectFeatureTest, UnregisterOnAnUnknownNameFailsHarmlessly)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    EXPECT_FALSE(compositor->UnregisterProjectFeature("PF_Test_ThisNameWasNeverRegistered"));
}

// 7. An unwired stage (PreOpaque) passed to RegisterProjectFeature() is
// refused, releasing its claimed slot back - confirmed by then registering
// kMaxConcurrentProjectRenderFeatures (16) OTHER distinct names and
// observing all of them succeed (proving the refused attempt did not leak a
// slot).
TEST(RenderFeatureCompositorProjectFeatureTest, AnUnwiredStageIsRefusedWithoutLeakingItsSlot)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor unwiredDescriptor = MakeRenderFeatureDescriptor(
        "PF_Test_UnwiredStage", RenderFeatureStage::PreOpaque, 0, RenderFeatureBlendMode::Replace);
    EXPECT_FALSE(compositor->RegisterProjectFeature(
        unwiredDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));

    // The refused entry must never appear in DebugSnapshot() either (never
    // pushed into m_postComposite/m_preUi).
    const std::vector<RenderFeatureDebugEntry> snapshotAfterRefusal = compositor->DebugSnapshot();
    EXPECT_EQ(FindByName(snapshotAfterRefusal, "PF_Test_UnwiredStage"), nullptr);

    for (int i = 0; i < 16; ++i) {
        const std::string name = "PF_Test_UnwiredStageFiller_" + std::to_string(i);
        const GtePluginRenderFeatureDescriptor fillerDescriptor =
            MakeRenderFeatureDescriptor(name.c_str(), RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
        EXPECT_TRUE(compositor->RegisterProjectFeature(
        fillerDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }))
            << "filler #" << i;
    }
}

// 9. A registered project feature's DebugSnapshot() entry has
// isProjectFeature == true - the single most important assertion for that
// field's own correctness (see 3.5 above). isV3 itself was deleted outright
// by the better-render-pass-2 campaign, PHASE4.
TEST(RenderFeatureCompositorProjectFeatureTest, DebugSnapshotEntryIsMarkedAsProjectFeatureNotV3)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor descriptor = MakeRenderFeatureDescriptor(
        "PF_Test_DebugSnapshotMarking", RenderFeatureStage::PreUI, 5, RenderFeatureBlendMode::AlphaOver);
    ASSERT_TRUE(compositor->RegisterProjectFeature(
        descriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }));

    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* found = FindByName(snapshot, "PF_Test_DebugSnapshotMarking");
    ASSERT_NE(found, nullptr);
    EXPECT_TRUE(found->isProjectFeature);
    EXPECT_EQ(found->stage, "PreUI");
    EXPECT_EQ(found->blendMode, "AlphaOver");
}

// 6. UnregisterProjectFeature() called with the name of an EXISTING
// moduleV2/moduleV3 entry must fail and must not disturb the slot free list.
// This exact scenario needs a REAL loaded plugin, which this fixture cannot
// easily provide (per the phase file's own item 6, this is an accepted,
// documented gap for this phase) - instead, this test confirms the CODE-LEVEL
// guarantee the real implementation relies on: FindEntryByName() returning a
// non-null Entry whose projectCallback is unset is refused BEFORE any slot
// mutation happens. Since RegisterProjectFeature() is the only way this test
// binary can construct an Entry at all, this test instead proves the
// symmetric, always-reachable half of the same guarantee: calling
// UnregisterProjectFeature() on a name this compositor has never seen at all
// (covered by test 5 above) and on a name that IS a real project feature
// (covered by test 3 above) are both already exercised: this test adds the
// one remaining angle - confirming a refusal (test 5's unknown-name case)
// never mutates the free list, by registering exactly
// kMaxConcurrentProjectRenderFeatures features after an unknown-name refusal
// and observing all of them still succeed.
TEST(RenderFeatureCompositorProjectFeatureTest, AFailedUnregisterAttemptNeverLeaksOrConsumesASlot)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    EXPECT_FALSE(compositor->UnregisterProjectFeature("PF_Test_NeverRegisteredForUnregisterAttempt"));

    for (int i = 0; i < 16; ++i) {
        const std::string name = "PF_Test_UnregisterNoLeakFiller_" + std::to_string(i);
        const GtePluginRenderFeatureDescriptor fillerDescriptor =
            MakeRenderFeatureDescriptor(name.c_str(), RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
        EXPECT_TRUE(compositor->RegisterProjectFeature(
        fillerDescriptor, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, rg::TextureHandle, VkExtent2D, const ScenePassReadHandles&, const RenderFeatureCameraData&) { }))
            << "filler #" << i;
    }
}

} // namespace gte
