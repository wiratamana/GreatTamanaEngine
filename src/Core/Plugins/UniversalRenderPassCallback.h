#pragma once

// Types behind Core::RegisterRenderPass(), the one way to register a built-in render pass.
// Passes that must write straight into the shared view target use Core::RegisterProjectRenderPassProvider()
// instead; RegisterViewContentPassName()/RegisterFinalizeForSamplingHook() register no pass at all.

#include "ProjectRenderFeatureCallback.h"
#include "RenderFeatureDescriptor.h"

namespace gte {

// Mirrors RenderFeatureStage. BeforeEverything (Core's internal clear pass) and AfterEverything
// (required on PostComposite/PreUI passes) are deliberately not choosable here.
enum class FrameEvent {
    PreOpaque,       // Before any opaque geometry is drawn.
    PostOpaque,      // Immediately after opaque geometry is drawn.
    PostTransparent, // Immediately after transparent geometry is drawn.
    PostComposite,   // Near the end of the frame - gets a private target, auto-blended onto the screen.
    PreUI,           // Same contract as PostComposite, runs immediately after it, same frame.
};

// One callback shape for every FrameEvent. Before PostComposite, fields that do not exist yet are placeholders:
// invalid `privateTarget`, empty `extent`/`cameraData`, and (PreOpaque only) invalid `currentViewHandles`.
using UniversalRenderPassAction = ProjectRenderFeatureCallback;

} // namespace gte
