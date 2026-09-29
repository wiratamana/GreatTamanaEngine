# PHASE6 COMPLETION REPORT — Iron Rule v2: bidirectional detector (Clause B + Clause C)

Campaign folder: `task_manager/editor-core-separation-22/`
Branch: `feature/editor-core-separation` (unchanged, as required)
Status: **Complete.** Two new, permanent, self-enforcing detectors now exist
alongside `editor-core-separation-21`'s own **untouched, still-passing**
`RenderPassHonestyChecker`/`RenderPassHonestyGuard` (Clause A): Clause B
("ran but not shown") and Clause C ("disabled side effect still visible").
Both were proven, live, by deliberately reintroducing the exact original bug
each one targets, confirming a fresh `GTE_LOG_ERROR` fires, then fully
reverting (confirmed via `git status` showing zero diff on the reverted
files).

## Pre-checks (required reading, done first)

1. `PHASE0_MASTER_STRATEGY.md` re-read in full — Step 1.2's Clause A/B/C
   wording and Step 2.3's "why the existing detector can't see this"
   argument are this phase's own starting point, quoted verbatim in the new
   header comments below.
2. `PHASE5_COMPLETION_REPORT.md` re-read in full — confirmed PHASE5 only
   touched `RenderGraphMetadata.h/.cpp`/`RenderGraphPanel.cpp`/
   `RenderGraphMetadataTests.cpp` (the "Render Graph" panel's own duplicate-
   row unification) and never touched `FrameDebuggerData.h/.cpp`,
   `RenderPassHonestyChecker.h/.cpp`, or `RenderPassHonestyGuard.h/.cpp` —
   this phase's own starting assumptions about those files' current shape
   were confirmed accurate by direct re-reading before writing any code.
3. Re-read `RenderPassHonestyChecker.h/.cpp` and `RenderPassHonestyGuard.h/.cpp`
   (`editor-core-separation-21`, PHASE5) in full — both mirrored exactly by
   this phase's own two new detector pairs, and both left **completely
   unmodified** by this phase (confirmed by `git status` never listing
   either file).

## Clause B detector — "ran but not shown"

### Design

`src/Editor/FrameDebuggerCoverageChecker.h`/`.cpp` (new), mirroring
`RenderPassHonestyChecker.h`'s exact file-organization pattern (a
dependency-light, ImGui-free, ordinary-data-only pure function):

- `CollectPassNamesPresentInFrameDebuggerTree(const FrameDebuggerSnapshot&)` —
  recursively walks every node in `tree.rootNodes` (and every descendant, at
  any depth) and collects `details->passName` for every node with
  `isDrawCall == true && details.has_value()`. Confirmed, by direct reading
  of `FrameDebuggerData.cpp`'s `BuildComputeDispatchLeaf()`/
  `BuildGraphicsPassLeaf()`/`BuildRenderOpaqueLeaf()`, that every real
  pass-level node this builder ever produces already sets
  `details->passName` to the pass's own real, raw `RenderGraphPassSnapshot::name`
  — so the phase file's own stated risk ("if the real underlying pass name
  is not currently retained anywhere reachable from a built node... fix
  this as part of this phase") turned out to be a **non-issue**: the tree
  already retains exactly the information needed, with zero code change
  required to `FrameDebuggerData.cpp` itself. A node that ALSO owns a child
  event row (`WrapPassWithOwnedChildEvent()`'s own real shape — the parent
  keeps its own `isDrawCall`/`details` unchanged even once it gains a
  child) is still found correctly, since the collection walk never special-
  cases "has children" vs. "is a leaf" — it only checks `isDrawCall`/`details`.
- `DetectPassesMissingFromFrameDebuggerTree(passes, tree)` — for each pass
  in `passes` that is (a) non-culled, (b) NOT
  `RenderPassCategory::FrameDebuggerInternal`, (c) NOT `ViewScope::SceneView`,
  reports its name if it is absent from the set above. Exactly matches the
  phase file's own Step 3.1 wording, verbatim.

`src/Editor/FrameDebuggerCoverageGuard.h`/`.cpp` (new) — the Logger-aware
singleton wrapper, an exact structural twin of `RenderPassHonestyGuard`
(same "log once per NEW incident, erase when cleared" rule), logging
`GTE_LOG_ERROR("FrameDebuggerCoverage", ...)`.

