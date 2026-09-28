#pragma once

#include <filesystem>
#include <string>

#include "../../Scene/SceneDocument.h"

namespace gte {

class Core;
class Renderer;
class EditorHost;

// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE2 - the real snapshot value type. `document` is exactly
// what Scene/SceneBuilder.h's BuildSceneDocumentFromRegistry() already
// produces for the live world - the SAME value type Editor/SceneIO.cpp's own
// SaveScene() and EditorHotReloadDebugCapability::BuildSceneSnapshotJson()
// (GET /project_assembly/debug/scene_snapshot) both already build, from the
// SAME function. Captures every entity, every built-in AND
// Project-Assembly-defined custom reflected component - see
// PHASE0_MASTER_STRATEGY.md Section 2 for the full "why this is already
// almost the whole feature" reasoning. Deliberately does NOT capture
// anything outside the ECS Registry - see the permanent honest-boundary
// documentation this campaign's own PHASE5 adds to
// docs/conventions/project-assembly-system.md.
struct HotReloadStateSnapshot {
    SceneDocument document;
};

// HOOK POINT A - called BEFORE anything is torn down. Builds a full, generic
// ECS-Registry snapshot via Scene/SceneBuilder.h's
// BuildSceneDocumentFromRegistry() - THE SAME function
// EditorHotReloadDebugCapability::BuildSceneSnapshotJson() (GET
// /project_assembly/debug/scene_snapshot) already uses for the identical
// live world, which is this phase's own primary live cross-check tool.
// Refreshes core.GetAssetDatabase() exactly ONCE per cycle
// (PHASE0_MASTER_STRATEGY.md, LDD-HR7) - RestoreProjectAssemblyHotReloadState()
// (PHASE3) reuses this SAME, already-refreshed instance later in the SAME
// cycle, without refreshing it a second time.
HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& core, const std::filesystem::path& projectRootDirectory);

// HOOK POINT B - called AFTER the (new-or-rolled-back) code's own
// GTE_RegisterProject has already run (see PerformProjectAssemblyHotReload()'s
// own sequencing, editor-core-separation-14 PHASE4 - unchanged by this
// campaign), so every render-pass/panel/component-type registration the
// now-running code needs already exists BEFORE this call ever tries to
// apply saved component data referencing it - this ordering is what makes
// applying a saved custom-component field safe (ComponentTypeRegistry::Find()
// would return nullptr, and the field would be silently, confusingly
// dropped, if this ran too early). Real body, as of PHASE3 (this campaign) -
// reuses Scene/SceneBuilder.h's ClearEntireScene() + the new
// ReconstructSceneFromDocument(), sharing the SAME code the Editor's own
// Ctrl+O LoadScene() now goes through. Note the new Renderer& parameter -
// ReconstructSceneFromDocument() needs one, and
// PerformProjectAssemblyHotReload() already has a live Renderer& renderer in
// scope at its own call site.
void RestoreProjectAssemblyHotReloadState(Core& core, Renderer& renderer, const HotReloadStateSnapshot& snapshot,
    const std::filesystem::path& projectRootDirectory);

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3). THE orchestrator - PHASE4 (this file, as of PHASE4) gives
// this its REAL, permanent body: freeze (already true by construction - it
// runs synchronously on the main thread) -> capture state (HOOK POINT A) ->
// back up binaries -> unload (GPU-safe, ledger-safe) -> compile
// synchronously (with a Windows message pump keeping the OS happy) -> on
// success, load the fresh binaries; on any failure, restore the backup and
// reload the OLD binaries -> restore state (HOOK POINT B) -> done.
// Called SYNCHRONOUSLY from EditorHost::Run()'s own main loop - blocks the
// calling (main) thread for its entire duration (LDD-HR4). Targets exactly
// one project (LDD-HR5).
// mirrors ResolveProjectAssemblyOutputDirectory()/ResolveCMakeBuildDirectory()'s
// own identical "take the resolved directory as an explicit parameter,
// never resolve it internally" precedent (ProjectAssemblyBuildRunner.h),
// for the exact same layering reason. Just use the two parameters as handed
// in by the caller - do not re-resolve them, and do not #include
// "../../Editor/ProjectRootPath.h" here.
//
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE1 - resolved by the CALLER (EditorHost::Run()'s own
// drain point, gte_editor-tier), exactly mirroring
// outputDirectory/buildDirectory's own existing precedent immediately
// above (see this whole file's own header comment, and
// PHASE0_MASTER_STRATEGY.md Section 2.2, for the full layering hazard
// this avoids: ResolveProjectRootDirectory() is gte_editor-tier only,
// defined in src/Editor/ProjectRootPath.cpp, and this file must never
// call it directly). PHASE2/PHASE3 (this same campaign) pass this
// straight through, unchanged, into CaptureProjectAssemblyHotReloadState()/
// RestoreProjectAssemblyHotReloadState() respectively.
void PerformProjectAssemblyHotReload(const std::string& projectName, Core& core, Renderer& renderer,
    EditorHost* editorHost, const std::filesystem::path& outputDirectory, const std::filesystem::path& buildDirectory,
    const std::filesystem::path& projectRootDirectory);

} // namespace gte
