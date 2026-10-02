// A deliberately TRIVIAL standalone-compile proof: this file #includes
// ONLY Core/Plugins/ProjectScenePassCallback.h and nothing else (besides
// <gtest/gtest.h>), confirming the new header compiles as its own
// translation unit with no other engine header pulled in first - zero
// circular dependency back into Core.h/RenderFeatureCompositor.h.
#include "../../../src/Core/Plugins/ProjectScenePassCallback.h"

#include <gtest/gtest.h>

// A default-constructed gte::ScenePassReadHandles carries null handles and
// null Vulkan objects - the exact contract a PostOpaque/PostTransparent
// provider must overwrite before handing it to a registered callback.
TEST(ProjectScenePassCallbackHeaderCompilesStandaloneTests, DefaultConstructedReadHandlesAreNull)
{
    gte::ScenePassReadHandles handles;
    EXPECT_EQ(handles.colorSampler, VK_NULL_HANDLE);
    EXPECT_EQ(handles.depthImageView, VK_NULL_HANDLE);
    EXPECT_EQ(handles.depthSampler, VK_NULL_HANDLE);
}

// A lambda matching ProjectScenePassCallback's signature assigns cleanly -
// proves the type is usable as a real std::function target.
TEST(ProjectScenePassCallbackHeaderCompilesStandaloneTests, CallbackAcceptsAMatchingLambda)
{
    gte::ProjectScenePassCallback callback = [](gte::rg::RenderGraphBuilder&, gte::rg::RenderPassBlackboard&,
                                                  gte::rg::RenderViewId, const gte::ScenePassReadHandles&) {};
    EXPECT_TRUE(static_cast<bool>(callback));
}
