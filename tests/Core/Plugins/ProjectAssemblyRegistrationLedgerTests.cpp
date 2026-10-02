// Tier-1 tests for src/Core/Plugins/ProjectAssemblyRegistrationLedger.h/.cpp
// (editor-core-separation-13 campaign, Project Assembly Hot Reload plan,
// BIG-STEP 2, PHASE3 -
// PHASE3_REGISTRATION_LEDGER_CLASS_AND_ENTRY_POINT_WIRING.md, section 3.6).
//
// ProjectAssemblyRegistrationLedger::Instance() is a genuine process-wide
// singleton (mirrors ComponentTypeRegistry::Instance()/
// EditorPanelRegistry::Instance()'s own precedent), so every test below uses
// its own uniquely-named, nowhere-else-used project name(s)/panel
// name(s)/component typeName(s) so tests never interfere with each other
// regardless of run order, mirroring EditorPanelRegistryTests.cpp's own
// established convention exactly.
//
// Tests 5/6 need a real gte::Core& to pass into UnregisterEverythingFor() -
// reuses CoreHeadlessConstructionTests.cpp's own exact fixture precedent
// (HeadlessSurfaceProvider + a trivial NoopHostServices), GTEST_SKIP()-ing
// identically if this machine's Vulkan driver/loader doesn't support
// VK_EXT_headless_surface (editor-core-separation-1 campaign, PHASE18).
//
// editor-core-separation-23 campaign, PHASE4
// (PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md) - extended with
// RecordRenderFeature()/renderFeatureNames coverage, plus a real
// Core::RegisterProjectRenderFeature() end-to-end round trip mirroring
// FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring's
// own precedent, and an explicit render-feature-before-render-pass-provider
// teardown ordering test.

#include "Core/Plugins/ProjectAssemblyRegistrationLedger.h"
#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "ECS/Reflection/ComponentTypeRegistry.h"
#include "ECS/Reflection/ReflectFieldMacros.h"
#include "Core/Plugins/RenderFeatureCompositor.h"
#include "Core/Plugins/RenderFeatureDebugEntry.h"
#include "../../Fakes/HeadlessSurfaceProvider.h"

#include <gtest/gtest.h>

#include <memory>
#include <string_view>

