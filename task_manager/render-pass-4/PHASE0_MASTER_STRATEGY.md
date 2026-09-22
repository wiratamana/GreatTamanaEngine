# PHASE0_MASTER_STRATEGY — RenderPassEvent Ordering Truth Campaign (`render-pass-4`)

_Part of the `feature/render-pass-impl` branch. Lives under
`task_manager/render-pass-4/`. This is the ORCHESTRATOR document — every
other `PHASEn_*.md` file in this same folder is a child of this one. Read
this file FIRST, always, before touching any child phase. This campaign is
a direct follow-up to `render-pass-1`/`render-pass-2`/`render-pass-3`
(`task_manager/render-pass-1|2|3/PHASE0_MASTER_STRATEGY.md`) — read
`render-pass-3`'s own `PHASE0_MASTER_STRATEGY.md` too if anything below
references a term ("`RenderPipeline`", "`ProviderTiming`", "Locked Design
Decision 5") you don't already recognize; this document does not repeat
that whole campaign's own background from scratch._

## Step 1: The Goal (Where are we going?)

`RenderPassEvent` (`BeforeEverything`/`PreOpaques`/`Opaques`/`AfterOpaques`/
`Transparents`/`AfterTransparents`/`AfterEverything`, `RenderGraphTypes.h`)
LOOKS like the mechanism that decides render-pass order. It is not — real
ordering is 100% a function of which pass got DECLARED first, in C++ call
order, into `RenderGraphBuilder`'s internal `m_passes` list.
`RenderGraphCompiler::Compile()`'s own RAW/WAW dependency-edge construction
(`RenderGraphCompiler.cpp`) never reads `PassRecord::renderPassEvent` at
all — confirmed by that field's own doc comment, three separate times, in
`RenderGraphTypes.h`. This already caused one real, live, confirmed
production bug (see Step 2 below) that was patched around, not fixed at
the root.

**This campaign has two goals, run as two genuinely separate lines of
work, exactly per this campaign's own kickoff Q&A (see "Locked Design
Decisions" below):**

1. **A safety net, shipped first, that changes no scheduling behavior at
   all**: a debug-visible, always-on detector inside
   `RenderGraphCompiler.cpp` itself that loudly reports — via an
   unconditional `stderr` message plus a debug-build `assert()` — the
   exact moment a pass's declared `RenderPassEvent` contradicts what its
   real, compiler-enforced resource dependencies say must actually happen.
   Nobody should ever again have to rediscover this bug class by staring
   at a solid-white/black Game View and manually bisecting registration
   order, the way it was originally found.
2. **A real fix, shipped second, clearly labeled as a genuine behavior
   change**: make `RenderGraphCompiler::Compile()` actually USE
   `RenderPassEvent` as the ordering key its RAW/WAW dependency scan
   processes passes in — instead of raw declaration order — so a pass
   tagged with an earlier `RenderPassEvent` than another pass it doesn't
   even depend on can no longer accidentally end up on the wrong side of
   a dependency purely because of which C++ file happened to call
   `AddRenderPass()`/register its provider first.

Both goals are pure code changes to existing, already-shipped files —
nothing here is a rewrite of `RenderGraphCompiler`, `RenderGraph.cpp`, or
`RenderGraphBarrierPlanner.cpp`'s actual barrier/lifetime logic. The
topological-sort-plus-reachability shape Phase 3 of the original Render
Graph campaign built stays exactly as it is; only WHAT ORDER it processes
passes in, and WHETHER it complains about a contradiction, changes.

## Step 2: The Situation (Where are we now?)

Confirmed by direct source inspection — this document is the single
source of truth for every fact quoted in every child phase; do not
re-derive these from scratch.

1. **`RenderPassEvent` is real, shipped, and stamped onto every pass, but
   read by nothing that decides order.** `RenderGraphTypes.h` (~line 403)
   states outright: "a purely descriptive SORT HINT for the new
   `RenderPipeline` declaration layer... NOT a dependency mechanism (real
   ordering is still fully enforced by `RenderGraphCompiler`'s own RAW/WAW
   dependency analysis, which never reads this field...)". `PassRecord`'s
   own `renderPassEvent` field comment (~line 595) repeats this a second
   time: "read by NOTHING in `RenderGraph.cpp`/`RenderGraphCompiler.cpp`/
   `RenderGraphBarrierPlanner.cpp`". This is not a bug in the comment — it
   is a completely accurate description of `RenderGraphCompiler.cpp`'s
   actual code today (verified directly: `Compile()`'s entire RAW/WAW
   edge-construction loop, `~lines 106-162`, only ever indexes by
   declaration position `i`, and never once reads `.renderPassEvent`).
2. **This already caused a real, live, confirmed bug — and the fix that
   shipped is a targeted patch, not a structural one.**
   `src/Application/Application.cpp` (~lines 626-644) carries this exact,
   already-in-the-repo comment on the `"AtmosphereComposite"` provider
   registration: *"CORRECTNESS-CRITICAL, confirmed by live testing during
   this phase — MUST be registered with `ProviderTiming::AfterDeferredPasses`
   ...: this pass READS the SAME texture handle `"RenderOpaque"`/
   `"DrawSkyBackground"` WRITE, so it must be declared STRICTLY AFTER them
   in the underlying pass list — registering it with the default
   `BeforeDeferredPasses` timing... causes `RenderGraphCompiler::Compile()`'s
   own resource-versioning scan to see this pass's read BEFORE any writer
   is known, silently CULLING `"RenderOpaque"`/`"DrawSkyBackground"`
   entirely (confirmed live: Game/Scene View rendered solid white/black,
   and the Render Graph panel showed `"RenderOpaque"` as `"culled"`)."*
   `AtmosphereComposite`'s own `RenderPassEvent` (`AfterTransparents`,
   correctly the LAST tier) was never wrong — the bug was purely about
   which C++ registration slot it landed in relative to `RenderOpaque`'s
   own declaration. The actual fix that shipped
   (`RenderPipeline.h`'s `ProviderTiming::BeforeDeferredPasses`/
   `AfterDeferredPasses` two-phase split, `render-pass-3` campaign, PHASE3)
   is a real, working, but NARROW patch: it only protects the boundary
   between "providers that call `frame.builder` immediately" and
   "providers that defer into a `RenderPassDesc` list that gets sorted by
   `.order` before flushing" — see `RenderPipeline.h`'s own
   `ProviderTiming` doc comment (~lines 389-424) for the full, self-aware
   write-up of exactly this narrowness.
