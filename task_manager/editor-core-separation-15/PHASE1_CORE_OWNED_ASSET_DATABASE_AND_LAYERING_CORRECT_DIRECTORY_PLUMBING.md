# PHASE1 — Core-Owned AssetDatabase & Layering-Correct Directory Plumbing

Parent: `PHASE0_MASTER_STRATEGY.md` (read first — LDD-HR6/7/8/9, Sections
2.2/2.3 of that file are load-bearing context for this phase).

Depends on: nothing (first implementation phase).
Blocks: PHASE2 (needs `Core::GetAssetDatabase()` to exist) and PHASE3
(needs `PerformProjectAssemblyHotReload()`'s new 7th parameter already
threaded through).

End state of this phase: `Core` owns a persistent `AssetDatabase` member,
reachable via a new accessor; `PerformProjectAssemblyHotReload()`'s
signature gains one new, caller-resolved `const std::filesystem::path&
projectRootDirectory` parameter; `EditorHost::Run()`'s own drain point
resolves it exactly like it already resolves `outputDirectory`/
`buildDirectory`. **Both hook functions remain the EXACT SAME no-op stubs
they are today** — this phase is pure, low-risk plumbing, proven by a
build + a single smoke-tested hot-reload cycle (still a no-op state-wise,
by design), not by any new capture/restore logic.

---

## STEP 1 — `Core` gains a persistent `AssetDatabase`

