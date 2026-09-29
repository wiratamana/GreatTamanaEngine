# CAMPAIGN COMPLETION REPORT — `editor-core-separation-20`: Fixing the Pink/Magenta Screen Bug in the Render Graph

Branch: `feature/editor-core-separation`. Three phases — `PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md`,
`PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md`, `PHASE3_REGRESSION_TESTS_AND_FULL_VERIFICATION.md`. Parent:
`PHASE0_MASTER_STRATEGY.md`.

## The bug, in plain language

A user disabled render passes via the Editor's "Render Graph" panel (or `GET /render_graph/set_pass_enabled`) and
got a solid, flat, magenta/pink screen instead of a sensible fallback image — and even re-enabling ONLY the
atmosphere/sky passes (while `RenderOpaque` stayed off) still showed pink instead of a visible sky.

## The two confirmed root causes

1. **No guaranteed clear.** The Game/Scene View render target's own `CLEAR` load-op used to be a side effect
   bolted onto `"RenderOpaque"` — the one pass most likely to be the FIRST thing a user disables. With
   `RenderOpaque` off, nothing ever cleared the view target, so it showed whatever undefined GPU memory happened to
   already be there (this particular GPU/driver combination happens to display that as flat magenta). Worse: even a
   brand-new, always-on clearing pass would have been silently CULLED by `RenderGraphCompiler::Compile()`'s own
   backward-reachability analysis unless something explicitly marked the view target as a kept root for that frame
   — since nothing did, by construction, before this fix.
2. **5 real Atmosphere render-graph passes never actually consulted the pass-toggle registry at all.** Their
   "Enabled" checkbox/HTTP toggle updated bookkeeping only — the passes kept running every frame regardless,
   meaning disabling them (or their upstream dependency) could never actually change what got drawn, and — more
   seriously — a naive per-pass toggle fix without cascading skip logic would have handed an invalid, disabled
   upstream's output handle to a downstream pass that assumed it was always valid, a real, confirmed,
   engine-crashing hazard (an out-of-bounds `physicalVolumeTextures[0xFFFFFFFF]` access) that this campaign found
   and fixed BEFORE it ever shipped.

## What changed, file by file

### PHASE1 — Guaranteed View-Target Clear

- **`src/Core/Core.cpp`**: added a brand-new, permanently-enabled `"ClearViewTarget"` render-graph provider
  (`ProviderScope::PerActiveView`, `RenderPassEvent::BeforeEverything` — the first real production consumer of that
  enum value) that owns the ONE guaranteed `CLEAR` load-op on the Game/Scene View target's color+depth attachments,
  and unconditionally appends that view's own `viewTarget` handle to `frame.finalTextureOutputs` every single frame
  — this is what keeps the pass (and the whole write-after-write chain built on top of it) alive through the render
  graph compiler's backward-reachability culling, even when every other pass that would otherwise read `viewTarget`
  back is disabled/absent. `"RenderOpaque"`'s own `setup` lambda switched its color/depth attachment writes from
  `CLEAR` to `LOAD`, since it no longer owns the clear.
- **`src/Renderer/RenderGraph/RenderPassToggleRegistry.cpp`/`.h`**: `IsDenyListed()` now also denies
  `"ClearViewTarget"` (alongside the pre-existing `"Present"`) — this pass can never be switched off via the panel
  or HTTP, by design; a view that is never cleared has no defined fallback content.

### PHASE2 — Honor the Atmosphere Pass Toggles

- **`src/Renderer/Atmosphere/AtmospherePassToggleLogic.h`** (new file): a single, pure, Tier-1-testable function,
  `ShouldDeclareAtmospherePassThisFrame(bool passEnabledThisFrame, bool allUpstreamHandlesValid)`.
