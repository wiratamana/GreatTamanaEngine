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

#include "Logging.h" // editor-core-separation-2 campaign, PHASE3 - LogEntry/LogQueryFilter for ILogQueryCapability below.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

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

    // editor-core-separation-1 campaign, PHASE6
    // (PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md) - added
    // during PHASE6's own implementation, not originally sketched by PHASE5:
    // EngineCommandDispatch.cpp's SaveScene/LoadScene handling needs to
    // resolve a caller-supplied EMPTY path to the engine's one hardcoded
    // default scene location - previously done by calling src/Editor/SceneIO.h's
    // own free `DefaultScenePath()` function directly. That function's real
    // implementation depends on Editor/ProjectRootPath.h (an
    // SDL_GetBasePath()-based, genuinely Editor-only helper) - a second,
    // separate reason (beyond SaveScene()/LoadScene() themselves) this
    // Core-destined dispatcher can no longer call into src/Editor/ directly.
    // Mirrors SceneIO.h's real `std::filesystem::path DefaultScenePath()`
    // signature exactly.
    virtual std::filesystem::path DefaultScenePath() const = 0;
};

// editor-core-separation-2 campaign, PHASE3 - closes the real, pre-existing
// gte_core -> gte_editor-only-symbol dependency editor-core-separation-1
// left open (NetworkServer.cpp calling Logger::Query()/Clear()/EntryCount()/
// IsEnabled()/LatestEntryId() directly - see that campaign's own
// CAMPAIGN_COMPLETION_REPORT.md, "What remains genuinely open", option (b)).
// Mirrors ISceneIOCapability exactly: gte_core-tier code (NetworkServer.cpp)
// holds only a nullable pointer to this interface, asking a plain runtime
// null-check instead of a compile-time #if - a future Player host that
// never registers a real implementation gets a safe "logging unavailable"
// answer for free.
//
// `IsEnabled()` mirrors Logger::IsEnabled()'s own real, current signature
// (a `static constexpr bool` returning `true` unconditionally today) - a
// `nullptr` ILogQueryCapability* at the NetworkServer.cpp call site is what
// represents "this build has no logger at all", NOT a `false` return from
// this method; the two must never be conflated.
class ILogQueryCapability {
public:
    virtual ~ILogQueryCapability() = default;

    virtual std::vector<LogEntry> Query(const LogQueryFilter& filter) = 0;
    virtual void Clear() = 0;
    virtual std::size_t EntryCount() const = 0;
    virtual bool IsEnabled() const = 0;
    virtual std::uint64_t LatestEntryId() const = 0;
};

// editor-core-separation-12 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 1) - answers "what is the live, ground-truth state of the
// Project Assembly Hot Reload system RIGHT NOW", and lets an external HTTP
// caller (gte_send_request) trigger a bare compile, or (once a future
// BIG-STEP 3 campaign fills in the real body) a full compile+reload cycle.
// Mirrors ISceneIOCapability/ILogQueryCapability's own "gte_core-tier
// NetworkServer.cpp holds only a nullable pointer" contract exactly -
// nullptr means every route backed by this interface answers 503, never
// crashes.
//
// DEVIATION from this feature's own external design doc
// (HOTRELOAD_BIGSTEP_01_LIVE_DEBUG_AND_COMPILE_RELOAD_TRIGGERS_2026-09-28.txt,
// Section 3): that doc sketches a plain, no-argument
// `virtual std::string GetSceneSnapshotJson() const = 0;`, implying a
// lock-free, direct network-thread read of the live ECS Registry. This is
// UNSAFE - the Registry is mutated every frame by the main thread with no
// synchronization of its own (see AGENTS.md, "Entity-Component-System").
// `BuildSceneSnapshotJson(Game&)` below is instead called ONLY from the
// main thread (via a new EngineCommandBridge command - see
// Application/EngineCommandDispatch.cpp), exactly mirroring how
// ISceneIOCapability::SaveScene/LoadScene are already, correctly, only ever
// called with a live Game& handed to them by that SAME main-thread-only
// dispatch path. See task_manager/editor-core-separation-12/
// PHASE0_MASTER_STRATEGY.md, Section 3.1, Correction 1, for the full
// reasoning.
class IHotReloadDebugCapability {
public:
    virtual ~IHotReloadDebugCapability() = default;