namespace gte {
namespace {

// A trivial, no-op IHostServices - mirrors CoreHeadlessConstructionTests.cpp's
// own identical NoopHostServices exactly (Core's constructor requires a real
// IHostServices&, but these tests never need to observe anything logged
// through it).
class NoopHostServices : public IHostServices {
public:
    void Log(LogLevel /*level*/, std::string_view /*message*/) override {}
};

// A small, test-local POD component type - NOT a real ECS component -
// mirrors ComponentTypeRegistryTests.cpp's own DummyReflectedComponent
// precedent, given its own distinct name so it can never collide with
// anything any other test file in this same binary registers.
struct LedgerRoundTripDummyComponent {
    float value = 0.0f;
};

// Finds `name` inside a RenderFeatureCompositor::DebugSnapshot() result, or
// nullptr if absent - mirrors RenderFeatureCompositorProjectFeatureTests.cpp's
// own identical helper.
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

// editor-core-separation-23 campaign, PHASE4 - extended to also assert on
// renderFeatureNames (RecordRenderFeature()) alongside the pre-existing
// panel/component-type coverage.
TEST(ProjectAssemblyRegistrationLedgerTest, BeginRecordThenEndCapturesPanelComponentTypeAndRenderFeatureUnderTheActiveProject)
{
    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    ledger.BeginRecordingFor("LedgerTestProject_Alpha");
    ledger.RecordPanel("LedgerTestProject_Alpha_PanelA");
    ledger.RecordComponentType("LedgerTestProject_Alpha_CompA");
    ledger.RecordRenderFeature("LedgerTestProject_Alpha_RenderFeatureA");
    ledger.RecordPreOpaqueFeature("LedgerTestProject_Alpha_PreOpaqueFeatureA");
    ledger.EndRecording();

    const ProjectAssemblyRegistrationLedger::Entry entry = ledger.PeekEntry("LedgerTestProject_Alpha");
    ASSERT_EQ(entry.panelNames.size(), 1u);
    EXPECT_EQ(entry.panelNames[0], "LedgerTestProject_Alpha_PanelA");
    ASSERT_EQ(entry.componentTypeNames.size(), 1u);
    EXPECT_EQ(entry.componentTypeNames[0], "LedgerTestProject_Alpha_CompA");
    ASSERT_EQ(entry.renderFeatureNames.size(), 1u);
    EXPECT_EQ(entry.renderFeatureNames[0], "LedgerTestProject_Alpha_RenderFeatureA");
    ASSERT_EQ(entry.preOpaqueFeatureNames.size(), 1u);
    EXPECT_EQ(entry.preOpaqueFeatureNames[0], "LedgerTestProject_Alpha_PreOpaqueFeatureA");
    EXPECT_TRUE(entry.renderPassNames.empty());
}

// editor-core-separation-23 campaign, PHASE4 - extended to also cover
// RecordRenderFeature()'s identical no-active-bracket-is-a-safe-no-op
// contract.
TEST(ProjectAssemblyRegistrationLedgerTest, RecordPanelAndRenderFeatureWithNoActiveBracketAreSilentlyIgnored)
{
    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    // No BeginRecordingFor() call active at all right now - this must be a
    // safe, silent no-op, never a crash, and must never leak into any
    // project's own ledger entry.
    ledger.RecordPanel("LedgerTestProject_ShouldBeIgnored_PanelName");
    ledger.RecordRenderFeature("LedgerTestProject_ShouldBeIgnored_RenderFeatureName");
    ledger.RecordPreOpaqueFeature("LedgerTestProject_ShouldBeIgnored_PreOpaqueFeatureName");

    // Confirm it did not create a spurious entry under its own literal text
    // (were it ever mistakenly treated as a project name by a bug).
    const ProjectAssemblyRegistrationLedger::Entry stray = ledger.PeekEntry("LedgerTestProject_ShouldBeIgnored_PanelName");
    EXPECT_TRUE(stray.panelNames.empty());
    EXPECT_TRUE(stray.componentTypeNames.empty());
    EXPECT_TRUE(stray.renderPassNames.empty());
    EXPECT_TRUE(stray.renderFeatureNames.empty());
    EXPECT_TRUE(stray.preOpaqueFeatureNames.empty());
    const ProjectAssemblyRegistrationLedger::Entry strayRenderFeature =
        ledger.PeekEntry("LedgerTestProject_ShouldBeIgnored_RenderFeatureName");
    EXPECT_TRUE(strayRenderFeature.renderFeatureNames.empty());
    EXPECT_TRUE(strayRenderFeature.preOpaqueFeatureNames.empty());

    // Confirm a genuinely fresh project, begun/ended immediately afterward,
    // is not polluted by the ignored calls above.
    ledger.BeginRecordingFor("LedgerTestProject_Zeta");
    ledger.EndRecording();
    const ProjectAssemblyRegistrationLedger::Entry zeta = ledger.PeekEntry("LedgerTestProject_Zeta");
    EXPECT_TRUE(zeta.panelNames.empty());
    EXPECT_TRUE(zeta.componentTypeNames.empty());
    EXPECT_TRUE(zeta.renderPassNames.empty());
    EXPECT_TRUE(zeta.renderFeatureNames.empty());
    EXPECT_TRUE(zeta.preOpaqueFeatureNames.empty());
}

TEST(ProjectAssemblyRegistrationLedgerTest, NestedBeginEndBracketsForTheSameProjectAccumulateIntoOneEntry)
{
    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    // Mirrors a _Game.dll load followed by its sibling _Editor.dll load, for
    // the SAME project - two separate Begin/End brackets, same projectName,
    // must accumulate into the SAME ledger entry, not two separate ones.
    ledger.BeginRecordingFor("LedgerTestProject_Beta");
    ledger.RecordComponentType("LedgerTestProject_Beta_CompB1");
    ledger.EndRecording();

    ledger.BeginRecordingFor("LedgerTestProject_Beta");
    ledger.RecordPanel("LedgerTestProject_Beta_PanelB1");
    ledger.EndRecording();

    const ProjectAssemblyRegistrationLedger::Entry entry = ledger.PeekEntry("LedgerTestProject_Beta");
    ASSERT_EQ(entry.componentTypeNames.size(), 1u);
    EXPECT_EQ(entry.componentTypeNames[0], "LedgerTestProject_Beta_CompB1");
    ASSERT_EQ(entry.panelNames.size(), 1u);
    EXPECT_EQ(entry.panelNames[0], "LedgerTestProject_Beta_PanelB1");
}

TEST(ProjectAssemblyRegistrationLedgerTest, PeekEntryForANeverLoadedProjectReturnsAGenuinelyEmptyEntry)
{
    const ProjectAssemblyRegistrationLedger::Entry entry =
        ProjectAssemblyRegistrationLedger::Instance().PeekEntry("LedgerTestProject_NeverLoaded_Peek");
    EXPECT_TRUE(entry.renderPassNames.empty());
    EXPECT_TRUE(entry.renderFeatureNames.empty());
    EXPECT_TRUE(entry.panelNames.empty());
    EXPECT_TRUE(entry.componentTypeNames.empty());
    EXPECT_TRUE(entry.preOpaqueFeatureNames.empty());
}

TEST(ProjectAssemblyRegistrationLedgerTest, UnregisterEverythingForANeverLoadedProjectIsASafeNoOp)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;

