#pragma once

#include "Time.h"

namespace gte {

// Small, explicit, extensible aggregate of "things the engine's own
// gameplay/simulation layer needs every frame" (see
// task_manager/frame-debugger-1/PHASE0_MASTER_STRATEGY.md, Locked Design
// Decision #3). Deliberately minimal today - just `time` - NOT a dumping
// ground for every engine subsystem: a future genuine need (e.g. a
// frame-debugger "currently inspected pass" handle) should be added here
// explicitly, one real, justified field at a time, exactly like
// src/Editor/EditorContext.h's own header comment already documents for
// that struct's fields.
//
// Owned by Application (Application::m_engineContext - see PHASE2/PHASE4),
// advanced exactly once per frame via `time.Advance(...)`, and passed down
// by const reference into Game::Update() - never copied around casually,
// and never mutated by anything except Application::Run() itself.
struct EngineContext {
    Time time;
};

} // namespace gte