    // "Idle" whenever no cycle is currently running (always "Idle" until a
    // future BIG-STEP 3 campaign starts calling
    // ProjectAssemblyHotReloadDebugStatus::Set()/Finish()). lastOutcome/
    // lastErrorMessage describe the MOST RECENTLY COMPLETED cycle (persist
    // across the transition back to Idle) - both "" until the first cycle
    // ever completes.
    struct Status {
        std::string phase = "Idle";
        std::string projectName;
        std::uint64_t cycleId = 0;
        std::uint64_t phaseElapsedMilliseconds = 0;
        std::string lastOutcome;       // "" | "Success" | "RolledBack" | "CriticalFailure"
        std::string lastErrorMessage;  // "" unless lastOutcome needs explaining
    };
    virtual Status GetHotReloadStatus() const = 0;

    // Placeholder-shaped until a future BIG-STEP 2 campaign builds the real
    // ProjectAssemblyRegistrationLedger class - this campaign's own
    // implementation (PHASE2) always returns every list empty, never an
    // error.
    struct LedgerEntry {
        std::vector<std::string> renderPassNames;
        // editor-core-separation-23 campaign, PHASE4
        // (PHASE4_HOT_RELOAD_LEDGER_TEARDOWN_WIRING.md) - mirrors
        // ProjectAssemblyRegistrationLedger::Entry's own new field exactly
        // (same name, same position relative to renderPassNames).
        std::vector<std::string> renderFeatureNames;
        std::vector<std::string> panelNames;
        std::vector<std::string> componentTypeNames;
    };
    virtual LedgerEntry GetLedgerEntry(const std::string& projectName) const = 0;

    // Genuinely real, live, today - a thin wrapper over
    // ProjectAssemblyHost::GetLoadedAssemblyFileNames(), guarded by
    // GetHotReloadEngineStateMutex().
    virtual std::vector<std::string> GetLoadedAssemblyFileNames() const = 0;

    // Genuinely real, live, today - a thin wrapper over
    // ComponentTypeRegistry::Instance().AllSortedByTypeName().
    virtual std::vector<std::string> GetRegisteredComponentTypeNames() const = 0;

    // Genuinely real, live, today - builds the SAME generic SceneDocument
    // JSON File > Save Scene/POST /save_scene already produce, but returns
    // it in-memory (never written to disk). MUST be called only from the
    // main thread with a live Game& (see this interface's own header
    // comment above, and Application/EngineCommandDispatch.cpp's new
    // EngineCommandKind::GetSceneSnapshot case, PHASE2) - never called
    // directly from a NetworkServer.cpp route handler.
    virtual std::string BuildSceneSnapshotJson(Game& game) = 0;

    // Fire-and-forget - calls the EXISTING TriggerProjectAssemblyCompile()
    // directly (already backgrounded, already thread-safe against
    // concurrent triggers for the SAME project). Returns false only if the
    // in-flight guard rejects it (a build for this project is already
    // running) - never blocks waiting for the compile itself to finish;
    // the caller polls GET /get_logs to watch it happen.
    virtual bool TriggerCompileOnly(const std::string& projectName) = 0;

    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1. Thin wrapper over
    // Core/Plugins/ProjectAssemblyBuildRunner.h's own
    // IsProjectAssemblyBuildInFlight() - lets a UI surface show a
    // "(compiling...)" hint for the CURRENTLY ACTIVE project without
    // depending on ProjectAssemblyBuildRunner.h directly (mirrors this
    // whole interface's existing "gte_core-tier code never needs to see
    // the real implementation header" contract).
    virtual bool IsCompileInFlight(const std::string& projectName) const = 0;

