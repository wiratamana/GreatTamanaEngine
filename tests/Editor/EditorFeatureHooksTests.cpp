#include "Editor/EditorExtensions.h"
#include "Editor/EditorFeatureHooks.h"
#include "Editor/EditorFeatureModule.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace gte {
namespace {

using Log = std::vector<std::string>;

class FakeViewHook : public IEditorViewHook {
public:
    FakeViewHook(std::string name, Log& log, bool claims = false) : m_name(std::move(name)), m_log(log), m_claims(claims) {}

    bool ClaimsPointer() const noexcept override { return m_claims; }
    void OnViewInput(const EditorViewContext& view) override
    {
        m_log.push_back("input " + m_name + (view.pointerFree ? " free" : " blocked"));
    }
    void OnViewOverlay(const EditorViewContext& view) override
    {
        m_log.push_back("overlay " + m_name + (view.kind == EditorViewKind::Game ? " game" : " scene"));
    }

private:
    std::string m_name;
    Log& m_log;
    bool m_claims;
};

TEST(EditorFeatureHooksTest, ViewHooksRunInRegistrationOrder)
{
    Log log;
    FakeViewHook a("A", log);
    FakeViewHook b("B", log);
    EditorFeatureHooks hooks;
    hooks.AddViewHook(&a);
    hooks.AddViewHook(&b);
    hooks.AddViewHook(&a); // A second add of the same hook is ignored.

    EditorViewContext view;
    view.hovered = true;
    hooks.RunViewInput(view);
    hooks.RunViewOverlay(view);
    EXPECT_EQ((Log{ "input A free", "input B free", "overlay A game", "overlay B game" }), log);
}

TEST(EditorFeatureHooksTest, AClaimingHookBlocksThePointerForEveryone)
{
    Log log;
    FakeViewHook a("A", log);
    FakeViewHook claimer("C", log, /*claims=*/true);
    EditorFeatureHooks hooks;
    hooks.AddViewHook(&a);
    hooks.AddViewHook(&claimer);

    EditorViewContext view;
    view.hovered = true;
    hooks.RunViewInput(view);
    EXPECT_EQ((Log{ "input A blocked", "input C blocked" }), log);
}

TEST(EditorFeatureHooksTest, NoHoverMeansNoFreePointer)
{
    Log log;
    FakeViewHook a("A", log);
    EditorFeatureHooks hooks;
    hooks.AddViewHook(&a);
    hooks.RunViewInput(EditorViewContext{});
    EXPECT_EQ((Log{ "input A blocked" }), log);
}

TEST(EditorFeatureHooksTest, ARemovedHookIsNotCalledAndSceneKindIsPassedThrough)
{
    Log log;
    FakeViewHook a("A", log);
    FakeViewHook b("B", log);
    EditorFeatureHooks hooks;
    hooks.AddViewHook(&a);
    hooks.AddViewHook(&b);
    hooks.RemoveViewHook(&a);
    EditorViewContext view;
    view.kind = EditorViewKind::Scene;
    hooks.RunViewOverlay(view);
    EXPECT_EQ((Log{ "overlay B scene" }), log);
}

TEST(EditorFeatureHooksTest, AQueuedMenuActionRunsOnceLaterAndReportsItsResult)
{
    int runs = 0;
    EditorFeatureHooks hooks;
    hooks.AddMenuItem({ "File", "Open Thing", {}, [&runs] {
        ++runs;
        return EditorActionResult{ true, "opened" };
    } });
    ASSERT_EQ(hooks.MenuItems().size(), 1u);
    EXPECT_TRUE(hooks.MenuItemEnabled(0));

    hooks.QueueMenuAction(0);
    EXPECT_EQ(runs, 0); // The click only queues.
    EXPECT_FALSE(hooks.TakeMenuStatus().fresh);

    hooks.RunQueuedMenuActions();
    EXPECT_EQ(runs, 1);
    const EditorMenuStatus status = hooks.TakeMenuStatus();
    EXPECT_TRUE(status.fresh);
    EXPECT_FALSE(status.isError);
    EXPECT_EQ(status.message, "opened");
    EXPECT_FALSE(hooks.TakeMenuStatus().fresh); // Shown once.

    hooks.RunQueuedMenuActions();
    EXPECT_EQ(runs, 1);
}

TEST(EditorFeatureHooksTest, MenuEnabledCallbackAndBadIndexAreRespected)
{
    bool enabled = false;
    EditorFeatureHooks hooks;
    hooks.AddMenuItem({ "File", "Close Thing", [&enabled] { return enabled; }, [] { return EditorActionResult{}; } });
    hooks.AddMenuItem({ "File", "No action", {}, {} }); // An item without an action is refused.
    EXPECT_EQ(hooks.MenuItems().size(), 1u);
    EXPECT_FALSE(hooks.MenuItemEnabled(0));
    enabled = true;
    EXPECT_TRUE(hooks.MenuItemEnabled(0));
    EXPECT_FALSE(hooks.MenuItemEnabled(7));
    hooks.QueueMenuAction(7); // Out of range: ignored.
    hooks.RunQueuedMenuActions();
    EXPECT_FALSE(hooks.TakeMenuStatus().fresh);
}

TEST(EditorFeatureHooksTest, AThrowingMenuActionBecomesAnErrorStatusAndLaterActionsStillRun)
{
    int later = 0;
    EditorFeatureHooks hooks;
    hooks.AddMenuItem({ "File", "Bad", {}, []() -> EditorActionResult { throw std::runtime_error("boom"); } });
    hooks.AddMenuItem({ "File", "Good", {}, [&later] {
        ++later;
        return EditorActionResult{ false, "refused" };
    } });
    hooks.QueueMenuAction(0);
    hooks.QueueMenuAction(1);
    hooks.RunQueuedMenuActions();
    EXPECT_EQ(later, 1);
    const EditorMenuStatus status = hooks.TakeMenuStatus(); // The last result wins.
    EXPECT_TRUE(status.isError);
    EXPECT_EQ(status.message, "refused");

    hooks.QueueMenuAction(0);
    hooks.RunQueuedMenuActions();
    const EditorMenuStatus failed = hooks.TakeMenuStatus();
    EXPECT_TRUE(failed.isError);
    EXPECT_NE(failed.message.find("boom"), std::string::npos);
}

TEST(EditorFeatureHooksTest, AMenuActionMayAddItemsWhileRunning)
{
    EditorFeatureHooks hooks;
    hooks.AddMenuItem({ "File", "Grow", {}, [&hooks] {
        for (int i = 0; i < 32; ++i) {
            hooks.AddMenuItem({ "File", "Extra", {}, [] { return EditorActionResult{}; } });
        }
        return EditorActionResult{ true, "grown" };
    } });
    hooks.QueueMenuAction(0);
    hooks.RunQueuedMenuActions();
    EXPECT_EQ(hooks.MenuItems().size(), 33u);
    EXPECT_EQ(hooks.TakeMenuStatus().message, "grown");
}

class HookModule final : public IEditorFeatureModule, private IEditorViewHook {
public:
    explicit HookModule(Log& log) : m_log(log) {}
    const char* ModuleName() const override { return "HookModule"; }
    void OnAttach(const EditorModuleAttach& attach) override
    {
        if (attach.hooks != nullptr) {
            attach.hooks->AddViewHook(this);
            attach.hooks->AddMenuItem({ "File", "From module", {}, [] { return EditorActionResult{ true, "ok" }; } });
        }
    }
    void OnViewOverlay(const EditorViewContext&) override { m_log.push_back("module overlay"); }

private:
    Log& m_log;
};

TEST(EditorFeatureHooksTest, AModuleReachesTheHooksThroughTheAttachContext)
{
    Log log;
    std::vector<std::unique_ptr<IEngineFeatureModule>> modules;
    modules.push_back(std::make_unique<HookModule>(log));
    EditorExtensions extensions;
    EditorModuleSet set;
    set.Attach(modules, &extensions);

    EditorFeatureHooks& hooks = extensions.Get<EditorFeatureHooks>();
    EXPECT_EQ(hooks.MenuItems().size(), 1u);
    hooks.RunViewOverlay(EditorViewContext{});
    EXPECT_EQ((Log{ "module overlay" }), log);
}

TEST(EditorFeatureHooksTest, WithoutExtensionsTheModuleGetsNoHooks)
{
    Log log;
    std::vector<std::unique_ptr<IEngineFeatureModule>> modules;
    modules.push_back(std::make_unique<HookModule>(log));
    EditorModuleSet set;
    set.Attach(modules, nullptr);
    EXPECT_TRUE(log.empty());
}

} // namespace
} // namespace gte
