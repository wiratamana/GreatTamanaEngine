# PHASE0 — MASTER STRATEGY: "The Engine Is STILL Lying" — Render Pass / Frame Debugger Honesty Campaign, Round 2

Campaign folder: `task_manager/editor-core-separation-22/`
Branch: `feature/editor-core-separation` (stay on it, do not create a new branch)
Project root: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE7_*.md`) must be read together with this file. Every child phase must,
at its own start, re-read this file plus the completion report of the phase
immediately before it (`PHASEn-1_COMPLETION_REPORT.md`, written by that
phase) — there might be a clue for continuation, a discovered root cause, or
a locked decision that changes how the next phase must be carried out.

A full prior campaign, `task_manager/editor-core-separation-21/`, already
fixed one confirmed instance of this exact class of bug (the
`AtmosphereAerialPerspectiveCompositePass` toggle lie) and built a permanent
detector (`RenderPassHonestyChecker.h`/`RenderPassHonestyGuard.h`). **That
detector is still in the codebase, still correct for what it checks, and
must NOT be deleted or weakened.** This campaign exists because the user
found THREE MORE, DIFFERENT lies that the previous campaign's own detector
structurally cannot see — this is not a regression of PHASE5 of that
campaign, it is a genuinely different bug SHAPE the previous detector was
never designed to catch. Read `task_manager/editor-core-separation-21/CAMPAIGN_COMPLETION_REPORT.md`
before starting PHASE1 — it is required context, not optional background.

---

## Step 1: The Goal (Where are we going?)

1. **Three, specific, reported-with-screenshots bugs must be killed**:
   1. Two `RenderOpaque` rows (and two `DrawSkyBackground` rows) appear in
      the "Render Graph" panel's live pass table, but only ONE row for each
      appears in the "Disabled Built-In Passes" section once toggled off —
      an apparent contradiction in how many "things" the panel believes
      exist for the exact same pass name.
   2. `DemoRenderFeaturePlugin_Clear` (and `DemoRenderFeatureSecondPlugin_Clear`)
      appear as real, enabled rows in the "Render Graph" panel, but produce
      **zero** visible nodes anywhere in the Frame Debugger's own captured
      event tree — a pass that (as far as the user can see) runs, yet the
      Frame Debugger shows nothing for it at all.
   3. Disabling `DrawSkyBackground` via the "Render Graph" panel does
      **not** stop the sky from being visible in either the Frame Debugger's
      own captured preview OR the live Game View — the exact "iron rule"
      violation `editor-core-separation-21` was supposed to have permanently
      eliminated, but for a DIFFERENT pass, through a DIFFERENT mechanism.
2. **The iron rule from `editor-core-separation-21`, restated, and now
   EXTENDED with a second clause that campaign's own detector never
   enforced**:
   > A render pass's declared/enabled state and the Frame Debugger's own
   > displayed event tree must NEVER disagree.
   > **Clause A** (already enforced by `RenderPassHonestyChecker` since
   > `editor-core-separation-21`): if a pass is disabled, it must not run,
   > and it must not appear as an executed leaf anywhere the Frame Debugger
   > or the Render Graph panel can show it.
   > **Clause B** (NOT previously enforced by any automatic detector — this
   > campaign's own PHASE6 must close this gap): if a pass runs (survives
   > culling, is non-culled in the real `RenderGraphSnapshot`), the Frame
   > Debugger's own tree MUST show it as a real leaf somewhere. There is no
   > acceptable middle ground, and no pass is exempt from Clause B except a
   > small, explicitly-named, permanently-documented allowlist of the Frame
   > Debugger's OWN ephemeral internal replay scaffolding (see Step 2.3
   > below) — never a whole `RenderPassCategory` value shared with real
   > user-facing features.
   > **Clause C** (new, this campaign): a pass's own toggle-off state must
   > gate EVERY observable side effect its own declaration code produces —
   > not only whether its own `RenderPassDesc` reaches the graph, but also
   > any data (blackboard publish, cached callback, member-variable
   > mutation) that some OTHER, independently-toggled pass might read and
   > reproduce that effect from, regardless of the first pass's own
   > disabled state.
3. Land this with a full clean build and full `ctest` regression pass at the
   very end (only at the very end — see the Workflow rules below), plus a
   `docs/conventions/` write-up and an `AGENTS.md` addition matching this
   codebase's own established documentation convention.

## Step 2: The Situation (Where are we now?) — three CONFIRMED root causes, found by static code tracing, each with exact file/line evidence

This is not a guess-and-check campaign. Unlike `editor-core-separation-21`'s
own PHASE1 (which had to build live diagnostic instrumentation from
scratch), this master strategy phase already traced all three bugs down to
their exact mechanical cause by reading the real, current source. Every
child phase below still requires LIVE, HTTP-driven verification of its own
fix (never "looks right, ship it") — but no phase needs to re-diagnose from
zero.

### 2.1 — Root Cause #1: duplicate pass rows are BY DESIGN, but the two UI sections disagree on how to summarize that design

`src/Renderer/RenderGraph/RenderPipeline.h`'s `RenderPipeline::DeclareOnePhase()`
(~line 548-601) invokes a `ProviderScope::PerActiveView` provider ONCE PER
ACTIVE VIEW (Game View AND Scene View, when both are visible), each
invocation pushing its own `RenderPassDesc` into the SAME shared
`m_scratchCollected` vector — both instances carrying the IDENTICAL
`debugName` (e.g. `"RenderOpaque"`, registered at `src/Core/Core.cpp`
~line 635; `"DrawSkyBackground"`, registered ~line 824). The generic toggle
gate (`RenderPipeline.h` ~line 588-591,
`m_passToggleRegistry->NoteDeclaredAndCheckEnabled(desc.debugName)`) is
correctly, consistently keyed by NAME — toggling `"RenderOpaque"` off does
genuinely, consistently remove BOTH the Game View and Scene View instances,
every time — **this part is already honest**.

The user-visible confusion is a UI/data-model mismatch, not a logic bug:
- `src/Editor/Panels/RenderGraphPanel.cpp`'s `BuildPassTable()`/`BuildPassRow()`
  (~line 51-170) renders ONE ROW PER SNAPSHOT ENTRY — i.e. exactly the raw,
  per-view-duplicated cardinality (2 rows for `"RenderOpaque"` when both
  views are active). Its own doc comment (line 53-63) already, explicitly,
  documents this as "a real, permanent, INTENTIONAL fact about this table".
- `BuildDisabledBuiltInPassesSection()` (~line 355-387) instead reads
  `RenderPassToggleRegistry::ListAll()`, which is backed by
  `std::unordered_map<std::string, RenderPassToggleState>` (`RenderPassToggleRegistry.h`
  ~line 99) — inherently ONE ENTRY PER UNIQUE NAME, by construction.
- The two sections are both individually "correct" relative to their own
  data source, but they present two DIFFERENT cardinalities for the exact
  same underlying pass name with zero cross-reference or annotation
  explaining why — this is exactly what the user experiences as "the
  engine is lying about how many RenderOpaque passes exist."
- PHASE5 fixes this with a genuine data-model/UI rework (not a comment
  update) — see that phase file for the full design.

### 2.2 — Root Cause #2: `RenderPassCategory::Debug` is a SEMANTICALLY OVERLOADED, MISUSED category, and the Frame Debugger tree has no generic "catch every survivor" fallback for Graphics-kind passes

`src/Renderer/RenderGraph/RenderGraphTypes.h` (~line 387-390) defines
`RenderPassCategory::Debug`'s OWN, unambiguous, already-written contract:

> `Debug` — "Frame-Debugger-internal replay passes / Compute Blur
> Validation — never a real Frame Debugger tree citizen themselves (already
> filtered out, or shown under their own separate heading)."

`src/Editor/FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()`
correctly implements exactly that contract at its own "view region" walk
(~line 852-856): any surviving Graphics-kind pass tagged
`RenderPassCategory::Debug` is unconditionally `continue`'d — invisible,
by design, matching the category's own documented meaning.

**The actual bug**: `src/Core/Plugins/PluginRenderPassBuilderAdapter.cpp`'s
`AddFullscreenClearPass()` (~line 67-82, the function that produces
`DemoRenderFeaturePlugin_Clear`/`DemoRenderFeatureSecondPlugin_Clear`) tags
itself `RenderPassCategory::Debug` too — but its own doc comment (line
19-22) gives a DIFFERENT, INCOMPATIBLE reason: "the deliberate, correct
category for every plugin-contributed pass... genuinely optional/
debug-flavored passes". This is a real, confirmed misunderstanding of what
`RenderPassCategory::Debug` actually means in this codebase, introduced
historically (editor-core-separation-3 campaign) and never caught. The
pass is real, user-toggleable, and NOT Frame-Debugger-internal scaffolding
— yet it gets the exact same "never a tree citizen" treatment as the Frame
Debugger's own ephemeral `FrameDebuggerReplayStepN` passes
(`src/Editor/FrameDebuggerReplayPasses.cpp` ~line 183, the ACTUAL intended
user of this category value). `src/Editor/GBufferValidation.cpp` (~line 164,
207) and `src/Editor/ComputeBlurValidation.cpp` (~line 109) make the exact
same category misuse for the exact same wrong reason — those two currently
have no OBSERVABLE bug today only because they are also `ViewScope::SceneView`,
independently excluded by a completely different filter
(`FrameDebuggerData.cpp` line 785/853/906's own `pass.viewScope ==
rg::ViewScope::SceneView` checks) — but their category tag is still
factually wrong and a latent trap for the next feature that reuses this
same (mis)pattern.

**A second, independent, structural gap makes this worse than "just fix the
category tag"**: `DemoRenderFeaturePlugin_Clear` is registered with
`rg::RenderPassEvent::AfterEverything` (the latest possible tier), so in
true execution order it is sorted to sit AFTER the real Aerial Perspective
Composite compute passes. `BuildRealFrameDebuggerSnapshot()`'s own
"view region" walk (~line 835-889) `break`s out ENTIRELY the first time it
encounters ANY surviving Compute-kind pass (line 838-843) — handing the
rest of the array off to a SEPARATE loop (~line 898-919) that ONLY ever
processes Compute-kind passes (line 906: `if (pass.kind !=
rg::PassKind::Compute ...) continue;`), silently skipping ANY Graphics-kind
pass positioned in that same tail region — REGARDLESS of its category.
**Simply reclassifying `DemoRenderFeaturePlugin_Clear`'s category away from
`Debug` is therefore NOT sufficient by itself** — a genuine, generic
"nothing that survives is ever silently dropped" fallback bucket is
required (mirroring the `"Compute Dispatches (Pre/Post-GameView)"`
fallback buckets that ALREADY exist for Compute-kind passes, ~line 755-781
— there is currently no Graphics-kind equivalent). PHASE4 designs and
builds exactly this.

### 2.3 — Root Cause #3: a disabled pass's SIDE EFFECT survives it, and an unrelated, independently-toggled pass reproduces the effect

`src/Core/Core.cpp`'s `"DrawSkyBackground"` provider (~line 824-874)
unconditionally builds a `recordSkyBackground` callback and, for Game View,
`frame.blackboard.Publish<std::function<void(VkCommandBuffer)>>(kGameSkyBackgroundCallbackKey,
recordSkyBackground)` (line 847-849) — **before any toggle-registry check
of any kind**. Unlike every one of `AtmosphereLutRenderer`'s five toggle-
aware methods (which each call `ShouldDeclareAtmospherePassThisFrame(...)`
and early-return BEFORE producing any side effect — the pattern
`editor-core-separation-20`'s own PHASE2 invented specifically to prevent
this exact class of bug), `"DrawSkyBackground"`'s provider has NO early
gate of its own. It relies ENTIRELY on the generic, LATE gate inside
`RenderPipeline::DeclareOnePhase()` (`RenderPipeline.h` ~line 588-591) —
but that gate only decides whether the collected `RenderPassDesc` reaches
`builder.AddRenderPass()`. It does nothing to undo the blackboard
`Publish()` call that already happened as a side effect of merely invoking
the provider lambda, which happens unconditionally, every frame, regardless
of enabled state.

`Core.cpp` (~line 1211-1233) later, unconditionally, `Fetch()`es this same
cached callback (`gameSkyBackgroundCallbackForReplay`) and, whenever the
Frame Debugger has an active step/replay request pending
(`m_editorLayer->ConsumePendingFrameDebuggerReplayRequest()` — exactly the
state the user's own third screenshot shows: Editor paused, "Step" mode
active), forwards it into `FrameDebuggerCaptureContext::AddReplayPasses()`
(`src/Editor/FrameDebuggerReplayPasses.cpp` ~line 88-216). That function
checks its OWN, SEPARATE, umbrella toggle (`"FrameDebuggerReplay"`, line
102) — a toggle that has nothing to do with `"DrawSkyBackground"` at all —
and then, on its own dedicated "sky step" (line 206-208):
```cpp
if (isSkyStep && recordSkyBackground) {
    recordSkyBackground(ctx.cmd);
}
```
unconditionally redraws the sky into the replay/preview target, completely
independent of whether the real `"DrawSkyBackground"` pass declared
anything this frame at all. This is the exact mechanical explanation for
"I disabled DrawSkyBackground but the Frame Debugger and Game View still
show Sky" — the REAL `"DrawSkyBackground"` pass is honestly, correctly
absent from the graph when disabled (confirmed by re-reading the generic
gate); a COMPLETELY DIFFERENT, honestly-enabled pass
(`"FrameDebuggerReplayStepN"`'s own dedicated sky step) is what is actually
drawing it, using a side channel that never checked the first pass's own
toggle at all.

**Why `RenderPassHonestyChecker` (editor-core-separation-21, PHASE5) cannot
catch this**: that detector (`src/Editor/RenderPassHonestyChecker.h`/`.cpp`)
compares each NON-CULLED pass NAME in the snapshot against that SAME name's
own toggle state. `"FrameDebuggerReplayStepN"` reports its OWN toggle
(`"FrameDebuggerReplay"`) honestly — there is no name-level contradiction
for the detector to see. The lie lives ENTIRELY inside what that pass's
`execute` lambda actually DOES (reproducing a disabled pass's own visual
effect via a smuggled callback), which is invisible to any detector that
only ever compares names against registry state. PHASE6 must close this gap
with a fundamentally different kind of check.

### 2.4 — Tools available for LIVE diagnosis/verification (use them; do not just re-read source and guess)

- `GET /render_graph` and `GET /render_graph/passes` — the real, live,
  per-frame `RenderGraphSnapshot`/toggle-registry JSON (`NetworkRoutes.cpp`).
- `GET /render_graph/set_pass_enabled?name=<X>&enabled=<true|false>` —
  drives the exact same mutation the panel's checkbox performs.
- `GET /frame_debugger/open`, `/enable?value=<bool>`, `/capture`,
  `/select_event?index=<N>`, `/state`, `/step` — drive and inspect the
  Frame Debugger end-to-end without touching the mouse.
- `GET /get_game_view` / `GET /get_swapchain` / `GET /get_texture` (via
  `gte_send_request`) — a real rendered-frame screenshot, for a final,
  human-verifiable visual sanity check (e.g. confirming no sky pixels
  remain once `"DrawSkyBackground"` is disabled AND a replay step is
  active).
- `GET /get_logs?category=<X>&since_id=<N>` / `POST /clear_logs` — the
  engine's OWN internal logging system (`gte::Logger`, `GTE_LOG_DEBUG/INFO/
  WARNING/ERROR`). **This is the ONLY logging mechanism to use for any new
  diagnostic or permanent instrumentation in this campaign — never
  `printf`/`std::cout`/`OutputDebugString`/`fprintf(stderr, ...)`.** Note:
  a few EXISTING call sites in this codebase (e.g.
  `GpuDrivenBatches`'s pipeline-resolve failure, `RenderPassBlackboard::ReportUnusedPublishesIfAny()`)
  still use `std::fprintf(stderr, ...)` from BEFORE this rule was
  established — leave those untouched (out of scope) unless a phase below
  explicitly says otherwise; never add a NEW one.
- `run_app_background` to launch `build\GreatTamanaEditor.exe` non-blocking,
  `gte_send_request` to poke it while it runs, `stop_app_background` to
  close it when done. Never leave a stray instance running between phases.

## Step 3: The Plan (detailed strategy)

This campaign is split into 7 implementation phases, each its own `.md`
file in this same folder, plus this PHASE0 orchestrator. Every phase file
follows the same Step 1/Step 2/Step 3 structure as this master file. **No
phase is allowed to include a step that does not involve writing/editing
code, adding a test, adding logging instrumentation, or running a
build/verification command that directly gates a code change.**

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_FIX_DRAWSKYBACKGROUND_TOGGLE_SIDE_CHANNEL_LEAK.md` | Fix Root Cause #3 (Step 2.3): gate `"DrawSkyBackground"`'s own blackboard publish behind an early, generalized, reusable toggle guard mirroring `ShouldDeclareAtmospherePassThisFrame` — the confirmed, specific, reported bug. |
| 2 | `PHASE2_SYSTEMIC_AUDIT_TOGGLE_SIDE_CHANNEL_LEAKS.md` | Widen the manhunt: an exhaustive audit of EVERY `RenderPassBlackboard::Publish()` call site and every side-effect-bearing `RenderPipeline` provider in `Core.cpp`/plugin adapters, classified Confirmed-Lie/Already-Honest, becoming PHASE3's fix backlog. |
| 3 | `PHASE3_FIX_AUDIT_FINDINGS_SIDE_CHANNEL_LEAKS.md` | Fix every Confirmed-Lie finding from PHASE2, one concrete code change + live verification per finding. |
| 4 | `PHASE4_FRAME_DEBUGGER_TREE_COMPLETE_COVERAGE_AND_CATEGORY_FIX.md` | Fix Root Cause #2 (Step 2.2): correct the `RenderPassCategory::Debug` misuse (introduce a correctly-scoped, dedicated category/tag for genuinely Frame-Debugger-internal scaffolding ONLY) and add a generic "nothing survives silently dropped" fallback bucket to `BuildRealFrameDebuggerSnapshot()`. |
| 5 | `PHASE5_UNIFY_DUPLICATE_PASS_ROWS_RENDER_GRAPH_PANEL.md` | Fix Root Cause #1 (Step 2.1): rework the "Render Graph" panel's pass table to present one row per unique pass name per regime (annotated by which view(s) contributed it, with summed stats), eliminating the cardinality mismatch against "Disabled Built-In Passes". |
| 6 | `PHASE6_IRON_RULE_V2_BIDIRECTIONAL_DETECTOR.md` | Extend the permanent "Render Pass Honesty" detector with the missing Clause B (Step 1.2 above) and a new check for Root-Cause-#3-shaped side-channel leaks, so future regressions of THESE bug shapes are caught automatically, loudly, in-engine. |
| 7 | `PHASE7_FULL_REGRESSION_DOCS_AND_FINAL_VERIFICATION.md` | Full clean build, full `ctest` regression pass, `AGENTS.md`/`docs/conventions/` documentation update, and a final, live, HTTP-driven, end-to-end proof reproducing (and disproving) all three original screenshots' own scenarios. |

