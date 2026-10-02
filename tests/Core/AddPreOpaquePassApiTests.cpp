// Tier-1 tests for Core::AddPreOpaquePass()/RemovePreOpaquePass()
// (better-render-pass-5 effort, BLOCK 3, PHASE3/PHASE6 -
// task_manager/better-render-pass-5/PHASE6_TIER1_TEST_COVERAGE.md).
//
// Mirrors RegisterProjectRenderFeatureApiTests.cpp's own exact
// HeadlessSurfaceProvider + NoopHostServices + real Core fixture
// pattern and 63-vs-64-byte boundary coverage.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Plugins/RenderFeatureDebugEntry.h"
#include "../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace gte {
namespace {

class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel /*level*/, std::string_view /*message*/) override { }
};

std::unique_ptr<Core> TryMakeHeadlessCore(HeadlessSurfaceProvider& surfaceProvider, NoopHostServices& hostServices)
{
    try {
        return std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception&) {
        return nullptr;
    }
}

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

TEST(AddPreOpaquePassApiTest, DebugNameOfExactly63BytesSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name63(63, 'A');
    ASSERT_EQ(name63.size(), 63u);

    const bool registered = core->AddPreOpaquePass(
        name63.c_str(), [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { });
    EXPECT_TRUE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), name63), nullptr);
}

TEST(AddPreOpaquePassApiTest, DebugNameOf64BytesIsRejectedAndNeverTruncated)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name64(64, 'B');
    const std::string name64Prefix63 = name64.substr(0, 63);

    const bool registered = core->AddPreOpaquePass(
        name64.c_str(), [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { });
    EXPECT_FALSE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    EXPECT_EQ(FindByName(snapshot, name64), nullptr);
    EXPECT_EQ(FindByName(snapshot, name64Prefix63), nullptr);
}

TEST(AddPreOpaquePassApiTest, NullDebugNameIsRefusedWithoutCrashing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    EXPECT_FALSE(core->AddPreOpaquePass(
        nullptr, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));

    core->RemovePreOpaquePass(nullptr);
    EXPECT_TRUE(core->AddPreOpaquePass("CoreApi_PreOpaque_StillUsableAfterNullDebugName",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
}

TEST(AddPreOpaquePassApiTest, DuplicateRefusalPropagatesBackAsFalse)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPreOpaquePass("CoreApi_PreOpaque_DuplicateOriginal",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
    EXPECT_FALSE(core->AddPreOpaquePass("CoreApi_PreOpaque_DuplicateOriginal",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
}

TEST(AddPreOpaquePassApiTest, UnregisterOnANeverRegisteredNameIsASafeNoOp)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    core->RemovePreOpaquePass("CoreApi_PreOpaque_NeverRegistered");

    EXPECT_TRUE(core->AddPreOpaquePass("CoreApi_PreOpaque_StillUsableAfterNoOpRemove",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
}

TEST(AddPreOpaquePassApiTest, RegisterRemoveReRegisterRoundTripSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const char* name = "CoreApi_PreOpaque_RoundTrip";
    ASSERT_TRUE(
        core->AddPreOpaquePass(name, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));

    core->RemovePreOpaquePass(name);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), name), nullptr);

    EXPECT_TRUE(
        core->AddPreOpaquePass(name, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), name), nullptr);
}

// An explicit, non-default priority is honored exactly.
TEST(AddPreOpaquePassApiTest, ExplicitPriorityIsHonored)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPreOpaquePass(
        "CoreApi_PreOpaque_ExplicitPriority", [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { },
        /*priority=*/99));

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const RenderFeatureDebugEntry* found =
        FindByName(compositor->DebugSnapshot(), "CoreApi_PreOpaque_ExplicitPriority");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->priority, 99);
}

} // namespace gte
