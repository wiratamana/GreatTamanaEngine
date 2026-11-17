#include "Editor/EditorExtensions.h"
#include "Editor/EditorFeatureModule.h"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gte {
namespace {

using Log = std::vector<std::string>;

class FakeModule : public IEditorFeatureModule {
public:
    FakeModule(std::string name, int order, Log& log, std::string prefix = {}, bool claims = true)
        : m_name(std::move(name)), m_order(order), m_log(log), m_prefix(std::move(prefix)), m_claims(claims)
    {
    }

    const char* ModuleName() const override { return m_name.c_str(); }
    int TickOrder() const noexcept override { return m_order; }
    void OnAttach(const EditorModuleAttach& attach) override
    {
        m_log.push_back("attach " + m_name);
        sawDirectory = &attach.modules;
    }
    void OnUiRequests() override { m_log.push_back("ui " + m_name); }
    void OnFrameTick(const EditorFrameTick&) override { m_log.push_back("tick " + m_name); }
    void OnShutdown() override { m_log.push_back("shutdown " + m_name); }
    std::string_view CommandPrefix() const noexcept override { return m_prefix; }
    std::optional<ModuleCommandResult> OnCommand(const ModuleCommandRequest& request) override
    {
        m_log.push_back("cmd " + m_name);
        if (!m_claims) {
            return std::nullopt;
        }
        ModuleCommandResult result;
        result.body = m_name + ":" + request.action;
        return result;
    }

    const IEditorModuleDirectory* sawDirectory = nullptr;

private:
    std::string m_name;
    int m_order;
    Log& m_log;
    std::string m_prefix;
    bool m_claims;
};

class PlainModule final : public IEngineFeatureModule {
public:
    const char* ModuleName() const override { return "Plain"; }
};

using Modules = std::vector<std::unique_ptr<IEngineFeatureModule>>;

Modules MakeThree(Log& log)
{
    Modules modules;
    modules.push_back(std::make_unique<FakeModule>("C", 20, log));
    modules.push_back(std::make_unique<PlainModule>());
    modules.push_back(std::make_unique<FakeModule>("A", 0, log));
    modules.push_back(std::make_unique<FakeModule>("B", 10, log));
    return modules;
}

TEST(EditorFeatureModuleTest, HooksRunInTickOrderAndPlainModulesAreIgnored)
{
    Log log;
    Modules modules = MakeThree(log);
    {
        EditorModuleSet set;
        set.Attach(modules, nullptr);
        EXPECT_EQ(set.EditorModuleCount(), 3u);
        set.ApplyUiRequests();
        set.FrameTick({});
        log.push_back("end");
        ASSERT_GE(log.size(), 10u);
        EXPECT_EQ((Log{ "attach A", "attach B", "attach C", "ui A", "ui B", "ui C", "tick A", "tick B", "tick C", "end" }), log);
    }
}

TEST(EditorFeatureModuleTest, EqualOrdersKeepCreationOrder)
{
    Log log;
    Modules modules;
    modules.push_back(std::make_unique<FakeModule>("X", 5, log));
    modules.push_back(std::make_unique<FakeModule>("Y", 5, log));
    modules.push_back(std::make_unique<FakeModule>("Z", 5, log));
    EditorModuleSet set;
    set.Attach(modules, nullptr);
    set.FrameTick({});
    EXPECT_EQ((Log{ "attach X", "attach Y", "attach Z", "tick X", "tick Y", "tick Z" }), log);
}

TEST(EditorFeatureModuleTest, ShutdownRunsInReverseTickOrderWhenTheSetDies)
{
    Log log;
    Modules modules = MakeThree(log);
    {
        EditorModuleSet set;
        set.Attach(modules, nullptr);
        log.clear();
    }
    EXPECT_EQ((Log{ "shutdown C", "shutdown B", "shutdown A" }), log);
}

TEST(EditorFeatureModuleTest, ModulesFindEachOtherByNameThroughTheDirectory)
{
    Log log;
    Modules modules = MakeThree(log);
    EditorModuleSet set;
    set.Attach(modules, nullptr);

    auto* const a = dynamic_cast<FakeModule*>(set.FindModule("A"));
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->sawDirectory, static_cast<const IEditorModuleDirectory*>(&set));
    EXPECT_EQ(set.Find<FakeModule>("B")->ModuleName(), std::string("B"));
    EXPECT_EQ(set.Find<FakeModule>("Plain"), nullptr); // Exists, but is not that type.
    EXPECT_EQ(set.FindModule("Missing"), nullptr);
    EXPECT_EQ(set.FindFeature("A"), nullptr);          // Not a render feature.
    EXPECT_EQ(set.FindEndpoint("A"), nullptr);
}

TEST(EditorFeatureModuleTest, AnUnattachedSetFindsNothing)
{
    EditorModuleSet set;
    EXPECT_EQ(set.FindModule("A"), nullptr);
    EXPECT_EQ(set.FindFeature("A"), nullptr);
    set.ApplyUiRequests();
    set.FrameTick({});
}

TEST(EditorFeatureModuleTest, ACommandGoesToTheModuleWithTheMatchingPrefixFirst)
{
    Log log;
    Modules modules;
    modules.push_back(std::make_unique<FakeModule>("Default", 0, log));
    modules.push_back(std::make_unique<FakeModule>("Prefixed", 10, log, "water."));
    EditorModuleSet set;
    set.Attach(modules, nullptr);
    log.clear();

    const ModuleCommandResult result = set.RunCommand({ "water.add_lake", {} });
    EXPECT_EQ(result.body, "Prefixed:water.add_lake");
    EXPECT_EQ(log, (Log{ "cmd Prefixed" }));
}

