// Unit tests for the editor-core-separation-21 campaign's PHASE2
// (task_manager/editor-core-separation-21/
// PHASE2_FIX_AERIAL_PERSPECTIVE_COMPOSITE_TOGGLE_LIE.md) new, pure
// "did the toggle registry's own enabled states actually change" comparison
// (src/Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h). Entirely
// Tier-1 - no Vulkan device, no live RenderPassToggleRegistry instance
// involved at all, mirroring AtmospherePassToggleLogicTests.cpp's own
// precedent.

#include "Renderer/RenderGraph/RenderPassToggleChangeDetectionLogic.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

RenderPassToggleState MakeState(const std::string& name, bool enabled, bool everDeclared = true)
{
    RenderPassToggleState state;
    state.name = name;
    state.enabled = enabled;
    state.everDeclaredThisSession = everDeclared;
    return state;
}

TEST(RenderPassToggleChangeDetectionLogicTest, ReturnsFalseForTwoIdenticalEmptySnapshots)
{
    EXPECT_FALSE(DidRenderPassToggleEnabledStatesChange({}, {}));
}

TEST(RenderPassToggleChangeDetectionLogicTest, ReturnsFalseWhenNothingChanged)
{
    const std::vector<RenderPassToggleState> before = {
        MakeState("AtmosphereAerialPerspectiveCompositePass", true),
        MakeState("RenderOpaque", true),
    };
    const std::vector<RenderPassToggleState> after = {
        MakeState("AtmosphereAerialPerspectiveCompositePass", true),
        MakeState("RenderOpaque", true),
    };

    EXPECT_FALSE(DidRenderPassToggleEnabledStatesChange(before, after));
}

TEST(RenderPassToggleChangeDetectionLogicTest, ReturnsTrueWhenOnePassWasDisabled)
{
    const std::vector<RenderPassToggleState> before = {
        MakeState("AtmosphereAerialPerspectiveCompositePass", true),
        MakeState("RenderOpaque", true),
    };
    const std::vector<RenderPassToggleState> after = {
        MakeState("AtmosphereAerialPerspectiveCompositePass", false), // Toggled off.
        MakeState("RenderOpaque", true),
    };

    EXPECT_TRUE(DidRenderPassToggleEnabledStatesChange(before, after));
}

TEST(RenderPassToggleChangeDetectionLogicTest, ReturnsTrueWhenAPassWasNewlyRegistered)
{
    const std::vector<RenderPassToggleState> before = {
        MakeState("RenderOpaque", true),
    };
    const std::vector<RenderPassToggleState> after = {
        MakeState("RenderOpaque", true),
        MakeState("SomeFuturePass", false),
    };

    EXPECT_TRUE(DidRenderPassToggleEnabledStatesChange(before, after));
}

TEST(RenderPassToggleChangeDetectionLogicTest, IgnoresEverDeclaredThisSessionOnItsOwn)
{
    // A pass simply running for the FIRST time this session (everDeclaredThisSession
    // flipping false -> true) with its own `enabled` value completely unchanged must
    // NOT, by itself, be reported as a change - see this header's own doc comment for
    // why (it is not a reason to force a fresh Frame Debugger capture).
    const std::vector<RenderPassToggleState> before = {
        MakeState("SomeFuturePass", false, /*everDeclared=*/false),
    };
    const std::vector<RenderPassToggleState> after = {
        MakeState("SomeFuturePass", false, /*everDeclared=*/true),
    };

    EXPECT_FALSE(DidRenderPassToggleEnabledStatesChange(before, after));
}

} // namespace
} // namespace gte::rg