- **`src/Renderer/Atmosphere/AtmosphereLutRenderer.h`/`.cpp`**: all 5 direct-`AddRenderPass()`-calling methods
  (`AddTransmittanceLutPass`, `AddMultiScatteringLutPass`, `AddSkyViewLutPass`, `AddAerialPerspectiveVolumePass`,
  `AddAerialPerspectiveCompositePass`) gained a trailing, defaulted `RenderPassToggleRegistry*` parameter and a
  real toggle-registry consult + cascading `IsValid()`-based upstream-handle check before declaring their pass —
  returning an invalid handle (never `std::optional`, matching this codebase's existing precedent) when skipped.
  `AddAerialPerspectiveVolumeDebugSlicePass()` itself (Frame-Debugger-only, LDD-5) was explicitly left untouched.
- **`src/Application/AtmospherePassSequence.h`/`.cpp`**: the 3 wrapper functions gained the same trailing
  parameter and forward/cascade it correctly through the whole LUT dependency chain
  (Transmittance → MultiScattering → SkyView/AerialPerspectiveVolume → Composite).
- **`src/Core/Core.cpp`**: 4 provider bodies (`"AtmosphereSharedLut"`, `"AtmosphereViewLut"`,
  `"DrawSkyBackground"`, `"AtmosphereComposite"`) updated to forward the toggle registry and gained two confirmed
  hazard fixes: (a) the Frame-Debugger-only `AddAerialPerspectiveVolumeDebugSlicePass()` call is now guarded by
  `viewLuts.aerialPerspectiveVolumeHandle.IsValid()` — closing the exact out-of-bounds crash this campaign's own
  investigation found and confirmed live; (b) the `AtmosphereComposite` provider now checks its own returned
  `composited` handle's validity before publishing it onto the render-pass blackboard, closing a second, related
  "invalid handle silently propagated downstream" gap.
- **`tests/Renderer/Atmosphere/AtmospherePassToggleLogicTests.cpp`** (new file, 3 tests) +
  `tests/CMakeLists.txt` (1 new line).

### PHASE3 — Regression Tests, Full Build/Test, Final Verification

- **`tests/Renderer/RenderGraph/RenderPassToggleRegistryTests.cpp`**: 1 new test,
  `SetEnabledFalseOnClearViewTargetIsRefusedByTheDenyList`, protecting PHASE1's deny-list (`LDD-2`) forever.
- **`tests/Renderer/RenderGraph/RenderPipelineTests.cpp`**: the existing
  `CollectedPassesAreSortedByOrderRegardlessOfRegistrationOrProviderScope` test was extended (not duplicated) with
  a 4th, `RenderPassEvent::BeforeEverything`-tagged provider registered LAST, proving it always sorts strictly
  first regardless of registration order — the exact load-bearing property `"ClearViewTarget"` depends on.
- Full clean build: zero errors.
- Full `ctest` regression run: **1993 tests, 100% of executed tests passing, 8 legitimate environment-gated
  skips, zero failures** — see `PHASE3_COMPLETION_REPORT.md` for a full, honest breakdown of exactly how many of
  those tests are attributable to this campaign (+4: PHASE2's 3 `AtmospherePassToggleLogicTest` cases + this
  phase's own 1 new deny-list case) versus other, undocumented, pre-existing work (+55 tests / +1 skip, landed
  between `editor-core-separation-15`'s own last documented `AGENTS.md` baseline of 1934/7 and the start of this
  campaign, without a matching `AGENTS.md` update at the time — a real documentation gap, not something this
  campaign broke).
- Final live, HTTP-driven re-verification of all 5 of PHASE0's Success Criteria against the freshly rebuilt engine
  — all 5 ✅ (see `PHASE3_COMPLETION_REPORT.md` for the exact evidence).

## Explicitly out of scope (restated so nobody assumes it was silently fixed too)

- **LDD-5**: `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()` itself (the Frame-Debugger-only
  volume-slice-preview pass) was never given its own toggle-registry consult — only its CALLER in `Core.cpp`
  gained a validity guard against the confirmed crash. Its own "Enabled" checkbox (if it even has one exposed) may
  still be inert. Not fixed here — out of scope by explicit PHASE0 decision.
- **LDD-6**: The Plugin Render Feature system (`RenderFeatureCompositor`, `demo_render_feature*` plugins, the
  `_v2`/`_v3` plugin ABI) — including its own, separately inert `DemoRenderFeaturePlugin_Clear`/
  `DemoRenderFeatureSecondPlugin_Clear` toggle-registry entries — was never touched. A `PostComposite`/`Replace`
  plugin (confirmed present on this development machine, `DemoRenderFeatureV3Third`) can and does fully overwrite
  the Game View's final composited pixels regardless of this campaign's own fix — this is real, pre-existing,
  documented, and unrelated.
- **LDD-7**: `src/Application/RenderPasses.cpp`'s free functions `AddRenderOpaquePass()`/`AddDrawSkyBackgroundPass()`
  remain confirmed dead code (nothing calls them) — never touched, never deleted, per explicit PHASE0 decision.
- The Frame Debugger's own `EditorHost.cpp` capture-route staleness (discovered during PHASE2's own live
  verification — disabling `AtmosphereAerialPerspectiveCompositePass` alone does not visibly change
  `GET /get_game_view`'s returned bytes, because the capture route always prefers the persistent
  `"GameViewComposited"` texture once it has ever been created, with no "was this rewritten THIS frame" check) is a
  pre-existing gap in capture-route code this campaign never touches — confirmed safe (zero crash, zero warning),
  just visually different from what PHASE2's own phase file predicted.

## Final verdict

All 5 of PHASE0's own Success Criteria are confirmed, live, end-to-end, against a freshly rebuilt engine:

1. ✅ Disabling `RenderOpaque`+`DrawSkyBackground`+`RenderTransparent`+every Atmosphere pass shows a solid, dark
   clear color — never magenta — and `"ClearViewTarget"` itself is confirmed `"is_culled": false` in this exact
   scenario.
2. ✅ Disabling only `RenderOpaque` (keeping `DrawSkyBackground`+Atmosphere enabled) shows a real, visible,
   byte-identical-to-baseline rendered sky.
3. ✅ Disabling `AtmosphereTransmittanceLutPass` alone genuinely removes it and every cascading downstream pass
   from the render graph — no longer a fake, inert checkbox — with zero crash (the confirmed hazard this campaign
   found and fixed).
4. ✅ Re-enabling everything returns to the exact prior, byte-identical baseline — zero regression.
5. ✅ A full clean build + full `ctest` regression pass, zero new failures (1993 tests, 100% of executed passing).