### 3.1 — Locked Decisions (apply to every phase, do not re-litigate)

1. **Never use `printf`/`std::cout`/`fprintf`/`OutputDebugString` for any
   NEW diagnostic or permanent code in this campaign.** Always
   `GTE_LOG_DEBUG`/`_INFO`/`_WARNING`/`_ERROR`, retrieved via `GET /get_logs`.
2. **Never run a full clean build or full `ctest` regression pass except in
   PHASE7.** Every other phase uses an INCREMENTAL build (`cmake --build
   build`) as its compile-check gate, plus live, targeted, HTTP-driven
   checks via `run_app_background`/`gte_send_request`/`stop_app_background`
   for behavior verification.
3. **Every phase that changes `gte_core`-tier logic must add or update a
   Tier-1 test** wherever the change is expressible as pure logic (it
   usually is — see `AtmospherePassToggleLogic.h`'s/`RenderPassToggleChangeDetectionLogic.h`'s
   own precedent of extracting a decision into a pure, dependency-free,
   directly-unit-testable function).
4. **Every phase must end with**: an incremental compile check succeeding,
   a `.md` completion report (`PHASEn_COMPLETION_REPORT.md`) written into
   this same folder, and a git commit (`git_add` + `git_commit`) covering
   both the code change and the report.
