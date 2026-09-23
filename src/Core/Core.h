#pragma once

#include "EngineContext.h"
#include "IHostServices.h"
#include "InputFrame.h"
#include "ISurfaceProvider.h"
#include "../Game/Game.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"

namespace gte {

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE documented
// exception to "gte_core never includes anything under src/Editor/":
// Core.h forward-declares IEditorLayer ONLY (this class holds nothing but a
// bare, nullable pointer to it) - Core.cpp is the one place that
// #includes the real src/Editor/EditorLayer.h header, mirroring
// src/Game/RenderSystem.h's own pre-existing FrameDebuggerCaptureContext*
// forward-declaration precedent exactly.
class IEditorLayer;

// Placeholder shape (editor-core-separation-1 campaign, PHASE12) - Core's own
// public contract (design doc Section 5.2) commits to exposing frame
// statistics via GetFrameStats(), but no phase in this 19-phase campaign
// actually wires real Profiling::FrameProfiler/DrawStats data into it -
// Application (and, later, EditorHost) keep talking to
// Profiling::FrameProfiler::Instance() directly, exactly as they do today.
// Left deliberately empty until a real, future need drives its actual
// shape, rather than guessing fields nothing populates or consumes yet.
struct FrameStats {
};

// gte_core's own public facade (design doc, Section 5; PHASE0's Locked
// Design Decision #8's own EXTENDED contract). Constructed by injecting an
// ISurfaceProvider& (the host's own window/surface abstraction) and an
// IHostServices& (the host's own logging/diagnostics hook) - Core itself
// never sees SDL, ImGui, or any concrete Editor type by name; the ONE
// exception is the nullable IEditorLayer* hook above, consulted only
// through a forward-declared pointer, never a concrete Editor include.
//
// editor-core-separation-1 campaign, PHASE12 (Core Class Skeleton and
// Construction) builds ONLY this skeleton: members, the constructor, plain
// forwarding accessors, and the SetEditorLayerHook() setter.
// Update()/BuildFrame()/Present() are deliberately empty stubs pending
// PHASE13 (Core Frame Orchestration Extraction), which physically moves
// Application::Run()'s real per-frame body here - this phase must cause
// ZERO runtime behavior change, so nothing calls these three methods yet,
// and nothing calls through m_editorLayer yet either.
//
// DEVIATION FROM THE DESIGN DOC'S OWN SECTION 2.1 INVENTORY (documented,
// evidence-based - see PHASE12_COMPLETION_REPORT.md for the full reasoning):
// the design doc's own ownership graph also lists "two rg::RenderPipeline
// instances" as Core-owned. Core's own FROZEN public contract (Section 5.2)
// exposes no accessor for either RenderPipeline instance at all, and its
// ONLY real consumers today (Application::RegisterOffscreenRenderPipelineProviders()/
// RegisterPresentRenderPipelineProvider()/Run()) do not move into Core until
// PHASE13 - moving the two RenderPipeline instances here now would leave
// Application with literally no way to reach them. They stay
// Application-owned members until PHASE13 moves them together with their
// only real consumer, the per-frame orchestration logic itself.
class Core {
public:
    Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices);

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(Core&&) = delete;

    // PHASE13 stubs - see that phase's own file for the real body this
    // campaign eventually gives them (Application::Run()'s own per-frame
    // orchestration, physically relocated here). Deliberately empty no-ops
    // today - nothing in this campaign calls any of these three yet;
    // Application::Run() keeps its own, completely unmodified body/behavior
    // until PHASE13.
    void Update(const InputFrame& input, float deltaTime);
    void BuildFrame();
    void Present();

    Renderer& GetRenderer() noexcept { return m_renderer; }
    Registry& GetRegistry() noexcept { return m_game.GetRegistry(); }
    Game& GetGame() noexcept { return m_game; }
    rg::RenderGraph& GetRenderGraph() noexcept { return m_renderGraph; }
    EngineContext& GetEngineContext() noexcept { return m_engineContext; }
    Time& GetTime() noexcept { return m_engineContext.time; }
    const FrameStats& GetFrameStats() const noexcept { return m_frameStats; }

    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE
    // nullable, "big" opaque hook mirroring FrameDebuggerCaptureContext*'s
    // own already-proven small-scale opaque-pointer pattern (never a new
    // Bucket-B-style micro interface, and never split into smaller pieces -
    // see that Locked Design Decision's full text). EditorHost (gte_editor)
    // supplies the real ImGuiEditorLayer instance after constructing it
    // (PHASE15); a Player host never calls this, leaving it nullptr
    // forever. Core::BuildFrame() (PHASE13) calls through this pointer,
    // ALWAYS null-checked, for ONLY the render-graph-frame-building subset
    // of IEditorLayer's methods PHASE0 enumerates - never for UI-building/
    // input-routing methods, which stay a host-level
    // (Application/EditorHost) concern and never reach Core at all.
    void SetEditorLayerHook(IEditorLayer* editorLayer) noexcept { m_editorLayer = editorLayer; }

private:
    // Declared first - independent of every other member below, and not
    // itself part of the ctor initializer-list ordering concern the real
    // owned members are (it uses its own default member initializer,
    // nullptr).
    IEditorLayer* m_editorLayer = nullptr;

    // Ownership relocated here from Application (editor-core-separation-1
    // campaign, PHASE12) - see design doc Section 2.1's ownership graph and
    // PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md's own Step 3.
    // Declaration order matters (matches real constructor-initializer-list
    // order): m_renderer needs `surfaceProvider` (the constructor
    // parameter) only; m_renderGraph needs m_renderer already constructed;
    // m_game/m_engineContext have no dependency on either.
    Renderer m_renderer;
    rg::RenderGraph m_renderGraph;
    Game m_game;
    EngineContext m_engineContext;

    // See FrameStats's own doc comment above - a placeholder, never
    // written to by anything in this campaign.
    FrameStats m_frameStats;
};

} // namespace gte