3. **`RenderPipeline::DeclareOnePhase()` DOES sort by `RenderPassEvent`
   today — but only within its own narrow slice of the world.**
   `RenderPipeline.h` (~line 532): `std::stable_sort(m_scratchCollected...,
   [](a, b){ return a.order < b.order; })` runs once per `ProviderTiming`
   phase, and only over passes a provider DEFERRED into a `RenderPassDesc`
   (never over a pass an "immediate" provider declared directly via
   `frame.builder`, and never over a pass declared the old way, straight
   through `RenderGraphBuilder::AddRenderPass()`, bypassing `RenderPipeline`
   entirely — e.g. `AddFrameDebuggerReplayPasses()`'s replay steps,
   `ComputeBlurValidation.cpp`'s own pass, both deliberately kept off the
   new provider system forever per `render-pass-3`'s own Locked Design
   Decision 4). By the time `RenderGraphCompiler::Compile()` actually runs,
   ALL of these — deferred-and-sorted, immediate-and-unsorted, and legacy
   direct calls — have already been flattened into one single
   `CompiledGraphInput::passes` list, in whatever order they happened to
   be pushed, and `Compile()` itself treats that flattened order as
   ground truth with zero awareness that `RenderPassEvent` was ever
   supposed to mean anything.
4. **The bug pattern is structurally simple and 100% reproducible from
   the compiler's own documented algorithm.** `RenderGraphCompiler.cpp`'s
   own header comment (~line 37) explains the RAW-edge rule precisely:
   *"the most recent pass, among those declared so far, that wrote it"*.
   "Declared so far" is doing all the load-bearing work in that sentence —
   a pass whose real writer is declared LATER, in C++ terms, than the
   pass that reads it, can never be linked to that writer at all: the
   writer simply isn't "so far" yet when the reader is processed. The
   read then resolves to "no prior writer" (`writer = -1`, no edge), and
   if the write also can't reach a `finalOutputs` root through any OTHER
   path, Phase 3's backward-reachability scan (`Compile()`, ~lines
   164-236) culls it — invisibly, with no error, no warning, nothing
   printed anywhere.
