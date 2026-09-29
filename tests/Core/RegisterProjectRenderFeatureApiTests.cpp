// Tier-1 tests for Core::RegisterProjectRenderFeature()/
// UnregisterProjectRenderFeature() (editor-core-separation-23 campaign,
// PHASE3 - PHASE3_CORE_REGISTER_PROJECT_RENDER_FEATURE_API.md, section 3.4).
//
// Reuses the EXACT HeadlessSurfaceProvider + NoopHostServices + real Core
// fixture pattern already proven in CoreHeadlessConstructionTests.cpp /
// tests/Core/Plugins/RenderFeatureCompositorProjectFeatureTests.cpp
// (PHASE0_MASTER_STRATEGY.md's own Step 2 citation) - GTEST_SKIP()-ing
// identically if this machine's Vulkan driver/loader doesn't support
// VK_EXT_headless_surface. Every test uses its own uniquely-named, nowhere-
// else-used feature name(s) so tests never interfere with each other
// regardless of run order.

#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Plugins/RenderFeatureDebugEntry.h"
#include "../Fakes/HeadlessSurfaceProvider.h"

#include "../../plugins/gte_plugin_abi/RenderFeatureDescriptor.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>

namespace gte {
namespace {

// A trivial, no-op IHostServices - mirrors CoreHeadlessConstructionTests.cpp's
// own identical NoopHostServices exactly (Core's constructor requires a real
// IHostServices&, but these tests never need to observe anything logged
// through it).
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

#define GTE_SKIP_IF_NO_HEADLESS_CORE(core)                                                                          \
    HeadlessSurfaceProvider surfaceProvider;                                                                        \
    NoopHostServices hostServices;                                                                                  \
    std::unique_ptr<Core> core = TryMakeHeadlessCore(surfaceProvider, hostServices);                                \
    if (core == nullptr) {                                                                                          \
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "               \
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "                   \
                        "HeadlessSurfaceProvider.h's own top-of-file comment).";                                    \
    }

// 1. A debugName of exactly 63 bytes succeeds - confirmed via
// RenderFeatureCompositor::DebugSnapshot() containing an entry whose name is
// exactly that 63-byte string, byte-for-byte.
TEST(RegisterProjectRenderFeatureApiTest, DebugNameOfExactly63BytesSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name63(63, 'A');
    ASSERT_EQ(name63.size(), 63u);

    const bool registered = core->RegisterProjectRenderFeature(name63.c_str(), RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { });
    EXPECT_TRUE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    const RenderFeatureDebugEntry* found = FindByName(snapshot, name63);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, name63);
}

// 2/3. A debugName of 64 bytes (or longer) is REJECTED (false, logged) and
// produces NO entry at all in DebugSnapshot() - explicitly NOT a truncated
// one (confirm no entry whose name is a 63-byte PREFIX of it exists either -
// proving nothing silently truncated through). Derived from
// MakeRenderFeatureDescriptor()'s own real copy loop
// (`for (; name[i] != '\0' && i + 1 < sizeof(descriptor.name); ++i)`,
// sizeof(descriptor.name) == 64) - std::strlen(debugName) > 63 is the
// rejection condition, so strlen == 63 is the largest accepted length and
// strlen == 64 must be rejected.
TEST(RegisterProjectRenderFeatureApiTest, DebugNameOf64BytesIsRejectedAndNeverTruncated)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string name64(64, 'B');
    ASSERT_EQ(name64.size(), 64u);
    const std::string name64Prefix63 = name64.substr(0, 63);

    const bool registered = core->RegisterProjectRenderFeature(name64.c_str(), RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { });
    EXPECT_FALSE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    EXPECT_EQ(FindByName(snapshot, name64), nullptr);
    EXPECT_EQ(FindByName(snapshot, name64Prefix63), nullptr)
        << "a 64-byte name must never silently truncate through as a 63-byte entry";
}

