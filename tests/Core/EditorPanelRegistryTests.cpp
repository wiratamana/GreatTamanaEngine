// Tier-1 tests for src/Core/EditorPanelRegistry.h/.cpp (editor-core-
// separation-3 campaign, PHASE4_EDITOR_PANEL_CAPABILITY_AND_REGISTRY.md) -
// replaces the old tests/Editor/EditorPanelCatalogTests.cpp (deleted this
// same phase, since Core/EditorPanelCatalog.h itself was deleted, mirroring
// the "delete the old mechanism, don't leave a parallel one" precedent
// PHASE3.4 of this same phase file cites).
//
// EditorPanelRegistry::Instance() is process-wide, mutable state that
// persists for the lifetime of this whole test binary (a Meyers singleton,
// mirroring LoggerLogSink::Instance()'s own precedent - see
// tests/Core/LogSinkTests.cpp's own header comment) - unlike Logger, it has
// no Clear()/reset method (by design: production code registers every
// built-in panel name, then every plugin panel, exactly ONCE per process,
// at EditorHost construction time - see PHASE0_MASTER_STRATEGY.md's Locked
// Design Decision #9 - there is no real call site that ever needs to reset
// it). Every test below therefore registers its own uniquely-named,
// test-only panel name(s) (never a real built-in name like "Hierarchy") so
// tests never interfere with each other regardless of run order, and never
// asserts an absolute AllNames()/PluginPanels() size - only that ITS OWN
// registered name(s) round-trip correctly.

#include "Core/EditorPanelRegistry.h"
#include "Core/EditorPanelModule.h" // relocated here, better-render-pass-2 PHASE1.

#include <gtest/gtest.h>

#include <algorithm>

namespace gte {
namespace {

class FakeEditorPanelModule final : public IEditorPanelModule_v1 {
public:
    explicit FakeEditorPanelModule(const char* name)
        : m_name(name)
    {
    }

    const char* GetPanelName() const override { return m_name; }
    void BuildPanel(IPluginPanelDrawContext& /*ctx*/) override { m_buildPanelCallCount++; }

    int BuildPanelCallCount() const noexcept { return m_buildPanelCallCount; }

private:
    const char* m_name;
    int m_buildPanelCallCount = 0;
};

bool Contains(const std::vector<std::string>& names, const std::string& name)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

TEST(EditorPanelRegistryTest, RegisterBuiltinPanelName_MakesIsKnownNameTrueAndAppearsInAllNames)
{
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Test_Builtin_Panel_Alpha");

    EXPECT_TRUE(EditorPanelRegistry::Instance().IsKnownName("Test_Builtin_Panel_Alpha"));
    EXPECT_TRUE(Contains(EditorPanelRegistry::Instance().AllNames(), "Test_Builtin_Panel_Alpha"));
}

TEST(EditorPanelRegistryTest, RegisterBuiltinPanelName_DoesNotAppearInPluginPanels)
{
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Test_Builtin_Panel_Beta");

    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        EXPECT_NE(entry.name, "Test_Builtin_Panel_Beta");
    }
}

TEST(EditorPanelRegistryTest, RegisterPluginPanel_MakesIsKnownNameTrueAndAppearsInAllNamesAndPluginPanels)
{
    FakeEditorPanelModule fakeModule("Test_Plugin_Panel_Gamma");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_Panel_Gamma", &fakeModule);

    EXPECT_TRUE(EditorPanelRegistry::Instance().IsKnownName("Test_Plugin_Panel_Gamma"));
    EXPECT_TRUE(Contains(EditorPanelRegistry::Instance().AllNames(), "Test_Plugin_Panel_Gamma"));

    bool foundInPluginPanels = false;
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Plugin_Panel_Gamma") {
            foundInPluginPanels = true;
            EXPECT_EQ(entry.module, &fakeModule);
        }
    }
    EXPECT_TRUE(foundInPluginPanels);
}

TEST(EditorPanelRegistryTest, RegisterPluginPanel_ModulePointerIsUsableAndDrawsThroughTheCuratedContext)
{
    FakeEditorPanelModule fakeModule("Test_Plugin_Panel_Delta");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_Panel_Delta", &fakeModule);

    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Plugin_Panel_Delta") {
            class NoOpDrawContext final : public IPluginPanelDrawContext {
            public:
                void Text(const char* /*text*/) override { }
                bool Button(const char* /*label*/) override { return false; }
                void Separator() override { }
            };
            NoOpDrawContext drawContext;
            entry.module->BuildPanel(drawContext);
        }
    }

    EXPECT_EQ(fakeModule.BuildPanelCallCount(), 1);
}

