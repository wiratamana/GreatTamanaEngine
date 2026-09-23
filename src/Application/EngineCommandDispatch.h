#pragma once

// network-impl-3 campaign, PHASE4 - the thin, MAIN-THREAD-ONLY dispatcher
// Application::Run() calls once per frame - the one place that actually
// unpacks an EngineCommandRequest and calls into Game's Phase-3 methods.
// Kept as its OWN small file (not inlined into Application.cpp) mirroring
// src/Application/RenderPasses.h/.cpp's own precedent of extracting Run()-
// helper free functions into a sibling file under src/Application/, so
// Application.cpp itself stays a thin orchestration script.

#include "EngineCommandBridge.h"
#include "../Core/EditorCapabilities.h" // PHASE6 of editor-core-separation-1 - ISceneIOCapability*, forward-declared-friendly (see below).

namespace gte {

class Game;
class Renderer;

// Executes ONE already-pending EngineCommandRequest against `game`/`renderer`
// (both must be the SAME live instances Application itself owns - this
// function is ONLY ever called from Application::Run(), on the main thread)
// and returns the completed EngineCommandResult, ready to hand to
// EngineCommandBridge::FulfillCommand(). Never throws - Game::
// InstantiatePrimitive()/DeleteEntityByName() (Phase 3) already degrade
// every failure mode into a plain success == false outcome, so this function
// has nothing further to catch.
//
// `sceneIOCapability` - editor-core-separation-1 campaign, PHASE6
// (PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md) - the nullable
// Bucket B capability hook (Core/EditorCapabilities.h, PHASE5) this function
// consults, at RUNTIME, for EngineCommandKind::SaveScene/LoadScene handling,
// replacing the compile-time `#if GTE_ENABLE_EDITOR` this function used to
// branch on. nullptr means "this build/host never registered a scene IO
// capability" (a future Player host, or today's GTE_ENABLE_EDITOR=OFF
// configuration) - the exact same "not available" fallback message/behavior
// as before is preserved for that case.
EngineCommandResult ExecuteEngineCommand(
    Game& game, Renderer& renderer, ISceneIOCapability* sceneIOCapability, const EngineCommandRequest& request);

} // namespace gte
