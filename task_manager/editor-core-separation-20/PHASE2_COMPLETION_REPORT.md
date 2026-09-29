# PHASE2 — Completion Report: Honor the Atmosphere Pass Toggles

Campaign: `editor-core-separation-20`. Parent: `PHASE0_MASTER_STRATEGY.md`. Phase file: `PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md`.

## What was done

### 1. New file: `src/Renderer/Atmosphere/AtmospherePassToggleLogic.h`

A single, header-only, `inline` pure function, `ShouldDeclareAtmospherePassThisFrame(bool passEnabledThisFrame,
bool allUpstreamHandlesValid) noexcept` — the ONE decision every one of the 5 toggle-aware `AddXxxPass()` methods
below needs before it is allowed to declare a render-graph pass at all. Zero dependency on `RenderPassToggleRegistry`
or `RenderGraphTypes.h` (every caller resolves its own two booleans first and passes plain `bool`s in), so this is
genuinely Tier-1-testable with no live `VkDevice`/`Renderer`/`RenderGraphBuilder` involved — copied verbatim from
the phase file's own Step 3.4a.

### 2. `src/Renderer/Atmosphere/AtmosphereLutRenderer.h` / `.cpp` — 5 methods gain a toggle-aware guard

Added a forward declaration of `gte::rg::RenderPassToggleRegistry` to the header (pointer-only use, no full
`#include`), and one new, trailing, defaulted `rg::RenderPassToggleRegistry* toggleRegistry = nullptr` parameter to
each of:

1. `AddTransmittanceLutPass` — no upstream handle to check (`allUpstreamHandlesValid = true` unconditionally).
2. `AddMultiScatteringLutPass` — checks `transmittanceLutHandle.IsValid()`.
3. `AddSkyViewLutPass` — checks `transmittanceLutHandle.IsValid() && multiScatteringLutHandle.IsValid()`.
4. `AddAerialPerspectiveVolumePass` — same two-handle check; returns `rg::VolumeTextureHandle{}` on skip (not
   `TextureHandle{}`).
5. `AddAerialPerspectiveCompositePass` — checks `aerialPerspectiveVolumeHandle.IsValid()` only (`sourceColorHandle`
   always comes from `viewData->colorTarget`, always valid by construction).

Each `.cpp` body's very first statement is now:
```cpp
const bool passEnabledThisFrame = toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("<exact debugName>");
if (!ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, <upstream validity>)) {
    return rg::TextureHandle{}; // or rg::VolumeTextureHandle{} for AddAerialPerspectiveVolumePass
}
```
placed BEFORE any `EnsureXInitialized()` call, using the exact literal debugName string already passed to that
same method's own `builder.AddRenderPass(...)` call a few lines below (copied, not retyped, to avoid a silent
typo). `AddAerialPerspectiveVolumeDebugSlicePass()` itself (the 6th, Frame-Debugger-only method, LDD-5) was left
completely untouched — signature and body byte-for-byte identical to before this phase.

`AtmosphereLutRenderer.cpp` gained two new includes: `AtmospherePassToggleLogic.h` and
`../RenderGraph/RenderPassToggleRegistry.h` (needed for the real `NoteDeclaredAndCheckEnabled()` call).

### 3. `src/Application/AtmospherePassSequence.h` / `.cpp` — the 3 wrapper functions

- `AddAtmosphereSharedLutPasses()` gained the same trailing parameter, forwards it to both calls, and short-circuits
  (returns early with `multiScatteringLutHandle` left invalid) if `transmittanceLutHandle` came back invalid.
- `AddAtmosphereViewLutPasses()` gained the same trailing parameter, forwards it to both calls, and skips BOTH calls
  (leaving both `skyViewLutHandle`/`aerialPerspectiveVolumeHandle` invalid) if either shared LUT handle is invalid.
- `AddAtmosphereCompositePass()` (the thin `RenderTexture&`-resolving wrapper around
  `AddAerialPerspectiveCompositePass()`) gained the same trailing parameter, forwarded straight through — no new
  internal branch needed (its own callee already does the one relevant check).

A forward declaration of `rg::RenderPassToggleRegistry` was added to `AtmospherePassSequence.h`'s existing
`namespace gte::rg { ... }` block, alongside the pre-existing `RenderGraphBuilder` forward declaration.

### 4. `src/Core/Core.cpp` — 4 real provider bodies updated (plus the confirmed crash fix)

1. **`"AtmosphereSharedLut"` provider**: `AddAtmosphereSharedLutPasses(...)` call now forwards
   `&m_renderPassToggleRegistry`.
2. **`"AtmosphereViewLut"` provider** — two changes:
   - `AddAtmosphereViewLutPasses(...)` call now forwards `&m_renderPassToggleRegistry`.
   - **THE CONFIRMED CRASH FIX (Step 3.7, item 2b)**: the `AddAerialPerspectiveVolumeDebugSlicePass()` call
     (Frame-Debugger-only volume-slice-preview, previously unconditional whenever `isGameView`) is now guarded by
     `viewLuts.aerialPerspectiveVolumeHandle.IsValid()` too — `if (isGameView && viewLuts.aerialPerspectiveVolumeHandle.IsValid())`.
     `AddAerialPerspectiveVolumeDebugSlicePass()`'s own signature/body remain completely untouched (LDD-5 honored);
     only its caller gained the guard.
