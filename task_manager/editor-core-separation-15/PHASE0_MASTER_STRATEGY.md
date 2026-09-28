# editor-core-separation-15 — PHASE0 MASTER STRATEGY
## Project Assembly Hot Reload — BIG-STEP 4 of 4: State Snapshot, Restore, and Verification Plan

Parent external plan (read this whole 5-file series once, in order, before
touching any code):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-2\HOTRELOAD_BIGSTEP_00_MASTER_INVESTIGATION_AND_ORCHESTRATION_2026-09-28.txt`
through `..._04_STATE_SNAPSHOT_RESTORE_AND_VERIFICATION_PLAN_2026-09-28.txt`.

Prior campaigns this one builds directly on top of (all three DONE, all
three re-verified against the real repo before this file was written):
- `task_manager/editor-core-separation-12/` — BIG-STEP 1 (Live Debug + Compile/Reload Trigger Surface).
- `task_manager/editor-core-separation-13/` — BIG-STEP 2 (Teardown Safety & Registration Ledger).
- `task_manager/editor-core-separation-14/` — BIG-STEP 3 (Synchronous Compile & Atomic Swap Orchestrator).

This campaign (`editor-core-separation-15`) implements BIG-STEP 4, the
**final** phase of the whole Hot Reload effort. When this campaign is DONE,
the entire, 4-BIG-STEP feature is DONE.

---

## STEP 1 — THE GOAL (where are we going?)

At the end of this campaign:

1. `POST /project_assembly/hot_reload?name=<X>` genuinely preserves the
   live ECS world across the freeze/unload/recompile/reload-or-rollback
   cycle: every entity, every built-in reflected component (Transform,
   Name, Camera, ...), **and every Project-Assembly-defined CUSTOM
   component type**, survives byte-for-byte, including values that were
   mutated at RUNTIME (not merely whatever a cold start would produce).
2. This is proven true for BOTH outcomes of a cycle: a successful reload
   (new code, old state) AND an automatic rollback (old code, old state,
   "as if the button was never pressed" — LDD-HR3).
3. `ComponentTypeRegistry`'s Hazard-1 fix (`editor-core-separation-13`) is
   proven correct under REAL reload conditions, with REAL runtime data,
   not just its own narrower isolation test.
4. The one, permanent, honest boundary of this whole feature — non-ECS
   C++ state does NOT survive — is written down permanently in
   `docs/conventions/project-assembly-system.md`, replacing that file's
   own now-STALE "no hot reload, anywhere, ever" claim (see Section 2.4
   below — this is a real, pre-existing documentation bug this campaign
   must fix, not merely a new fact to append).
5. `HotReloadStateSnapshot`'s two permanent no-op stubs
   (`CaptureProjectAssemblyHotReloadState()` /
   `RestoreProjectAssemblyHotReloadState()`, both
   `src/Core/Plugins/ProjectAssemblyHotReload.h/.cpp`) become real, and
   `PerformProjectAssemblyHotReload()`'s own control flow (`editor-core-
   separation-14`, PHASE4) needs no other change — only these two
   functions' bodies change, exactly as that campaign's own header
   comment promised.

---

## STEP 2 — THE SITUATION (where are we now?)

Everything below was confirmed by directly reading the real, current
source tree during this strategy's own preparation (`search_in_dir`/
`read_file`, never guessed, never taken only from the external plan's own
prose — the external plan's own HOOK POINT A/B pseudocode is REUSED where
still accurate and EXPLICITLY CORRECTED below where this investigation
found it would not compile or would reintroduce an already-fixed bug
class).

### 2.1 — What already exists and works (confirmed, reused as-is)

- `HotReloadStateSnapshot` is a deliberately EMPTY struct
  (`src/Core/Plugins/ProjectAssemblyHotReload.h`, line 19:
  `struct HotReloadStateSnapshot {};`), and both hook functions are real,
  literal, logged no-op stubs (`ProjectAssemblyHotReload.cpp` lines 23-38)
  — confirmed, live, by `editor-core-separation-14`'s own
  `CAMPAIGN_COMPLETION_REPORT.md`: *"HOOK POINT A/HOOK POINT B ... remain
  permanent, literal, logged no-op stubs, exactly as this whole campaign's
  own scope requires."*
- `PerformProjectAssemblyHotReload()`'s PERMANENT, already-shipped 6-parameter
  signature (`ProjectAssemblyHotReload.h` line 55-56):
  `(const std::string& projectName, Core& core, Renderer& renderer,
  EditorHost* editorHost, const std::filesystem::path& outputDirectory,
  const std::filesystem::path& buildDirectory)`. Its own body (`.cpp`
  lines 88-195) already calls `CaptureProjectAssemblyHotReloadState(core)`
  BEFORE any teardown and `RestoreProjectAssemblyHotReloadState(core,
  snapshot)` AFTER the (new-or-rolled-back) code's own
  `GTE_RegisterProject` has already run, on BOTH the success path and the
  rollback path — exactly the ordering BIG-STEP 4's own Step 1/2 pseudocode
  requires, unconditionally, already wired. **This campaign only fills in
  the BODIES of the two hook functions — it does not touch
  `PerformProjectAssemblyHotReload()`'s own control flow at all**, except
  for one small, additive, caller-supplied parameter (Section 2.2 below).
- `gte::SceneDocument` / `BuildSceneDocumentFromRegistry(Registry&, const
  AssetDatabase&)` / `ClearEntireScene(Registry&)`
  (`src/Scene/SceneBuilder.h/.cpp`) — confirmed **gte_core-tier**
  (`CMakeLists.txt` line 557-558/563-564, inside the big unconditional
  source list, well before the `add_library(gte_editor STATIC` block at
  line 1008) — freely callable from `src/Core/Plugins/
  ProjectAssemblyHotReload.cpp` (also gte_core-tier) with **zero layering
  concern**. `BuildSceneDocumentFromRegistry()` walks every root entity via
  `GetChildren(registry, kInvalidEntity)`, recurses, and for every
  registered `ComponentTypeDescriptor` (`ComponentTypeRegistry::Instance().
  AllSortedByTypeName()`) generically serializes it if the entity has it
  (`SceneBuilder.cpp` lines 20-55) — **no per-component-type branch
  anywhere**, so a Project Assembly's own custom component type is
  captured automatically the instant it registers itself, with zero new
  code in this function.
- `gte::EditorHotReloadDebugCapability::BuildSceneSnapshotJson(Game&
  game)` (`src/Editor/EditorHotReloadDebugCapability.cpp` lines 84-96) is
  the ALREADY-WORKING, ALREADY-LIVE-TESTED reference implementation of
  "build a `SceneDocument` from the live world and serialize it" — it is
  the real body behind `GET /project_assembly/debug/scene_snapshot`,
  confirmed reachable via `EngineCommandBridge::GetSceneSnapshot`
  (`EngineCommandDispatch.cpp` line 92-95, dispatched on the MAIN thread
  only). **This is this campaign's primary live cross-check tool** — any
  new capture logic this campaign writes must agree with what this
  existing endpoint already reports for the same live world.
- `gte::ProbeHotReloadMarker` (`Projects/ProjectAssemblyProbe/Assets/
  HelloGame.cpp` lines 66-68) — a real, already-registered
  (`gte::RegisterComponentType<ProbeHotReloadMarker>("ProbeHotReloadMarker",
  {...})`, line 73-75) custom ECS component type, added by
  `editor-core-separation-13`'s own PHASE5 specifically to exercise
  Hazard 1. **Confirmed, directly, by re-reading this exact file**: the
  type is registered, but **no entity in the whole probe project actually
  carries this component today** — `RegisterProbeGame()` never calls
  `registry.AddComponent<ProbeHotReloadMarker>(...)` anywhere. This is a
  real, concrete gap this campaign must close (Phase 4) — without a live
  entity carrying it, there is nothing for this campaign's own capture/
  restore/mutate-and-verify test to actually exercise.
- `Editor::SaveScene()`/`LoadScene()` (`src/Editor/SceneIO.h/.cpp`) — the
  full, real, already-shipping, recipe-aware round trip (PrimitiveSource/
  MeshAssetSource-aware Pass A, hierarchy Pass B1, generic field-apply
  Pass B2, sibling-index Pass B3 — `SceneIO.cpp` lines 55-386). Confirmed
  **gte_editor-tier** (`CMakeLists.txt` line 1047-1048, inside the
  `add_library(gte_editor STATIC` block). This whole function's body
  (from `Registry& registry = game.GetRegistry();` at line 69 through
  `return true;` at line 385) is what BIG-STEP 4's own Step 2 needs
  extracted into a shared, reusable function — Section 2.3 below explains
  exactly why this extraction cannot simply live where the external plan
  originally suggested.

### 2.2 — THE CRITICAL FINDING: HOOK POINT A/B's own pseudocode, taken
literally, reintroduces the EXACT layering bug `editor-core-separation-14`
already found and fixed once

`HOTRELOAD_BIGSTEP_04...txt`'s own Step 1 pseudocode for
`CaptureProjectAssemblyHotReloadState()` calls
`ResolveProjectRootDirectory()` directly, inline, inside
`src/Core/Plugins/ProjectAssemblyHotReload.cpp`. **This function is
defined ONLY in `src/Editor/ProjectRootPath.cpp`, confirmed
`CMakeLists.txt` line 1046 — gte_editor-tier, exactly the same file
`gte::ExecutableDirectory()` (the function `editor-core-separation-14`'s
own PHASE0 double-check already caught and fixed as a real, silent
`gte_core` → `gte_editor` layering violation) lives in.**
`ProjectAssemblyHotReload.cpp`'s own header comment states, in writing,
TODAY: *"this file lives in `src/Core/Plugins/`... and must NEVER call
`gte::ExecutableDirectory()` itself... do not `#include
"../../Editor/ProjectRootPath.h"` here"* — the exact same file, the exact
same rule, and the ORIGINAL BIG-STEP 4 plan's own HOOK POINT A pseudocode
would break this rule the moment it was typed in literally.

**The fix is the SAME fix `editor-core-separation-14` already applied to
`outputDirectory`/`buildDirectory`**: resolve the project root directory
on the CALLER's side (`EditorHost::Run()`'s own drain point,
`EditorHost.cpp` lines 465-480, already gte_editor-tier, already resolves
two sibling directories this exact same way), and pass it into
`PerformProjectAssemblyHotReload()` as one more plain
`std::filesystem::path` VALUE parameter, which forwards it, unchanged,
into both hook functions. **Phase 1 of this campaign does exactly this,
first, before any real capture/restore logic is written** — see that
phase's own file for the exact signature.

### 2.3 — THE SECOND CRITICAL FINDING: where the shared "rebuild a scene
from a `SceneDocument`" function must actually LIVE

The external plan's Step 2 says to extract `Editor::LoadScene()`'s own
steps (2)+(3) into a new, shared function, "called by BOTH the existing
`LoadScene()` and this new hot-reload restore path." Taken literally, this
would leave the shared function inside `src/Editor/SceneIO.h/.cpp` — but
that file is gte_editor-tier (Section 2.1 above), and
`RestoreProjectAssemblyHotReloadState()` (HOOK POINT B) is gte_core-tier
(`src/Core/Plugins/ProjectAssemblyHotReload.cpp`) — **gte_core must never
depend on gte_editor** (the whole reason `tools/ci/gte_core_player_link_probe/`
exists at all, per `editor-core-separation-14`'s own PHASE0 finding #1).
Extracting the shared function into `SceneIO.h` would make HOOK POINT B
either impossible to write cleanly, or would silently smuggle a
`gte_editor` dependency into `gte_core` — the EXACT class of mistake this
campaign must not repeat a second time.

**Confirmed, directly**: every single type `Editor::LoadScene()`'s own
recipe-aware reconstruction logic touches — `Game` (`Game.cpp`,
`CMakeLists.txt` line 816, gte_core-tier), `Renderer` (already passed
through, as a plain reference parameter, inside
`ProjectAssemblyHotReload.cpp` today — forwarded straight into
`ProjectAssemblyHost::UnloadProjectAssembly()` — CORRECTED, re-verified
directly: the specific example previously cited here,
`renderer.WaitForGpuIdle()`, is NOT actually called inside THIS file; that
exact call lives in the neighboring `ProjectAssemblyHost.cpp`, same
gte_core-tier `src/Core/Plugins/` folder, confirmed line 229. The
underlying point still holds either way — `Renderer` is already handled as
a plain, ordinary gte_core-tier type throughout this same folder), `AssetDatabase`, `Registry`,
`ComponentTypeRegistry`, `TransformHierarchy` — is **already gte_core-tier**.
`SceneBuilder.h`'s own current header comment claims this logic must stay
in `Editor/SceneIO.h` because it "needs a live `Renderer`" — **this
reasoning is stale and no longer correct**: `Renderer` has never been
gte_editor-tier. There is **no remaining technical reason** this logic
cannot move.

Per the user's own explicit, direct answer during this strategy's
preparation (*"unity have load scene on player build, so yeah. load scene
is core engine feature that can be controlled on editor side also"*) —
**this campaign MOVES the recipe-aware reconstruction logic into
`src/Scene/SceneBuilder.h/.cpp` (gte_core-tier)**, as a new function,
`ReconstructSceneFromDocument(Game&, Renderer&, const SceneDocument&,
const AssetDatabase&)`, sitting alongside `BuildSceneDocumentFromRegistry()`/
`ClearEntireScene()` — genuinely a core engine capability from now on
(mirrors Unity's own `SceneManager.LoadScene()` being available in a
Player build, not just the Editor), not merely a hot-reload-specific
workaround. `Editor::LoadScene()` becomes a thin wrapper: resolve the
project root + `AssetDatabase` + parse the file (still gte_editor-tier,
unchanged), then call this SAME shared function. See Phase 3 for the full,
exact extraction plan, including the mandatory regression proof that this
refactor does not change `LoadScene()`'s own observable behavior at all.

### 2.4 — THE THIRD FINDING: `docs/conventions/project-assembly-system.md`
is currently WRONG, and has been wrong since `editor-core-separation-12`
shipped

Confirmed, directly, `search_in_dir` for `"Hot Reload"` and `"hot reload"`
across `docs/conventions/project-assembly-system.md`: this file states, in
THREE separate places (lines 63, 156, 307), **"No hot reload, anywhere,
ever"** as a still-standing Locked Design Decision (`LDD4`). This was TRUE
when `editor-core-separation-11` shipped it. It has been FALSE since
`editor-core-separation-14` shipped a real, working `POST
/project_assembly/hot_reload`. **No campaign since has corrected this
file.** This is a real, pre-existing documentation bug (not something this
campaign introduces) that this campaign's own Phase 5 must fix — not by
deleting the historical record of why `LDD4` existed, but by adding a
clearly-dated section explaining it was SUPERSEDED by
`editor-core-separation-12` through `-15`, with a link to
`HOTRELOAD_BIGSTEP_00...txt` and this campaign's own honest-boundary
statement (Section 3, LDD-HR9 below).

### 2.5 — Locked Design Decisions this campaign inherits verbatim (restated
from `HOTRELOAD_BIGSTEP_00...txt` and `editor-core-separation-14`'s own
"Locked Design Decisions" section — not re-litigated here)

LDD-HR1 (full ECS-Registry state scope, regardless of owning `.dll`),
LDD-HR2 (Game+Editor `.dll` pair reloads together, engine never reloads),
LDD-HR3 (automatic rollback on failure, indistinguishable from never
having pressed the button), LDD-HR4 (hard, synchronous, whole-engine
freeze), LDD-HR5 (one manual trigger, exactly one targeted project, other
loaded projects untouched).

---

## STEP 3 — THE PLAN (how do we get there?)

### 3.1 — New Locked Design Decisions this campaign adds (resolved via
direct `ask_questions` during this strategy's own preparation — restated
here so every phase file can cite them by number without re-litigating)

**LDD-HR6 — The runtime-mutation test hook is ONE narrow, hardcoded,
test-only capability, never a generic mutation surface.** To prove a
custom component's value is genuinely PRESERVED (not merely re-produced
fresh by the reloaded code), the live verification test must mutate that
value while the engine is running, before triggering a reload. The user
explicitly chose: *"Add ONE narrow, hardcoded debug HTTP route that only
ever sets this ONE specific test component's value (safest, smallest,
cannot be misused for anything else)."* Phase 4 adds exactly one new
method, `IHotReloadDebugCapability::SetProbeHotReloadMarkerValueForTesting(int
value) -> bool`, routed as `POST
/project_assembly/debug/set_probe_marker_value?value=<N>` — internally
implemented via the SAME generic, already-existing reflection primitives
(`ComponentTypeDescriptor::tryGetMutableComponent`/`FieldDescriptor::readJson`,
`ECS/Reflection/ComponentTypeDescriptor.h`) every other generic
field-apply call site in this engine already uses — but the ENTITY LOOKUP
and the COMPONENT/FIELD NAMES this one new method targets are hardcoded to
`"ProbeHotReloadMarker"`/`"value"` only. It can never be repurposed to
mutate any other component/field — this is what makes it "narrow," not the
underlying mechanism it happens to reuse.

**LDD-HR7 — `Core` gains ONE persistent, engine-owned `AssetDatabase`
instance, refreshed exactly once per hot-reload cycle, deliberately NOT
yet unified with the Editor's own separate `ProjectPanel`-owned instance.**
Resolved via the user's own explicit answer, *"simulate how unity behave
with Assets folder"* — Unity's own Asset Database is a persistent,
kept-live index, not a throwaway scan repeated on every operation. This
campaign adds `Core::GetAssetDatabase() noexcept -> AssetDatabase&` (a
plain value member, mirrring `m_pluginHost`'s own "no constructor
dependency on any other `Core` member" placement precedent, `Core.h` line
617), refreshed via `RefreshFromDirectory(projectRootDirectory)` exactly
ONCE per cycle, inside `CaptureProjectAssemblyHotReloadState()` — BOTH
hook points then share this SAME, already-refreshed instance (answers the
user's own "scan once, share between the two steps" preference directly).
**Honest boundary, stated explicitly, not silently glossed over**: this is
a SEPARATE `AssetDatabase` instance from the one `src/Editor/Panels/
ProjectPanel.h` already owns and keeps live for the Project Browser panel,
and SEPARATE again from the throwaway one-shot instances `Editor::SaveScene()`/
`LoadScene()` already build fresh on every call (`SceneIO.cpp` lines
27-30/76-78) — this campaign does NOT unify all three into one single
engine-wide instance (a legitimate, real, future refactor, explicitly
flagged as OUT OF SCOPE here, exactly like BIG-STEP 4's own Step 5(b)
option is flagged as future work rather than silently attempted this
campaign).

**LDD-HR8 — Multi-Project-Assembly entity-ID churn during a single-project
reload is an accepted, explicitly documented limitation, not a blocking
defect.** `RestoreProjectAssemblyHotReloadState()`'s own design (per
LDD-HR1, "full... regardless of which dll owns it") calls
`ClearEntireScene()` (destroying EVERY entity in the Registry, including
ones belonging to OTHER, simultaneously-loaded Project Assemblies not
being reloaded this cycle) and then replays the FULL captured snapshot
back — every entity's DATA survives faithfully, but every entity's
`Entity` ID/handle changes, for every project, not just the one being
reloaded. Resolved via the user's own explicit answer: *"Not a concern
right now - in practice only one game project is ever loaded at a time,
so this basically never happens."* This campaign does not attempt a
scoped, per-project-only restore (a substantially larger feature requiring
an entity-to-owning-project attribution system that does not exist today)
— Phase 5's own documentation update states this limitation explicitly,
by name, so a future reader is never surprised by it.

**LDD-HR9 — The permanent, user-facing honest-boundary statement lives in
`docs/conventions/project-assembly-system.md`, replacing (not merely
appending past) that file's own stale `LDD4`.** See Section 2.4 above.

### 3.2 — Phase list and dependency graph

```
PHASE0 (this file)
    |
    v
PHASE1 — Core-Owned AssetDatabase & Layering-Correct Directory Plumbing
    (Core::GetAssetDatabase(); PerformProjectAssemblyHotReload() gains its
    7th parameter, projectRootDirectory, threaded from EditorHost::Run();
    hook points remain no-op stubs this phase — PURE, safe, low-risk
    plumbing, proven by a quick build + smoke test only)
    |
    v
PHASE2 — HOOK POINT A: Capture (CaptureProjectAssemblyHotReloadState()'s
    real body — reuses BuildSceneDocumentFromRegistry() + Core's now-owned
    AssetDatabase; cross-checked live against the ALREADY-WORKING
    GET /project_assembly/debug/scene_snapshot endpoint)
    |
    v
PHASE3 — Shared Reconstruction Function & HOOK POINT B: Restore (the big,
    load-bearing refactor — extract Editor::LoadScene()'s own recipe-aware
    body into Scene/SceneBuilder.h/.cpp's new ReconstructSceneFromDocument(),
    core-tier; LoadScene() becomes a thin wrapper; HOOK POINT B calls the
    SAME shared function; mandatory Ctrl+S/Ctrl+O regression proof this
    refactor changed NOTHING observable)
    |
    v
PHASE4 — Probe Fixture: A Real Marker Entity + The One Narrow Testing-Only
    Mutation Route (LDD-HR6) — without this phase, PHASE5's own live test
    has nothing concrete to mutate/verify
    |
    v
PHASE5 — Full Live Verification (success path + rollback path, both
    proving ECS state genuinely survives AND the new/changed render
    feature is genuinely live), the ONE full build + full `ctest`
    regression pass this whole campaign is permitted to run, the permanent
    documentation fix (LDD-HR9), and campaign closeout.
```

Each phase's own Definition of Done is a strict subset gate for the next —
PHASE3 in particular must not begin until PHASE1/2 are proven (a
mid-refactor layering mistake compounds badly, per this repo's own
established engineering culture, already cited twice in this file).

### 3.3 — Non-goals (explicit, so nobody "helpfully" expands scope)

- Does NOT implement BIG-STEP 4's own named future option (b): a
  Project-Assembly-authored `GTE_SerializeProjectState()`/
  `GTE_RestoreProjectState()` export pair for non-ECS state. Named,
  flagged, not built.
- Does NOT unify `Core`'s new `AssetDatabase` member with
  `ProjectPanel`'s own separate instance, or with `SceneIO.cpp`'s own
  throwaway per-call instances (LDD-HR7).
- Does NOT build a scoped, per-project-only world restore (LDD-HR8).
- Does NOT change `PerformProjectAssemblyHotReload()`'s own control-flow
  ordering, `ProjectAssemblyHotReloadDebugStatus`'s phase names, the
  backup/rollback mechanism, or anything else `editor-core-separation-14`
  already shipped — this campaign is a strict, additive fill-in of two
  named hook points plus the one supporting-parameter/plumbing change
  Section 2.2 requires.
- Does NOT decide the permanent ImGui "Compile & Reload" button's exact
  placement — unchanged non-goal from every prior phase of this whole
  effort.

### 3.4 — How to report progress (mirrors `editor-core-separation-13`/`-14`'s
own convention exactly)

Each phase writes its own `PHASEn_COMPLETION_REPORT.md` in this same
folder: what was actually built, any real, mechanically-confirmed
deviation from that phase's own file (and why), and any NEW gap found the
same way `HOTRELOAD_BIGSTEP_00...txt`'s own Hazards were found. PHASE5
also writes `CAMPAIGN_COMPLETION_REPORT.md`, summarizing the whole
campaign, exactly like `editor-core-separation-13`/`-14` did.

Reading order for whoever implements this: this file, then `PHASE1_...md`
through `PHASE5_...md`, in numeric order — do not skip ahead, and re-read
each earlier phase's own completion report before starting the next
(mirrors this whole effort's own established convention).
