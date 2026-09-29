# PHASE3 — Systemic Audit: Find Every Other "Lie" In The Rendering Code

Parent: `PHASE0_MASTER_STRATEGY.md` (MUST READ FIRST).
Previous phase report: `PHASE2_COMPLETION_REPORT.md` (MUST READ FIRST).

---

## Step 1: The Goal (Where are we going?)

Produce a complete, exhaustive, itemized ledger of EVERY togglable/
enable-checkbox surface this engine exposes (Editor panel checkboxes, HTTP
`GET`/`POST` mutation routes) cross-referenced against EVERY real
`RenderGraphBuilder::AddRenderPass()` call site, classifying each pairing as
one of:
- **Confirmed-Lie**: the toggle exists, is visible to the user, and does NOT
  actually, unconditionally gate that pass's declaration/execution.
- **False-Positive**: looked suspicious but a live HTTP check proves it is
  actually honest.
- **Already-Honest**: verified correct, no further action needed.
- **No-Toggle-Exists**: a real pass with no user-facing enable/disable
  control at all (not itself a lie, but worth noting for completeness).

This ledger becomes PHASE4's exact, complete fix backlog — PHASE4 must not
discover new work; PHASE3 must find it all first.

## Step 2: The Situation (Where are we now?)

This codebase has a genuinely large number of independent toggle mechanisms,
built up over many separate campaigns, that were never audited against each
other as one system:

1. **`RenderPassToggleRegistry`** (`src/Renderer/RenderGraph/RenderPassToggleRegistry.h`)
   — the "Disabled Built-In Passes" section AND the per-regime pass tables
   (`BuildPassRow()`) in `RenderGraphPanel.cpp`, plus
   `GET /render_graph/set_pass_enabled` / `GET /render_graph/passes`. Known
   consumers today (confirmed by `search_in_dir` for
   `NoteDeclaredAndCheckEnabled`): `AtmosphereLutRenderer.cpp` (5 methods:
   `AddTransmittanceLutPass`, `AddMultiScatteringLutPass`, `AddSkyViewLutPass`,
   `AddAerialPerspectiveVolumePass`, `AddAerialPerspectiveCompositePass`),
   `Core.cpp`'s `"AtmosphereComposite"` and `"GpuSkinning"` provider guards.
   Everything else declared via `RenderGraphBuilder::AddRenderPass()`
   anywhere in the engine that is NOT one of these calls is, by definition,
   NOT gated by this registry at all today, yet may still show up in the
   Render Graph panel's regime tables with an apparently-functional-looking
   checkbox (`BuildPassRow()` reads/writes `RenderPassToggleRegistry` for
   EVERY row in those tables unconditionally — meaning the checkbox WIDGET
   exists and looks real for every single declared pass, whether or not that
   pass's own declare-time code path actually consults the registry back).
   **This single fact is very likely the single largest source of "lies" in
   this codebase** — the UI cannot tell the difference between an honestly
   wired pass and a decorative one, because `BuildPassRow()` is generic and
   the registry entry auto-creates itself the first time
   `RenderPipeline::DeclareOnePhase()`'s own generic flush loop sees ANY
   pass name at all (see `RenderPassToggleRegistry.h`'s own header comment:
   "Auto-discovery, not a hardcoded pass-name list... the first time
   `RenderPipeline::DeclareOnePhase()` actually sees a given debugName this
   session, it is auto-registered here").
   **Read this carefully**: `RenderPipeline::DeclareOnePhase()`'s own generic
   flush loop calling `NoteDeclaredAndCheckEnabled()` for every `RenderPassDesc`
   a provider returns via `out.push_back(desc)` means passes declared THAT WAY
   (the majority — see `AGENTS.md`'s "Render Pass System" section's
   description of `render-pass-3`) genuinely ARE gated, generically, for free.
   The ONLY passes at real risk of being a lie are ones whose provider
   BYPASSES the `out.push_back(desc)` return path and instead calls
   `frame.builder.AddRenderPass()` DIRECTLY inside its own lambda body — the
   two the codebase already confesses to (`"AtmosphereComposite"`,
   `"GpuSkinning"` — both already fixed, per `AGENTS.md`), and the two demo
   plugin "Clear" passes (`PluginRenderPassBuilderAdapter::AddFullscreenClearPass()`,
   confirmed NOT toggle-registry-aware in PHASE0 Step 2.2), and possibly
   others this phase must find.
2. **`RenderFeatureCompositor::SetFeatureEnabled(name, enabled)`**
   (`src/Core/Plugins/RenderFeatureCompositor.h/.cpp`) — an ENTIRELY
   SEPARATE mechanism from `RenderPassToggleRegistry`, backing the "Plugin
   Render Features" checkboxes in the same panel (e.g.
   `[PostComposite] DemoRenderFeatureV2 - blend Replace (DISABLED)` in the
   screenshot) and `GET /render_graph/set_feature_enabled`. This is NOT the
   demo plugins' own "Clear" pass (that one is item 3 below) — it is the
   actual blend/compositing stage. Must be independently verified honest.
3. **Direct, un-registried `AddRenderPass()` calls with no toggle concept at
   all**, e.g. `PluginRenderPassBuilderAdapter::AddFullscreenClearPass()`
   (the demo-plugin "Clear" passes — already flagged, PHASE0 Step 2.2),
   `ComputeBlurValidation.cpp`'s pass (gated by its OWN separate
   `GET /render_graph/set_blur_enabled` route/flag, NOT
   `RenderPassToggleRegistry` — check whether that flag actually gates
   declaration or just something cosmetic), `GBufferValidation.cpp`
   (`GET /render_graph/set_gbuffer_enabled`, same question), the Frame
   Debugger's own replay passes (`FrameDebuggerReplayPasses.cpp`, explicitly
   documented as permanently un-migrated — confirm this is INTENTIONALLY
   invisible to any user-facing toggle, i.e. this file's passes never claim
   to be togglable in the first place, which would make it a
   "No-Toggle-Exists" classification, not a lie), and any Project Assembly
   provider (`ProjectAssemblyProbe.FillTexture`, visible in the screenshot's
   own Frame Debugger tree) which calls
   `Core::RegisterProjectRenderPassProvider()` — check whether IT flows
   through `RenderPipeline::DeclareOnePhase()`'s generic flush loop (honest
   by construction) or bypasses it.
