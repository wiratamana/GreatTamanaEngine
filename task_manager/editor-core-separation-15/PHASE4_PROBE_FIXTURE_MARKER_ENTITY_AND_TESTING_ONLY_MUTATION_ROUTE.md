# PHASE4 — Probe Fixture: A Real Marker Entity + The One Narrow Testing-Only Mutation Route

Parent: `PHASE0_MASTER_STRATEGY.md` (LDD-HR6 is the load-bearing decision
for this phase). Read `PHASE1`/`PHASE2`/`PHASE3`'s own completion reports
first.

Depends on: PHASE2 (capture) AND PHASE3 (restore), both fully done.
Blocks: PHASE5 (the full live verification test has nothing concrete to
mutate/verify without this phase).

End state of this phase: `ProjectAssemblyProbe`'s own `GTE_RegisterProject`
spawns one real, permanent entity carrying a live `ProbeHotReloadMarker`
component with a known starting value; exactly ONE new, narrow,
hardcoded, test-only HTTP debug route exists that can change that
component's value at runtime, for verification purposes only (LDD-HR6) —
and nothing else.

---

## STEP 1 — Give `ProbeHotReloadMarker` a real, live entity

Confirmed, current, `Projects/ProjectAssemblyProbe/Assets/HelloGame.cpp`:
the type is registered (lines 73-75) but no entity anywhere carries it.
Add, inside `RegisterProbeGame(gte::Core& core)`, immediately after the
existing `gte::RegisterComponentType<ProbeHotReloadMarker>(...)` call (i.e.
right before the existing `core.RegisterProjectRenderPassProvider(...)`
call that already follows it):

```cpp
// editor-core-separation-15 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 4), PHASE4 - a real, permanent, LIVE entity carrying
// ProbeHotReloadMarker, so a real hot-reload cycle has something concrete
// to preserve (editor-core-separation-13's own PHASE5 only registered the
// TYPE, to exercise Hazard 1's duplicate-registration assert - it never
// spawned an entity, which is why THIS campaign's own capture/restore
// proof needed this addition). Deliberately kept a plain, top-level root
// entity with no children/no parent - the ONE new debug route below
// (Step 2) only ever needs to find "the first live entity carrying
// ProbeHotReloadMarker", a flat GetChildren(registry, kInvalidEntity)
// root scan, no recursion needed for this single, simple fixture.
gte::Registry& registry = core.GetGame().GetRegistry();
const gte::Entity markerEntity = registry.CreateEntity();
registry.AddComponent<gte::Transform>(markerEntity); // every entity BuildSceneDocumentFromRegistry() walks must have one - see Scene/SceneBuilder.h's own doc comment.
registry.AddComponent<gte::Name>(markerEntity).value = "ProbeHotReloadMarkerEntity";
registry.AddComponent<ProbeHotReloadMarker>(markerEntity).value = 7;
```

New `#include`s this needs at the top of `HelloGame.cpp` (confirmed not
already present): `"../../../src/ECS/Components/Transform.h"`,
`"../../../src/ECS/Components/Name.h"`, `"../../../src/ECS/Registry.h"`
(mirrors this file's own existing `"../../../src/..."` relative-include
convention exactly, confirmed lines 22-33).

**Use `POST /project_assembly/debug/compile_only?name=ProjectAssemblyProbe`
to iterate on this change with fast compiler feedback (`GET /get_logs`)
before ever attempting a live reload** — mirrors this whole effort's own
repeated "de-risk with `compile_only` first" convention.

**Verify concretely**: after a real reload (or a fresh engine launch) with
this change live, confirm via `GET /project_assembly/debug/scene_snapshot`
that an entity named `"ProbeHotReloadMarkerEntity"` exists with a
`"ProbeHotReloadMarker": { "value": 7 }` component block.

---

## STEP 2 — The one, narrow, hardcoded, testing-only mutation route
(LDD-HR6)

`src/Core/EditorCapabilities.h` — append ONE new pure-virtual method to
`IHotReloadDebugCapability` (confirmed current interface, `class
IHotReloadDebugCapability` opens at line 173 and the class itself closes
at line 237/`};` — re-verify this exact line by direct read before editing,
since earlier phases in this same folder may have already appended lines
above it — append immediately after `TriggerHotReload()`, the interface's
own last method today):

```cpp
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
```

