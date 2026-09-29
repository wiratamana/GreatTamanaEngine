# editor-core-separation-19 — PHASE0 DOUBLE-CHECK REPORT (pre-implementation pass)

This is a second-iteration verification pass over all 3 strategy files in this
campaign folder (`PHASE0_MASTER_STRATEGY.md`, `PHASE1_CAPABILITY_WIRING_AND_COMPILE_MENU_ITEM.md`,
`PHASE2_TESTS_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md`), performed BEFORE
any implementation work started. No code was changed by this pass — only the
strategy `.md` files themselves and this report.

## What was checked

Every concrete, cited claim in all 3 files was independently re-verified
against the real, current source tree (never trusted at face value), by
direct `read_file`/`read_line`/`search_in_dir` calls, cross-referenced with
the two external master-plan `.txt` files
(`PROJECTWORKFLOW_BIGSTEP_01_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`,
`PROJECTWORKFLOW_BIGSTEP_05_COMPILE_MENU_2026-09-28.txt`) and the 3 prior
campaigns' own completion reports (`editor-core-separation-16/17/18`).
Specifically re-confirmed, one by one:

- `IEditorLayer` (`src/Editor/EditorLayer.h`) has **exactly two** existing
  capability setters today — `SetProjectLifecycleCapability()` (line 726)
  and `SetAssetScaffoldingCapability()` (line 738) — confirmed by a direct
  `search_in_dir` for `Capability(` inside this exact file (2 hits, no more).
  **No `SetHotReloadDebugCapability`-shaped method, and no forward
  declaration or `#include` of `IHotReloadDebugCapability` at all, exists
  anywhere in `EditorLayer.h` today** — confirmed by a whole-codebase
  `search_in_dir` for `IHotReloadDebugCapability` (32 hits in 14 files, zero
  of them in `EditorLayer.h`) and for `SetHotReloadDebugCapability` (zero
  hits anywhere in `src/`). PHASE0's headline claim here is still exactly
  true today.
- `BuildDockspaceAndMenuBar(EditorContext& ctx, Game& game, Renderer& renderer)`
  really does take exactly 3 parameters today — `DockLayout.h` line 16,
  `DockLayout.cpp` line 112, one real call site (`ImGuiEditorLayer.cpp` line
  529), confirmed by `search_in_dir` across the whole codebase (every other
  hit is a comment/mention, not a call).
- Exactly two classes implement `IEditorLayer` today (`ImGuiEditorLayer`,
  `NullEditorLayer`), confirmed by `search_in_dir` for `: public IEditorLayer`
  — including inside `tests/`, where zero test doubles subclass it.
- `DockLayout.cpp` line 196 is still, verbatim,
  `if (ImGui::MenuItem("Compile", nullptr, false, false)) {}` — unchanged
  since `editor-core-separation-16`.
- Every other exact line-number citation across both files — `ProjectAssemblyBuildRunner.h`
  line 125 / `.cpp` lines 32-49, `EditorCapabilities.h` lines 173-256 (228 for
  `TriggerCompileOnly()`), `EditorHotReloadDebugCapability.h` line 36 / `.cpp`
  lines 101-108, `NetworkServer.cpp` lines 1314-1331, `EditorContext.h` lines
  ~302-311, `DockLayout.cpp`'s toast block lines 297-310, `EditorHost.cpp`
  lines 214/241/252, `ImGuiEditorLayer.cpp` lines 529/964-967/1212,
  `tests/CMakeLists.txt` line 2166, `ProjectAssemblyBuildRunnerBackupRestoreTests.cpp`
  lines 132-169+ — were each individually re-read and confirmed byte-for-byte
  accurate against the real, current files. `IsProjectAssemblyBuildInFlight`
  and `IsCompileInFlight` were confirmed to not exist anywhere yet (correctly
  pre-implementation).
