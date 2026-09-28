# PHASE4 COMPLETION REPORT — Probe Fixture: A Real Marker Entity + The One Narrow Testing-Only Mutation Route

Campaign: `editor-core-separation-15` (Project Assembly Hot Reload plan, BIG-STEP 4).
Phase file: `PHASE4_PROBE_FIXTURE_MARKER_ENTITY_AND_TESTING_ONLY_MUTATION_ROUTE.md`.

## What was actually built

All the wiring described in PHASE4's own plan was implemented, with **one
significant, load-bearing deviation** (see "Deviations from the plan" below —
a genuine crash-causing bug, not a style choice):

1. **`Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`**: `RegisterProbeGame()`
   now spawns one real, permanent entity carrying `Name{"ProbeHotReloadMarkerEntity"}`,
   `Transform{}` (default), and `ProbeHotReloadMarker{value=7}`.
2. **`src/Core/EditorCapabilities.h`**: `IHotReloadDebugCapability` gained one
   new pure-virtual method, `SetProbeHotReloadMarkerValueForTesting(int value) -> bool`,
   appended immediately after `TriggerHotReload()`.
3. **`src/Editor/EditorHotReloadDebugCapability.h`/`.cpp`**: implements the
   override, plus a new `SetEngineCommandBridge(EngineCommandBridge&)` setter
   and `m_engineCommandBridge` member (mirrors `SetHotReloadCommandBridge()`/
   `m_hotReloadCommandBridge` exactly). A forward declaration of
   `EngineCommandBridge` was added to the header (no new `#include`), and the
   `.cpp` gained `#include "../Application/EngineCommandBridge.h"`.
4. **`src/Application/EngineCommandBridge.h`**: new `EngineCommandKind::SetProbeHotReloadMarkerValueForTesting`,
   new payload struct `SetProbeHotReloadMarkerValueForTestingCommand{ int value = 0; }`,
   both wired as new tagged fields on `EngineCommandRequest`/`EngineCommandResult`.
5. **`src/Game/EngineCommandResults.h`**: new outcome struct
   `SetProbeHotReloadMarkerValueForTestingOutcome{ bool success = false; }`
   (deliberately minimal, no `editorAvailable`/`errorMessage`, per LDD-HR6).
6. **`src/Application/EngineCommandDispatch.cpp`**: new `#include`s
   (`ComponentTypeRegistry.h`, `TransformHierarchy.h`) and a new
   `case EngineCommandKind::SetProbeHotReloadMarkerValueForTesting:` block,
   built exactly per the phase file's own corrected snippet — the JSON object
   passed to `FieldDescriptor::readJson()` is `{"value": <N>}`, keyed by the
   field's own name, never a bare scalar.
7. **`src/Network/NetworkRoutes.h`/`.cpp`**: new `ParsedSetProbeMarkerValueQuery`
   struct + `ParseSetProbeMarkerValueQuery()`, mirroring
   `ParsedFrameDebuggerSelectEventQuery`/`ParseFrameDebuggerSelectEventQuery()`
   exactly, reusing the file's own existing `TryParseWholeInt()` helper.
8. **`src/Network/NetworkServer.cpp`**: new route,
   `POST /project_assembly/debug/set_probe_marker_value?value=<N>`, mirroring
   `POST /project_assembly/debug/compile_only`'s exact shape (400 on invalid/
   missing `value`, 503 if the capability is unavailable, `{"success":bool}`
   otherwise).
9. **`src/Editor/EditorHost.cpp`**: added the new setter's call site,
   `s_editorHotReloadDebugCapability.SetEngineCommandBridge(m_commandBridge);`,
   immediately after the existing `SetHotReloadCommandBridge()` call.

No new production `.cpp`/`.h` file was added — every change above is an edit
to an existing file, confirmed against the phase file's own assumption before
starting, and re-confirmed true afterward. `CMakeLists.txt` needed no change.

## Deviations from the plan — ONE major, safety-critical deviation

