// Tier-1 tests for Core::AddPostOpaquePass()/RemovePostOpaquePass().
//
// Mirrors AddPreOpaquePassApiTests.cpp's own exact HeadlessSurfaceProvider +
// NoopHostServices + real Core fixture pattern and 63-vs-64-byte boundary
// coverage, plus new cross-stage collision coverage against all four other
// stages sharing the same name namespace (PostComposite/PreUI/PreOpaque/
// PostTransparent).

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

TEST(AddPostOpaquePassApiTest, DebugNameOfExactly63BytesSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name63(63, 'A');
    ASSERT_EQ(name63.size(), 63u);

    const bool registered = core->AddPostOpaquePass(name63.c_str(),
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
    EXPECT_TRUE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), name63), nullptr);
}

TEST(AddPostOpaquePassApiTest, DebugNameOf64BytesIsRejectedAndNeverTruncated)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name64(64, 'B');
    const std::string name64Prefix63 = name64.substr(0, 63);

    const bool registered = core->AddPostOpaquePass(name64.c_str(),
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { });
    EXPECT_FALSE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    EXPECT_EQ(FindByName(snapshot, name64), nullptr);
    EXPECT_EQ(FindByName(snapshot, name64Prefix63), nullptr);
}

TEST(AddPostOpaquePassApiTest, NullDebugNameIsRefusedWithoutCrashing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    EXPECT_FALSE(core->AddPostOpaquePass(
        nullptr, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    core->RemovePostOpaquePass(nullptr);
    EXPECT_TRUE(core->AddPostOpaquePass("CoreApi_PostOpaque_StillUsableAfterNullDebugName",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostOpaquePassApiTest, DuplicateRefusalPropagatesBackAsFalse)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPostOpaquePass("CoreApi_PostOpaque_DuplicateOriginal",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_FALSE(core->AddPostOpaquePass("CoreApi_PostOpaque_DuplicateOriginal",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostOpaquePassApiTest, UnregisterOnANeverRegisteredNameIsASafeNoOp)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    core->RemovePostOpaquePass("CoreApi_PostOpaque_NeverRegistered");

    EXPECT_TRUE(core->AddPostOpaquePass("CoreApi_PostOpaque_StillUsableAfterNoOpRemove",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostOpaquePassApiTest, RegisterRemoveReRegisterRoundTripSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const char* name = "CoreApi_PostOpaque_RoundTrip";
    ASSERT_TRUE(core->AddPostOpaquePass(
        name, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    core->RemovePostOpaquePass(name);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), name), nullptr);

    EXPECT_TRUE(core->AddPostOpaquePass(
        name, [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), name), nullptr);
}

TEST(AddPostOpaquePassApiTest, ExplicitPriorityIsHonored)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPostOpaquePass(
        "CoreApi_PostOpaque_ExplicitPriority",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { },
        /*priority=*/99));

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const RenderFeatureDebugEntry* found = FindByName(compositor->DebugSnapshot(), "CoreApi_PostOpaque_ExplicitPriority");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->priority, 99);
}

// --- Cross-stage name-collision coverage ------------------------------------
// PostOpaque/PostTransparent/PreOpaque/PostComposite/PreUI all share ONE
// global name namespace - registering under PostOpaque after any of the
// other four already claimed the same name must be refused.

TEST(AddPostOpaquePassApiTest, NameAlreadyUsedByAPostCompositeEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor descriptor = MakeRenderFeatureDescriptor(
        "CoreApi_PostOpaque_CollidesWithPostComposite", RenderFeatureStage::PostComposite, 0, RenderFeatureBlendMode::Replace);
    ASSERT_TRUE(
        compositor->RegisterProjectFeature(descriptor, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));

    EXPECT_FALSE(core->AddPostOpaquePass("CoreApi_PostOpaque_CollidesWithPostComposite",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostOpaquePassApiTest, NameAlreadyUsedByAPreUiEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);

    const GtePluginRenderFeatureDescriptor descriptor = MakeRenderFeatureDescriptor(
        "CoreApi_PostOpaque_CollidesWithPreUi", RenderFeatureStage::PreUI, 0, RenderFeatureBlendMode::Replace);
    ASSERT_TRUE(
        compositor->RegisterProjectFeature(descriptor, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));

    EXPECT_FALSE(core->AddPostOpaquePass("CoreApi_PostOpaque_CollidesWithPreUi",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostOpaquePassApiTest, NameAlreadyUsedByAPreOpaqueEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPreOpaquePass("CoreApi_PostOpaque_CollidesWithPreOpaque",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { }));

    EXPECT_FALSE(core->AddPostOpaquePass("CoreApi_PostOpaque_CollidesWithPreOpaque",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

TEST(AddPostOpaquePassApiTest, NameAlreadyUsedByAPostTransparentEntryIsRefused)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    ASSERT_TRUE(core->AddPostTransparentPass("CoreApi_PostOpaque_CollidesWithPostTransparent",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));

    EXPECT_FALSE(core->AddPostOpaquePass("CoreApi_PostOpaque_CollidesWithPostTransparent",
        [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId, const ScenePassReadHandles&) { }));
}

} // namespace gte
