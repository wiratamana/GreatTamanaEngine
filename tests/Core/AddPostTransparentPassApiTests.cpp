// Tier-1 tests for Core::AddPostTransparentPass()/RemovePostTransparentPass().
//
// Mirrors AddPostOpaquePassApiTests.cpp's own exact shape, substituting the
// AfterTransparents stage's own entry points.

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

TEST(AddPostTransparentPassApiTest, DebugNameOfExactly63BytesSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name63(63, 'A');
    ASSERT_EQ(name63.size(), 63u);

    const bool registered = core->AddPostTransparentPass(name63.c_str(),
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
    EXPECT_TRUE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), name63), nullptr);
}

TEST(AddPostTransparentPassApiTest, DebugNameOf64BytesIsRejectedAndNeverTruncated)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name64(64, 'B');
    const std::string name64Prefix63 = name64.substr(0, 63);

    const bool registered = core->AddPostTransparentPass(name64.c_str(),
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
    EXPECT_FALSE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    EXPECT_EQ(FindByName(snapshot, name64), nullptr);
    EXPECT_EQ(FindByName(snapshot, name64Prefix63), nullptr);
}

TEST(AddPostTransparentPassApiTest, NullDebugNameIsRefusedWithoutCrashing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    EXPECT_FALSE(core->AddPostTransparentPass(
        nullptr, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    core->RemovePostTransparentPass(nullptr);
    EXPECT_TRUE(core->AddPostTransparentPass("CoreApi_PostTransparent_StillUsableAfterNullDebugName",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostTransparentPassApiTest, DuplicateRefusalPropagatesBackAsFalse)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPostTransparentPass("CoreApi_PostTransparent_DuplicateOriginal",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_FALSE(core->AddPostTransparentPass("CoreApi_PostTransparent_DuplicateOriginal",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostTransparentPassApiTest, UnregisterOnANeverRegisteredNameIsASafeNoOp)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    core->RemovePostTransparentPass("CoreApi_PostTransparent_NeverRegistered");

    EXPECT_TRUE(core->AddPostTransparentPass("CoreApi_PostTransparent_StillUsableAfterNoOpRemove",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostTransparentPassApiTest, RegisterRemoveReRegisterRoundTripSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const char* name = "CoreApi_PostTransparent_RoundTrip";
    ASSERT_TRUE(core->AddPostTransparentPass(
        name, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    core->RemovePostTransparentPass(name);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), name), nullptr);

    EXPECT_TRUE(core->AddPostTransparentPass(
        name, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), name), nullptr);
}

TEST(AddPostTransparentPassApiTest, ExplicitPriorityIsHonored)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPostTransparentPass(
        "CoreApi_PostTransparent_ExplicitPriority",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { },
        /*priority=*/99));

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const RenderFeatureDebugEntry* found =
        FindByName(compositor->DebugSnapshot(), "CoreApi_PostTransparent_ExplicitPriority");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->priority, 99);
}

// --- Cross-stage name-collision coverage ------------------------------------
// PostTransparent/PostOpaque/PreOpaque/PostComposite/PreUI all share ONE
// global name namespace - registering under PostTransparent after any of the
// other four already claimed the same name must be refused.

TEST(AddPostTransparentPassApiTest, NameAlreadyUsedByAPostCompositeEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor descriptor = MakeRenderFeatureDescriptor(
        "CoreApi_PostTransparent_CollidesWithPostComposite", RenderFeatureStage::PostComposite, 0,
        RenderFeatureBlendMode::Replace);
    ASSERT_TRUE(
        compositor->RegisterProjectFeature(descriptor, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));

    EXPECT_FALSE(core->AddPostTransparentPass("CoreApi_PostTransparent_CollidesWithPostComposite",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostTransparentPassApiTest, NameAlreadyUsedByAPreUiEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor descriptor = MakeRenderFeatureDescriptor(
        "CoreApi_PostTransparent_CollidesWithPreUi", RenderFeatureStage::PreUI, 0, RenderFeatureBlendMode::Replace);
    ASSERT_TRUE(
        compositor->RegisterProjectFeature(descriptor, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));

    EXPECT_FALSE(core->AddPostTransparentPass("CoreApi_PostTransparent_CollidesWithPreUi",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostTransparentPassApiTest, NameAlreadyUsedByAPreOpaqueEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPreOpaquePass("CoreApi_PostTransparent_CollidesWithPreOpaque",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));

    EXPECT_FALSE(core->AddPostTransparentPass("CoreApi_PostTransparent_CollidesWithPreOpaque",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostTransparentPassApiTest, NameAlreadyUsedByAPostOpaqueEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPostOpaquePass("CoreApi_PostTransparent_CollidesWithPostOpaque",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    EXPECT_FALSE(core->AddPostTransparentPass("CoreApi_PostTransparent_CollidesWithPostOpaque",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

} // namespace gte