`src/Editor/EditorHotReloadDebugCapability.h` — declare the override
(append after `TriggerHotReload()`), plus one new setter/member pair
(mirrors `m_hotReloadCommandBridge`'s own exact "setter, not a constructor
parameter" shape immediately below it):

```cpp
bool SetProbeHotReloadMarkerValueForTesting(int value) override;

// editor-core-separation-15 campaign, PHASE4 - gives this capability a
// live EngineCommandBridge& to submit the one new
// SetProbeHotReloadMarkerValueForTesting engine command through (this is
// EditorHost's own GENERAL m_commandBridge - the SAME bridge
// GetSceneSnapshot/SaveScene/LoadScene/etc already share - NOT
// m_hotReloadCommandBridge, which is a separate, dedicated bridge only
// for a full hot-reload CYCLE). Same "setter, not a constructor
// parameter" reasoning as SetProjectAssemblyHost()/
// SetHotReloadCommandBridge() above - called exactly once, from
// EditorHost's own constructor body.
void SetEngineCommandBridge(EngineCommandBridge& bridge) noexcept;
```

...and one new private member, alongside `m_hotReloadCommandBridge`:

```cpp
// editor-core-separation-15 campaign, PHASE4 - defaulted null so this
// class's existing default, no-argument constructor is completely
// untouched; SetProbeHotReloadMarkerValueForTesting() defensively returns
// false if this is still null (should never happen in real production
// wiring).
EngineCommandBridge* m_engineCommandBridge = nullptr;
```

(`EngineCommandBridge` needs a forward declaration near this header's
existing `class ProjectAssemblyHotReloadCommandBridge;` forward
declaration — this header must NOT gain a real `#include
"../Application/EngineCommandBridge.h"`, mirroring this whole header's own
existing "carries ZERO dependency on the concrete types its .cpp needs"
discipline stated in its own header comment.)

`src/Editor/EditorHotReloadDebugCapability.cpp` — real body, appended
after `TriggerHotReload()`'s own body, PLUS the new setter's body appended
after `SetHotReloadCommandBridge()`'s own body:

```cpp
bool EditorHotReloadDebugCapability::SetProbeHotReloadMarkerValueForTesting(int value)
{
    // Called from the NETWORK thread (a route handler) - the live
    // Registry has no synchronization of its own (see this whole class's
    // own header comment), so this submits a new EngineCommandKind
    // through EditorHost's general EngineCommandBridge and blocks until
    // the main thread's own per-frame drain point services it - mirrors
    // GetSceneSnapshot's identical bridge-based precedent (Application/
    // EngineCommandDispatch.cpp), NOT TriggerHotReload()'s own separate,
    // dedicated ProjectAssemblyHotReloadCommandBridge (that bridge is only
    // for a FULL hot-reload cycle, never for a plain, fast, single-frame
    // Registry mutation like this one).
    if (m_engineCommandBridge == nullptr) {
        return false; // Should never happen in real production wiring - see SetEngineCommandBridge()'s own call-ordering guarantee.
    }
    EngineCommandRequest request;
    request.kind = EngineCommandKind::SetProbeHotReloadMarkerValueForTesting;
    request.setProbeHotReloadMarkerValueForTesting.value = value;
    const EngineCommandBridge::SubmitResult submit = m_engineCommandBridge->SubmitAndWait(request);
    if (submit.alreadyPending || submit.timedOut || !submit.result.has_value()) {
        // Mirrors every other bool-returning method on this same
        // interface (TriggerCompileOnly/TriggerHotReload): a submission
        // failure collapses to a plain, honest `false`, never a crash and
        // never a dereference of an empty std::optional.
        return false;
    }
    return submit.result->setProbeHotReloadMarkerValueForTesting.success;
}

void EditorHotReloadDebugCapability::SetEngineCommandBridge(EngineCommandBridge& bridge) noexcept
{
    m_engineCommandBridge = &bridge;
}
```

**Concrete wiring, mirroring `GetSceneSnapshot`'s own existing precedent
exactly** (`Application/EngineCommandBridge.h` lines 64/100/125/145,
`EngineCommandDispatch.cpp` lines 92-101 — re-confirm these exact line
numbers by direct read before editing, since PHASE1-3 of this same
campaign may have already shifted them):

1. `EngineCommandBridge.h` — one new `EngineCommandKind::SetProbeHotReloadMarkerValueForTesting`
   (appended to the existing enum, alongside `GetSceneSnapshot`), one new
   payload struct `SetProbeHotReloadMarkerValueForTestingCommand { int
   value = 0; }` (in this same header, alongside `GetSceneSnapshotCommand`),
   one new outcome struct `SetProbeHotReloadMarkerValueForTestingOutcome {
   bool success = false; }` (in `Game/EngineCommandResults.h`, alongside
   `GetSceneSnapshotOutcome` — deliberately WITHOUT that struct's own
   `editorAvailable`/`errorMessage` fields: this method's own caller,
   `EditorHotReloadDebugCapability` itself, is the ONLY thing that ever
   submits this command, so "the Editor module isn't available" can never
   actually happen for this one — keep this outcome struct genuinely
   minimal, matching LDD-HR6's own "narrow, not general" spirit), both
   added as new tagged fields on `EngineCommandRequest`/`EngineCommandResult`.
2. `EngineCommandDispatch.cpp` — one new
   `case EngineCommandKind::SetProbeHotReloadMarkerValueForTesting:` block
   (needs two new `#include`s this file does not yet have:
   `"../ECS/Reflection/ComponentTypeRegistry.h"` and
   `"../ECS/TransformHierarchy.h"`, for `GetChildren()`):

   ```cpp
   case EngineCommandKind::SetProbeHotReloadMarkerValueForTesting: {
       bool success = false;
       if (const ComponentTypeDescriptor* descriptor = ComponentTypeRegistry::Instance().Find("ProbeHotReloadMarker");
           descriptor != nullptr) {
           for (const Entity root : GetChildren(game.GetRegistry(), kInvalidEntity)) {
               if (void* component = descriptor->tryGetMutableComponent(game.GetRegistry(), root); component != nullptr) {
                   // MUST be a JSON OBJECT keyed by the field's own name
                   // ("value"), never the bare scalar itself -
                   // FieldDescriptor::readJson()'s real implementation
                   // (ReflectFieldMacros.h's GTE_REFLECT_FIELD-generated
                   // lambda) does `in.contains(#member)` /
                   // `in.at(#member)` internally - passing a bare
                   // nlohmann::json(value) here would make contains()
                   // silently return false (nlohmann::json::contains() on
                   // a non-object value is well-defined and simply
                   // returns false, never throws), so readJson() would
                   // silently return true having written NOTHING - a
                   // false-positive "success" that never actually mutates
                   // anything. Confirmed against the real, current
                   // ReflectFieldMacros.h before writing this.
                   nlohmann::json fields;
                   fields["value"] = request.setProbeHotReloadMarkerValueForTesting.value;
                   std::string errorMessage;
                   for (const FieldDescriptor& field : descriptor->fields) {
                       if (field.name == "value") {
                           success = field.readJson(component, fields, errorMessage);
                       }
                   }
                   break; // First live entity carrying this component wins - Step 1's own fixture is a single, plain root, so this is unambiguous.
               }
           }
       }
       result.setProbeHotReloadMarkerValueForTesting.success = success;
       break;
   }
   ```

3. `src/Network/NetworkRoutes.h`/`.cpp` — one new parsed-query pair,
   mirroring `ParsedFrameDebuggerSelectEventQuery`/
   `ParseFrameDebuggerSelectEventQuery()`'s own exact shape (`NetworkRoutes.h`,
   declared right after `struct ParsedFrameDebuggerEnableQuery` — a
   "value" query parameter parsed as a whole decimal integer via the SAME
   private `TryParseWholeInt()` helper already defined in `NetworkRoutes.cpp`'s
   own anonymous namespace, reused here, not reinvented):

   ```cpp
   // GET/POST value=<N> query parsing for
   // POST /project_assembly/debug/set_probe_marker_value - "value" must be
   // present and parse as a whole decimal integer, otherwise "missing or
   // invalid required query parameter: value - must be an integer".
   struct ParsedSetProbeMarkerValueQuery {
       bool valid = false;
       std::string errorMessage;
       int value = 0;
   };
   ParsedSetProbeMarkerValueQuery ParseSetProbeMarkerValueQuery(const std::string& valueParam);
   ```

   `.cpp` body (mirrors `ParseFrameDebuggerSelectEventQuery()`'s own body
   exactly, substituting the field name/message):

   ```cpp
   ParsedSetProbeMarkerValueQuery ParseSetProbeMarkerValueQuery(const std::string& valueParam)
   {
       ParsedSetProbeMarkerValueQuery result;
       int parsedValue = 0;
       if (!TryParseWholeInt(valueParam, parsedValue)) {
           result.errorMessage = "missing or invalid required query parameter: value - must be an integer";
           return result;
       }
       result.value = parsedValue;
       result.valid = true;
       return result;
   }
   ```

4. `src/Network/NetworkServer.cpp` — one new route, mirroring `POST
   /project_assembly/debug/compile_only`'s own exact shape (same file,
   same `hotReloadDebugCapability` capture already in scope at that point
   in the function):

   ```cpp
   server.Post("/project_assembly/debug/set_probe_marker_value",
       [hotReloadDebugCapability](const httplib::Request& req, httplib::Response& res) {
       const ParsedSetProbeMarkerValueQuery parsed = ParseSetProbeMarkerValueQuery(req.get_param_value("value"));
       if (!parsed.valid) {
           res.status = 400;
           res.set_content(BuildGenericErrorResponseJson(parsed.errorMessage), "application/json");
           return;
       }
       if (hotReloadDebugCapability == nullptr) {
           res.status = 503;
           res.set_content(BuildGenericErrorResponseJson("hot reload debug capability not available"), "application/json");
           return;
       }
       const bool success = hotReloadDebugCapability->SetProbeHotReloadMarkerValueForTesting(parsed.value);
       nlohmann::json body;
       body["success"] = success;
       res.set_content(body.dump(), "application/json");
   });
   ```

   Route contract:

   ```
   POST /project_assembly/debug/set_probe_marker_value?value=<N>
       -> { "success": true } (200) - the mutation genuinely reached a live
          entity's live component.
       -> { "success": false } (200) - "no live entity currently carries
          ProbeHotReloadMarker" (e.g. the probe project is unloaded) is a
          normal, honest, non-error outcome, mirrors compile_only's own
          "already building" -> 200 convention.
       -> 400 (missing/non-integer `value` query param)
       -> 503 (capability unavailable)
   ```

5. `src/Editor/EditorHost.cpp` — the new setter's own call site, appended
   immediately after the existing
   `s_editorHotReloadDebugCapability.SetHotReloadCommandBridge(m_hotReloadCommandBridge);`
   line (confirmed current, line 232 — re-verify by direct read before
   editing):

   ```cpp
   s_editorHotReloadDebugCapability.SetEngineCommandBridge(m_commandBridge);
   ```

   (`m_commandBridge` is `EditorHost`'s own pre-existing general
   `EngineCommandBridge` member — confirmed, `EditorHost.h`, already in
   scope inside this same constructor body.)

---

## STEP 3 — Live verification for this phase alone (PHASE5 runs the FULL
end-to-end reload test; this phase only proves the new route itself
works, in isolation, without yet triggering a reload)

1. Launch the engine. `GET /project_assembly/debug/scene_snapshot` —
   confirm `"ProbeHotReloadMarkerEntity"` / `value: 7` (Step 1's own
   baseline).
2. `POST /project_assembly/debug/set_probe_marker_value?value=42` —
   confirm `{"success": true}`.
3. `GET /project_assembly/debug/scene_snapshot` again — confirm `value`
   is now `42` (proves the mutation route genuinely reaches the live
   Registry, and that the JSON shape `field.readJson()` receives is
   correct — a wrong-shaped call would report `{"success": true}` while
   leaving `value` unchanged at `7`; if that happens, the JSON
   construction in the new `EngineCommandDispatch.cpp` case is the first
   place to re-check).
4. Set it back to `7` (`POST .../set_probe_marker_value?value=7`) before
   ending this phase's own session, so PHASE5 starts from the documented
   baseline.
5. Send `POST /project_assembly/debug/set_probe_marker_value?value=`
   (empty/missing `value`) — confirm `400`, never a crash.

---

## Definition of Done — this phase only

- [ ] `ProjectAssemblyProbe`'s `RegisterProbeGame()` spawns a real,
      permanent entity carrying `ProbeHotReloadMarker{value=7}`, confirmed
      live via `GET /project_assembly/debug/scene_snapshot`.
- [ ] `IHotReloadDebugCapability::SetProbeHotReloadMarkerValueForTesting()`
      exists, is wired end-to-end (interface -> `EditorHotReloadDebugCapability`
      -> `EngineCommandBridge` -> `EngineCommandDispatch.cpp` -> the new
      HTTP route), and is confirmed live per Step 3 above — including the
      round-trip check (Step 3.3) that the value ACTUALLY CHANGED, not
      merely that the route reported `true`.
- [ ] The route's own honest `false`/`503`/`400` outcomes are confirmed
      (e.g. calling it while the probe project is deliberately unloaded
      returns `{"success": false}`, an empty/non-integer `value` returns
      `400` — never a crash).
- [ ] The marker's value is confirmed reset to `7` (the documented
      baseline) before this phase ends.

## What this phase does NOT do

- Does NOT widen this new capability into a generic "set any component
  field" route — hardcoded to `ProbeHotReloadMarker`/`value` only,
  permanently (LDD-HR6).
- Does NOT itself trigger a hot-reload cycle to prove persistence across
  one — that full, combined test (mutate, reload, re-check) is PHASE5's
  own job, deliberately kept separate so this phase's own new route can be
  verified in isolation first.
- Does NOT reuse `m_hotReloadCommandBridge` (the dedicated, full-cycle hot
  reload bridge) for this plain, single-frame Registry mutation — those
  are two genuinely different bridges, each with its own single purpose;
  conflating them would make a future BIG-STEP 3 change to the hot-reload
  cycle's own bridge semantics risk silently breaking this unrelated
  testing-only route too.