**PHASE4's own Step 1 snippet, taken literally, crashes the engine.** The
phase file instructed `HelloGame.cpp` to call
`registry.AddComponent<gte::Transform>(markerEntity)` and
`registry.AddComponent<gte::Name>(markerEntity).value = "..."` directly, from
inside the `ProjectAssemblyProbe_Game.dll`'s own `RegisterProbeGame()`. Doing
exactly this was implemented first, built successfully, and then **crashed
the running engine with a SIGSEGV within ~2 seconds of every launch**,
confirmed three independent ways before concluding this was real:

1. `run_app_background` + `gte_send_request` consistently failed to connect
   (`connection refused`) because the process had already died.
2. `wevtutil qe Application` (Windows Event Log) showed a real
   `Application Error` / Exception code `0xc0000005` (access violation) for
   `GreatTamanaEditor.exe`, reproducibly, across three separate launches.
3. Running the exact same binary under `gdb -batch -ex run -ex bt` produced a
   consistent, reproducible backtrace:
   `SIGSEGV` inside `gte::ComponentStorage<gte::SkeletalAnimator>::EntityAt()`,
   called from `gte::AnimationSystem::EvaluatePoses()` → `Game::Update()` →
   `Core::Update()` → `EditorHost::Run()` → `main()` — i.e. the very first
   normal frame update after startup, in a system that has **nothing to do**
   with the new marker entity (it never has a `SkeletalAnimator` component).

**Isolation, to rule out "pre-existing environment flakiness"**: `git stash`ed
every tracked change, manually reverted `HelloGame.cpp` to its pre-PHASE4
content (`Projects/` is `.gitignore`d, so `git stash` does not touch it), did
a clean incremental build, and ran the identical `gdb -batch -ex run -ex bt`
command — **the baseline ran cleanly for the full 30-second observation
window with zero crash**. Re-applying *only* the entity-spawn snippet (git
changes restored, since none of those execute any code until an HTTP request
hits the new route) reproduced the exact same crash. This conclusively proves
the crash is caused by the entity-spawn snippet itself, not environment
flakiness, not the other tracked-file changes, and not a pre-existing defect.

**Root cause**: `gte::detail::ComponentTypeId<T>()` (`src/ECS/Registry.h`)
assigns every distinct component type `T` a small integer slot number via a
Meyer's-singleton pattern — a function-local `static std::size_t next = 0;`
inside `NextComponentTypeId()`, incremented once per distinct `T` the first
time `ComponentTypeId<T>()` is ever instantiated. This is a **header-only,
inline template** — when the SAME template is instantiated from two
*separately linked binary images* (`GreatTamanaEditor.exe` and
`ProjectAssemblyProbe_Game.dll`), each image gets its **own, independent
copy** of that local static counter (this is standard, expected C++/PE-COFF
behavior for `.dll` boundaries on this toolchain — there is no automatic
cross-image deduplication of an inline function's function-local statics,
unlike `ComponentTypeRegistry::Instance()`, which is a plain out-of-line
singleton defined in a real `.cpp` file and therefore correctly exported/
imported as one single shared symbol). The practical consequence: the exact
same C++ type `gte::Transform` gets assigned **a different numeric slot ID**
depending on whether `ComponentTypeId<Transform>()` happens to run inside the
`.exe`'s own compiled code or inside the `.dll`'s own compiled code. Since
`Registry::m_pools` is one single, shared `std::vector<std::unique_ptr<IComponentPool>>`
(there genuinely is only one live `Registry` object — this part is fine),
calling `registry.AddComponent<gte::Transform>(entity)` **from DLL-compiled
code** looks up `m_pools[dllSideId]` — a slot that, from the `.exe`'s own
numbering, may already be occupied by a **completely unrelated** type's
`ComponentStorage<U>` (in this run, `SkeletalAnimator`). `Registry::Storage<T>()`'s
own `if (!m_pools[id]) { m_pools[id] = std::make_unique<ComponentStorage<T>>(); }`
check sees a **non-null** entry already there (the real `SkeletalAnimator`
pool) and therefore skips construction, then
`static_cast<ComponentStorage<T>&>(*m_pools[id])` reinterprets that pool's
memory as if it were a `ComponentStorage<Transform>` — a straightforward,
silent type-confusion/memory-corruption bug the instant `.Add()` is called
on it, which is exactly what later crashes `AnimationSystem::EvaluatePoses()`
when it reads the now-corrupted `SkeletalAnimator` storage's internal
`std::vector`s.