4. **Present/GpuSkinning/ClearViewTarget deny-listed passes** — these are
   INTENTIONALLY non-disable-able (`RenderPassToggleRegistry::IsDenyListed()`).
   Confirm the Editor UI/HTTP correctly REJECTS attempts to disable them
   (`SetEnabled()` already returns `false` for a deny-listed name — confirm
   the panel/route surfaces that rejection to the user rather than silently
   pretending it succeeded, which would itself be a small lie worth noting,
   even if deliberate/acceptable).
5. **A third candidate, found and statically confirmed by this campaign's own
   PHASE0 double-check pass (`PHASE0_MASTER_STRATEGY.md` Step 2.2b — read it
   for the full citation trail) — this phase must still confirm it LIVE, not
   just trust the static finding**: `src/Application/RenderPasses.cpp`'s
   `AddGpuSkinningPasses()` (~line 209) calls `builder.AddRenderPass(request.name,
   ...)` directly, per GPU-skinning dispatch, with zero `RenderPassToggleRegistry`
   consult — reached from `Core::RegisterPresentRenderPipelineProvider()`'s
   `"Present"` provider (`Core.cpp` ~line 1017-1025), ONLY when
   `Core::Present()`'s `needsDirectGameRender` is `true` (`Core.cpp` ~line 1395:
   `m_gameTargetThisFrame == nullptr && m_sceneTargetThisFrame == nullptr` — the
   "direct-render-to-swapchain fallback" `AGENTS.md`'s "GPU-Driven Rendering"
   section already documents as real production code, reachable whenever
   neither the Game View nor Scene View panel currently holds an active render
   target this frame). This bypasses BOTH the offscreen `"GpuSkinning"`
   provider's own outer guard (`Core.cpp` ~line 476) AND
   `RenderPipeline::DeclareOnePhase()`'s generic auto-registration entirely.
   **Live-verify** by: toggling `"GpuSkinning"` off via
   `GET /render_graph/set_pass_enabled?name=GpuSkinning&enabled=false`, then
   driving the engine into the direct-render-to-swapchain path (the simplest
   reliable way: use `GET /activate_tab` or simply query `GET /render_graph`
   while neither the "Game" nor "Scene" panel tab has ever been activated this
   session, OR add a temporary diagnostic log line at `Core::Present()`'s own
   `needsDirectGameRender` computation to confirm exactly when it is `true` in
   this build/session — remove the temporary log line again once the
   confirmation is captured), and confirming whether a GPU-skinned model's
   dispatch pass still appears in a fresh `GET /render_graph/passes`/Frame
   Debugger capture. Note: `AddPresentPass()` itself needs NO toggle check —
   `"Present"` is permanently deny-listed by design, so its own direct
   `builder.AddRenderPass()` call is NOT a finding.
6. **Pre-confirmed dead code, NOT a lie — already verified during PHASE0's own
   double-check via a plain, exhaustive `search_in_dir`, no live check needed**:
   `src/Application/RenderPasses.cpp`'s `AddRenderOpaquePass()`/
   `AddDrawSkyBackgroundPass()` free functions have zero callers anywhere in
   `src/` outside `RenderPasses.cpp`/`RenderPasses.h` themselves and doc
   comments referencing them by name — matching `AGENTS.md`'s own
   "confirmed-dead" description from `editor-core-separation-20`. List them in
   the ledger as `No-Toggle-Exists` (dead code) for completeness; do not spend
   a live HTTP check on these two specifically.

## Step 3: The Plan

### 3.1 — Build the master pass inventory (static, then confirmed live)