    std::unique_ptr<Core> core;
    try {
        core = std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment). Real failure: "
                     << e.what();
    }
    ASSERT_NE(core, nullptr);

    // Never loaded, never recorded anywhere in this binary - must not crash,
    // must not throw, only logs at INFO level (see UnregisterEverythingFor()'s
    // own doc comment).
    ProjectAssemblyRegistrationLedger::Instance().UnregisterEverythingFor("LedgerTestProject_NeverLoaded_Unregister", *core);

    // Still genuinely empty afterward - idempotent.
    const ProjectAssemblyRegistrationLedger::Entry entry =
        ProjectAssemblyRegistrationLedger::Instance().PeekEntry("LedgerTestProject_NeverLoaded_Unregister");
    EXPECT_TRUE(entry.renderPassNames.empty());
    EXPECT_TRUE(entry.renderFeatureNames.empty());
    EXPECT_TRUE(entry.panelNames.empty());
    EXPECT_TRUE(entry.componentTypeNames.empty());
    EXPECT_TRUE(entry.preOpaqueFeatureNames.empty());
}

// The single most important test in this whole phase - proves the ENTIRE
// wiring end-to-end in isolation, without needing a real Project Assembly
// .dll at all: a real component type registered through
// RegisterComponentType<T>() INSIDE a BeginRecordingFor()/EndRecording()
// bracket must (a) actually land in ComponentTypeRegistry, (b) be recorded
// by the ledger, and then UnregisterEverythingFor() must remove it from
// BOTH places.
TEST(ProjectAssemblyRegistrationLedgerTest, FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;

    std::unique_ptr<Core> core;
    try {
        core = std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment). Real failure: "
                     << e.what();
    }
    ASSERT_NE(core, nullptr);

    const std::string projectName = "LedgerTestProject_Gamma_RoundTrip";
    const std::string typeName = "LedgerRoundTripDummyComponent";

    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    ledger.BeginRecordingFor(projectName);
    RegisterComponentType<LedgerRoundTripDummyComponent>(typeName, {
        GTE_REFLECT_FIELD(LedgerRoundTripDummyComponent, value),
    });
    ledger.EndRecording();

    ASSERT_NE(ComponentTypeRegistry::Instance().Find(typeName), nullptr);
    const ProjectAssemblyRegistrationLedger::Entry beforeUnregister = ledger.PeekEntry(projectName);
    ASSERT_EQ(beforeUnregister.componentTypeNames.size(), 1u);
    EXPECT_EQ(beforeUnregister.componentTypeNames[0], typeName);

    ledger.UnregisterEverythingFor(projectName, *core);

    EXPECT_EQ(ComponentTypeRegistry::Instance().Find(typeName), nullptr);
    const ProjectAssemblyRegistrationLedger::Entry afterUnregister = ledger.PeekEntry(projectName);
    EXPECT_TRUE(afterUnregister.componentTypeNames.empty());
    EXPECT_TRUE(afterUnregister.panelNames.empty());
    EXPECT_TRUE(afterUnregister.renderPassNames.empty());
    EXPECT_TRUE(afterUnregister.renderFeatureNames.empty());
}