Wired into `FrameDebuggerPanel::TriggerCapture()` (the same chokepoint
`RenderPassHonestyGuard` already uses), immediately after
`BuildRealFrameDebuggerSnapshot()` produces `snapshot` — always runs (no
nullability concern, unlike Clause A/C, since both its inputs —
`graphSnapshot`/`snapshot` — are already unconditionally available at that
point in `TriggerCapture()`).

### Tests (`tests/Editor/FrameDebuggerCoverageCheckerTests.cpp`, 9 cases)

Every case the phase file's own Step 3.1 asked for, plus 2 extra:
`EveryNonCulledSurvivorPresentInTreeProducesEmptyResult` (a),
`OneSurvivorMissingFromTreeIsReported` (b),
`CulledPassOmittedFromTreeIsNeverReported` (c),
`SceneViewPassOmittedFromTreeIsNeverReported` (d),
`FrameDebuggerInternalCategoryPassOmittedFromTreeIsNeverReported` (e),
`NestedGroupsAtAnyDepthAreStillWalkedCorrectly` (a pass-level node that also
owns a child event row is still found), `MultipleMissingSurvivorsAreAllReported`,
`EmptyTreeAndEmptyPassesProducesEmptyResult`.

### Live, deliberately-reintroduced-then-reverted proof (Step 3.2)

1. Temporarily changed the ONE line in `FrameDebuggerData.cpp`'s "Other
   Render Passes" sweep (the actual gate deciding `DemoRenderFeaturePlugin_Clear`'s
   fate — confirmed by tracing: this pass is `RenderPassEvent::AfterEverything`,
   Graphics-kind, positioned after every surviving compute pass, so it is
   never touched by the view-region walk or the post-GameView compute loop
   — only the final sweep's own `pass.category == rg::RenderPassCategory::FrameDebuggerInternal`
   check governs it) from `FrameDebuggerInternal` back to `Debug` — the
   exact old, buggy check `editor-core-separation-22`'s own PHASE4 replaced,
   and `DemoRenderFeaturePlugin_Clear`'s own category tag is still, today,
   `RenderPassCategory::Debug` (PHASE4 corrected the CATEGORY'S MEANING, not
   this pass's own tag — see `PHASE0_MASTER_STRATEGY.md`'s Step 2.2).
2. Incremental build succeeded. Launched a real `GreatTamanaEditor.exe`
   (PID 15116) via `run_app_background`.
3. `GET /frame_debugger/open` → `GET /frame_debugger/enable?value=true` →
   `GET /frame_debugger/capture` (`totalEventCount` dropped 79 → 73,
   consistent with both Demo plugin passes' own parent+child event pairs
   vanishing).
4. `GET /get_logs?category=FrameDebuggerCoverage` → **2 fresh
   `GTE_LOG_ERROR` entries**, naming `DemoRenderFeaturePlugin_Clear` and
   `DemoRenderFeatureSecondPlugin_Clear` exactly, both reading: *"genuinely
   executed this frame (non-culled, non-FrameDebuggerInternal, non-SceneView)
   but has NO corresponding leaf anywhere in the captured Frame Debugger
   event tree... (Clause B violation)."* — this is the exact, reported Root
   Cause #2 bug, reproduced live, on demand, by this permanent detector.
5. Stopped the app, reverted the one line back to `FrameDebuggerInternal`,
   rebuilt (succeeded), and confirmed via `git status` that
   `src/Editor/FrameDebuggerData.cpp` is **not listed as modified at all** —
   a byte-for-byte-identical revert, zero net diff.
6. Live baseline (BEFORE step 1, and again AFTER step 5's revert +
   rebuild): `GET /get_logs?category=FrameDebuggerCoverage` /
   `?category=FrameDebuggerSideChannel` / `?category=RenderPassHonesty` /
   `?min_level=Error` all stayed `{"count":0}` across a real
   open+enable+capture cycle — confirms zero false positives from either
   new detector against the real, unmodified engine.

## Clause C detector — "disabled side effect still visible"

### Design

`src/Editor/FrameDebuggerSideChannelChecker.h`/`.cpp` (new) — per the phase
file's own Step 3.3, a deliberately **narrow, curated-allowlist** detector,
never a fully general one (see "Honest scoping statement" below):