TEST(EditorFeatureModuleTest, ACommandWithoutAPrefixMatchGoesToTheDefaultModule)
{
    Log log;
    Modules modules;
    modules.push_back(std::make_unique<FakeModule>("Default", 0, log));
    modules.push_back(std::make_unique<FakeModule>("Prefixed", 10, log, "water."));
    EditorModuleSet set;
    set.Attach(modules, nullptr);
    log.clear();

    EXPECT_EQ(set.RunCommand({ "open", {} }).body, "Default:open");
    EXPECT_EQ(log, (Log{ "cmd Default" }));
}

TEST(EditorFeatureModuleTest, ADeclinedCommandFallsThroughAndAnUnclaimedOneIsA404)
{
    Log log;
    Modules modules;
    modules.push_back(std::make_unique<FakeModule>("Picky", 0, log, "", /*claims=*/false));
    modules.push_back(std::make_unique<FakeModule>("Other", 5, log));
    EditorModuleSet set;
    set.Attach(modules, nullptr);
    EXPECT_EQ(set.RunCommand({ "x", {} }).body, "Other:x");

    Modules nobody;
    nobody.push_back(std::make_unique<FakeModule>("Picky", 0, log, "", /*claims=*/false));
    EditorModuleSet none;
    none.Attach(nobody, nullptr);
    const ModuleCommandResult result = none.RunCommand({ "x", {} });
    EXPECT_EQ(result.httpStatus, 404);
    EXPECT_NE(result.body.find("'x'"), std::string::npos);
}

struct SlotA { int value = 1; };
struct SlotB { int value = 2; };

class ThrowingModule final : public IEditorFeatureModule {
public:
    ThrowingModule(int order, Log& log) : m_order(order), m_log(log) {}

    const char* ModuleName() const override { return "Thrower"; }
    int TickOrder() const noexcept override { return m_order; }
    void OnUiRequests() override
    {
        ++uiCalls;
        throw std::runtime_error("ui boom");
    }
    void OnFrameTick(const EditorFrameTick&) override
    {
        ++tickCalls;
        throw 42; // Not a std::exception.
    }
    std::string_view CommandPrefix() const noexcept override { return "boom."; }
    std::optional<ModuleCommandResult> OnCommand(const ModuleCommandRequest&) override
    {
        ++commandCalls;
        throw std::runtime_error("cmd \"boom\"");
    }

    int uiCalls = 0;
    int tickCalls = 0;
    int commandCalls = 0;

private:
    int m_order;
    Log& m_log;
};

TEST(EditorFeatureModuleTest, AThrowingHookDoesNotStopTheFrameOrTheOtherModules)
{
    Log log;
    Modules modules;
    auto thrower = std::make_unique<ThrowingModule>(5, log);
    ThrowingModule* const throwerPtr = thrower.get();
    modules.push_back(std::make_unique<FakeModule>("A", 0, log));
    modules.push_back(std::move(thrower));
    modules.push_back(std::make_unique<FakeModule>("C", 10, log));
    EditorModuleSet set;
    set.Attach(modules, nullptr);
    log.clear();

    for (int frame = 0; frame < 3; ++frame) {
        set.ApplyUiRequests();
        set.FrameTick({});
    }
    EXPECT_EQ(throwerPtr->uiCalls, 3);   // Still called every frame; only the log is rate limited.
    EXPECT_EQ(throwerPtr->tickCalls, 3);
    ASSERT_EQ(log.size(), 12u);
    EXPECT_EQ(log[0], "ui A");
    EXPECT_EQ(log[1], "ui C");
    EXPECT_EQ(log[2], "tick A");
    EXPECT_EQ(log[3], "tick C");
}

TEST(EditorFeatureModuleTest, AThrowingCommandBecomesAnHttp500WithAnEscapedMessage)
{
    Log log;
    Modules modules;
    auto thrower = std::make_unique<ThrowingModule>(0, log);
    ThrowingModule* const throwerPtr = thrower.get();
    modules.push_back(std::move(thrower));
    modules.push_back(std::make_unique<FakeModule>("Other", 5, log));
    EditorModuleSet set;
    set.Attach(modules, nullptr);

    const ModuleCommandResult result = set.RunCommand({ "boom.go", {} });
    EXPECT_EQ(result.httpStatus, 500);
    EXPECT_EQ(result.contentType, "application/json");
    EXPECT_NE(result.body.find("\"success\":false"), std::string::npos);
    EXPECT_NE(result.body.find("module 'Thrower' threw: cmd \\\"boom\\\""), std::string::npos);
    EXPECT_EQ(throwerPtr->commandCalls, 1);

    // The next command is still served: a throw does not poison the set.
    EXPECT_EQ(set.RunCommand({ "other", {} }).body, "Other:other");
    EXPECT_EQ(set.RunCommand({ "boom.again", {} }).httpStatus, 500);
    EXPECT_EQ(throwerPtr->commandCalls, 2);
}

TEST(EditorExtensionsTest, EachTypeGetsItsOwnStableSlot)
{
    EditorExtensions extensions;
    SlotA& a = extensions.Get<SlotA>();
    a.value = 7;
    SlotB& b = extensions.Get<SlotB>();
    EXPECT_EQ(&a, &extensions.Get<SlotA>());
    EXPECT_EQ(extensions.Get<SlotA>().value, 7);
    EXPECT_EQ(b.value, 2);
    for (int i = 0; i < 64; ++i) {
        extensions.Get<std::vector<int>>().push_back(i); // A rehash must not move the other slots.
    }
    EXPECT_EQ(&a, &extensions.Get<SlotA>());
}

} // namespace
} // namespace gte