// editor-core-separation-23 campaign, PHASE4 - the render-feature mirror of
// FullRoundTripThroughARealComponentTypeRegistrationProvesTheWholeWiring
// immediately above: a real Project Assembly render feature, registered
// through Core::RegisterProjectRenderFeature() (PHASE3) INSIDE a
// BeginRecordingFor()/EndRecording() bracket, must (a) actually land in
// RenderFeatureCompositor::DebugSnapshot(), (b) be recorded by the ledger,
// and then UnregisterEverythingFor() must remove it from BOTH places -
// proving PHASE4's own teardown wiring genuinely reaches
// RenderFeatureCompositor::UnregisterProjectFeature(), not merely clears the
// ledger's own bookkeeping.
TEST(ProjectAssemblyRegistrationLedgerTest, FullRoundTripThroughARealRenderFeatureRegistrationProvesTheWholeWiring)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;

    std::unique_ptr<Core> core;
    try {
        core = std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment). Real failure: "
                     << e.what();
    }
    ASSERT_NE(core, nullptr);

    const std::string projectName = "LedgerTestProject_Delta_RenderFeatureRoundTrip";
    const std::string featureName = "LedgerTestProject_Delta_RenderFeatureRoundTrip_Feature";

    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    ledger.BeginRecordingFor(projectName);
    const bool registered = core->RegisterProjectRenderFeature(featureName.c_str(), RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { });
    ledger.EndRecording();
    ASSERT_TRUE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    ASSERT_NE(FindByName(compositor->DebugSnapshot(), featureName), nullptr);

    const ProjectAssemblyRegistrationLedger::Entry beforeUnregister = ledger.PeekEntry(projectName);
    ASSERT_EQ(beforeUnregister.renderFeatureNames.size(), 1u);
    EXPECT_EQ(beforeUnregister.renderFeatureNames[0], featureName);

    ledger.UnregisterEverythingFor(projectName, *core);

    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), featureName), nullptr);
    const ProjectAssemblyRegistrationLedger::Entry afterUnregister = ledger.PeekEntry(projectName);
    EXPECT_TRUE(afterUnregister.renderFeatureNames.empty());
    EXPECT_TRUE(afterUnregister.componentTypeNames.empty());
    EXPECT_TRUE(afterUnregister.panelNames.empty());
    EXPECT_TRUE(afterUnregister.renderPassNames.empty());
}

// better-render-pass-5 effort, BLOCK 3, PHASE6 - the PreOpaque mirror of
// FullRoundTripThroughARealRenderFeatureRegistrationProvesTheWholeWiring
// immediately above: a real PreOpaque feature, registered through
// Core::AddPreOpaquePass() (PHASE3) INSIDE a BeginRecordingFor()/
// EndRecording() bracket, must (a) actually land in
// RenderFeatureCompositor::DebugSnapshot(), (b) be recorded by the
// ledger under preOpaqueFeatureNames, and then UnregisterEverythingFor()
// must remove it from BOTH places - proving PHASE4's own teardown
// wiring genuinely reaches RenderFeatureCompositor::UnregisterPreOpaqueFeature(),
// not merely clears the ledger's own bookkeeping. This is the source
// spec's (BLOCK3_WIRE_PRE_POST_OPAQUE_STAGES.txt, Section 7) own Test 5.
TEST(ProjectAssemblyRegistrationLedgerTest, FullRoundTripThroughARealPreOpaqueFeatureRegistrationProvesTheWholeWiring)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;

    std::unique_ptr<Core> core;
    try {
        core = std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment). Real failure: "
                     << e.what();
    }
    ASSERT_NE(core, nullptr);

    const std::string projectName = "LedgerTestProject_Eta_PreOpaqueRoundTrip";
    const std::string featureName = "LedgerTestProject_Eta_PreOpaqueRoundTrip_Feature";

    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    ledger.BeginRecordingFor(projectName);
    const bool registered = core->AddPreOpaquePass(
        featureName.c_str(), [](rg::RenderGraphBuilder&, rg::RenderPassBlackboard&, rg::RenderViewId) { });
    ledger.EndRecording();
    ASSERT_TRUE(registered);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    ASSERT_NE(FindByName(compositor->DebugSnapshot(), featureName), nullptr);

    const ProjectAssemblyRegistrationLedger::Entry beforeUnregister = ledger.PeekEntry(projectName);
    ASSERT_EQ(beforeUnregister.preOpaqueFeatureNames.size(), 1u);
    EXPECT_EQ(beforeUnregister.preOpaqueFeatureNames[0], featureName);

    ledger.UnregisterEverythingFor(projectName, *core);

    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), featureName), nullptr);
    const ProjectAssemblyRegistrationLedger::Entry afterUnregister = ledger.PeekEntry(projectName);
    EXPECT_TRUE(afterUnregister.preOpaqueFeatureNames.empty());
    EXPECT_TRUE(afterUnregister.renderFeatureNames.empty());
    EXPECT_TRUE(afterUnregister.componentTypeNames.empty());
    EXPECT_TRUE(afterUnregister.panelNames.empty());
    EXPECT_TRUE(afterUnregister.renderPassNames.empty());
}