    // THIS campaign's OWN implementation (PHASE2) is a permanent placeholder
    // for this whole campaign's lifetime - always returns false, does
    // nothing else. A future BIG-STEP 3 campaign replaces ONLY this
    // method's body - this interface's own signature never changes for
    // that.
    virtual bool TriggerHotReload(const std::string& projectName) = 0;

    // editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 4), PHASE4 - LDD-HR6 (PHASE0_MASTER_STRATEGY.md). Sets the
    // live "ProbeHotReloadMarker" component's own "value" field on whichever
    // entity currently carries it (see EditorHotReloadDebugCapability.cpp's
    // own real body for the exact, generic-reflection-based mechanism this
    // reuses) - returns false if no such entity currently exists (e.g. the
    // probe project is not currently loaded), or if the request could not be
    // serviced (see this method's own .cpp-side comment for the full list of
    // honest false-return reasons). DELIBERATELY NARROW: this method's own
    // hardcoded target ("ProbeHotReloadMarker"'s "value" field) can never be
    // repurposed to mutate any OTHER component/field - it exists SOLELY so
    // this campaign's own live verification test (PHASE5) can prove a
    // genuinely runtime-mutated custom-component value survives a hot
    // reload, not merely whatever a cold start would produce. This is NOT a
    // general "set any component field over HTTP" capability, and must never
    // be widened into one (see this same interface's own pre-existing
    // non-goals, editor-core-separation-12's PHASE0/BIG-STEP-1 file, Section
    // 2(f-g)'s closing paragraph).
    virtual bool SetProbeHotReloadMarkerValueForTesting(int value) = 0;
};

// editor-core-separation-16 campaign (On-Engine Project Workflow plan,
// BIG-STEP 2) - answers "can this build create a Project Assembly's
// on-disk source folder at all" - mirrors IHotReloadDebugCapability's own
// "gte_core-tier NetworkServer.cpp holds only a nullable pointer" contract.
// nullptr means every route backed by this interface answers 503.
class IProjectLifecycleCapability {
public:
    virtual ~IProjectLifecycleCapability() = default;

    struct CreateProjectOutcome {
        bool success = false;
        std::string errorMessage; // only meaningful when success == false
        std::string createdSourceDirectory; // absolute path, only meaningful when success == true
    };

    // Validates `name` (IsValidProjectAssemblyIdentifierName()), rejects a
    // name that already exists as a folder (or any file) under the
    // resolved Project Assembly source root, then writes the 3-file
    // scaffold and marks the result as the new ActiveProjectAssemblyState.
    // Pure filesystem + a synchronous `cmake` reconfigure child-process
    // call - safe to call from ANY thread (never touches live
    // Core/Registry/GPU state), so this method needs NO cross-thread
    // bridge, unlike a future hot-reload-shaped capability.
    virtual CreateProjectOutcome CreateNewProjectAssembly(const std::string& name) = 0;

    // editor-core-separation-17 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 3), PHASE3. Outcome of one "Open Project" attempt.
    struct OpenProjectOutcome {
        bool success = false;         // true even for Tier NotBuildable/NotCompiled (LDD-PW4) - false only for NotAProject or an internal error
        std::string errorMessage;     // only meaningful when success == false
        std::string statusMessage;    // human-readable outcome, ALWAYS populated when success == true
        bool loadAttempted = false;
        bool loadSucceeded = false;
    };

    // Struct used by ListProjectAssemblies() below - deliberately a plain,
    // dependency-free value type (no ProjectValidityTier leaked through this
    // public interface header would be fine too, since ProjectAssemblyBuildRunner.h
    // is already gte_core-tier and safe to include here - but a plain int/
    // string pair keeps this specific interface's own JSON-shaping trivial for
    // NetworkRoutes.cpp, mirroring this file's existing preference for plain
    // scalars in every outcome struct above).
    struct ProjectListEntry {
        std::string name;
        std::string tierName; // "NotAProject" / "NotBuildable" / "NotCompiled" / "Compiled" / "AlreadyLoaded"
    };