- `KnownRiskBlackboardKeyRule{ rg::RenderPassId key; std::string keyDebugName;
  std::string gatingToggleName; }` and `KnownRiskBlackboardKeyRules()` — a
  hand-maintained `static const std::vector`, **4 entries**, one per
  blackboard key `PHASE2_COMPLETION_REPORT.md`'s own audit ledger
  identified as carrying this exact risk shape:
  1. `"Atmosphere.GameSkyBackgroundCallback"` gated by `"DrawSkyBackground"`
     — the **CONFIRMED** Root Cause #3 bug this campaign's own PHASE1 fixed.
  2. `"GpuSkinning.OutputBuffers"` gated by `"GpuSkinning"` — PHASE2 ledger
     finding #6/#9, already honest, kept as a permanent regression tripwire
     for the same risk shape.
  3/4. `"Atmosphere.CompositedOutput.Game"`/`"...Scene"` gated by
     `"AtmosphereComposite"` — PHASE2 ledger finding #10, same reasoning.
  Every literal string is a byte-for-byte copy of the matching `Core.cpp`
  constant (never a shared header-defined constant) — deliberately mirrors
  `FrameDebuggerData.cpp`'s own `kFrameDebuggerGameClearColor` precedent
  ("duplicate a hardcoded engine constant with a comment documenting the
  value it must be kept in sync with" — an already-accepted pattern in this
  codebase). `rg::RenderPassId`'s own hash is a pure, deterministic
  compile-time function of the literal's bytes (`operator""_passId`), so
  two independently-written, byte-identical literals always produce the
  exact same `RenderPassId` — confirmed directly by the live proof below
  (the checker's own copy of the literal correctly matched the real
  blackboard key `Core.cpp` actually published under).
- `DetectDisabledPassBlackboardKeyLeaks(rules, isEnabledLookup, wasPublishedLookup)` —
  mirrors `RenderPassHonestyChecker.h`'s own dependency-injection shape
  exactly: for each rule, if `isEnabledLookup(rule.gatingToggleName)` is
  false AND `wasPublishedLookup(rule.key)` is true, reports
  `rule.keyDebugName`. Pure, Tier-1-testable with zero live
  `RenderPassToggleRegistry`/`RenderPassBlackboard` object.

`src/Editor/FrameDebuggerSideChannelGuard.h`/`.cpp` (new) — the Logger-aware
singleton wrapper, another exact structural twin of `RenderPassHonestyGuard`,
logging `GTE_LOG_ERROR("FrameDebuggerSideChannel", ...)`.

### The real, load-bearing plumbing gap this phase had to close