3. **`"DrawSkyBackground"` provider**: the existing `!viewLuts.has_value() || !sharedLuts.has_value()` early-return
   gained a third condition, `|| !viewLuts->skyViewLutHandle.IsValid()`.
4. **`"AtmosphereComposite"` provider** — two changes:
   - The existing `!viewLuts.has_value()` early-return gained a second condition,
     `|| !viewLuts->aerialPerspectiveVolumeHandle.IsValid()`, and the `AddAtmosphereCompositePass(...)` call now
     forwards `&m_renderPassToggleRegistry`.
   - **The second gap's fix (Step 3.7, item 4b)**: the `composited` `TextureHandle` returned by
     `AddAtmosphereCompositePass()` is now checked with `if (!composited.IsValid()) { return; }` BEFORE
     `frame.finalTextureOutputs.push_back(composited)` / `frame.blackboard.Publish<rg::TextureHandle>(...)` —
     closing the gap where an individually-disabled `"AtmosphereAerialPerspectiveCompositePass"` (upstream volume
     still valid) would otherwise have published an invalid handle onto the blackboard, silently bypassing
     `FindPluginRenderFeatureTarget()`'s own `composited.value_or(viewData->colorTarget)` fallback.

`AtmosphereLutRenderer.h`'s forward-declared `RenderPassToggleRegistry` is real, already-included via `Core.h`
(no new include needed in `Core.cpp` for this).

### 5. New Tier-1 test: `tests/Renderer/Atmosphere/AtmospherePassToggleLogicTests.cpp`

3 tests exercising every branch of `ShouldDeclareAtmospherePassThisFrame()` (copied verbatim from the phase file's
Step 3.8), plus one new line added to `tests/CMakeLists.txt`'s test source list immediately after
`Renderer/Atmosphere/DirectionalLightResolverTests.cpp`.

## Compile result

`cmake --build build` (incremental) completed successfully — 27 build steps, zero errors, zero new warnings.
`GreatTamanaEditor.exe`, `GreatTamanaEngineTests.exe`, and both Project Assembly `.dll`s relinked cleanly.

## New Tier-1 test result

Ran `tests\GreatTamanaEngineTests.exe --gtest_filter=AtmospherePassToggleLogicTest.*` directly (not the full suite —
LDD-8 defers that to PHASE3): **3/3 tests passed**, 0 ms total.

## Live verification (via `run_app_background` + `gte_send_request`)

Same honestly-disclosed complication as PHASE1's own report applies here too: this machine's `build/plugins/`
folder has 7 permanent demo Plugin Render Feature `.dll`s loaded by default, one of which
(`DemoRenderFeatureV3Third`, `PostComposite`/`Replace`) fully replaces the Game View's final composited pixels —
a real, working, pre-existing, **explicitly out-of-scope** mechanism (PHASE0 LDD-6). These 7 `.dll`s were
**temporarily moved out of `build/plugins/`** (a pure file-system move, no code change) for the entire verification
session below, then **moved back immediately afterward**, restoring the exact original `build/plugins/` contents
before this task ends.

1. **Baseline** (`GET /get_game_view`, every pass at its default enabled state): a normal, real, blue-to-warm-horizon
   atmosphere sky gradient over black, 37222 bytes — byte-identical to PHASE1's own baseline capture. `GET /get_logs?min_level=warning`
   returned zero entries.
2. **The mechanical proof for Root Cause #2, AND the confirmed-crash regression test (Step 3.7, item 2b)**:
   `GET /render_graph/set_pass_enabled?name=AtmosphereTransmittanceLutPass&enabled=false`, then `GET /render_graph` —
   confirmed `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass`, BOTH `AtmosphereSkyViewLutPass`
   entries (Game View + Scene View), BOTH `AtmosphereAerialPerspectiveVolumePass` entries, the
   `AtmosphereAerialPerspectiveVolumeDebugSlicePass` entry, `DrawSkyBackground`, and `AtmosphereComposite` were all
   **genuinely absent** from the pass list (not merely `is_culled: true` — not declared at all this frame), proving
   the cascading skip logic works correctly end-to-end through the whole dependency chain.
   **CRITICALLY: `GET /get_logs?min_level=warning` immediately afterward returned zero entries, and the engine
   process was confirmed still running and responsive (`GET /get_game_view` succeeded with a real image, no
   connection error) — the 3.7 item 2b crash fix is confirmed working.** Before that fix, this exact step (Game
   View is always the active view) would have called `AddAerialPerspectiveVolumeDebugSlicePass()` with an invalid
   volume handle, hitting the documented out-of-bounds `physicalVolumeTextures[0xFFFFFFFF]` access inside
   `RenderGraph::ApplyUsageBarrierIfNeeded()` — this is the single most important check in this phase, and it
   passed.