- Confirmed the current git branch is `feature/editor-core-separation` (per
  the prerequisites) and that `Projects/` contains only `ProjectAssemblyProbe`
  today (the expected clean starting state for PHASE2's own live smoke test).

## What was changed, and why

**`PHASE1_CAPABILITY_WIRING_AND_COMPILE_MENU_ITEM.md` was rewritten (v2).**
Two real, concrete problems were found in its exact edit instructions —
both would have caused real trouble for whoever implements this phase next:

1. **A genuine compile-breaking gap in the `EditorLayer.h` instructions.**
   The old text told the implementer to merely "confirm `class
   IHotReloadDebugCapability;` ... is already visible in this header before
   this line — it must already be, since `IProjectLifecycleCapability`/
   `IAssetScaffoldingCapability` are already used as parameter types here."
   This is **false, confirmed directly**: `IHotReloadDebugCapability` has
   zero occurrences anywhere in `EditorLayer.h` today (unlike the other two
   capability types, which really are forward-declared at lines 88/94). If
   an implementer literally followed the old instruction (a "confirm" step,
   implying nothing needs to change), the new pure virtual added right after
   would name an undeclared type and the very next incremental build would
   fail to compile. The rewritten file makes this an explicit, mandatory
   first edit — add a new forward declaration, mirroring the existing
   `IProjectLifecycleCapability`/`IAssetScaffoldingCapability` precedent
   exactly — before the pure virtual is added.
2. **An ambiguous, data-loss-risking edit range in the `DockLayout.cpp`
   instructions.** The old text said to replace "the ENTIRE current
   placeholder line 196 ... together with its own preceding comment block
   (lines 179-186)" — describing two non-adjacent line ranges as if they
   were one edit. The REAL file has genuinely working, currently-shipping
   "Open Project..." code (its own comment, the menu item itself, and a
   `ImGui::Separator();`) sitting physically BETWEEN those two ranges (lines
   188-195). An implementer who read this instruction as "replace the
   contiguous span from line 179 to line 196" (a very natural, easy
   misreading) would have silently deleted the working "Open Project..."
   feature. The rewritten file spells out the real, current structure of
   this whole menu block explicitly, requires two clearly separate,
   independently-described edits, and adds an explicit post-edit
   verification step in Step 4 ("re-read the Open Project... block and
   confirm it is still present, byte-for-byte unchanged") specifically to
   catch this failure mode if it happens anyway.

Every other citation, line number, and design decision in `PHASE1` was
confirmed accurate and was carried over unchanged (only reformatted where the
two fixes above required restructuring the surrounding section).

## What was left as-is, and why

- **`PHASE0_MASTER_STRATEGY.md` was left completely unchanged.** Every
  citation, every line number, and its own central finding (the missing
  3rd `IEditorLayer` capability setter, and the missing
  `projectWorkflowStatusSetTime` line in the source `.txt` file's own code
  sketch) were independently re-confirmed, exactly as described, against the
  real, current source tree. Nothing wrong or stale was found in this file.
- **`PHASE2_TESTS_LIVE_VERIFICATION_AND_CAMPAIGN_CLOSEOUT.md` was left
  completely unchanged.** Its own citations (`tests/CMakeLists.txt` line
  2166, the `ConcurrentSynchronousBuildsForTheSameProjectAreMutuallyExclusive`
  precedent test's real line range, the live-verification HTTP sequence, the
  regression-arithmetic baseline of 1986) were all confirmed accurate. Two
  genuinely trivial wording imprecisions were noticed and deliberately left
  alone rather than risking a rewrite for near-zero benefit: (a) its own
  citation of "`ProjectAssemblyBuildRunner.h` lines 108-114, 'calling it
  again for a DIFFERENT project ... is fine'" quotes phrasing that is
  actually closest to the `.cpp` file's anonymous-namespace comment (lines
  29-31), not a verbatim quote from the `.h` file's lines 108-114 (though
  the `.h` file's own text at that range says the same thing in different
  words — "Safe to call again while a previous build for a DIFFERENT
  project is still running"); (b) its description of `tests/CMakeLists.txt`'s
  hand-maintained list as following an "alphabetical/grouped" convention is
  more precisely "grouped by campaign, in chronological landing order" (it
  only happens to also read alphabetically for the 3 existing
  `ProjectAssemblyBuildRunner*Tests.cpp` entries by coincidence of their own
  names). Neither of these affects where any file/line actually needs to be
  edited, so they were not treated as worth the risk of an unnecessary
  rewrite of an otherwise-correct file.

## Conclusion

Two real, concrete implementation-blocking/data-loss-risking gaps were found
and fixed in `PHASE1`. `PHASE0` and `PHASE2` were independently verified
accurate against the real, current codebase and left unchanged. This
campaign's downstream implementation phases (PHASE1, then PHASE2) are now
being delegated as separate tasks.