5. **Whenever a phase discovers a genuine design ambiguity or a decision
   only the user (or a delegating orchestrator standing in for the user)
   can make, it MUST use `ask_questions` before proceeding.** Every phase
   file below calls out its own likely decision points explicitly, but an
   implementer must use `ask_questions` for ANY other genuine ambiguity it
   personally discovers too.
6. **Implementation-phase agents (anyone actually executing PHASE1..PHASE7)
   must NOT call `delegate_task`.** Delegation is reserved for the
   orchestrating/double-checking layer above these phase files.
7. **The existing `RenderPassHonestyChecker`/`RenderPassHonestyGuard`
   detector from `editor-core-separation-21` must survive this whole
   campaign unweakened.** PHASE6 ADDS new checks alongside it; it never
   removes or narrows the existing Clause-A check.
8. **`RenderPassCategory::Debug`'s existing, documented meaning
   ("Frame-Debugger-internal, never a tree citizen") is the one that WINS**
   — PHASE4 fixes every MISUSE of it (real, user-toggleable features
   wrongly tagged this way) rather than redefining the category itself to
   mean something looser. A brand-new, separately-named value/marker is
   introduced for the cases that actually need "genuinely optional/
   debug-flavored, but still a real tree citizen when it runs."
9. **Naming discipline**: this campaign is `editor-core-separation-22`
   purely because of this folder's location under `task_manager/` (matching
   the numbered sibling folders already there) — it is NOT actually about
   `gte_core`/`gte_editor` library separation.