5. **A permanent, real cost of NOT fixing this (Goal 1's own motivation):**
   any future pass author who trusts `RenderPassEvent`'s name/values to
   mean "this decides order" (a completely reasonable assumption from the
   name alone) will silently reproduce the exact `AtmosphereComposite`
   failure the moment they register two providers whose real data
   dependency runs opposite to their C++ registration order — and the
   compiler gives them zero warning when it happens. `render-pass-3`'s own
   fix only protects the two specific `ProviderTiming` phases it created;
   it does nothing for two passes that both land in the SAME phase (e.g.
   two `BeforeDeferredPasses` immediate providers, or two legacy
   `AddRenderPass()` calls) in the wrong relative order.

## Locked Design Decisions (from direct user Q&A — do not re-litigate these)

Every one of these was asked and answered directly by the user in this
campaign's own kickoff session — do not ask them again.

1. **Both a safety-net detector AND a real ordering fix are in scope for
   this one campaign — as two separate phases, not one combined change.**
   The detector (PHASE1) ships first and changes zero scheduling
   behavior. The real fix (PHASE2) ships second, is explicitly labeled as
   a genuine behavior change, and is validated against the detector
   itself plus the existing regression suite before being considered done.
2. **The detector's reaction, when it fires, is BOTH of the following, at
   once, never just one:** (a) an UNCONDITIONAL `stderr` message (compiled
   into every build configuration, Debug and Release alike — a quick
   manual/Release test session must be able to see it without crashing),
   AND (b) a debug-build `assert()` immediately after — plain
   `assert()` from `<cassert>`, mirroring this codebase's own existing
   convention (`RenderGraphBuilder.cpp`'s `assert(name != nullptr && ...)`
   call sites) rather than inventing a new `GTE_ASSERT`-style macro. Plain
   `assert()` already compiles to nothing under `NDEBUG` — this gives "hard
   stop in Debug/CI, silent-but-loud in Release" for free, with no manual
   `#ifndef NDEBUG` guard needed around the assert call itself (only the
   detection+report logic needs to run unconditionally in both configs;
   the crash-vs-no-crash split is `assert()`'s own, already-standard
   behavior).
3. **`RenderPassEvent` keeps its exact current name.** Do NOT rename it
   (e.g. to `RenderPassSortHint` or similar) anywhere in this campaign.
   Every doc-comment clarification this campaign makes must strengthen the
   EXISTING name's own documentation instead of introducing a new one.
4. **The known, separate `RenderPipeline.h` "immediate provider always
   lands before every deferred provider within its own `ProviderTiming`
   phase" gap gets NO behavior change in this campaign** — it is treated
   exactly like every other source of "the wrong C++ registration order
   relative to RenderPassEvent" that PHASE1's detector already generically
   covers. See PHASE1's own Step 3 for exactly why this generic,
   compiler-level detector transparently covers this gap too, for free,
   with zero new code inside `RenderPipeline.h` itself.
5. **A permanent regression test reproducing the exact historical bug
   pattern is required** (a reader-pass declared before its writer-pass,
   with a `RenderPassEvent` combination that implies the opposite
   execution order) — in `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`
   and/or `RenderPipelineTests.cpp`. See PHASE1 Step 3.4 and PHASE2 Step
   3.6 (PHASE2's Step 3.3 is the edge/Kahn's-tie-break rewire itself, not
   its tests — its own regression tests live in Step 3.6, "Tests").
6. **Fewer phases than the 3-5-phase campaigns before it** — this is
   narrower in scope than `render-pass-1`/`2`/`3`. Three real phases
   total (plus this orchestrator), not five.

## Step 3: The Plan — Phase Index

Each phase below is its own `PHASEn_*.md` file in this same folder. Work
through them in order — later phases assume earlier ones already landed.
Every child phase file follows the same "Goal / Situation / Plan /
Definition of Done / What We Will NOT Do" shape the three prior
`render-pass-*` campaigns already used.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md` | Add a pure, Tier-1-testable `DetectRenderPassEventContradictions()` function to `RenderGraphCompiler`, wire it into `Compile()` with an unconditional `stderr` report + debug `assert()`, strengthen every relevant doc comment, add the permanent regression test. Zero scheduling-behavior change. |
| 2 | `PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md` | The real fix: audit every real pass's current (declaration position, `RenderPassEvent`) pair, then make `Compile()` process passes in a stable-sorted-by-`RenderPassEvent` "effective order" instead of raw declaration order for its RAW/WAW scan and Kahn's-algorithm tie-break — a genuine, clearly-labeled behavior change, validated against PHASE1's own detector and the full existing test suite. |
| 3 | `PHASE3_FINAL_INTEGRATION_FULL_BUILD_AND_LIVE_VERIFICATION.md` | The ONLY phase that runs a full build + `ctest` regression suite + a live, HTTP-driven visual verification of the real Game/Scene View. Campaign completion report, `AGENTS.md` update, explicit write-up of the one genuine behavior change (PHASE2) for future readers. |

## What This Strategy-Writing Pass Does NOT Do

This document and its three siblings are the OUTPUT of a strategy-planning
pass, not an implementation pass. Nothing under `src/`, `tests/`, or any
other engine file has been touched by writing these four `.md` files. Each
`PHASEn_*.md` file is a to-do list for whoever implements that phase next —
that implementer is the one who writes real code, runs a real incremental
compile, and produces that phase's own `PHASEn_COMPLETION_REPORT.md`
inside this same folder (not written yet, by design).

## Cross-Cutting Rules For Every Phase (do not repeat verbatim in each
child, but every implementer must follow these)

- **No full build/regression test until PHASE3.** Every earlier phase does
  an INCREMENTAL compile check only (build just the `gte_core`/
  `GreatTamanaEngineTests` targets that were touched — do not run the full
  test suite or a full clean rebuild). Debugging via `run_app_background`
  + `gte_send_request` (visual/HTTP verification of the specific thing that
  phase changed) is encouraged and does NOT count as "full build/full
  regression test."
- **Every phase ends with**: (a) an incremental compile check (and,
  where useful, a quick live visual/HTTP smoke check of just the thing that
  phase touched), (b) a short Markdown completion report written into this
  SAME folder (`task_manager/render-pass-4/PHASEn_COMPLETION_REPORT.md`),
  (c) a git commit of the code changes + the report together.
- **Stay on branch `feature/render-pass-impl`.** Never switch branches.
  Always read `README.md` and `AGENTS.md` at the repo root before starting
  a phase, and always read the previous phase's own completion report
  first — it may carry a clue, a deviation, or an open question for the
  next phase to pick up.
- **`AGENTS.md`'s general house rules still apply in full**: Clean
  Architecture layering, RAII, everything lives in `namespace gte` (or
  `gte::rg` for Render Graph internals), and the Testability/
  Regression-Safety rules — `DetectRenderPassEventContradictions()`
  (PHASE1) and the effective-order sort key computation (PHASE2) are both
  excellent Tier-1 candidates with zero live `VkDevice` needed; add/update
  a matching test file under `tests/` in the SAME change, never as an
  afterthought.
- **IMPORTANT — delegation discipline.** If, while executing ANY phase
  (this one included), you find yourself needing to hand off further work
  via `delegate_task`, you MUST explicitly instruct that delegated task, in
  its own prompt text, to use the `ask_questions` tool for any genuine
  design ambiguity it hits — and to, in turn, pass that SAME instruction on
  to anything IT further delegates. This rule propagates recursively,
  forever, down every level of delegation this campaign ever produces.
- **If you are genuinely unsure about a design choice this document (or
  your own phase file) does not already pin down, use `ask_questions` to
  ask the user directly rather than guessing.** Every phase file below has
  already resolved every design decision the kickoff Q&A session covered;
  if you hit a NEW ambiguity these docs don't already answer, that is
  exactly when to stop and ask.
- **Never re-litigate a Locked Design Decision above.** In particular: do
  NOT rename `RenderPassEvent`, do NOT fold PHASE1 and PHASE2 into a
  single combined change, and do NOT attempt to "fix" the
  `ProviderTiming` immediate-vs-deferred gap with new `RenderPipeline.h`
  behavior — that gap is intentionally left to PHASE1's generic detector
  only. All were explicitly, directly decided by the user — treat them as
  settled facts, not as open design questions to reconsider.