`src/Core/Core.h` — confirmed current includes (lines 1-58) do NOT
include `Assets/AssetDatabase.h` anywhere; add it as a new, file-scope
include (mirrors `Plugins/ProjectAssemblyHost.h`'s own "concrete,
gte_core-owned mechanism class, included by name" precedent, lines 47-50):

```cpp
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE1.
#include "../Assets/AssetDatabase.h"
```

Public accessor, placed immediately after `GetProjectAssemblyHost()`
(`Core.h` line 351 — mirrors that method's own "read accessor for a
gte_editor-tier capability class that needs a live reference into a
gte_core-owned object" precedent exactly):

```cpp
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE1 - a persistent, engine-owned AssetDatabase,
// refreshed exactly ONCE per hot-reload cycle by
// CaptureProjectAssemblyHotReloadState() (PHASE2), then reused AS-IS by
// RestoreProjectAssemblyHotReloadState() (PHASE3) later in the SAME
// cycle - mirrors Unity's own persistent Assets-folder database concept
// (kept live, not rescanned from scratch on every single operation).
// Deliberately a SEPARATE instance from Editor/Panels/ProjectPanel.h's
// own separately-owned, separately-refreshed AssetDatabase (used by the
// Project Browser panel), and separate again from the throwaway one-shot
// instances Editor/SceneIO.cpp's SaveScene()/LoadScene() already build
// fresh on every call - unifying all three is real, legitimate, OUT OF
// SCOPE future work (PHASE0_MASTER_STRATEGY.md, LDD-HR7) - do not attempt
// it as part of this phase.
AssetDatabase& GetAssetDatabase() noexcept { return m_assetDatabase; }
```

Private member, placed immediately after `m_projectAssemblyHost`
(`Core.h` line 624 — "no constructor dependency on any other `Core`
member" placement precedent, mirrors that member's own doc comment):

```cpp
// editor-core-separation-15 campaign, PHASE1 - see GetAssetDatabase()'s
// own doc comment above. Default-constructed, empty, until the first hot
// reload cycle calls RefreshFromDirectory() on it (PHASE2) - never
// refreshed at engine startup by this phase, deliberately (nothing reads
// it before PHASE2 exists).
AssetDatabase m_assetDatabase;
```

**Verify concretely**: `AssetDatabase` must be default-constructible with
no constructor arguments (confirmed already true — every existing call
site, e.g. `SceneIO.cpp` line 29/77, does `AssetDatabase assetDatabase;`
with no arguments). Confirm this compiles with a plain incremental build
before proceeding — `AssetDatabase.h`'s own class must not, itself,
`#include` anything gte_editor-tier (confirmed clean today — it lives in
`src/Assets/`, the same gte_core-tier folder as `AssetTypes.h`/
`GtaFile.h`).

---

## STEP 2 — `PerformProjectAssemblyHotReload()`'s new, permanent 7th
parameter

`src/Core/Plugins/ProjectAssemblyHotReload.h` — extend the existing
signature (confirmed current, lines 55-56) with one new trailing
parameter (append, do not reorder the existing six — no existing call site
other than `EditorHost.cpp`'s own single call site exists, confirmed by
`search_in_dir` for `PerformProjectAssemblyHotReload(` across `tests/`
returning zero real call sites, only one comment reference):

```cpp
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
```

`ProjectAssemblyHotReload.cpp` — update the definition's own parameter
list to match (mechanical, no body change yet — the two hook-function
calls inside it, `CaptureProjectAssemblyHotReloadState(core)`/
`RestoreProjectAssemblyHotReloadState(core, snapshot)`, are updated by
PHASE2/PHASE3 respectively to actually pass `projectRootDirectory`
through — **this phase does not touch either hook function's own call
site or body at all**, only the enclosing function's own parameter list).

---

## STEP 3 — `EditorHost::Run()`'s drain point resolves and forwards it

Confirmed current call site, `src/Editor/EditorHost.cpp` lines 465-480 —
extend the existing two `ResolveXxxDirectory()` calls with a third,
sibling call, then extend the `PerformProjectAssemblyHotReload(...)` call
itself with the new argument:

```cpp
const std::filesystem::path outputDirectory = ResolveProjectAssemblyOutputDirectory(gte::ExecutableDirectory());
const std::filesystem::path buildDirectory = ResolveCMakeBuildDirectory(gte::ExecutableDirectory());
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE1 - resolved HERE for the exact same reason
// outputDirectory/buildDirectory are: this file is gte_editor-tier
// (already includes ProjectRootPath.h - confirmed, this translation
// unit's own #include list), PerformProjectAssemblyHotReload() itself is
// gte_core-tier and must never resolve this path internally.
const std::filesystem::path projectRootDirectory = ResolveProjectRootDirectory();
PerformProjectAssemblyHotReload(*requestedProject, m_core, m_renderer, this, outputDirectory, buildDirectory, projectRootDirectory);
```

**Verify concretely**: confirm `EditorHost.cpp` already `#include`s
`"ProjectRootPath.h"` (needed for `ResolveProjectAssemblyOutputDirectory`/
`ResolveCMakeBuildDirectory`, both declared alongside
`ResolveProjectRootDirectory` in that same header per
`ProjectAssemblyBuildRunner.h`'s own precedent) — if `ResolveProjectRootDirectory`
itself is declared in a DIFFERENT header (`src/Editor/ProjectRootPath.h`
directly, rather than re-exported by `ProjectAssemblyBuildRunner.h`),
confirm the real, current include and add it if missing. Do not guess —
`read_file`/`search_in_dir` the real, current top of `EditorHost.cpp`
before writing this line.

---

## STEP 4 — Quick, low-risk verification (no new state-preservation logic
exists yet — this phase only proves the plumbing compiles and does not
regress the existing, working orchestrator)

1. Incremental build (`cmake --build build`) — confirm success, zero new
   warnings introduced by this phase's own three edited files.
2. Launch `GreatTamanaEditor.exe` via `run_app_background`. Confirm, via
   `gte_send_request`, `GET /project_assembly/debug/loaded_assemblies`
   shows the probe's `.dll`s loaded (baseline, unrelated to this phase's
   own change).
3. Trigger one real, full hot-reload cycle (`POST
   /project_assembly/hot_reload?name=ProjectAssemblyProbe`, no source
   change needed — a clean recompile with no edits still exercises the
   FULL success path) and confirm, via `GET
   /project_assembly/hot_reload/status`, `lastOutcome: "Success"` — this
   proves the new 7th parameter did not break anything about the
   already-shipped orchestrator (both hook functions are STILL no-op
   stubs this phase, so no state-preservation claim is being tested here
   — only "did adding a parameter and a new Core member break the
   existing, working cycle").
4. `stop_app_background` when done.

---

## Definition of Done — this phase only

- [ ] `Core::GetAssetDatabase()`/`m_assetDatabase` exist, compile, and are
      reachable; `Core.h` does not gain any gte_editor-tier `#include`.
- [ ] `PerformProjectAssemblyHotReload()`'s signature has its new,
      permanent 7th parameter, `projectRootDirectory`, in both the header
      and the `.cpp` definition.
- [ ] `EditorHost::Run()`'s own drain point resolves and forwards it,
      exactly mirroring the existing `outputDirectory`/`buildDirectory`
      precedent.
- [ ] A full incremental build succeeds with no new warnings.
- [ ] A live, real, full hot-reload cycle (no source change) still
      completes with `lastOutcome: "Success"`, proving zero regression to
      the already-shipped orchestrator.

## What this phase does NOT do

- Does NOT implement any real capture/restore logic — both hook functions
  remain byte-for-byte the same no-op stubs `editor-core-separation-14`
  shipped.
- Does NOT refresh `Core::m_assetDatabase` at any point — nothing calls
  `RefreshFromDirectory()` on it yet (PHASE2's job).
- Does NOT touch `ReconstructSceneFromDocument()`/`Editor::LoadScene()`
  at all — that whole refactor is PHASE3's job, deliberately kept
  completely separate from this phase's own narrow plumbing change.