The phase file's own Step 3.3 item 2 anticipated this exactly: *"this
requires `RenderPassBlackboard` to expose a way to check 'was key X
published this frame'... add a small, additive `bool WasPublishedThisFrame(RenderPassId)
const` accessor if one does not already exist."* One did not exist, and
**neither did any way to reach a live `RenderPassBlackboard` object from the
Editor at all** — `Core::BuildFrame()`'s own offscreen-regime blackboard was
a plain **stack-local variable**, scoped to (and destroyed at the end of)
the `RenderGraph::Execute()` builder callback, long before
`ImGuiEditorLayer::BuildUI()` (and, through it,
`FrameDebuggerPanel::TriggerCapture()`) ever runs later the same frame. This
is a second, genuinely real gap this phase had to close, not just the one
accessor method the phase file called out by name:

1. **`src/Renderer/RenderGraph/RenderPipeline.h`** —
   `RenderPassBlackboard::WasPublishedThisFrame(RenderPassId) const noexcept`
   added, right after `Fetch<T>()`: a linear scan by key (mirrors `Fetch<T>()`'s
   own scan), returning presence only, never touching the debug-only
   `wasFetched` bookkeeping (a presence PROBE, not a real fetch — must never
   perturb `ReportUnusedPublishesIfAny()`'s own "was this ever legitimately
   fetched" logic).
2. **`src/Core/Core.h`/`Core.cpp`** — the offscreen regime's own
   `rg::RenderPassBlackboard blackboard;` local (`Core::BuildFrame()`'s
   Execute() callback) **promoted to a real Core member**,
   `m_offscreenBlackboardThisFrame`, so it survives past the callback's own
   return. `.BeginFrame()` is still called at the exact same call site,
   every frame — this promotion changes **only** the object's own lifetime,
   never when/how often its contents are cleared and rebuilt (confirmed:
   every existing `frame.blackboard.Publish()`/`.Fetch()` call site, and the
   two bare `blackboard.Fetch()`/`.ReportUnusedPublishesIfAny()` calls
   inside the later replay-path code, all continued to compile and behave
   unmodified once retargeted at the member instead of the local). New
   `const rg::RenderPassBlackboard& GetOffscreenBlackboardForFrameDebugger() const noexcept`
   accessor, mirroring `GetGpuDrivenBatchDebugInfo()`'s own "populated fresh
   every frame, safe to read afterward" precedent exactly.
3. **`src/Editor/EditorLayer.h`** — `IEditorLayer::BuildUI()` gained one
   new, trailing, NEVER-null `const rg::RenderPassBlackboard& offscreenBlackboard`
   parameter (mirrors `renderPassToggleRegistry`'s own "never null, Core
   owns exactly one instance" convention).
4. **`src/Editor/NullEditorLayer.cpp`** — matching no-op override.
5. **`src/Editor/ImGuiEditorLayer.cpp`** — matching override, forwarding
   `&offscreenBlackboard` (as `const rg::RenderPassBlackboard*`) into
   `m_frameDebuggerPanel.Build(...)`'s own new trailing parameter.
6. **`src/Editor/EditorHost.cpp`** — the one real `m_editorLayer->BuildUI(...)`
   call site, now passing `m_core.GetOffscreenBlackboardForFrameDebugger()`.
7. **`src/Editor/Panels/FrameDebuggerPanel.h`/`.cpp`** — `Build()` gained a
   new, nullable, trailing `const rg::RenderPassBlackboard* offscreenBlackboard = nullptr`
   parameter (mirrors `toggleRegistry`'s own exact nullability convention),
   cached into a new `m_frameBlackboard` member the same way
   `m_frameToggleRegistry` already is.

`TriggerCapture()` now runs Clause C right after Clause A, gated (like
Clause A) on both `m_frameToggleRegistry != nullptr` and the new
`m_frameBlackboard != nullptr`.

### Tests (`tests/Editor/FrameDebuggerSideChannelCheckerTests.cpp`, 6 cases)

`EnabledToggleNeverReportsEvenIfPublished`, `DisabledToggleWithKeyPublishedIsReported`,
`DisabledToggleWithKeyNotPublishedIsNotReported`,
`MultipleRulesOnlyGenuinelyLeakingOnesAreReported`, `EmptyCallablesReturnEmptyDefensively`,
and `RealKnownRiskBlackboardKeyRulesContainsEveryDocumentedEntry` (a sanity
check on the real, curated list itself — catches a future accidental
deletion of an entry).

### Live, deliberately-reintroduced-then-reverted proof (equivalent to Step 3.2, for Clause C)

1. Temporarily commented out the single `return;` inside `Core.cpp`'s
   `"DrawSkyBackground"` provider's own early guard (PHASE1's own fix,
   `if (!rg::ShouldDeclareBuiltInPassThisFrame(...)) { return; }` →
   `{ /* return; commented out */ }`) — reintroducing PHASE1's own original,
   confirmed bug: the callback publish becomes unconditional again,
   regardless of the toggle's own state.
2. Incremental build succeeded. Launched a real `GreatTamanaEditor.exe`
   (PID 18420).
3. `GET /render_graph/set_pass_enabled?name=DrawSkyBackground&enabled=false` →
   `{"success":true}`. `GET /frame_debugger/open` → `.../enable?value=true` →
   `.../capture` (`totalEventCount: 77`, `hasCapturedFrame: true`).
4. `GET /get_logs?category=FrameDebuggerSideChannel` → **exactly 1 fresh
   `GTE_LOG_ERROR`**: *"RenderPassBlackboard key
   'Atmosphere.GameSkyBackgroundCallback' was published this frame even
   though its own gating pass is DISABLED... (Clause C violation)."* — note
   this fired on the very next capture with **no Frame Debugger replay/step
   choreography needed at all**: Clause C compares "was published" directly
   against "is the gating toggle disabled", so the mere unconditional
   Publish() call (regardless of whether any consumer — e.g.
   `FrameDebuggerReplayPasses.cpp`'s own sky step — ever actually reads it
   back this particular capture) is already the observable leak this
   detector targets.
5. `GET /get_logs?category=RenderPassHonesty` / `?category=FrameDebuggerCoverage`
   both stayed `{"count":0}` — confirms Clause C's own reintroduced
   regression did not perturb either sibling detector.
   `GET /get_logs?min_level=Error` showed **exactly the 1 entry** above —
   zero crash, zero unrelated error.
6. Re-enabled `"DrawSkyBackground"`, stopped the app, reverted the one-line
   comment-out back to a real `return;`, rebuilt (succeeded), and confirmed
   via `search_in_dir`/re-reading that `Core.cpp`'s
   `"DrawSkyBackground"` provider's own guard is byte-identical to before
   this experiment (only this phase's OWN intentional blackboard-promotion
   edits remain in the file's real diff).

### Honest scoping statement (Step 3.3, required)

This is a **curated allowlist-based check, not a fully general one** — it
covers exactly the 4 blackboard keys `PHASE2_COMPLETION_REPORT.md`'s own
audit identified as carrying this risk shape (1 Confirmed-Lie fixed by
PHASE1, 3 Already-Honest-but-same-shape kept as tripwires), and nothing
else. It cannot, and does not try to, catch:

- A **future new** blackboard key with this same risk shape that nobody
  manually adds to `KnownRiskBlackboardKeyRules()` — this is a real,
  permanent gap, by design (see Step 3.3's own explicit acceptance of this
  trade-off, and `PHASE0_MASTER_STRATEGY.md`'s own reasoning: "a much bigger
  undertaking than this campaign's own remaining budget justifies").
- A side-channel leak that is NOT shaped like "an opaque
  `RenderPassBlackboard` key gated by exactly one named toggle" — e.g. a
  leak through a plain `Core`-owned member variable never routed through
  the blackboard at all (this campaign's PHASE2 finding #19, the
  `GpuDrivenBatches` entity-exclusion bug PHASE3 fixed, is exactly this
  OTHER shape — Clause C's detector, as built, cannot see it, and was never
  asked to; that bug's own regression protection is its own dedicated
  `GpuDrivenBatchEntityExclusionLogicTests.cpp`, written by PHASE3).
- A legitimate, honest cross-provider data hand-off being misclassified as
  a leak (e.g. `"Atmosphere.SharedLuts"`) — deliberately **excluded** from
  the curated list, since PHASE2's own ledger classified it
  **No-Toggle-Exists** (no single umbrella toggle name gates it; its real
  consumers check per-field HANDLE VALIDITY, not blackboard presence) —
  including it in this list would have produced exactly the false-positive
  risk Step 3.3's own intro warns against.

Any future new blackboard key with this same risk shape (a provider that
Publishes a value some OTHER, independently-toggled pass reads back to
reproduce a visual/behavioral effect) **must be manually added to
`KnownRiskBlackboardKeyRules()`** by whoever adds it — `AGENTS.md` will be
updated to state this explicitly in PHASE7, per this phase's own
instruction.

## Existing Clause A detector — confirmed unweakened

`src/Editor/RenderPassHonestyChecker.h`/`.cpp` and
`RenderPassHonestyGuard.h`/`.cpp` are **byte-for-byte unmodified** by this
phase (neither file appears in `git status`'s modified list). Its own 6
pre-existing tests (`tests/Editor/RenderPassHonestyCheckerTests.cpp`) all
still pass, re-run alongside every new test this phase adds (20/20, see
below) — this phase adds new checks strictly ALONGSIDE Clause A, never
touching, narrowing, or removing it, per `PHASE0_MASTER_STRATEGY.md`'s
Locked Decision #7.

## Incremental build + targeted tests

```
cmake --build build                                                                                       -> succeeded (every rebuild this phase, including both temporary-regression/revert cycles)
ctest -C Debug --output-on-failure -R "RenderPassHonestyChecker|FrameDebuggerCoverageChecker|FrameDebuggerSideChannelChecker"
  -> 20/20 passed (6 pre-existing RenderPassHonestyCheckerTest + 8 new FrameDebuggerCoverageCheckerTest + 6 new FrameDebuggerSideChannelCheckerTest)
