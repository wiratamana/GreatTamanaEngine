#pragma once

namespace gte {

// gte_core's own host-agnostic per-frame input snapshot (design doc, Section
// 5.2: "Core never sees SDL_Event. Whoever owns the main loop... translates
// raw platform input into this engine-owned type itself... and calls
// core.Update(inputFrame, dt)"). Deliberately a placeholder/empty shape as of
// PHASE12 (editor-core-separation-1 campaign, Core Class Skeleton and
// Construction) - this phase only needs the TYPE to exist so
// Core::Update()'s signature compiles; it does not yet carry any real
// button/axis/mouse-delta data, and nothing constructs a real one yet
// (Core::Update() itself is still an empty PHASE13 stub - see Core.h).
// PHASE13 (Core Frame Orchestration Extraction) is what actually threads a
// real, populated value through here, built from the same InputState/
// EventTranslator machinery Application::Run() already drives today.
struct InputFrame {
};

} // namespace gte
