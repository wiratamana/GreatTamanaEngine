# PHASE2 — Systemic audit: which OTHER passes leak a side effect past their own toggle?

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-22/`
Previous phase report to read first: `PHASE1_COMPLETION_REPORT.md` — the
`RenderPassToggleGuard.h` helper this phase reuses everywhere, and the exact
shape of the confirmed bug this audit is widening the search for.

## Step 1: The Goal (Where are we going?)

Produce an exhaustive, itemized, evidence-backed ledger — one row per
candidate — of EVERY place in the engine where a `RenderPipeline` provider
(or a plugin/feature adapter using the equivalent pattern) performs an
observable side effect (a `RenderPassBlackboard::Publish()` call, a
cached-callback/handle hand-off some OTHER independently-toggled consumer
reads, a member-variable write another system reads regardless of this
frame's graph contents) that is NOT gated by an early, PHASE1-style guard
BEFORE that side effect happens. Classify each as **Confirmed-Lie**
(the side effect survives the pass's own disabled state and is
consumed/visible somewhere) / **Already-Honest** (either no side effect
exists, or an early guard already exists, or the consumer itself
independently degrades safely with zero visible effect) / **No-Toggle-
Exists** (the "pass" this side effect belongs to has no user-facing
enable/disable surface at all, so there is nothing to be dishonest about).
This ledger becomes PHASE3's fix backlog — PHASE3 must not re-derive it.

## Step 2: The Situation (Where are we now?)

`PHASE0_MASTER_STRATEGY.md`'s Step 2.3, and `PHASE1_COMPLETION_REPORT.md`,
already establish ONE confirmed instance
(`"DrawSkyBackground"` → `kGameSkyBackgroundCallbackKey` →
`AddReplayPasses()`'s own sky step) and the fix pattern
(`ShouldDeclareBuiltInPassThisFrame()`, applied at the very top of the
provider, before any side effect). This phase's job is to find every OTHER
instance of the exact same SHAPE — it is explicitly NOT a re-run of
`editor-core-separation-21`'s own PHASE3 audit (which looked for passes
declared with ZERO toggle consult at all — a different, already-fixed bug
shape). This audit's own subject is narrower and more subtle: a pass that
DOES honestly consult the toggle registry for its OWN `RenderPassDesc`, but
ALSO does something else, earlier, that nothing gates.

### 2.1 — Known candidate list to check first (a starting point, not the full answer — this audit must be exhaustive, not stop here)

A `search_in_dir` for `blackboard.Publish` in `src/Core/Core.cpp` found
exactly 5 call sites at the time PHASE0 was written (line numbers will
shift once PHASE1 lands — re-run the search fresh, do not trust these
numbers):
1. `kAtmosphereSharedLutKey` (`"AtmosphereSharedLut"` provider) — PHASE0's
   own static read suggests this is Already-Honest (the entry is published
   unconditionally, but its own INTERNAL passes are individually toggle-
   gated via `AddAtmosphereSharedLutPasses(..., &m_renderPassToggleRegistry)`,
   and nothing downstream trusts the mere PRESENCE of this blackboard entry
   as "the LUT passes definitely ran" without ALSO checking handle
   validity) — CONFIRM this live, do not accept the static read alone.
2. `kGpuSkinningOutputsKey` (`"GpuSkinning"` provider) — this provider
   already has its OWN early guard (`NoteDeclaredAndCheckEnabled("GpuSkinning")`,
   checked BEFORE the publish) — likely Already-Honest, confirm live.
3. `kAtmosphereViewLutGameKey`/`kAtmosphereViewLutSceneKey` (`"AtmosphereViewLut"`
   provider) — publishes AFTER `AddAtmosphereViewLutPasses()` returns real,
   validity-checked handles — likely Already-Honest, confirm live.
4. `kGameSkyBackgroundCallbackKey` (`"DrawSkyBackground"` provider) —
   FIXED by PHASE1. Re-confirm the fix holds after this phase's own
   `search_in_dir` re-run (line numbers shifted).
5. `kGameCompositedOutputKey`/`kSceneCompositedOutputKey`
   (`"AtmosphereComposite"` provider) — publishes ONLY after an explicit
   `if (!composited.IsValid()) { return; }` check — likely Already-Honest,
   confirm live.

Beyond `Core.cpp`'s own `blackboard.Publish` call sites, this audit must
ALSO check:
- Every OTHER `RenderPassFrameContext::finalTextureOutputs`/
  `finalVolumeTextureOutputs`/`builder` (the `mutable` fields, `RenderPipeline.h`
  ~line 392-393, 364) push/call performed by a provider — does any provider
  push a root output or call `builder.AddRenderPass()`/`ImportTexture()`
  DIRECTLY (bypassing the deferred `RenderPassDesc` mechanism entirely, the
  `ProviderTiming`/"Step 3.3b" pattern `RenderPipeline.h` ~line 344-364
  documents) in a way that is NOT itself preceded by an equivalent early
  toggle guard? (`AtmosphereSharedLut`/`AtmosphereViewLut`/`AtmosphereComposite`
  reach `frame.builder` directly this way — each needs its OWN confirmation,
  not an assumption that "it's Atmosphere, so it's fine", since PHASE0's
  own Step 2.1 in `editor-core-separation-21` already proved one Atmosphere
  pass WAS buggy once before.)
- `src/Core/Plugins/LegacyRenderFeatureOrchestrator.cpp`'s
  `ContributeRenderGraphPasses()` (reached from `"PluginRenderFeatures"`,
  `Core.cpp` ~line 1000-1006) — does it publish/mutate anything an
  UNRELATED, independently-toggled consumer might read regardless of a
  given plugin feature's own enabled state?
- Every member-variable write performed inside a provider lambda BEFORE its
  own toggle check would fire (e.g. `m_gpuDrivenBatchesThisFrame`,
  `m_currentViewDataThisFrame`, `m_gpuDrivenBatchedEntitiesThisFrame`) —
  is any of these READ by code that has no toggle awareness of its own
  (e.g. a stats/profiler readout, a different panel)? A stale/leftover
  value read by a debug-only readout with no user-facing "is this pass
  really running" implication is Already-Honest by this campaign's own
  Iron Rule (which is about RENDER PASS EXECUTION / FRAME DEBUGGER VISUAL
  fidelity specifically, not every last piece of internal bookkeeping) —
  use judgment, and `ask_questions` if a specific case feels genuinely
  borderline.
- `FrameDebuggerCaptureContext`'s OWN state (`src/Editor/FrameDebuggerCapture.h`/`.cpp`)
  — does it cache anything ACROSS frames that could go stale relative to a
  toggle flipped mid-session (e.g. `SetReplayStepPreviews()`,
  `FrameDebuggerReplayPasses.cpp` line 214)? This is the SAME general
  class of risk PHASE1 fixed for `kGameSkyBackgroundCallbackKey` — confirm
  no sibling cache has the identical staleness risk.

### 2.2 — Ledger format (use this exact shape, one row per finding)

For every candidate: **Side-effect location** (file:line) / **What it
publishes/mutates** / **Who consumes it, and how** (file:line) / **Does the
consumer check the SAME toggle the producer's own name would imply?** /
**Classification** (Confirmed-Lie/Already-Honest/No-Toggle-Exists) /
**Live verification performed** (the exact HTTP calls/log lines that prove
the classification, not just a code-reading argument — mirrors
`editor-core-separation-21`'s own PHASE3 evidentiary bar).

## Step 3: The Plan (detailed strategy)

1. Re-run `search_in_dir` for `blackboard.Publish(`, `.Fetch<`,
   `frame.builder.`, and `frame.finalTextureOutputs.push_back`/
   `finalVolumeTextureOutputs.push_back` across `src/Core/Core.cpp`,
   `src/Core/Plugins/`, and `src/Application/` — build the full candidate
   list fresh (do not trust PHASE0's own line numbers, which will have
   shifted after PHASE1's edit).
2. For each candidate, trace forward from the publish/mutation site to
   every consumer, and trace backward from the producer's own provider
   entry to confirm exactly what toggle name (if any) is supposed to gate
   it.
3. For each candidate classified as an OPEN QUESTION rather than a clean
   Confirmed-Lie/Already-Honest (i.e. genuinely ambiguous — e.g. "is this
   READ by anything a user would call 'the Frame Debugger lying to me'"),
   use `ask_questions` with the SPECIFIC scenario spelled out, exactly as
   `editor-core-separation-21`'s own PHASE3 did (3 questions, all resolved
   toward the maximal, most-honest fix scope — expect a similar
   preference here unless the user says otherwise).
4. Live-verify EVERY classification, not just the ones that look
   suspicious on paper — an "Already-Honest" verdict reached by code
   reading alone is not acceptable evidence for this ledger; a quick
   `GET /render_graph/set_pass_enabled` + `GET /get_game_view`/`/get_logs`
   round-trip per candidate is cheap and is exactly what
   `editor-core-separation-21`'s own PHASE3 did for its 25-row ledger.
5. Write the finished ledger directly into this phase's own completion
   report (not a separate file — PHASE0's own Locked Decision #9/Note 2
   equivalent: keep the total file count fixed).

### 3.1 — What this phase explicitly does NOT do

No code fix lands in this phase except whatever MINIMAL, temporary
diagnostic logging (via `GTE_LOG_DEBUG`, never `printf`) is needed to
confirm a genuinely ambiguous live-verification case — remove any such
temporary logging before this phase ends unless it has permanent
diagnostic value (mirrors `editor-core-separation-21`'s own PHASE1/PHASE2
discipline). All REAL fixes land in PHASE3.

### 3.2 — End of phase

1. Incremental build still succeeds (only if any temporary instrumentation
   was added and needs compiling/removing — otherwise this phase may have
   zero net code diff, which is fine: the ledger itself, committed as part
   of the completion report, IS this phase's deliverable).
2. Write `PHASE2_COMPLETION_REPORT.md` with the full ledger, every
   `ask_questions` interaction and its resolution, and the exact live-
   verification evidence per row.
3. `git_add` + `git_commit` covering the report (and any temporary
   instrumentation that was kept/removed).
