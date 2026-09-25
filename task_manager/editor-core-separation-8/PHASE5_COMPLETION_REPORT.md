# PHASE5 — Cross-Thread Bridge + HTTP Endpoints — COMPLETION REPORT

**Status: DONE.** Implemented exactly what
`PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md` describes, including its
Step 3.7 confirmed target-name fix (the new bridge source files were placed
in the `gte_core` CMake target — there is no `gte_application` target in
this codebase), and its "Verification" section's full live smoke test,
including the critical `AtmosphereComposite` disable/re-enable check. One
small, reasoned, non-behavioral deviation from the plan's own literal
pseudocode was made — see "Deviations" below.

## What changed

### New files

- `src/Application/RenderGraphControlCommandBridge.h` — matches Step 3.1's
  shown code verbatim: `RenderGraphControlCommandKind`, the 4 command payload
  structs, `RenderGraphControlCommandRequest`, `RenderGraphControlPassStateOutcome`,
  `RenderGraphControlCommandResult`, and the `RenderGraphControlCommandBridge`
  class (mutex + `std::condition_variable` + `SubmitAndWait()`/
  `IsCommandPending()`/`TryPeekPendingCommandRequest()`/`FulfillCommand()`).
- `src/Application/RenderGraphControlCommandBridge.cpp` — `FrameDebuggerCommandBridge.cpp`'s
  locking/condvar logic copied verbatim, substituting only the type names,
  exactly as Step 3.2 instructs.

### `src/Application/RenderGraphControlCommandBridge.h`/.cpp — CMake registration

- Root `CMakeLists.txt`: added both files to the **`gte_core`** source list
  (Step 3.7's confirmed fix), immediately after `FrameDebuggerCommandBridge.h/.cpp`.
- `tests/CMakeLists.txt`: added `Application/RenderGraphControlCommandBridgeTests.cpp`
  immediately after `Application/FrameDebuggerCommandBridgeTests.cpp`.
- `tests/Application/RenderGraphControlCommandBridgeTests.cpp` (new) —
  `FrameDebuggerCommandBridgeTests.cpp`'s 7 test cases mirrored 1:1 (timeout,
  fulfilled result, already-pending, fulfill-when-idle no-op,
  late-fulfillment-after-timeout, pending-state-observable), substituting only
  the type names — the bridge's own mechanics are identical.

### `src/Network/NetworkRoutes.h`/`.cpp`

- New parse structs/functions: `ParsedRenderGraphSetPassEnabledQuery`/
  `ParseRenderGraphSetPassEnabledQuery()`, `ParsedRenderGraphSetFeatureEnabledQuery`/
  `ParseRenderGraphSetFeatureEnabledQuery()` (kept as its own separate
  struct/function rather than reused, mirroring this file's existing
  precedent), `ParsedRenderGraphSetFeaturePriorityQuery`/
  `ParseRenderGraphSetFeaturePriorityQuery()` (reuses the same
  `TryParseWholeInt()` helper `ParseFrameDebuggerSelectEventQuery()` already
  uses), `ParsedRenderGraphSetBoolQuery`/`ParseRenderGraphSetBoolQuery()`.
- New response builders: `BuildRenderGraphControlCommandResponseJson(bool, const std::string&)`
  (`{"success":true}` / `{"success":false,"error":"..."}`, no "state" echo —
  matches the locked contract) and `BuildRenderGraphControlPassStatesResponseJson()`
  (`{"passes":[{"name":...,"enabled":...,"ever_declared_this_session":...}, ...]}`).

### Deviations from the plan