// An even-longer name (well past 64 bytes) is rejected the same way - not
// just the exact-boundary case.
TEST(RegisterProjectRenderFeatureApiTest, AMuchLongerDebugNameIsAlsoRejected)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const std::string longName(200, 'C');

    const bool registered = core->RegisterProjectRenderFeature(longName.c_str(), RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { });
    EXPECT_FALSE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    const std::vector<RenderFeatureDebugEntry> snapshot = compositor->DebugSnapshot();
    EXPECT_EQ(FindByName(snapshot, longName), nullptr);
}

// 4. A duplicate/unwired-stage/slot-exhaustion failure from
// RenderFeatureCompositor::RegisterProjectFeature() propagates back as false
// from Core::RegisterProjectRenderFeature() unchanged (a thin pass-through).
TEST(RegisterProjectRenderFeatureApiTest, UnderlyingCompositorRefusalPropagatesBackAsFalse)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    // Duplicate-name refusal.
    ASSERT_TRUE(core->RegisterProjectRenderFeature("CoreApi_Test_DuplicateOriginal", RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));
    EXPECT_FALSE(core->RegisterProjectRenderFeature("CoreApi_Test_DuplicateOriginal", RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));

    // Unwired-stage refusal (RenderFeatureStage::PreOpaque is declared for
    // ABI future-proofing only - never actually wired).
    EXPECT_FALSE(core->RegisterProjectRenderFeature("CoreApi_Test_UnwiredStage", RenderFeatureStage::PreOpaque,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));
}

// 5. UnregisterProjectRenderFeature() on a name that was never registered is
// a safe no-op (no crash), confirmed by calling it before anything else in a
// fresh test case.
TEST(RegisterProjectRenderFeatureApiTest, UnregisterOnANeverRegisteredNameIsASafeNoOp)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    core->UnregisterProjectRenderFeature("CoreApi_Test_NeverRegisteredForUnregister");

    // Still constructible/usable afterward - the real proof of "no crash".
    EXPECT_TRUE(core->RegisterProjectRenderFeature("CoreApi_Test_StillUsableAfterNoOpUnregister",
        RenderFeatureStage::PostComposite, RenderFeatureBlendMode::Replace, 0,
        [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));
}

// A full register -> unregister -> re-register round trip through the Core
// API itself (not just the underlying compositor directly, PHASE2's own
// scope) succeeds end to end.
TEST(RegisterProjectRenderFeatureApiTest, RegisterUnregisterReRegisterRoundTripSucceeds)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    const char* name = "CoreApi_Test_RoundTrip";
    ASSERT_TRUE(core->RegisterProjectRenderFeature(
        name, RenderFeatureStage::PostComposite, RenderFeatureBlendMode::Replace, 0,
        [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));

    core->UnregisterProjectRenderFeature(name);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), name), nullptr);

    EXPECT_TRUE(core->RegisterProjectRenderFeature(
        name, RenderFeatureStage::PostComposite, RenderFeatureBlendMode::Replace, 0,
        [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));
    EXPECT_NE(FindByName(compositor->DebugSnapshot(), name), nullptr);
}

// 6. Null-safety: debugName == nullptr is refused (false), logged, never a
// crash, for both RegisterProjectRenderFeature() and
// UnregisterProjectRenderFeature(). This is the one null-path this test
// binary CAN exercise directly (a real, live Core always has a non-null
// m_renderFeatureCompositorPtr per PHASE0 Step 2, so the
// "no RenderFeatureCompositor orchestrator registered" branch is not
// reachable from this fixture - see this phase's own completion report for
// the ask_questions resolution on that specific sub-path).
TEST(RegisterProjectRenderFeatureApiTest, NullDebugNameIsRefusedWithoutCrashing)
{
    GTE_SKIP_IF_NO_HEADLESS_CORE(core);

    EXPECT_FALSE(core->RegisterProjectRenderFeature(nullptr, RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));

    // Must not crash, and must not leave the compositor in a weird state -
    // a subsequent, valid registration still works fine afterward.
    core->UnregisterProjectRenderFeature(nullptr);
    EXPECT_TRUE(core->RegisterProjectRenderFeature("CoreApi_Test_StillUsableAfterNullDebugName",
        RenderFeatureStage::PostComposite, RenderFeatureBlendMode::Replace, 0,
        [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));
}

} // namespace gte