// editor-core-separation-23 campaign, PHASE4 - the teardown ORDERING claim
// itself: registers BOTH a render feature AND a render-pass provider for the
// SAME test project (in that order), then confirms UnregisterEverythingFor()
// removes BOTH cleanly. Directly observing "feature torn down strictly
// before provider" from outside this class is impractical without a deeper
// instrumentation hook (neither UnregisterProjectFeature() nor
// UnregisterProjectRenderPassProvider() invoke their own owning
// callback/provider on teardown, so there is no externally-observable side
// effect to hook) - per this phase file's own explicitly-offered fallback
// (section 3.4, item 4), the actual execution-order guarantee is confirmed
// by DIRECT CODE REVIEW of ProjectAssemblyRegistrationLedger.cpp's own
// UnregisterEverythingFor() body (the renderFeatureNames loop appears
// strictly BEFORE the renderPassNames loop in source order - see this
// phase's own completion report for the exact line citation), with the live,
// real hot-reload dynamic proof deferred to PHASE6.
TEST(ProjectAssemblyRegistrationLedgerTest, UnregisterEverythingForTearsDownBothRenderFeatureAndRenderPassProviderCleanly)
{
    HeadlessSurfaceProvider surfaceProvider;
    NoopHostServices hostServices;

    std::unique_ptr<Core> core;
    try {
        core = std::make_unique<Core>(surfaceProvider, hostServices);
    } catch (const std::exception& e) {
        GTEST_SKIP() << "Core construction needs a real, valid VkSurfaceKHR - this machine's Vulkan "
                        "driver/loader apparently does not support VK_EXT_headless_surface (see "
                        "HeadlessSurfaceProvider.h's own top-of-file comment). Real failure: "
                     << e.what();
    }
    ASSERT_NE(core, nullptr);

    const std::string projectName = "LedgerTestProject_Epsilon_OrderingProof";
    const std::string featureName = "LedgerTestProject_Epsilon_OrderingProof_Feature";
    const std::string renderPassName = "LedgerTestProject_Epsilon_OrderingProof_Pass";

    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    ledger.BeginRecordingFor(projectName);
    ASSERT_TRUE(core->RegisterProjectRenderFeature(featureName.c_str(), RenderFeatureStage::PostComposite,
        RenderFeatureBlendMode::Replace, 0, [](rg::RenderGraphBuilder&, rg::TextureHandle, VkExtent2D) { }));
    core->RegisterProjectRenderPassProvider(renderPassName.c_str(), rg::ProviderScope::Once,
        [](const rg::RenderPassFrameContext&, std::vector<rg::RenderPassDesc>&) { });
    ledger.EndRecording();

    const ProjectAssemblyRegistrationLedger::Entry beforeUnregister = ledger.PeekEntry(projectName);
    ASSERT_EQ(beforeUnregister.renderFeatureNames.size(), 1u);
    ASSERT_EQ(beforeUnregister.renderPassNames.size(), 1u);

    RenderFeatureCompositor* compositor = core->GetRenderFeatureCompositor();
    ASSERT_NE(compositor, nullptr);
    ASSERT_NE(FindByName(compositor->DebugSnapshot(), featureName), nullptr);

    ledger.UnregisterEverythingFor(projectName, *core);

    EXPECT_EQ(FindByName(compositor->DebugSnapshot(), featureName), nullptr);
    const ProjectAssemblyRegistrationLedger::Entry afterUnregister = ledger.PeekEntry(projectName);
    EXPECT_TRUE(afterUnregister.renderFeatureNames.empty());
    EXPECT_TRUE(afterUnregister.renderPassNames.empty());
}

} // namespace gte