TEST(EditorPanelRegistryTest, RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredBuiltinName)
{
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Test_Builtin_For_Collision_Eta");

    FakeEditorPanelModule collidingModule("Test_Builtin_For_Collision_Eta");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Builtin_For_Collision_Eta", &collidingModule);

    // The name must still resolve to the ORIGINAL (built-in) registration,
    // never the plugin's - i.e. it must NOT appear a second time in
    // PluginPanels(), proving the plugin's own registration was refused.
    int occurrencesInPluginPanels = 0;
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Builtin_For_Collision_Eta") {
            ++occurrencesInPluginPanels;
        }
    }
    EXPECT_EQ(occurrencesInPluginPanels, 0);
}

TEST(EditorPanelRegistryTest, RegisterPluginPanel_RefusesACollisionWithAnAlreadyRegisteredPluginName)
{
    FakeEditorPanelModule firstModule("Test_Plugin_For_Collision_Theta");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_For_Collision_Theta", &firstModule);

    FakeEditorPanelModule secondModule("Test_Plugin_For_Collision_Theta");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_For_Collision_Theta", &secondModule);

    // Exactly ONE entry must exist for this name, and it must still point at
    // the FIRST module (first-registered wins, second is refused) - never
    // two entries for the same name.
    int occurrences = 0;
    const IEditorPanelModule_v1* resolvedModule = nullptr;
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Plugin_For_Collision_Theta") {
            ++occurrences;
            resolvedModule = entry.module;
        }
    }
    EXPECT_EQ(occurrences, 1);
    EXPECT_EQ(resolvedModule, &firstModule);
}

TEST(EditorPanelRegistryTest, IsKnownName_UnregisteredNameReturnsFalse)
{
    EXPECT_FALSE(EditorPanelRegistry::Instance().IsKnownName("Test_This_Name_Was_Never_Registered_Epsilon"));
}

TEST(EditorPanelRegistryTest, IsKnownName_IsCaseSensitive)
{
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Test_Case_Sensitive_Zeta");

    EXPECT_TRUE(EditorPanelRegistry::Instance().IsKnownName("Test_Case_Sensitive_Zeta"));
    EXPECT_FALSE(EditorPanelRegistry::Instance().IsKnownName("test_case_sensitive_zeta"));
}

// editor-core-separation-13 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 2, PHASE2 - Hazard 2 fix) - Tier-1 coverage for
// EditorPanelRegistry::UnregisterPluginPanel(), including the
// m_allNames-collision-guard regression this phase exists to prevent (see
// PHASE2_EDITOR_PANEL_REGISTRY_UNREGISTER_HAZARD2.md, Step 3.4, items 1-4).
// Each test uses its own unique, nowhere-else-used name(s) and tears down
// (via UnregisterPluginPanel()) whatever it registers, mirroring this file's
// own pre-existing convention exactly.

TEST(EditorPanelRegistryTest, UnregisterPluginPanel_RemovesFromBothAllNamesAndPluginPanels)
{
    FakeEditorPanelModule fakeModule("Test_Plugin_Panel_Iota");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_Panel_Iota", &fakeModule);

    ASSERT_TRUE(EditorPanelRegistry::Instance().IsKnownName("Test_Plugin_Panel_Iota"));
    ASSERT_TRUE(Contains(EditorPanelRegistry::Instance().AllNames(), "Test_Plugin_Panel_Iota"));

    EditorPanelRegistry::Instance().UnregisterPluginPanel("Test_Plugin_Panel_Iota");

    EXPECT_FALSE(EditorPanelRegistry::Instance().IsKnownName("Test_Plugin_Panel_Iota"));
    EXPECT_FALSE(Contains(EditorPanelRegistry::Instance().AllNames(), "Test_Plugin_Panel_Iota"));
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        EXPECT_NE(entry.name, "Test_Plugin_Panel_Iota");
    }
}

