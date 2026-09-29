# PHASE0 — MASTER STRATEGY: "The Engine Is Lying" — Render Pass / Frame Debugger Honesty Campaign

Campaign folder: `task_manager/editor-core-separation-21/`
Branch: `feature/editor-core-separation` (stay on it, do not create a new branch)
Project root: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE6_*.md`) must be read together with this file. Every child phase must,
at its own start, re-read this file plus the completion report of the phase
immediately before it (`PHASEn-1_COMPLETION_REPORT.md`, written by that
phase) — there might be a clue for continuation, a discovered root cause, or
a locked decision that changes how the next phase must be carried out.

---

## Step 1: The Goal (Where are we going?)

1. **The one, specific, reported bug must be killed**: the user disabled
   `AtmosphereAerialPerspectiveCompositePass` via the Editor's "Render Graph"
   panel (or `GET /render_graph/set_pass_enabled`), and the Frame Debugger
   still shows it as a REAL, EXECUTED compute-dispatch event this frame (real
   GPU time, real read/write textures, a real Shader/Pass name) — see the
   attached screenshot: the checkbox under "Disabled Built-In Passes" is
   unticked for `AtmosphereAerialPerspectiveCompositePass`, yet the Frame
   Debugger's own event tree, under `Compute Dispatches (Post-GameView)`,
   still shows `AtmosphereAerialPerspectiveCompositePass -> Compute Dispatch`
   as event #14 of 15, fully populated with live data. This is a lie the
   engine is telling its own developer, and it must stop.
2. **The iron rule, from this campaign forward, permanently**:
   > **A render pass's declared/enabled state and the Frame Debugger's own
   > displayed event tree must NEVER disagree. If a pass is disabled, it must
   > not run, and it must not appear as an executed leaf anywhere the Frame
   > Debugger or the Render Graph panel can show it. If a pass runs, the
   > Frame Debugger MUST show it. There is no acceptable middle ground.**
3. **Widen the manhunt**: the one reported case is very likely not the only
   offender. Every other "Enabled" checkbox/toggle anywhere in the rendering
   pipeline (built-in passes, Atmosphere LUT/composite passes, Plugin Render
   Feature per-feature enable, plugin-authored demo "Clear" passes, the
   GBuffer Validation / Compute Blur Validation debug toggles, anything else
   discovered during the audit) must be confirmed HONEST — i.e., toggling it
   off truly, unconditionally removes that pass's execution AND its Frame
   Debugger appearance the very next frame, with zero crash, zero dangling
   state, and correct graceful degradation of anything downstream that used
   to consume its output.
4. **Make the iron rule self-enforcing, permanently, in code** — not just a
   promise in a `.md` file. A concrete, automatic, always-on (debug build)
   detector must exist so that if ANY future change (by a human or another
   LLM agent) reintroduces this exact class of bug, the engine LOUDLY tells
   on itself via its own internal logging system (`GTE_LOG_ERROR`, retrievable
   via `GET /get_logs`) the very next time it happens — never silently, never
   requiring a human to notice a screenshot mismatch again.
5. Land this with a full clean build and full `ctest` regression pass at the
   very end (only at the very end — see the Workflow rules below), plus a
   `docs/conventions/` write-up and an `AGENTS.md` addition matching this
   codebase's own established documentation convention (see every prior
   `editor-core-separation-*`/`render-pass-*` campaign's own entry in
   `AGENTS.md` for the expected tone/format/precision).

## Step 2: The Situation (Where are we now?)

### 2.1 — What already exists and (on paper) looks correct

A prior campaign, `editor-core-separation-20`
(`task_manager/editor-core-separation-20/PHASE0_MASTER_STRATEGY.md`,
`CAMPAIGN_COMPLETION_REPORT.md`), already targeted almost this EXACT bug and
believed it fixed it: PHASE2 of that campaign
(`PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md`) added a pure helper,
`src/Renderer/Atmosphere/AtmospherePassToggleLogic.h`'s
`ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame,
allUpstreamHandlesValid)`, threaded into all five of
`AtmosphereLutRenderer`'s toggle-aware `AddXxxPass()` methods
(`src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`), including
`AddAerialPerspectiveCompositePass()` at line ~778, which — reading the code
today — DOES appear to correctly guard its own `builder.AddRenderPass(...)`
call behind:
```cpp
const bool passEnabledThisFrame = toggleRegistry == nullptr
    || toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveCompositePass");
