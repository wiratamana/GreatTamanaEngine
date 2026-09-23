#include "Core.h"

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE gte_core
// .cpp file allowed to #include the real src/Editor/EditorLayer.h header
// (Core.h itself only ever forward-declares IEditorLayer). Harmless to
// include a phase early, even before PHASE13 adds the first real call
// through m_editorLayer - this phase (PHASE12) only needs it so a future
// PHASE13 diff touches Core.cpp's includes exactly once, not twice.
#include "../Editor/EditorLayer.h"

namespace gte {

Core::Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices)
    : m_renderer(surfaceProvider)
    , m_renderGraph(m_renderer)
    , m_game()
    , m_engineContext()
{
    // editor-core-separation-1 campaign, PHASE12 - hostServices is accepted
    // (satisfying Core's own frozen public contract, design doc Section
    // 5.2) but not yet stored/used anywhere - no phase before PHASE13/16
    // needs Core to actually report anything through it. Silencing the
    // "unused parameter" case explicitly rather than leaving it
    // unreferenced.
    (void)hostServices;
}

void Core::Update(const InputFrame& /*input*/, float /*deltaTime*/)
{
    // PHASE13 stub (Core Frame Orchestration Extraction) - deliberately
    // empty. Application::Run() still owns and drives the real per-frame
    // orchestration body today; this phase (PHASE12) must cause ZERO
    // runtime behavior change, so nothing calls this method yet.
}

void Core::BuildFrame()
{
    // PHASE13 stub - see Update()'s own doc comment above.
}

void Core::Present()
{
    // PHASE13 stub - see Update()'s own doc comment above.
}

} // namespace gte