TEST(EditorPanelRegistryTest, UnregisterPluginPanel_ThenReRegisteringSameNameSucceedsWithoutCollisionRefusal)
{
    // This is the exact regression this phase exists to prevent: the
    // external plan's own sketch left m_allNames untouched, which would
    // make this second RegisterPluginPanel() call be silently refused by
    // IsKnownName()'s own collision guard - precisely what every reload
    // after the first does, for the campaign's own permanent
    // ProjectAssemblyProbe fixture's "Probe Panel".
    FakeEditorPanelModule firstModule("Test_Plugin_Panel_Kappa");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_Panel_Kappa", &firstModule);
    EditorPanelRegistry::Instance().UnregisterPluginPanel("Test_Plugin_Panel_Kappa");

    FakeEditorPanelModule secondModule("Test_Plugin_Panel_Kappa");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Plugin_Panel_Kappa", &secondModule);

    EXPECT_TRUE(EditorPanelRegistry::Instance().IsKnownName("Test_Plugin_Panel_Kappa"));
    int occurrences = 0;
    const IEditorPanelModule_v1* resolvedModule = nullptr;
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Plugin_Panel_Kappa") {
            ++occurrences;
            resolvedModule = entry.module;
        }
    }
    EXPECT_EQ(occurrences, 1);
    EXPECT_EQ(resolvedModule, &secondModule);

    EditorPanelRegistry::Instance().UnregisterPluginPanel("Test_Plugin_Panel_Kappa");
}

TEST(EditorPanelRegistryTest, UnregisterPluginPanel_NeverRegisteredNameIsASafeNoOp)
{
    EXPECT_NO_FATAL_FAILURE(
        EditorPanelRegistry::Instance().UnregisterPluginPanel("Test_Plugin_Panel_Never_Registered_Lambda"));
    EXPECT_FALSE(EditorPanelRegistry::Instance().IsKnownName("Test_Plugin_Panel_Never_Registered_Lambda"));
}

TEST(EditorPanelRegistryTest, UnregisterPluginPanel_DoesNotWeakenBuiltinNameCollisionProtection)
{
    EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Test_Builtin_For_Collision_Mu");

    FakeEditorPanelModule collidingModule("Test_Builtin_For_Collision_Mu");
    EditorPanelRegistry::Instance().RegisterPluginPanel("Test_Builtin_For_Collision_Mu", &collidingModule);

    // The plugin registration must still be refused - only plugin-registered
    // names become unregisterable, built-in names are untouched by this
    // phase's own change. NOTE: deliberately NOT calling
    // UnregisterPluginPanel("Test_Builtin_For_Collision_Mu") here to "clean
    // up" - per this method's own documented, spec-exact behavior it removes
    // ANY matching name from m_allNames regardless of how it got there, so
    // doing that here would actually remove this built-in name too, which is
    // not what this test is checking and would be a self-inflicted false
    // assumption, not a real bug (EditorPanelRegistry has no reset method by
    // design - see this file's own header comment - so built-in names
    // registered by tests are expected to persist for the rest of this test
    // binary's lifetime, exactly like every other RegisterBuiltinPanelName
    // test above).
    int occurrencesInPluginPanels = 0;
    for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
        if (entry.name == "Test_Builtin_For_Collision_Mu") {
            ++occurrencesInPluginPanels;
        }
    }
    EXPECT_EQ(occurrencesInPluginPanels, 0);
    EXPECT_TRUE(EditorPanelRegistry::Instance().IsKnownName("Test_Builtin_For_Collision_Mu"));
}

} // namespace
} // namespace gte

// This file ALSO registers the ONE global gtest Environment that seeds
// EditorPanelRegistry with the same real built-in panel names
// EditorHost.cpp's own constructor registers in production (see that file's
// own registration call, PHASE4) - for THIS WHOLE TEST BINARY, since no test
// here constructs a real EditorHost. Other test files elsewhere in this same
// binary (tests/Network/NetworkRoutesTests.cpp's ParseActivateTabQueryTests/
// BuildListTabsResponseJsonTests, tests/Network/
// ActivateTabEndpointEndToEndTests.cpp) depend on real names like "Profiler"
// already being known - mirrors tests/Core/LogSinkTests.cpp's own identical
// "one global Environment every other test file quietly depends on" shape.
namespace {

class SeedBuiltinEditorPanelNamesEnvironment : public ::testing::Environment {
public:
    void SetUp() override
    {
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Hierarchy");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Inspector");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Scene");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Game");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Memory");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Profiler");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Render Graph");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Jobs");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Atmosphere");
        ::gte::EditorPanelRegistry::Instance().RegisterBuiltinPanelName("Log");
    }
};

::testing::Environment* const g_seedBuiltinEditorPanelNamesEnvironment =
    ::testing::AddGlobalTestEnvironment(new SeedBuiltinEditorPanelNamesEnvironment());

} // namespace