### 3.2 — Why this shape (seven phases, not fewer/more)

- PHASE1 fixes the one bug with a screenshot AND a fully-traced root cause
  first, both because it is the highest-priority reported regression and
  because its fix ESTABLISHES the reusable "early toggle guard" pattern
  PHASE2/PHASE3 then apply systemically — doing the systemic audit before a
  single confirmed instance is fixed would mean auditing against a pattern
  that does not concretely exist in the codebase yet.
- PHASE2/PHASE3 are split audit-then-fix, mirroring `editor-core-separation-21`'s
  own proven PHASE3/PHASE4 shape, for the identical reason: the audit's own
  findings determine exactly how much work the fix phase has — writing the
  fix list before the audit finishes would be guessing.
- PHASE4 and PHASE5 are two SEPARATE phases despite both being "Frame
  Debugger/Render Graph panel" work because they fix two genuinely
  DIFFERENT root causes (#2 and #1) with no shared code path — merging them
  would create one oversized, harder-to-verify phase for no benefit.
- PHASE6 is its own phase because it is a genuinely different KIND of work
  (building permanent structural safeguards for TWO new bug shapes, not
  fixing a specific instance) and has its own Tier-1 test obligations
  independent of PHASE1-5's fixes.
- PHASE7 is always last, matching every prior campaign in this codebase's
  own history — full regression + docs + final verification, once, at the
  end, never spread across phases.

### 3.3 — Definition of Done for the whole campaign

1. Live HTTP proof: disabling `"DrawSkyBackground"` makes the sky
   disappear from the Game View AND from a Frame Debugger replay/step
   capture, with zero crash.
2. Live HTTP proof: enabling `DemoRenderFeaturePlugin_Clear`/
   `DemoRenderFeatureSecondPlugin_Clear` makes each appear as a real,
   named leaf somewhere in the very next Frame Debugger capture; disabling
   either makes it vanish from both the graph AND the tree.
3. Live HTTP proof: the "Render Graph" panel's own pass table and its
   "Disabled Built-In Passes" section never again present two different,
   unexplained counts for the same pass name.
4. Every other finding from PHASE2's audit is either fixed (PHASE3) or
   explicitly, freshly re-confirmed via `ask_questions` as an accepted,
   permanent non-goal.
5. A real, permanent, automatic detector (PHASE6) exists for BOTH new bug
   shapes (Clause B tree-omission, Clause C side-channel leakage) and has
   its own passing Tier-1 tests, alongside the untouched, still-passing
   `editor-core-separation-21` detector.
6. A full clean build and full `ctest` regression pass both succeed
   (PHASE7), with the test count only ever growing, never shrinking.
7. `AGENTS.md` and a new/updated `docs/conventions/*.md` file describe this
   campaign's own final, shipped behavior.
