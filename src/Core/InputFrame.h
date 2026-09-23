#pragma once

namespace gte {

class InputState;

// gte_core's own public-contract input struct (design doc Section 5.2).
// PHASE12 (Core Class Skeleton and Construction) left this deliberately
// empty. PHASE13 (Core Frame Orchestration Extraction) now extends it with
// EXACTLY the fields Core::Update() needs to do real work - per that phase's
// own explicitly-sanctioned "extend InputFrame, or add a small separate
// parameter" choice (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md, Step
// 3.2) - rather than growing Update()'s own frozen 2-parameter signature
// (design doc Section 5.2: `void Update(const InputFrame& input, float
// deltaTime);`).
struct InputFrame {
    // The real, continuous-polling input state Application/EditorHost
    // already builds up over this frame's own SDL event loop
    // (InputState::BeginFrame()/Apply() - see Application::Run()'s own SDL
    // polling loop, which stays a host-level concern, never moving into
    // Core). A non-owning pointer - Application/EditorHost keeps owning the
    // real InputState instance; Core::Update() just reads it for this one
    // call, forwarding it straight into Game::Update(). nullptr is never
    // expected in practice (a host always builds one, every frame), but
    // degrades gracefully (Game::Update() simply isn't called that frame)
    // rather than dereferencing a null pointer, mirroring this codebase's
    // other nullable-hook conventions (e.g. IEditorLayer* itself).
    const InputState* inputState = nullptr;

    // frame-debugger-1 campaign's own Pause/Step semantics (AGENTS.md, "Time
    // and Playback Pause"). PHASE0_MASTER_STRATEGY.md's Locked Design
    // Decision #8 puts IEditorLayer::IsPlaybackPaused()/
    // TryConsumeStepRequest()/NotifyFrameDebuggerStepConsumed() in the
    // HOST-LEVEL bucket (never called through Core's own m_editorLayer
    // hook) - Application/EditorHost resolves these directly against its
    // own m_editorLayer BEFORE calling Core::Update(), and threads the
    // already-resolved booleans in here as plain data. Core::Update() never
    // reaches through its own m_editorLayer hook for playback-pause
    // concerns at all.
    bool playbackPaused = false;
    bool stepRequested = false;
};

} // namespace gte
