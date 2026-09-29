# PHASE4 — Fix Every Confirmed-Lie Finding From The Audit

Parent: `PHASE0_MASTER_STRATEGY.md` (MUST READ FIRST).
Previous phase report: `PHASE3_COMPLETION_REPORT.md` (MUST READ FIRST — this
phase's entire task list IS that report's "PHASE4 fix backlog" section;
do not start without it).

---

## Step 1: The Goal (Where are we going?)

Every single row PHASE3 classified `Confirmed-Lie` is fixed, honestly,
verified live, by the end of this phase. Zero exceptions carried forward
silently — any row the user/orchestrator explicitly agreed (via PHASE3's own
`ask_questions` checkpoint) to defer is the ONLY acceptable exception, and it
must be listed explicitly, by name, in this phase's own completion report as
"deferred by explicit user decision", never silently dropped.

## Step 2: The Situation (Where are we now?)

This phase's task list is entirely determined by PHASE3. This file cannot
enumerate the exact final backlog in advance, but based on PHASE0's own
preliminary analysis (Step 2.2) and PHASE3's Step 3.2 checklist, the
following fix is already known, with high confidence, to be required
regardless of what else PHASE3 finds:

### 2.1 — `DemoRenderFeaturePlugin_Clear` / `DemoRenderFeatureSecondPlugin_Clear` are unconditionally declared

`src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp`'s
`AddFullscreenClearPass(const char* debugName, float r, float g, float b, float a)`
calls `m_builder.AddRenderPass(debugName, ...)` directly with no
`RenderPassToggleRegistry` consult at all. Whatever code CALLS
`AddFullscreenClearPass()` for these two demo plugins (locate it —
`search_in_dir` for `AddFullscreenClearPass` and for
`"DemoRenderFeaturePlugin_Clear"`/`"DemoRenderFeatureSecondPlugin_Clear"`
across `src/`/`plugins/`) is where the toggle checkbox's OWN name comes
from — the checkbox already exists and already writes to
`RenderPassToggleRegistry` (confirmed by its presence, unticked, under
"Disabled Built-In Passes" in the reported screenshot with the honest
"(never run yet this session)" annotation, proving `RenderPassToggleRegistry`
already has an entry for it — just one nothing ever reads back). This is
almost certainly the SAME "auto-discovery" mechanism PHASE3 Step 2 (item 1)
describes: `RenderPipeline::DeclareOnePhase()`'s generic flush loop calling
`NoteDeclaredAndCheckEnabled(debugName)` on every pass ANY provider declares,
whether or not that pass's own code path bothers to check the RETURN value.

**The fix, precisely**: `AddFullscreenClearPass()` needs its own boolean
enabled check before calling `m_builder.AddRenderPass(...)`, exactly mirroring
the `AtmosphereLutRenderer` five-method precedent from `editor-core-separation-20`
PHASE2 (`ShouldDeclareAtmospherePassThisFrame`-shaped: no upstream handle to
check here, so the check simplifies to just `passEnabledThisFrame` on its
own). Concretely:
1. `PluginRenderPassBuilderAdapter` needs access to a
   `rg::RenderPassToggleRegistry*` (mirroring how `AtmosphereLutRenderer`'s
   methods receive one as a parameter). Its ONLY real construction site today
   (confirmed by `search_in_dir` for `PluginRenderPassBuilderAdapter adapter(`
   across `src/`) is `src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp`'s
   `ContributeRenderGraphPasses()` (~line 47:
   `PluginRenderPassBuilderAdapter adapter(frame.builder, resolved->target);`),
   a method of a class that already holds a `Core& m_core` member — so the
   smallest fix is: add a third constructor parameter,
   `rg::RenderPassToggleRegistry* toggleRegistry`, to
   `PluginRenderPassBuilderAdapter`'s constructor (`.h`/`.cpp`), store it as a
   new private member, and pass `&m_core.GetRenderPassToggleRegistryMutable()`
   at this one call site. Re-run the `search_in_dir` yourself before coding in
   case a second construction site was added since this campaign's own
   double-check pass found only this one.
2. `AddFullscreenClearPass()` calls
   `toggleRegistry->NoteDeclaredAndCheckEnabled(debugName)` (reusing the
   SAME `debugName` parameter it already receives — no new/second name
   string needed) and returns early (declaring nothing) if disabled.
