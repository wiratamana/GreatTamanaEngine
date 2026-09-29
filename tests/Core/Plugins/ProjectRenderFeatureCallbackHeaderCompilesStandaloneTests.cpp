// editor-core-separation-23 campaign, PHASE1
// (PHASE1_PROJECT_RENDER_FEATURE_CALLBACK_HEADER_AND_ENTRY_THIRD_KIND.md,
// Step 3.2) - a deliberately TRIVIAL standalone-compile proof: this file
// #includes ONLY Core/Plugins/ProjectRenderFeatureCallback.h and nothing
// else (besides <gtest/gtest.h>), confirming the new header compiles as its
// OWN translation unit with no other engine header pulled in first - zero
// circular dependency back into Core.h/RenderFeatureCompositor.h.
#include "../../../src/Core/Plugins/ProjectRenderFeatureCallback.h"

#include <gtest/gtest.h>

// A default-constructed gte::ProjectRenderFeatureCallback is an empty
// std::function - operator bool() == false. This is the exact contract
// RenderFeatureCompositor::Entry::projectCallback (PHASE1) and PHASE2's own
// slot-pool resolution logic depend on: an Entry constructed for a
// gte_plugin_abi plugin (moduleV2/moduleV3 set) never accidentally looks
// like it also carries a project callback.
TEST(ProjectRenderFeatureCallbackHeaderCompilesStandaloneTests, DefaultConstructedCallbackIsEmpty)
{
    gte::ProjectRenderFeatureCallback callback;
    EXPECT_FALSE(static_cast<bool>(callback));
}
