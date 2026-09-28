#pragma once

#include <filesystem>
#include <string>

namespace gte {

class Core;
class Renderer;
class EditorHost;

// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE4. A deliberately EMPTY placeholder value type - a
// future BIG-STEP 4 campaign
// (HOTRELOAD_BIGSTEP_04_STATE_SNAPSHOT_RESTORE_AND_VERIFICATION_PLAN_2026-09-28.txt)
// gives this real fields (reusing gte::SceneDocument - see that file's own
// design). Kept as a named type (not "just skip these two calls entirely")
// so this orchestrator's own call sites/control flow never need to change
// shape when BIG-STEP 4 lands - only these two functions' BODIES change.
struct HotReloadStateSnapshot {};

// HOOK POINT A - called BEFORE anything is torn down. No-op stub this
// campaign (returns a default-constructed, empty snapshot).
HotReloadStateSnapshot CaptureProjectAssemblyHotReloadState(Core& core);

// HOOK POINT B - called AFTER the (new-or-rolled-back) code's own
// GTE_RegisterProject has already run, so every render-pass/panel/
// component-type registration the now-running code needs already exists.
// No-op stub this campaign.
void RestoreProjectAssemblyHotReloadState(Core& core, const HotReloadStateSnapshot& snapshot);

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