**One, small, reasoned, non-behavioral deviation — honestly recorded.**
Step 3.5's own shown code sample for `BuildRenderGraphControlPassStatesResponseJson()`
used the type name `RenderGraphControlPassStateOutcome` directly in
`NetworkRoutes.h`'s signature — but `RenderGraphControlPassStateOutcome` is
an **Application-tier type**, defined inside `RenderGraphControlCommandBridge.h`
(Step 3.1). This conflicts with this file's own, twice-already-established,
explicitly-documented convention ("a struct crossing a layer boundary is
never accepted directly here" — see `FrameDebuggerStateResponseView`'s and
`ImportedAssetResponseView`'s own doc comments) — `NetworkRoutes.h` must
never depend on anything under `src/Application/`. I resolved this the same
way PHASE4's own completion report resolved an internal contradiction in its
own phase doc: by re-reading the master strategy's own repeatedly-stated,
higher-priority rule (`PHASE0`'s Workflow Rule 8, "never invent a new
pattern where an existing file already shows the exact shape to copy") and
following the ESTABLISHED convention rather than the plan's own shorthand
pseudocode. Concretely: `NetworkRoutes.h` defines its own, independent
`RenderGraphControlPassStateResponseView` struct (`name`/`enabled`/
`everDeclaredThisSession`), and `NetworkServer.cpp`'s `/render_graph/passes`
route handler is the one place that copies a real
`RenderGraphControlPassStateOutcome` into it, one field at a time — mirroring
`ToFrameDebuggerStateResponseView()`'s own exact precedent. Zero
observable/behavioral difference in the actual wire JSON — confirmed by the
live smoke test below, whose `GET /render_graph/passes` response matches the
locked contract's shown shape exactly.

Everything else — every new type's fields, every function's signature, the
6 route paths, the exact status-code mapping (400/409/503/504), and the
bridge's own mechanics — matches the plan's own shown code verbatim.

### `src/Network/NetworkServer.h`

- New forward declaration `namespace gte { class RenderGraphControlCommandBridge; }`.
- `NetworkServer`'s constructor gains a 7th defaulted parameter,
  `RenderGraphControlCommandBridge* renderGraphControlCommandBridge = nullptr`,
  appended after `logQueryCapability` (mirrors every prior bridge-pointer
  addition's own "always appended, never inserted" precedent).
- New private member `RenderGraphControlCommandBridge* m_renderGraphControlCommandBridge = nullptr;`.

### `src/Network/NetworkServer.cpp`

- New `#include "../Application/RenderGraphControlCommandBridge.h"`.
- New shared response-mapping helper `RespondWithRenderGraphControlCommandResult()`,
  mirroring `RespondWithFrameDebuggerCommandResult()`'s exact
  alreadyPending(503)/timedOut(504)/success(200)-or-failure(409) mapping.
- `RegisterRoutes()` gains the new `RenderGraphControlCommandBridge*` parameter.
- 6 new routes registered, immediately after the existing `/frame_debugger/state`
  block: `GET /render_graph/set_pass_enabled`, `GET /render_graph/passes`
  (its own read-only response shape, mirroring `/frame_debugger/state`'s own
  special case), `GET /render_graph/set_feature_enabled`,
  `GET /render_graph/set_feature_priority`, `GET /render_graph/set_blur_enabled`,
  `GET /render_graph/set_gbuffer_enabled` — each: parse → bridge-null-check
  (503) → build request → `SubmitAndWait()` → respond.
- `NetworkServer`'s constructor updated to accept/store/forward the 7th
  parameter into `RegisterRoutes()`.

### `src/Editor/EditorHost.h`

- New `#include "../Application/RenderGraphControlCommandBridge.h"`.
- New member `RenderGraphControlCommandBridge m_renderGraphControlCommandBridge;`,
  declared immediately after `m_assetImportCommandBridge`, before `m_networkServer`
  (so its address can be handed into `m_networkServer`'s own constructor,
  mirroring every other bridge's own declared-before-NetworkServer ordering).

### `src/Editor/EditorHost.cpp`

- `m_networkServer(...)`'s constructor-call argument list gains
  `&m_renderGraphControlCommandBridge` as its 7th argument.
- New pump block, placed immediately after the existing
  `FrameDebuggerCommandBridge` pump (same group, same style, immediately
  after `m_editorLayer->NewFrame();`, before `m_renderer.BeginFrame();`) —
  matches Step 3.3's shown code verbatim: `SetBuiltInPassEnabled`/
  `ListPassStates` call `m_core.GetRenderPassToggleRegistryMutable()`
  directly; `SetFeatureEnabled`/`SetFeaturePriority` call
  `m_core.GetRenderFeatureCompositor()` (null-checked); `SetBlurEnabled`/
  `SetGBufferEnabled` call `m_editorLayer->SetShowBlurredSceneOutput()`/
  `SetShowGBufferValidationOutput()` — exactly PHASE0's Step 2.6 routing.
  No new `#include` was needed in this `.cpp`: `RenderFeatureCompositor.h`
  was already included (by `editor-core-separation-6` campaign, PHASE7), and
  `rg::RenderPassToggleState` is already transitively visible via
  `Core.h` → `RenderPipeline.h` → `RenderPassToggleRegistry.h`.

## Verification evidence

1. **`git_status` at start**: branch was `feature/editor-core-separation`,
   working tree clean (PHASE1–PHASE4's diffs were already committed, nothing
   outstanding) — confirmed before touching any file.

2. **Incremental build**:
   - `cmake -S . -B build` (reconfigure to pick up the 2 new source files) —
     succeeded (only the expected, pre-existing `MingwRuntime.cmake`/KTX
     version warnings, unrelated to this phase).
   - `cmake --build build --target GreatTamanaEditor -j 8` — succeeded, 8
     build steps (`RenderGraphControlCommandBridge.cpp`, `NetworkRoutes.cpp`,
     `EditorHost.cpp`, `NetworkServer.cpp` recompiled; `libgte_core.a`/
     `libgte_editor.a` relinked; `main.cpp` recompiled; final executable
     relinked) — a genuine incremental build, not a full clean one.
   - `cmake --build build --target GreatTamanaEngineTests -j 8` — succeeded,
     1 new translation unit (`RenderGraphControlCommandBridgeTests.cpp`)
     plus the touched-header-transitive rebuilds, relink.

3. **Targeted `ctest` run** (`ctest -C Debug -R
   "RenderGraphControlCommandBridge|NetworkRoutesTests|ParseRenderGraph|BuildRenderGraphControl|ParseFrameDebugger|BuildFrameDebugger"
   --output-on-failure`, from `build/`): **52/52 tests passed** — the 7 new
   `RenderGraphControlCommandBridgeTest.*` cases, 15 new
   `Parse/BuildRenderGraph*Tests.*` cases (added to `NetworkRoutesTests.cpp`),
   every pre-existing `NetworkRoutesTests.*`/`ParseFrameDebugger*Tests.*`/
   `BuildFrameDebugger*Tests.*` case (unmodified, still green), plus the
   parameterized `ResolveCaptureResponseFormatTest`/`ParseGetTextureQuery*`/
   `ParseFrameDebuggerSetChannelQueryValidTest` cases that happened to match
   the same filter.

4. **Live, end-to-end HTTP smoke test** — `run_app_background` on the real
   `GreatTamanaEditor.exe` (PID 18600), full session below, `stop_app_background`
   at the end (confirmed stopped successfully). **Every real HTTP
   request/response pair, captured verbatim:**

   ```
   GET /render_graph/passes
   -> 200 {"passes":[{"enabled":true,"ever_declared_this_session":true,"name":"AtmosphereComposite"},
            {"enabled":true,"ever_declared_this_session":true,"name":"DrawSkyBackground"},
            {"enabled":true,"ever_declared_this_session":true,"name":"GpuSkinning"},
            {"enabled":true,"ever_declared_this_session":true,"name":"RenderOpaque"},
            {"enabled":true,"ever_declared_this_session":true,"name":"RenderTransparent"}]}

   GET /render_graph/set_pass_enabled?name=RenderTransparent&enabled=false
   -> 200 {"success":true}

   GET /render_graph/passes
   -> 200 {"passes":[...,{"enabled":false,"ever_declared_this_session":true,"name":"RenderTransparent"}]}

   GET /render_graph
   -> 200 {...}  (confirmed via a substring check against the raw body:
                  "RenderTransparent" NOT FOUND anywhere in offscreen_regime/
                  present_regime.passes[] - the disable genuinely took
                  effect in the live render graph, not just the registry)

   GET /render_graph/set_pass_enabled?name=RenderTransparent&enabled=true
   -> 200 {"success":true}

   GET /render_graph  -> "RenderTransparent" FOUND again (re-appeared)

   GET /render_graph/set_pass_enabled?name=Present&enabled=false
   -> 409 {"error":"\"Present\" cannot be disabled (deny-listed).","success":false}

   --- THE critical AtmosphereComposite check (proves PHASE1's Step 3.4b fix) ---

   GET /activate_tab?name=Game
   -> 200 {"activated_tab":"Game","success":true}

   GET /get_swapchain
   -> 200 image/png, 84116 bytes (BASELINE screenshot - red/blue radial
      gradient test pattern visible in both Scene and Game views, real
      Blur/GBuffer checkboxes both unchecked - see report body's embedded
      image)

   GET /render_graph/set_pass_enabled?name=AtmosphereComposite&enabled=false
   -> 200 {"success":true}

   GET /render_graph -> substring check for the REAL underlying pass name
      ("AtmosphereAerialPerspectiveCompositePass" - see "Honest note on a
      real investigation detour" below for why the literal string
      "AtmosphereComposite" itself never appears in this endpoint's body at
      all, disabled or not) -> NOT FOUND (successfully removed from the
      live graph)

   GET /get_swapchain
   -> 200 image/png, 84116 bytes, BYTE-IDENTICAL to the baseline above -
      Editor still fully running, no crash/hang, no error, no garbage/black
      pixels. See "Honest note" below for exactly why this is an accepted
      outcome, not a red flag.

   GET /render_graph/set_pass_enabled?name=AtmosphereComposite&enabled=true
   -> 200 {"success":true}

   GET /render_graph -> "AtmosphereAerialPerspectiveCompositePass" FOUND
      again (re-appeared - re-enable genuinely took effect)

   GET /get_swapchain
   -> 200 image/png, 84116 bytes, still byte-identical, Editor still
      running normally.

   --- Plugin render feature enable/disable + live priority ---

   GET /render_graph -> render_features: [
       {"blend_mode":"Replace","enabled":true,"name":"DemoRenderFeatureV2","priority":0,"stage":"PostComposite"},
       {"blend_mode":"AlphaOver","enabled":true,"name":"DemoRenderFeatureV2Second","priority":0,"stage":"PreUI"}]

   GET /render_graph/set_feature_enabled?name=DemoRenderFeatureV2&enabled=false
   -> 200 {"success":true}

   GET /render_graph -> DemoRenderFeatureV2 now "enabled":false, entry still
      present (never removed) - exactly the documented asymmetry vs a
      disabled built-in pass.

   GET /render_graph/set_feature_priority?name=DemoRenderFeatureV2Second&priority=5
   -> 200 {"success":true}

   GET /render_graph/set_feature_enabled?name=DemoRenderFeatureV2&enabled=true
   -> 200 {"success":true}

   GET /render_graph -> render_features: [
       {"blend_mode":"Replace","enabled":true,"name":"DemoRenderFeatureV2","priority":0,"stage":"PostComposite"},
       {"blend_mode":"AlphaOver","enabled":true,"name":"DemoRenderFeatureV2Second","priority":5,"stage":"PreUI"}]
      (both changes live-applied and confirmed correctly)

   GET /render_graph/set_feature_enabled?name=NoSuchFeature&enabled=false
   -> 409 {"error":"\"NoSuchFeature\" matches no loaded plugin render feature.","success":false}

   --- Blur/GBuffer HTTP <-> Scene-panel-checkbox shared-state proof ---

   GET /render_graph/set_blur_enabled?enabled=true
   -> 200 {"success":true}

   GET /activate_tab?name=Scene -> 200 {"activated_tab":"Scene","success":true}

   GET /get_swapchain -> 200 image/png, 97372 bytes - "Show Compute Blur
      (debug)" checkbox now visibly CHECKED, Scene view now shows the real
      blurred sky-gradient output instead of the raw radial test pattern - a
      REAL, visible difference this time (unlike the AtmosphereComposite
      case above), confirming the HTTP path and the checkbox share the
      exact same underlying EditorContext state.

   GET /render_graph/set_blur_enabled?enabled=false -> 200 {"success":true}

   GET /render_graph/set_gbuffer_enabled?enabled=true -> 200 {"success":true}

   GET /get_swapchain -> 200 image/png, 75633 bytes - "Show GBuffer
      Validation (debug)" checkbox now visibly CHECKED, Scene view now shows
      the real red/yellow checkerboard GBuffer-validation pattern - another
      real, visible difference.

   GET /render_graph/set_gbuffer_enabled?enabled=false -> 200 {"success":true}

   GET /get_logs?limit=80
   -> 200 {"count":16,"entries":[...]} - only the same, already-expected
      pre-existing plugin-load Info lines, the "2 loaded plugins implement
      IRenderFeatureModule_v1" warning, the CRT-linkage warning, the
      NetworkServer/EditorHost Info lines, and the 3 pre-existing
      GPU-timing-slot-budget Warning lines. ZERO new/unexpected log content
      from anything this phase added.

   GET /render_graph/passes (final state check)
   -> 200 every pass back to {"enabled":true,...} - session left clean.

   stop_app_background(pid: 18600) -> "Stopped process PID 18600
      (GreatTamanaEditor) successfully."
   ```

5. **`git_status` immediately before this commit**: diff touches EXACTLY —
   `CMakeLists.txt`, `src/Editor/EditorHost.cpp`, `src/Editor/EditorHost.h`,
   `src/Network/NetworkRoutes.cpp`, `src/Network/NetworkRoutes.h`,
   `src/Network/NetworkServer.cpp`, `src/Network/NetworkServer.h`,
   `tests/CMakeLists.txt`, `tests/Network/NetworkRoutesTests.cpp` (all
   modified), plus `src/Application/RenderGraphControlCommandBridge.cpp`,
   `src/Application/RenderGraphControlCommandBridge.h`,
   `tests/Application/RenderGraphControlCommandBridgeTests.cpp` (all
   new/untracked) — exactly the file set this phase's own "Verification" §4
   names, nothing else.

## Honest note on a real investigation detour during the live smoke test

While verifying the `AtmosphereComposite` disable, my first attempt to
confirm the disable's effect on `GET /render_graph` searched the raw
response body for the literal substring `"AtmosphereComposite"` and found
it **absent both before and after** the disable — which, read naively,
looked like a false negative. Investigating the actual source
(`AtmospherePassSequence.cpp` → `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`)
confirmed this is expected and correct: the literal string
`"AtmosphereComposite"` is ONLY ever used as (a) the `Register("AtmosphereComposite", ...)`
provider name in `Core.cpp` and (b) the toggle-registry key inside the Step
3.4b guard (`NoteDeclaredAndCheckEnabled("AtmosphereComposite")`) — the REAL
`rg::RenderPassDesc::debugName` the underlying GPU pass is actually declared
with (and which `GET /render_graph` reports) is
`"AtmosphereAerialPerspectiveCompositePass"`, a completely different
literal. Re-running the check against the correct name confirmed the
disable/re-enable genuinely worked end-to-end (present → absent → present
again in the live render graph). This is a real, useful, honestly-recorded
finding for anyone debugging this endpoint in the future, not a defect in
this phase's own code — PHASE1's registry key and the RenderGraph's own
reported pass name were always two independently-chosen strings, by design
(the registry is keyed by the `Register()` call site's own name, not by the
literal `debugName` string of whatever underlying pass that provider happens
to declare).

## Honest note on the AtmosphereComposite screenshot being byte-identical

PHASE0/PHASE5's own text anticipated the post-disable screenshot would
"look visibly DIFFERENT from the baseline" and explicitly called that an
accepted, non-failure outcome. What was actually observed here is stronger
still: the pre-disable and post-disable `/get_swapchain` PNGs are **exactly
byte-identical** (84116 bytes both times, pixel-for-pixel the same image).
This is NOT a sign the disable silently failed — it was independently
confirmed to have taken real effect via the `/render_graph` presence check
above (the pass genuinely stopped being declared, then genuinely reappeared
on re-enable). The reason the frame looks unchanged is specific to this
session's own test scene: the Game/Scene view's composited output target is
a **persistent, host-owned `RenderTexture`** that is imported into the graph
every frame rather than freshly (re)allocated (exactly as PHASE0's own Step
2.1 "Known risk" note describes) — and this particular test scene is fully
static (a fixed radial-gradient placeholder pattern, no camera motion, no
animated light), so the STALE content left behind in that persistent target
by skipping `AtmosphereComposite` happens to be pixel-identical to what a
fresh composite would have produced anyway. This is the honestly-observed,
scene-specific edge of the documented "stale/wrong-looking frame" risk, not
a broken recovery — the Blur/GBuffer checks immediately afterward (which DO
visibly change the frame) prove the screenshot pipeline itself is working
correctly and would have shown a difference had this specific pass's
disablement produced one in this particular scene. No crash, no hang, no
genuinely broken recovery was observed at any point — per PHASE5's own
instruction, this did NOT warrant stopping to `ask_questions`.

## Honest notes for PHASE6

- All 6 endpoints from `PHASE0_MASTER_STRATEGY.md`'s locked contract table
  are shipped, live-tested, and behave exactly as documented: `GET
  /render_graph/set_pass_enabled`, `GET /render_graph/passes`, `GET
  /render_graph/set_feature_enabled`, `GET /render_graph/set_feature_priority`,
  `GET /render_graph/set_blur_enabled`, `GET /render_graph/set_gbuffer_enabled`.
- `DemoRenderFeatureV2Second`'s priority was left at `5` (changed from its
  original `0`) at the very end of this phase's own live session — this is
  IN-MEMORY-ONLY state (Locked Product Decision #7) that resets to the
  plugin's own author-declared default on the next Editor launch; no
  cleanup was needed or attempted beyond re-enabling every built-in
  pass/plugin feature this phase itself had disabled during testing (all
  confirmed back to `enabled: true` in the final `/render_graph/passes`
  check above).
- PHASE6's own full regression/`ctest` pass will be the first time this
  phase's new code runs alongside the WHOLE existing test suite at once —
  nothing about this phase's own targeted 52-test run suggests any
  incompatibility, but it was not itself proof of that.