if (!ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, aerialPerspectiveVolumeHandle.IsValid())) {
    return rg::TextureHandle{};
}
```
and every call site between the Editor's "Disabled Built-In Passes" checkbox
(`src/Editor/Panels/RenderGraphPanel.cpp`'s `BuildDisabledBuiltInPassesSection()`)
down to this guard (`Core.cpp`'s `"AtmosphereComposite"` provider ->
`AddAtmosphereCompositePass()` in `src/Application/AtmospherePassSequence.cpp`
-> `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`) appears, from
static reading alone, to correctly forward the SAME
`rg::RenderPassToggleRegistry*` instance (`Core::m_renderPassToggleRegistry`,
`src/Core/Core.h`) all the way through.

**This is exactly why this is a genuinely dangerous, high-priority bug and
not a quick one-line fix**: the code LOOKS right under a static read, yet the
user's own live, first-hand, screenshot-documented observation says it is
NOT right at runtime. One (or more) of the following must be true, and PHASE1
below exists to find out which, with LIVE, MECHANICALLY-CAPTURED EVIDENCE —
never by guessing:

- A regression: some campaign AFTER `editor-core-separation-20` (candidates
  to check first: `render-pass-6`, `render-pass-7`, `mrt-1`, the four
  Project-Assembly-Hot-Reload campaigns, `editor-core-separation-19`/`-20`
  itself) touched `Core.cpp`'s `"AtmosphereComposite"` provider, `AtmosphereLutRenderer.cpp`,
  `RenderPipeline.h`'s `DeclareOnePhase()`, or `RenderPassToggleRegistry` itself, and
  silently broke this exact wiring (e.g. a second, un-guarded call path was
  added; a refactor accidentally passed `nullptr` for `toggleRegistry`; a
  provider-registration-order change means a STALE registry read happens
  before the checkbox's `SetEnabled()` write is visible for that frame).
- A naming collision: this pass shares its exact literal debug name,
  `"AtmosphereAerialPerspectiveCompositePass"`, across BOTH Game View and
  Scene View declarations (an intentional, documented, permanent convention —
  see `AGENTS.md`'s "ImGui Widget ID Uniqueness" section referencing
  `editor-core-separation-10`). Confirm this sharing is not ALSO causing an
  `ImGuiUniqueId`/`ScopedUniqueId` ID collision on the checkbox itself, which
  could make clicking one row's checkbox silently toggle a DIFFERENT registry
  entry than the one displayed/labelled.
- A stale/frozen Frame Debugger capture: the panel has a "Pause" checkbox
  (`RenderGraphPanel`'s own `m_paused`, unrelated to the Frame Debugger's own
  separate Enable/Capture/Step controls) and the Frame Debugger's own
  "Enable"/"Capture" buttons (`src/Editor/Panels/FrameDebuggerPanel.h/.cpp`,
  `GET /frame_debugger/enable`/`/capture`/`/state`). If the visible capture in
  the screenshot was taken BEFORE the checkbox was unticked (a stale, un-
  refreshed capture), this would LOOK exactly like the reported bug while the
  underlying toggle mechanism is actually fine. This must be positively ruled
  out (or confirmed) with a live, scripted, HTTP-driven repro — never
  eyeballed.
- The `"AtmosphereComposite"` OUTER provider-level toggle name (checked
  inside `Core.cpp`'s own provider lambda, `NoteDeclaredAndCheckEnabled("AtmosphereComposite")`)
  and the `"AtmosphereAerialPerspectiveCompositePass"` INNER toggle name
  (checked inside `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()`
  itself) are two DIFFERENT registry entries that both gate the SAME visible
  feature from the user's point of view. Confirm the Render Graph panel is
  reading/writing the ONE the user actually interacted with in the
  screenshot, and that there isn't a confusing double-toggle UX bug hiding
  underneath a code-level correctness bug.
- A genuinely different, not-yet-found bug entirely (e.g. inside
  `RenderGraphCompiler::Compile()`'s culling logic, or inside
  `FrameDebuggerData.cpp`'s tree-building code itself fabricating/caching a
  leaf that does not actually correspond to this frame's real
  `RenderGraphSnapshot::passesInExecutionOrder`).

### 2.2 — Other known/likely "lies" already flagged in this codebase's own history

`AGENTS.md`'s own "Render Pass System" section, describing
`editor-core-separation-20`'s own **explicitly, permanently out of scope**
list, already CONFESSES to at least one more confirmed-inert toggle:

> "the Plugin Render Feature system (`RenderFeatureCompositor`,
> `demo_render_feature*` plugins) was never touched, including its own
> separately-inert demo-plugin clear toggles"

This maps directly to the screenshot's own "Disabled Built-In Passes" list,
which shows `DemoRenderFeaturePlugin_Clear (never run yet this session)` and
`DemoRenderFeatureSecondPlugin_Clear (never run yet this session)` sitting
right next to the reported bug. Reading
`src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp`'s
`AddFullscreenClearPass()` confirms it calls `m_builder.AddRenderPass(debugName, ...)`
DIRECTLY, with **zero** `RenderPassToggleRegistry` consult of any kind — this
pass is 100% unconditionally declared every frame regardless of whatever the
Render Graph panel's checkbox next to its name claims. **This is a second,
already-half-confirmed lie**, previously accepted as an explicit non-goal;
this campaign's own "iron rule" instruction from the user overrides that
prior acceptance — it must now be fixed too, not merely re-documented as
"still out of scope".

There may be more. Nobody has ever done a full, structured audit of every
single `RenderGraphBuilder::AddRenderPass()` call site in this codebase
cross-referenced against every togglable/enable-checkbox surface the Editor
or the HTTP API exposes. PHASE3 exists to do exactly that, exhaustively, for
the first time.

### 2.2b — A THIRD candidate lie, found by this campaign's own PHASE0 double-check (static only — PHASE3 must still confirm it live)

`src/Application/RenderPasses.cpp`'s `AddGpuSkinningPasses(rg::RenderGraphBuilder&
builder, Game& game, Renderer& renderer)` (~line 209) calls
`builder.AddRenderPass(request.name, ...)` **directly**, per GPU-skinning dispatch
request, with **zero** `RenderPassToggleRegistry` consult — the same shape of
problem as `AddFullscreenClearPass()` above. It is reached from
`Core::RegisterPresentRenderPipelineProvider()`'s `"Present"` provider
(`Core.cpp`, ~line 1017-1025: `AddGpuSkinningPasses(frame.builder, m_game,
m_renderer)`), called ONLY when `Core::Present()`'s own `needsDirectGameRender`
is `true` (`Core.cpp` ~line 1395: `m_gameTargetThisFrame == nullptr &&
m_sceneTargetThisFrame == nullptr`) — the same "direct-render-to-swapchain
fallback" `AGENTS.md`'s own "GPU-Driven Rendering" section already documents as
real, reachable production code (used whenever neither the Game View nor Scene
View panel currently holds an active render target), not a test-only dead
branch. This bypasses BOTH (a) the OFFSCREEN `"GpuSkinning"` provider's own
outer `NoteDeclaredAndCheckEnabled("GpuSkinning")` guard (`Core.cpp` ~line 476,
which only gates that SEPARATE, offscreen per-request `out.push_back(desc)`
loop) and (b) `RenderPipeline::DeclareOnePhase()`'s own generic flush-loop
auto-registration entirely, since `m_presentRenderPipeline.Register("Present",
...)`'s lambda never pushes anything into its own
`std::vector<rg::RenderPassDesc>&` parameter at all — it calls
`AddGpuSkinningPasses()`/`AddPresentPass()` directly instead. `AddPresentPass()`
itself needs no toggle check of its own — `"Present"` is permanently
deny-listed (`RenderPassToggleRegistry::IsDenyListed()`) by design. PHASE3's
Step 2 has the full detail and the exact live-verification approach to use for
this one; PHASE3's Step 2 also pre-confirms (already checked during this
double-check, by a plain `search_in_dir` for every call site) that
`RenderPasses.cpp`'s OTHER two free functions, `AddRenderOpaquePass()`/
`AddDrawSkyBackgroundPass()`, remain genuinely dead code (zero callers) exactly
as `AGENTS.md` already states — no live check is needed for those two.

### 2.3 — Tools available for LIVE diagnosis (use them; do not just re-read source and guess)

- `GET /render_graph` and `GET /render_graph/passes` — the real, live,
  per-frame `RenderGraphSnapshot`/toggle-registry JSON (`NetworkRoutes.cpp`).
- `GET /render_graph/set_pass_enabled?name=<X>&enabled=<true|false>` — drives
  the exact same mutation the panel's checkbox performs, scriptable over HTTP,
  no mouse/screenshot needed.
- `GET /frame_debugger/open`, `/enable?value=<bool>`, `/capture`,
  `/select_event?index=<N>`, `/state` — drive and inspect the Frame Debugger
  end-to-end without touching the mouse.
- `GET /get_game_view` / `GET /get_swapchain` / `GET /get_texture` (via
  `gte_send_request`) — a real rendered-frame screenshot, for a final,
  human-verifiable visual sanity check.
- `GET /get_logs?category=<X>&since_id=<N>` / `POST /clear_logs` — the
  engine's OWN internal logging system (`gte::Logger`, `GTE_LOG_DEBUG/INFO/
  WARNING/ERROR`). **This is the ONLY logging mechanism to use for any new
  diagnostic instrumentation in this campaign — never `printf`/`std::cout`/
  `OutputDebugString`/`fprintf(stderr, ...)` or any other standard C/C++
  logging facility.** Every phase below that adds temporary or permanent
  diagnostic logging must use `GTE_LOG_DEBUG`/`GTE_LOG_WARNING`/
  `GTE_LOG_ERROR` exclusively, with a distinct, greppable category string
  (e.g. `"RenderPassHonesty"`), and retrieve it via `gte_send_request` against
  `GET /get_logs`.
- `run_app_background` to launch `build\GreatTamanaEditor.exe` non-blocking,
  `gte_send_request` to poke it while it runs, `stop_app_background` to close
  it when done. Never leave a stray instance running between phases.

## Step 3: The Plan (detailed strategy)

This campaign is split into 6 phases, each its own `.md` file in this same
folder. Every phase file follows the same Step 1/Step 2/Step 3 structure as
this master file. **No phase is allowed to include a step that does not
involve writing/editing code, adding a test, adding logging instrumentation,
or running a build/verification command that directly gates a code change.**
Do not add filler phases like "we will not touch code this phase" — that
wastes tokens for nothing.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_LIVE_REPRODUCTION_AND_ROOT_CAUSE_DIAGNOSIS.md` | Reproduce the reported lie live over HTTP, add temporary `GTE_LOG_DEBUG` instrumentation at every decision point in the toggle chain, and pin down the EXACT mechanical root cause with captured log evidence — no fix yet, diagnosis only, but diagnosis IS code (temporary instrumentation is a real code change, committed). |
| 2 | `PHASE2_FIX_AERIAL_PERSPECTIVE_COMPOSITE_TOGGLE_LIE.md` | Implement the real fix for the confirmed root cause from PHASE1 (with concrete contingency guidance for each of the hypotheses in Step 2.1 above, so the phase is actionable regardless of which one turns out true), verify live over HTTP that the lie is gone, remove the temporary diagnostic-only logging (keep anything that has permanent diagnostic value). |
| 3 | `PHASE3_SYSTEMIC_AUDIT_RENDER_PASS_TOGGLE_HONESTY.md` | An exhaustive, itemized audit of EVERY `AddRenderPass()` call site in the engine cross-referenced against every togglable surface (Render Graph panel checkboxes, Plugin Render Feature enable checkboxes, GBuffer/Blur debug toggles), including this master file's own Step 2.2b third candidate (`AddGpuSkinningPasses()`'s direct-render-to-swapchain-fallback path) — classify each as Confirmed-Lie / False-Positive / Already-Honest / No-Toggle-Exists, with file/line citations, becoming PHASE4's fix backlog. This includes a live, HTTP-driven test pass through every classified item, not just code reading. |
| 4 | `PHASE4_FIX_AUDIT_FINDINGS.md` | Fix every Confirmed-Lie finding from PHASE3, including the already-known `DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear` inert toggles (Step 2.2 above), one concrete code change + live verification per finding. |
| 5 | `PHASE5_IRON_RULE_PERMANENT_MISMATCH_DETECTOR.md` | Build the permanent, automatic, self-enforcing "Render Pass Honesty" detector: a real runtime check (mirroring this codebase's own `ImGuiIdConflictTracker`/`DetectRenderPassEventContradictions` precedent) that fires a loud `GTE_LOG_ERROR` the instant any executed/non-culled pass in a captured frame disagrees with `RenderPassToggleRegistry`'s own recorded enabled state, plus a genuine Tier-1 test proving the detector fires on a synthetic contradiction and stays silent on a consistent case. |
| 6 | `PHASE6_FULL_REGRESSION_DOCS_AND_FINAL_VERIFICATION.md` | Full clean build, full `ctest` regression pass, `AGENTS.md`/`docs/conventions/` documentation update matching this codebase's own established style, and a final, live, HTTP-driven, screenshot-equivalent end-to-end proof that the iron rule now holds. |

### 3.1 — Locked Decisions (apply to every phase, do not re-litigate)

1. **Never use `printf`/`std::cout`/`fprintf`/`OutputDebugString` for any new
   diagnostic code in this campaign.** Always `GTE_LOG_DEBUG`/`_INFO`/
   `_WARNING`/`_ERROR` (`src/Editor/Logger.h`), retrieved via
   `GET /get_logs`. This is a hard, non-negotiable rule per this project's own
   `AGENTS.md` "Logging" section and the user's own explicit instruction.
2. **Never run a full clean build or full `ctest` regression pass except in
   PHASE6.** Every other phase uses an INCREMENTAL build
   (`cmake --build build`, which Ninja makes incremental automatically — do
   not `rm -rf build`/reconfigure unless something is genuinely broken) as its
   compile-check gate, plus live, targeted, HTTP-driven checks via
   `run_app_background`/`gte_send_request`/`stop_app_background` for behavior
   verification. This machine's full build/test cycle is slow — respect that.
3. **Every phase that changes `gte_core`-tier logic must add or update a
   Tier-1 test** per `AGENTS.md`'s "Testability & Regression Safety" section,
   wherever the change is expressible as pure logic (it usually is — see
   `AtmospherePassToggleLogic.h`'s own precedent of extracting the skip
   decision into a pure, dependency-free function specifically so it is
   Tier-1-testable).
4. **Every phase must end with**: an incremental compile check succeeding,
   a `.md` completion report (`PHASEn_COMPLETION_REPORT.md`) written into this
   same folder describing exactly what was done/found/verified, and a git
   commit (`git_add` + `git_commit`) covering both the code change and the
   report.
5. **Whenever a phase discovers a genuine design ambiguity or a decision only
   the user (or a delegating orchestrator standing in for the user) can make**
   — e.g. "should the two-toggle-name AtmosphereComposite/AerialPerspectiveCompositePass
   naming be collapsed into one, or is that its own, separate future
   campaign?" — it MUST use `ask_questions` before proceeding, exactly as the
   user's own instructions to this whole effort require. Every phase file
   below calls this out explicitly at its own likely decision points, but an
   implementer must use `ask_questions` for ANY other genuine ambiguity it
   personally discovers too, not just the ones anticipated here.
6. **Implementation-phase agents (anyone actually executing PHASE1..PHASE6)
   must NOT call `delegate_task`.** Delegation is reserved for the
   orchestrating/double-checking layer above these phase files. An
   implementation agent that discovers work needing further delegation must
   instead surface it via `ask_questions` or in its own completion report.
7. **The two already-known "lies" (Step 2.1's reported bug, Step 2.2's
   demo-plugin clear toggles) are IN SCOPE and must both be fixed by the end
   of this campaign — neither may be re-classified as "permanently out of
   scope" again without an explicit `ask_questions` confirmation first.**
8. **Naming discipline**: this campaign is `editor-core-separation-21`
   purely because of this folder's location under `task_manager/` (matching
   the numbered sibling folders already there) — it is NOT actually about
   `gte_core`/`gte_editor` library separation. Do not force any change in
   this campaign to relate to that unrelated, already-completed topic; the
   folder name is a sequence number, not a scope statement.

