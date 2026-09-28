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

#include "Core/Plugins/ProjectAssemblyRegistrationLedger.h"
#include "Core/Core.h"
#include "Core/IHostServices.h"
#include "ECS/Reflection/ComponentTypeRegistry.h"
#include "ECS/Reflection/ReflectFieldMacros.h"
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

} // namespace

TEST(ProjectAssemblyRegistrationLedgerTest, BeginRecordThenEndCapturesPanelAndComponentTypeUnderTheActiveProject)
{
    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    ledger.BeginRecordingFor("LedgerTestProject_Alpha");
    ledger.RecordPanel("LedgerTestProject_Alpha_PanelA");
    ledger.RecordComponentType("LedgerTestProject_Alpha_CompA");
    ledger.EndRecording();

    const ProjectAssemblyRegistrationLedger::Entry entry = ledger.PeekEntry("LedgerTestProject_Alpha");
    ASSERT_EQ(entry.panelNames.size(), 1u);
    EXPECT_EQ(entry.panelNames[0], "LedgerTestProject_Alpha_PanelA");
    ASSERT_EQ(entry.componentTypeNames.size(), 1u);
    EXPECT_EQ(entry.componentTypeNames[0], "LedgerTestProject_Alpha_CompA");
    EXPECT_TRUE(entry.renderPassNames.empty());
}

TEST(ProjectAssemblyRegistrationLedgerTest, RecordPanelWithNoActiveBracketIsSilentlyIgnored)
{
    ProjectAssemblyRegistrationLedger& ledger = ProjectAssemblyRegistrationLedger::Instance();

    // No BeginRecordingFor() call active at all right now - this must be a
    // safe, silent no-op, never a crash, and must never leak into any
    // project's own ledger entry.
    ledger.RecordPanel("LedgerTestProject_ShouldBeIgnored_PanelName");

    // Confirm it did not create a spurious entry under its own literal text
    // (were it ever mistakenly treated as a project name by a bug).
    const ProjectAssemblyRegistrationLedger::Entry stray = ledger.PeekEntry("LedgerTestProject_ShouldBeIgnored_PanelName");
    EXPECT_TRUE(stray.panelNames.empty());
    EXPECT_TRUE(stray.componentTypeNames.empty());
    EXPECT_TRUE(stray.renderPassNames.empty());

    // Confirm a genuinely fresh project, begun/ended immediately afterward,
    // is not polluted by the ignored call above.
    ledger.BeginRecordingFor("LedgerTestProject_Zeta");
    ledger.EndRecording();
    const ProjectAssemblyRegistrationLedger::Entry zeta = ledger.PeekEntry("LedgerTestProject_Zeta");
    EXPECT_TRUE(zeta.panelNames.empty());
    EXPECT_TRUE(zeta.componentTypeNames.empty());
    EXPECT_TRUE(zeta.renderPassNames.empty());
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
    EXPECT_TRUE(entry.panelNames.empty());
    EXPECT_TRUE(entry.componentTypeNames.empty());
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
    EXPECT_TRUE(entry.panelNames.empty());
    EXPECT_TRUE(entry.componentTypeNames.empty());
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
}

} // namespace gte