**The fix implemented** (a narrow, minimal, PHASE4-scoped workaround, not an
attempt to fix the underlying architectural gap — see "New gaps found"
below): `HelloGame.cpp` no longer calls `registry.AddComponent<gte::Transform>()`/
`<gte::Name>()` directly. Instead it goes through
`gte::ComponentTypeRegistry::Instance().Find("Transform")->ensureDefaultComponent(registry, entity)`
and `Find("Name")->tryGetMutableComponent(...)` + `FieldDescriptor::readJson()`
(the exact same generic, type-erased mechanism this same phase's own
`EngineCommandDispatch.cpp` case already uses for `ProbeHotReloadMarker`
itself). The key property that makes this safe: `ComponentTypeDescriptor`'s
function-pointer fields (`ensureDefaultComponent`/`tryGetMutableComponent`/
etc.) for **built-in** types were captured when `RegisterBuiltinComponentReflections()`
ran — a function defined in `src/ECS/Reflection/BuiltinComponentReflection.cpp`,
which is `gte_core`-tier and therefore compiled into, and executes inside,
the `.exe`'s own image. Calling these function pointers from DLL code (a
perfectly ordinary cross-module function-pointer call at the ABI level) still
runs the `.exe`'s own compiled body, using the `.exe`'s own — correct,
self-consistent — `ComponentTypeId<Transform>()`/`ComponentTypeId<Name>()`
values. `ProbeHotReloadMarker` itself was left as a **direct**
`registry.AddComponent<ProbeHotReloadMarker>(markerEntity).value = 7;` call,
which stays safe specifically because `RegisterComponentType<ProbeHotReloadMarker>()`
was *also* called from the DLL — every single access path to this one custom
type's storage (both this creation call, and every later access through the
`ComponentTypeDescriptor`'s own function pointers, which point at
DLL-compiled code since that's where `RegisterComponentType<ProbeHotReloadMarker>()`
ran) consistently uses the exact same DLL-local `ComponentTypeId<ProbeHotReloadMarker>()`
value — there is no cross-image mismatch as long as *only one side* ever
touches a given type.

Verified after the fix: a full clean rebuild, then `gdb -batch -ex run -ex bt`
for 30 seconds with **zero crash** (previously crashed within ~2 seconds,
100% reproducible across three separate runs before the fix).

## New gaps found (genuinely new, not previously documented anywhere in this
campaign or `editor-core-separation-13`)