### 3.2 — Why this shape (six phases, not fewer/more)

- PHASE1/PHASE2 are split because diagnosis and fix are genuinely different
  activities with different risk profiles: PHASE1 must not assume a fix
  before evidence exists (guessing here risks "fixing" the wrong thing, as
  the AGENTS.md history shows already happened once with this exact pass).
- PHASE3/PHASE4 are split the same way, at a wider scope: audit-then-fix,
  because the audit's own findings determine exactly how much work PHASE4
  actually has — writing PHASE4's fix list before PHASE3 finishes would be
  guessing.
- PHASE5 is its own phase because it is a genuinely different KIND of work
  (building a permanent structural safeguard, not fixing one more bug) and
  has its own Tier-1 test obligation independent of PHASE1-4's fixes.
- PHASE6 is always last, matching every single prior campaign in this
  codebase's own history (`render-pass-*`, `editor-core-separation-*`, etc. —
  see `AGENTS.md`) — full regression + docs + final verification, once, at
  the end, never spread across phases.

### 3.3 — Definition of Done for the whole campaign

1. Live HTTP proof: disabling `AtmosphereAerialPerspectiveCompositePass` via
   `GET /render_graph/set_pass_enabled` makes it vanish from BOTH
   `GET /render_graph/passes` execution data AND the Frame Debugger's own
   next capture — with zero crash, and the Game View still renders a sane
   image (no magenta, matching `editor-core-separation-20`'s own precedent).
2. Live HTTP proof: disabling `DemoRenderFeaturePlugin_Clear`/
   `DemoRenderFeatureSecondPlugin_Clear` genuinely stops that pass from
   declaring/executing.
3. Every other finding from PHASE3's audit is either fixed (PHASE4) or
   explicitly, freshly re-confirmed via `ask_questions` as an accepted,
   permanent non-goal (not silently carried over from a stale, pre-iron-rule
   decision).
4. A real, permanent, automatic detector (PHASE5) exists and has its own
   passing Tier-1 test.
5. A full clean build and full `ctest` regression pass both succeed (PHASE6),
   with the test count only ever growing, never shrinking, versus the
   pre-campaign baseline.
6. `AGENTS.md` and a new/updated `docs/conventions/*.md` file describe this
   campaign's own final, shipped behavior, in the same tone/precision as
   every prior campaign entry already in `AGENTS.md`.