ctest -C Debug --output-on-failure -R "RenderPipelineTest|CoreHeadlessConstruction"
  -> 14/14 passed, 1 pre-existing environment-dependent skip (RenderPipeline.h's own new WasPublishedThisFrame() causes zero regression)
ctest -C Debug --output-on-failure -R "FrameDebugger|RenderGraph|RenderPass|Core"
  -> 490/490 passed, 1 pre-existing environment-dependent skip (wider net cast deliberately, mirroring PHASE5's own precedent, to catch
     any collateral regression from the Core.cpp/EditorLayer.h/RenderPipeline.h changes - still not the full suite; PHASE7 owns that)
```

Never a full clean build, never a full `ctest` regression pass (reserved
for PHASE7), per Locked Decision #2 / this phase's own "End of phase" rule.

## Live-testing session summary

Two separate `run_app_background` sessions (PID 15116 for the Clause B
proof, PID 18420 for the Clause C proof), each driven entirely via
`gte_send_request`, each cleanly stopped via `stop_app_background` before
moving to the next step. No stray instance left running at the end.

## No delegation, no unresolved ambiguity

No `delegate_task` call was made (not permitted for this phase, per PHASE0
Locked Decision #6). No `ask_questions` call was needed — every design
decision this phase faced was already either locked by `PHASE0_MASTER_STRATEGY.md`
(Clause A must survive unweakened; Clause C must be scoped narrowly and
documented as such) or resolved directly by re-reading real, current source
(confirming `FrameDebuggerEventDetails::passName` already carries the real
pass identity; confirming exactly which one line in `FrameDebuggerData.cpp`
actually gates `DemoRenderFeaturePlugin_Clear`'s visibility) — no genuine
design ambiguity requiring a user/orchestrator decision was discovered.

## Files changed

- `src/Renderer/RenderGraph/RenderPipeline.h` (new
  `RenderPassBlackboard::WasPublishedThisFrame()`)
- `src/Core/Core.h` (new `m_offscreenBlackboardThisFrame` member, new
  `GetOffscreenBlackboardForFrameDebugger()` accessor)
- `src/Core/Core.cpp` (the offscreen `blackboard` local promoted to the new
  member; every call site retargeted)
- `src/Editor/EditorLayer.h` (`RenderPassBlackboard` forward declare;
  `BuildUI()` new trailing parameter)
- `src/Editor/NullEditorLayer.cpp` (matching no-op override)
- `src/Editor/ImGuiEditorLayer.cpp` (matching override; new trailing
  argument to `FrameDebuggerPanel::Build()`)
- `src/Editor/EditorHost.cpp` (new trailing argument to `BuildUI()`)
- `src/Editor/Panels/FrameDebuggerPanel.h` (`RenderPassBlackboard` forward
  declare; `Build()` new trailing parameter; new `m_frameBlackboard` member)
- `src/Editor/Panels/FrameDebuggerPanel.cpp` (new includes; `Build()`
  stores the new parameter; `TriggerCapture()` runs both new detectors)
- `src/Editor/FrameDebuggerCoverageChecker.h`/`.cpp` (new — Clause B pure
  detector)
- `src/Editor/FrameDebuggerCoverageGuard.h`/`.cpp` (new — Clause B
  Logger-aware wrapper)
- `src/Editor/FrameDebuggerSideChannelChecker.h`/`.cpp` (new — Clause C pure
  detector + curated allowlist)
- `src/Editor/FrameDebuggerSideChannelGuard.h`/`.cpp` (new — Clause C
  Logger-aware wrapper)
- `tests/Editor/FrameDebuggerCoverageCheckerTests.cpp` (new, 9 tests)
- `tests/Editor/FrameDebuggerSideChannelCheckerTests.cpp` (new, 6 tests)
- `CMakeLists.txt` (new source registrations, `gte_editor` target)
- `tests/CMakeLists.txt` (new test source registrations)
- `task_manager/editor-core-separation-22/PHASE6_COMPLETION_REPORT.md`
  (this file)

`src/Editor/FrameDebuggerData.cpp` and `src/Core/Core.cpp`'s
`"DrawSkyBackground"` guard were each TEMPORARILY modified for their own
live-proof, then fully reverted — confirmed via `git status` to carry zero
net diff from those two experiments (Core.cpp's own real, permanent, net
diff is ONLY the blackboard-promotion change described above).

## End of phase checklist (Step 3.4)

1. ✅ Incremental build (`cmake --build build`) succeeded, every time,
   including immediately after each temporary-regression/revert cycle.
2. ✅ Every new Tier-1 test passes (20/20 targeted filter), alongside a
   fresh, targeted re-run of `editor-core-separation-21`'s own existing
   `RenderPassHonestyCheckerTests.cpp` confirming zero regression there — a
   wider net (490/490, 1 pre-existing environment skip) was also cast,
   mirroring PHASE5's own precedent, with zero regression found anywhere.
3. ✅ This completion report written — both new detectors' own design, the
   live, deliberately-reintroduced-then-reverted proof for BOTH Clause B and
   Clause C, and the honest scoping statement for Clause C.
4. Next: `git_add` + `git_commit` covering every file changed and this
   report.
