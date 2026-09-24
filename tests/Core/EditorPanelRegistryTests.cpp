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
#include "../../plugins/gte_plugin_abi/IEditorPanelModule.h"
#include "../../plugins/gte_plugin_abi/IPluginPanelDrawContext.h"

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
