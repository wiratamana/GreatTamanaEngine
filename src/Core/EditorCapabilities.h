#pragma once

// editor-core-separation-1 campaign, PHASE5
// (PHASE5_EDITOR_CAPABILITY_INTERFACES_DESIGN.md) - Bucket B capability
// interfaces (design doc Section 2.6/7.1 Step 3, PHASE0_MASTER_STRATEGY.md's
// Locked Design Decision #2): MANY small, single-purpose interfaces, one per
// GENUINELY NEW "is the Editor's capability actually available" gap - never
// one big "god interface". Each interface here mirrors the already-proven,
// ALREADY-DESIGNED FrameDebuggerCaptureContext*/IEditorLayer* opaque-pointer
// pattern (Locked Design Decision #8) exactly: gte_core-destined code holds
// only a nullable pointer to the abstract interface (never the real
// gte_editor-owned implementation), asks a plain runtime null-check instead
// of a compile-time `#if GTE_ENABLE_EDITOR`, and a future Player host that
// never registers a capability gets a safe, uniform "not available" answer
// for free, with zero macro anywhere.
//
// Deliberately does NOT touch or duplicate IEditorLayer/
// FrameDebuggerCaptureContext* themselves - those are two ALREADY-DESIGNED,
// pre-existing hooks with their own dedicated phases (Phase 2, Phases 12-13)
// per Locked Design Decision #8, not new capability gaps this file declares
// interfaces for.
//
// ONLY ONE interface lives here as of PHASE5, not four, despite this
// campaign's own PHASE5 strategy file sketching four candidates (scene IO,
// Editor UI commands, asset import, GPU-driven-batch test spawning). Reading
// the real, current code (not guessing) found that THREE of those four
// candidate gaps are NOT genuinely new capability gaps at all - their real
// CORE-side call site (Application::Run(), Application.cpp) already routes
// unconditionally through the pre-existing, already-designed IEditorLayer*
// pointer, which already has a complete, correct "not available in this
// build" answer via NullEditorLayer.cpp - see PHASE5_COMPLETION_REPORT.md's
// own "Findings" section for the full, evidence-based reasoning behind each
// of the three:
//   - EditorUiCommandBridge.h (ActivateTab) -> IEditorLayer::ActivateTab()
//   - AssetImportCommandBridge.h -> IEditorLayer::ImportExternalAssetIntoProject()
//   - GpuDrivenBatchTestSpawner.h -> IEditorLayer::SpawnGpuDrivenTestBatch()
// Per Locked Design Decision #2's own text ("This rule governs ONLY the
// genuinely NEW Bucket B capability gaps"), a capability question that is
// already fully answered by the existing, out-of-scope IEditorLayer* hook
// gets no new interface here - inventing a duplicate mechanism for a
// question IEditorLayer already answers would itself violate Locked Design
// Decision #8's "never redesign/fragment IEditorLayer" rule by creating a
// second, parallel way to ask the exact same thing.
// Game.h/Game.cpp were also re-checked directly (search_in_dir for
// "GTE_ENABLE_EDITOR"/"not available" scoped to src/Game/) and confirmed to
// carry ZERO remaining macro or fallback-message content at all - nothing to
// design there either.

#include <filesystem>
#include <string>

namespace gte {

class Game;
class Renderer;

// Answers "can the current build save/load scenes via the Editor, and
// perform the operation if so". The one real, genuinely-new Bucket B gap
// this phase found: src/Application/EngineCommandDispatch.cpp's
// EngineCommandKind::SaveScene/LoadScene handling still answers this
// question with a compile-time `#if GTE_ENABLE_EDITOR` guarding a direct
// call into src/Editor/SceneIO.h's free SaveScene()/LoadScene() functions -
// converting that call site to a runtime null-check against this interface
// is PHASE6's job
// (PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md), not this
// phase's.
//
// src/Network/NetworkServer.cpp's POST /save_scene//load_scene route
// handlers never need to see this interface at all - they only ever read
// the plain SaveSceneOutcome::editorAvailable/errorMessage (and the LoadScene
// sibling) fields EngineCommandDispatch.cpp already produces via
// EngineCommandBridge, so nothing about their own shape changes here.
//
// Signatures mirror src/Editor/SceneIO.h's real
// `bool SaveScene(Game&, const std::filesystem::path&)`/
// `bool LoadScene(Game&, Renderer&, const std::filesystem::path&)` overloads
// exactly (confirmed by reading that header in full before writing this) -
// `outErrorMessage` is populated only when the call returns `false`,
// mirroring EngineCommandDispatch.cpp's own existing "errorMessage explains
// why" convention already used for every other Outcome-style result in this
// engine (see Game/EngineCommandResults.h).
class ISceneIOCapability {
public:
    virtual ~ISceneIOCapability() = default;

    // Returns true on success (the file was written); `scenePath`'s parent
    // directory is created first if missing, mirroring SceneIO.h's real
    // SaveScene() behavior. Returns false (writing nothing further beyond
    // whatever SceneIO.h's real implementation already guarantees) and sets
    // `outErrorMessage` on any I/O failure.
    virtual bool SaveScene(Game& game, const std::filesystem::path& scenePath, std::string& outErrorMessage) = 0;

    // Returns true on success (game's current scene was fully replaced by
    // `scenePath`'s contents). Returns false (game's scene left completely
    // untouched, mirroring SceneIO.h's real LoadScene() "never partially
    // clears the current scene on failure" guarantee) and sets
    // `outErrorMessage` when `scenePath` doesn't exist or fails to parse.
    virtual bool LoadScene(
        Game& game, Renderer& renderer, const std::filesystem::path& scenePath, std::string& outErrorMessage) = 0;
};

} // namespace gte