    // Callable from ANY thread. For a Tier 3 ("Compiled") folder, this method
    // BLOCKS the calling thread until the real load has been performed on the
    // main thread (via ProjectLifecycleLoadCommandBridge) - safe for a network
    // route handler (a genuinely separate OS thread), NEVER safe to call from
    // the engine's own main thread (see OpenProjectAssemblyOnMainThread() right
    // below for that caller instead - calling THIS method from the main thread
    // deadlocks the whole Editor, see EditorProjectLifecycleCapability.cpp's
    // own top-of-file comment for the full reasoning).
    virtual OpenProjectOutcome OpenProjectAssembly(const std::string& name) = 0;

    // Callable ONLY from the engine's own main thread (e.g. from inside
    // OpenProjectWindow::Build(), itself called from EditorHost::Run()'s own
    // per-frame ImGui build step). Identical outcome/business logic to
    // OpenProjectAssembly() above, but the Tier 3 real-load step is performed
    // DIRECTLY, inline, with NO cross-thread bridge/wait at all - there is no
    // thread to hop to, since the caller already IS the main thread. Mirrors
    // IEditorLayer::ImportExternalAssetIntoProject()'s own "one real function,
    // called directly by main-thread ImGui code" precedent.
    virtual OpenProjectOutcome OpenProjectAssemblyOnMainThread(const std::string& name) = 0;

    // Callable from ANY thread - a plain, read-only filesystem enumeration +
    // classification, touches no mutable engine state beyond a
    // GetHotReloadEngineStateMutex()-guarded read of the currently-loaded
    // assembly list (identical safety contract to ActiveProjectAssemblyState::
    // GetActive()). Enumerates every one-level-deep folder directly under the
    // resolved Project Assembly source root.
    virtual std::vector<ProjectListEntry> ListProjectAssemblies() = 0;
};

// editor-core-separation-18 campaign (On-Engine Project Workflow plan,
// BIG-STEP 4) - answers "can this build scaffold a new Render Pass/Compute
// Shader/Shader-Pair source file into the CURRENTLY ACTIVE Project
// Assembly's Assets/ folder". A deliberately SEPARATE interface from
// IProjectLifecycleCapability immediately above - scaffolding a file inside
// an already-active project is a genuinely different capability question
// from creating/opening the project itself (this file's own long-standing
// "one interface per genuinely new capability gap" convention). Mirrors
// every capability interface above: gte_core-tier code (NetworkServer.cpp)
// holds only a nullable pointer; nullptr means every route backed by this
// interface answers 503.
enum class AssetScaffoldKind { RenderPass, ComputeShader, ShaderPair };

class IAssetScaffoldingCapability {
public:
    virtual ~IAssetScaffoldingCapability() = default;

    struct ScaffoldOutcome {
        bool success = false;
        std::string errorMessage;              // meaningful only when success == false
        std::vector<std::string> createdFiles; // relative to Assets/, meaningful only when success == true
        std::string reminderMessage;           // "" for ShaderPair (no companion .cpp is generated for that kind)
    };

    // Callable from ANY thread - pure filesystem I/O against the CURRENT
    // ActiveProjectAssemblyState, plus (LDD-CA1, PHASE0_MASTER_STRATEGY.md)
    // deliberately NO CMake reconfigure call - gte_add_project()'s own
    // CONFIGURE_DEPENDS glob over Assets/*.cpp (and the *.vert/*.frag/*.comp
    // shader glob) already re-triggers CMake's configure step automatically
    // on the NEXT `cmake --build`, unlike CreateNewProjectAssembly()'s own
    // brand-new-FOLDER case. Never touches ActiveProjectAssemblyState's own
    // "which project is active" state - only reads it.
    virtual ScaffoldOutcome CreateAssetScaffold(AssetScaffoldKind kind, const std::string& name) = 0;
};

} // namespace gte