1. `search_in_dir` (recursive, `filter: *.cpp`) across `src/` for
   `.AddRenderPass(` and separately for `builder.AddRenderPass(` and
   `frame.builder.AddRenderPass(` to enumerate literally every direct call
   site. Cross-reference against `RenderPipeline.h`'s `DeclareOnePhase()`
   flush loop to determine, PER CALL SITE, whether it is reached via a
   provider's `out.push_back(desc)` return (generically honest) or via a
   provider lambda calling `AddRenderPass()`/a helper that calls it directly,
   itself, bypassing the return path (needs its own manual toggle check).
2. For every call site in the second category, `search_in_dir` for its own
   debug name string to see whether `NoteDeclaredAndCheckEnabled(<that same
   name>)` is called anywhere upstream of it, and if so, confirm the name
   strings match BYTE FOR BYTE (a mismatched string is its own subtle lie —
   the registry entry the checkbox controls is not the one actually checked).
3. Build a plain Markdown table (this becomes this phase's own completion
   report's centerpiece) with columns: `Pass debug name` | `Declared via
   (direct call / provider return)` | `File:line` | `Toggle mechanism
   consulted (RenderPassToggleRegistry / RenderFeatureCompositor / a bespoke
   flag / none)` | `Classification (Confirmed-Lie / False-Positive /
   Already-Honest / No-Toggle-Exists)` | `Live HTTP evidence`.

### 3.2 — Confirm every entry LIVE, not just by reading code

For every entry classified anything other than "No-Toggle-Exists" by static
reading, run the SAME kind of live HTTP repro script PHASE1 used (build
incrementally, `run_app_background`, drive via `gte_send_request` against
`GET /render_graph/passes`, the relevant `set_*_enabled` route, a fresh
`GET /frame_debugger/capture` + `GET /frame_debugger/state`, and
`GET /get_game_view` for a visual sanity check) to CONFIRM or OVERTURN the
static classification. A static reading that "looks honest" (like PHASE0's
own Step 2.1 finding for the original bug!) is not sufficient evidence on its
own in this codebase — this campaign exists precisely because static reading
was already proven insufficient once.

Specifically confirm, at minimum, these concrete items already flagged in
`PHASE0_MASTER_STRATEGY.md`:
- `DemoRenderFeaturePlugin_Clear` / `DemoRenderFeatureSecondPlugin_Clear` —
  expected result: Confirmed-Lie (already strongly suspected from static
  reading of `AddFullscreenClearPass()`).
- `RenderFeatureCompositor`'s per-feature `[PostComposite]`/`[PreUI]` enable
  checkboxes (e.g. `DemoRenderFeatureV2`, `DemoRenderFeatureV3`,
  `DemoRenderFeatureV3Third`, `DemoRenderFeatureV2Second`,
  `DemoRenderFeatureV3Second` from the screenshot) — verify `SetFeatureEnabled(false)`
  genuinely skips that feature's own `DispatchOps()`/blend contribution.
- `ComputeBlurValidation`'s blur-enabled toggle and `GBufferValidation`'s
  gbuffer-enabled toggle — verify both actually gate pass declaration, not
  merely a cosmetic display flag.
- Whether `ProjectAssemblyProbe.FillTexture` (and any other Project Assembly
  provider) is disable-able at all today, and if so, whether it is honest.
- The `"AtmosphereComposite"` vs `"AtmosphereAerialPerspectiveCompositePass"`
  dual-toggle-name situation flagged in `PHASE0_MASTER_STRATEGY.md` Step
  2.1 — is this confusing enough (two rows in the panel that both,
  independently, gate visibility of the SAME visual feature) to warrant its
  own UX finding even if both are individually honest post-PHASE2?

### 3.3 — Ambiguity checkpoint

If this audit surfaces a genuinely large number of Confirmed-Lie findings
(a realistic possibility given Step 2's own analysis above), or finds a
finding whose correct fix is not obvious/small (e.g. collapsing the
dual-toggle-name situation would be a real, user-visible behavior change to
the panel's own layout), use `ask_questions` to confirm scope/priority with
the user before PHASE4 starts implementing anything large. Do not let PHASE4
silently balloon in scope beyond what the user actually wants fixed right
now versus deferred to a future campaign — but do not silently defer
anything either without asking first, per PHASE0 Step 3.1 rule 7.

### 3.4 — Wrap-up

1. No production code fix lands in this phase (this phase's own code changes
   are limited to whatever temporary diagnostic logging Step 3.2's live
   checks require, under the `"RenderPassHonestyDiag"` category, mirroring
   PHASE1's own convention — remove dead-end instrumentation, keep anything
   that meaningfully proved a classification).
2. Incremental compile check must succeed (if any instrumentation was added).
3. Write `PHASE3_COMPLETION_REPORT.md` containing the FULL master ledger
   table from Step 3.1/3.2, with every row's live evidence cited, and a
   clearly labelled "PHASE4 fix backlog" section listing every Confirmed-Lie
   row in the exact order PHASE4 should tackle them.
4. `git_add` + `git_commit`.
5. `stop_app_background` the running engine instance before finishing.
6. Do NOT call `delegate_task`. Use `ask_questions` per Step 3.3 above.