3. Re-enabled `AtmosphereTransmittanceLutPass` — `GET /get_game_view` returned to the exact 37222-byte baseline,
   and `GET /render_graph` showed every Atmosphere pass declared again with normal, non-zero GPU timings.
4. **The second gap's fix (Step 3.7, item 4b)**, verified narrowly without a live plugin (per LDD-6):
   `GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false` (everything else
   enabled) — confirmed via `GET /render_graph/passes` that only this ONE pass shows `"enabled": false` (its own
   upstream LUTs/volume remained declared and running, confirmed via `GET /render_graph`'s full pass list no longer
   containing `AtmosphereAerialPerspectiveCompositePass`), and `GET /get_logs?min_level=error` returned zero entries
   — no crash, no error. **One honest deviation from the phase file's own predicted observation, found and recorded
   here rather than silently glossed over**: the phase file predicted `GET /get_game_view` would show "a normal
   rendered scene... just without the aerial-perspective composite blended on top." What it ACTUALLY showed was the
   byte-identical 37222-byte baseline image — because `EditorHost.cpp`'s pre-existing, **untouched-by-this-phase**
   capture logic (`m_atmosphereLutRenderer.CompositedOutput("GameViewComposited")`) always prefers the persistent
   `"GameViewComposited"` `RenderTexture` once it has EVER been created this session, with no notion of "was this
   texture actually rewritten THIS frame" — when `AddAerialPerspectiveCompositePass()`'s own new guard skips
   declaring the pass, that texture's pixel memory is simply never touched, so the capture route reads back
   whatever it last successfully held (in this case, indistinguishable from the immediately-prior good frame,
   since nothing else changed between captures). This is exactly the same class of "stale, not dangling" outcome
   Step 3.6 already accepts by design for `DrawSkyBackground()`'s own analogous map-lookup guard — genuinely safe
   (confirmed zero errors/warnings, confirmed process still running), just visually different from what the phase
   file predicted. This capture-route staleness is a pre-existing gap in `EditorHost.cpp`'s own frame-capture logic,
   never touched by this phase (out of scope — PHASE0/PHASE2 only ever touch pass-declaration code, never the
   network capture route), and the phase's own actual verification GOAL for this step — "no crash/error" — is fully
   confirmed regardless.
5. Re-enabled `AtmosphereAerialPerspectiveCompositePass` — `GET /get_game_view` returned to the exact 37222-byte
   baseline.
6. Re-ran the user's exact "atmosphere only" acceptance test end-to-end (disabled `RenderOpaque` +
   `RenderTransparent`, kept every Atmosphere pass + `DrawSkyBackground` enabled) — `GET /get_game_view` showed a
   real, visible rendered sky/atmosphere gradient (37222 bytes), matching PHASE0's Success Criterion #2, with zero
   new warnings/errors.
7. Restored every pass to its enabled state — `GET /get_game_view` returned to the exact 37222-byte baseline once
   more, and `GET /get_logs?min_level=warning` again returned zero entries, confirming zero regression from this
   whole phase's change set in the fully-enabled case.
8. `stop_app_background` — engine stopped cleanly.

After this verification, the 7 temporarily-moved `demo_render_feature*.dll` files were moved back into
`build/plugins/`, restoring the exact original plugin set before ending this task (confirmed via `dir`).

## Success criteria checked against PHASE0

1. N/A for this phase (PHASE1's own success criterion — already ✅ per `PHASE1_COMPLETION_REPORT.md`).
2. ✅ Disabling only `RenderOpaque` (`DrawSkyBackground` + Atmosphere passes enabled) shows a real, visible rendered
   sky/atmosphere — re-confirmed after this phase's own change set, still passing.
3. ✅ Disabling `AtmosphereTransmittanceLutPass` alone genuinely makes it (and everything downstream of it) disappear
   from `GET /render_graph`'s pass list — no longer a fake, inert checkbox. **Zero crash** — the confirmed
   engine-crashing hazard (Step 2/3.7 item 2b) this phase's own investigation identified is fixed and verified live.
4. ✅ Re-enabling every pass returns the engine to its exact prior, fully-rendered baseline visual output — no
   regression (byte-identical 37222-byte capture, both before/after every toggle round-trip performed in this
   session).
5. Full clean build + full `ctest` regression pass is PHASE3's job (LDD-8) — not run here, per instructions.

## Files changed

- `src/Renderer/Atmosphere/AtmospherePassToggleLogic.h` (**new file**)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h` (forward declaration + 5 method signatures)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` (2 new includes + 5 method-body guards)
- `src/Application/AtmospherePassSequence.h` (forward declaration + 3 wrapper signatures)
- `src/Application/AtmospherePassSequence.cpp` (3 wrapper bodies, cascading `IsValid()` guards)
- `src/Core/Core.cpp` (4 provider bodies: toggle-registry forwarding + the 2 confirmed-hazard guards)
- `tests/Renderer/Atmosphere/AtmospherePassToggleLogicTests.cpp` (**new file**)
- `tests/CMakeLists.txt` (1 new test source line)
- `task_manager/editor-core-separation-20/PHASE2_COMPLETION_REPORT.md` (this file)