1. **The core architectural bug itself, restated plainly**: `gte::Registry`'s
   `detail::ComponentTypeId<T>()` slot-numbering scheme (`src/ECS/Registry.h`)
   is **not safe** across the Project Assembly `.dll` boundary for any
   component type `T` that is used from **both** sides (the `.exe`/`gte_core`
   and a Project Assembly `.dll`) independently. This is a real,
   previously-undiscovered gap in the "Project Assembly System" itself (see
   `AGENTS.md`, "Project Assembly System"), not something specific to this
   one phase or this one probe fixture — **any** current or future Project
   Assembly `.dll` that calls `Registry::AddComponent<T>()`/`Storage<T>()`/
   `HasComponent<T>()`/`TryGetComponent<T>()` directly, for a **built-in**
   engine component type, risks the exact same silent memory corruption this
   phase found and worked around. This was never exercised before (confirmed:
   `search_in_dir` for `AddComponent` under `Projects/`/`plugins/` found zero
   matches before this phase's own first, crashing attempt) — this is a
   latent gap that simply had no prior test case to expose it.
2. **A second, related manifestation, confirmed live, that this phase's own
   narrow workaround does NOT fully close**: even with the fix above, `GET
   /project_assembly/debug/scene_snapshot` shows the pre-existing default
   Camera entity carrying a **spurious** `"ProbeHotReloadMarker": {"value": 0}`
   component it was never given — i.e. `ComponentTypeRegistry`'s generic,
   "walk every registered type over every entity" serialization (`Scene/SceneBuilder.cpp`'s
   `BuildSceneDocumentFromRegistry()`) incorrectly reports the Camera entity
   as carrying `ProbeHotReloadMarker` too. This is the exact same root cause
   as #1, just observed from the other direction: `ProbeHotReloadMarker`'s
   own DLL-local `ComponentTypeId<ProbeHotReloadMarker>()` slot happens to
   collide with whatever real, `.exe`-assigned slot the Camera entity's own
   legitimate component(s) occupy in the shared `m_pools` vector, so the
   DLL-compiled `hasComponent`/`tryGetConstComponent` function pointers
   `ComponentTypeRegistry` invokes for `ProbeHotReloadMarker` misreport/misread
   that unrelated slot's real data. **This did not affect this phase's own
   Definition of Done**: the mutation route's own entity-lookup loop
   (`GetChildren(registry, kInvalidEntity)`, in creation order) still reaches
   the real `ProbeHotReloadMarkerEntity` *first* and takes the early `break`,
   so `POST /project_assembly/debug/set_probe_marker_value` was confirmed,
   live, to correctly target the real entity and never the Camera's phantom
   copy (round-tripped `7 -> 42 -> 7` correctly, Camera's own phantom value
   stayed `0` throughout) — but this spurious-component artifact is a real,
   user-visible correctness bug in `GET /project_assembly/debug/scene_snapshot`'s
   own JSON today, for as long as any Project-Assembly-registered custom
   component type happens to collide with a real built-in type's slot number
   (essentially a coin flip, not something either side controls or can
   predict).
3. **Closing this properly is a real, non-trivial, future undertaking**, out
   of scope for this narrow phase: the underlying fix needs `Registry`/
   `ComponentTypeId<T>()` to hand out slot numbers from a single,
   process-wide, `.exe`-owned authority a `.dll` always queries into (instead
   of each image instantiating its own independent counter) — conceptually
   similar to how `ComponentTypeRegistry::Instance()` itself already works
   correctly (a real, out-of-line, exported/imported singleton, not a
   header-only template). This is flagged here, honestly and explicitly, for
   whichever future campaign is best positioned to take it on — it was NOT
   fixed as part of this phase, since PHASE0's own plan is explicit that
   PHASE4 "only needs to EDIT existing files" for a narrow probe fixture, and
   rearchitecting `Registry`'s slot-numbering scheme is unambiguously a much
   larger, separate undertaking than that.
4. **Process note**: `ask_questions` was used to ask the user how to proceed
   once this bug was confirmed (repurpose to the safe reflection-based
   workaround vs. attempt the deeper `Registry.h` fix vs. something else), but
   the 30-minute window expired with no response. Proceeded with the
   documented, narrower, lower-risk workaround (Option A) rather than
   attempting the larger architectural fix, and is documenting the finding
   here in full per the same question's own second part, so a human reviewer
   can weigh in after the fact.

## Live verification performed (this phase's own isolated route test, PHASE4's
own Step 3 — no hot-reload cycle triggered, per PHASE4's own scope)

Against a real running `GreatTamanaEditor.exe` (`run_app_background` →
`gte_send_request` → `stop_app_background`):

1. `GET /project_assembly/debug/scene_snapshot` — confirmed `"ProbeHotReloadMarkerEntity"`
   present with `"ProbeHotReloadMarker": {"value": 7}` (Step 1's own baseline).
2. `POST /project_assembly/debug/set_probe_marker_value?value=42` — `{"success":true}`.
3. `GET /project_assembly/debug/scene_snapshot` again — confirmed `value` is
   now genuinely `42` (not just that the route reported `true` — this is the
   exact bug class PHASE4's own plan already flagged as previously caught
   during review; re-confirmed truly fixed here).
4. `POST /project_assembly/debug/set_probe_marker_value?value=7` — reset back
   to the documented baseline; re-confirmed via a follow-up `GET
   /project_assembly/debug/scene_snapshot` showing `value: 7` again.
5. `POST /project_assembly/debug/set_probe_marker_value?value=` (empty) and
   `POST /project_assembly/debug/set_probe_marker_value` (missing entirely) —
   both returned `400` with `{"success":false,"error":"missing or invalid
   required query parameter: value - must be an integer"}`, never a crash.
6. `stop_app_background` cleanly stopped the engine process afterward.

## Compile check

`cmake --build build` (working directory the repo root): a full incremental
build succeeded cleanly (48/48 steps), including `gte_core`, `gte_editor`,
`GreatTamanaEditor.exe`, `GreatTamanaEngineTests.exe`, and both
`ProjectAssemblyProbe_Editor.dll`/`ProjectAssemblyProbe_Game.dll` targets, with
zero errors (`Select-String -Pattern 'error'` against the full build log found
nothing). No full `ctest` regression pass was run, per this phase's own scope
rules (that is PHASE5's job).

## Definition of Done — checked against the phase file's own list

- [x] `ProjectAssemblyProbe`'s `RegisterProbeGame()` spawns a real, permanent
      entity carrying `ProbeHotReloadMarker{value=7}`, confirmed live via `GET
      /project_assembly/debug/scene_snapshot`.
- [x] `IHotReloadDebugCapability::SetProbeHotReloadMarkerValueForTesting()`
      exists, is wired end-to-end (interface → `EditorHotReloadDebugCapability`
      → `EngineCommandBridge` → `EngineCommandDispatch.cpp` → the new HTTP
      route), and is confirmed live per the Step 3 checklist above — including
      the round-trip check that the value ACTUALLY CHANGED, not merely that
      the route reported `true`.
- [x] The route's own honest `false`/`503`/`400` outcomes are confirmed (400
      for missing/non-integer `value`, confirmed twice — empty string and
      fully absent param — never a crash; `503`'s own code path was inspected,
      not separately live-tested, since deliberately unloading the probe
      project is out of this phase's own scope).
- [x] The marker's value is confirmed reset to `7` before this phase ends.

## What this phase does NOT do (unchanged from the phase file's own list)

- Does NOT widen this new capability into a generic "set any component field"
  route — hardcoded to `ProbeHotReloadMarker`/`value` only, permanently
  (LDD-HR6).
- Does NOT itself trigger a hot-reload cycle to prove persistence across one
  — that full, combined test is PHASE5's own job.
- Does NOT reuse `m_hotReloadCommandBridge` for this plain, single-frame
  Registry mutation — uses the general `EngineCommandBridge` instead, exactly
  as the phase file specified.
- Does NOT attempt to fix the deeper `Registry::detail::ComponentTypeId<T>()`
  cross-`.dll`-boundary architectural gap described above — flagged honestly
  for a future campaign instead.

PHASE4 is complete, with the one significant, safety-critical deviation (and
its follow-on, only-partially-closed gap) fully disclosed above. PHASE5 (Full
Live Verification, success + rollback paths, plus the campaign's permanent
documentation fix) may begin — but should re-read this report first, since
PHASE5's own hot-reload cycle will exercise `ProbeHotReloadMarker` across a
real unload/reload, and the spurious-Camera-component artifact (gap #2 above)
may resurface there too and should not be mistaken for a new PHASE5 bug.