3. Confirm this does NOT break the "DEVIATION from Step 3.3's own sketch"
   `RenderPassEvent::AfterEverything` fix already documented in this same
   file's own header comment (Step 2.1's own history lesson: a write-only
   pass with the wrong `RenderPassEvent` tier silently gets overwritten/
   reordered) — the toggle-off early return must happen BEFORE any
   `RenderPassEvent`/attachment-declaration logic, exactly mirroring where
   `AtmosphereLutRenderer`'s own five methods place their early return.
4. If the enabled/disabled clear pass genuinely has NOTHING useful to render
   when off (unlike the Atmosphere composite pass, this "Clear" pass has no
   meaningful "fallback" texture — the whole POINT of a demo-plugin clear is
   just to prove the render-graph mechanism, so simply not declaring it is
   correct and sufficient, with no downstream consumer to gracefully
   degrade — confirm this by reading what, if anything, reads this pass's
   own written target afterward).

### 2.2 — Everything else PHASE3 found

Follow PHASE3's own backlog ordering. For each row:
1. Re-read the exact file/line PHASE3 cited.
2. Implement the smallest correct fix that makes the toggle honestly gate
   declaration — reuse `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled(name)`
   wherever the existing mechanism already fits (the overwhelmingly likely
   case for anything NOT already using a bespoke flag); only introduce a new
   mechanism if the finding is structurally different (e.g. a genuinely
   separate concept like `RenderFeatureCompositor::SetFeatureEnabled` turning
   out to be broken would need its OWN internal fix inside that class, not a
   `RenderPassToggleRegistry` retrofit — do not conflate the two systems).
3. Verify LIVE, using the exact same repro-script shape from PHASE1/PHASE3:
   toggle off via HTTP, fresh capture, confirm absence from the Frame
   Debugger tree AND from `GET /render_graph/passes`' execution data, confirm
   sane visual fallback via `GET /get_game_view`, toggle back on, confirm
   full restoration with no crash/leak.
4. Add/update a Tier-1 test for any newly-introduced pure logic (mirroring
   `AtmospherePassToggleLogic.h`'s pattern) wherever the finding's fix is
   expressible as pure logic.

## Step 3: The Plan

### 3.1 — Fix order

1. Item 2.1 above (`DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear`)
   first — already scoped in detail above, lowest risk, matches a
   well-established precedent exactly.
2. Then PHASE3's backlog, in the order PHASE3's own report lists it (PHASE3
   is expected to order it, easiest/lowest-risk first, in its own report).
3. If PHASE3 flagged the dual-toggle-name UX finding (`"AtmosphereComposite"`
   vs `"AtmosphereAerialPerspectiveCompositePass"`) as something to fix here
   (rather than deferred), and the agreed fix involves an actual panel-layout
   or naming change, treat that as its OWN sub-item with its own live
   verification — do not bundle it silently into another fix's commit.

### 3.2 — Ambiguity checkpoint

If, while implementing any fix, a genuinely reachable case turns up where
"honestly gating this pass" would leave some OTHER real production pass with
an invalid/dangling dependency (mirroring the exact
`physicalVolumeTextures[0xFFFFFFFF]` hazard `editor-core-separation-20`'s own
investigation found and fixed for the Atmosphere LUT chain) — use
`ask_questions` before deciding how far the cascading-validity-guard fix
needs to reach, exactly like that campaign's own precedent required.

### 3.3 — Wrap-up

1. Incremental compile check must succeed after every individual fix (not
   just once at the very end) — this keeps each fix independently
   bisectable if something breaks.
2. Write `PHASE4_COMPLETION_REPORT.md`: one sub-section per fixed finding,
   each with its own before/after live HTTP verification transcript, plus an
   explicit "Deferred by user decision" list (should be empty, or short and
   justified) at the end.
3. `git_add` + `git_commit` (one commit is fine if all fixes are small and
   related; split into multiple commits if that better isolates risk — use
   judgment, but every commit must leave the tree in a compiling state).
4. `stop_app_background` the running engine instance before finishing.
5. Do NOT call `delegate_task`. Use `ask_questions` per Step 3.2 above.
