# PHASE6 — Iron Rule v2: a permanent detector for "ran but not shown" AND "disabled side effect still visible"

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first — Step 1.2's
Clause A/B/C and Step 2.3's "why the existing detector can't see this"
argument are this phase's own starting point).
Campaign folder: `task_manager/editor-core-separation-22/`
Previous phase report to read first: `PHASE5_COMPLETION_REPORT.md`.

## Step 1: The Goal (Where are we going?)

Two NEW, permanent, automatic, always-on (debug build), self-enforcing
detectors must exist alongside `editor-core-separation-21`'s own
UNTOUCHED, still-passing `RenderPassHonestyChecker`/`RenderPassHonestyGuard`
(Clause A: "disabled pass still shown"):

1. **Clause B detector** ("ran but not shown"): fires the instant a
   non-culled, non-`FrameDebuggerInternal`-category, non-`ViewScope::SceneView`
   pass in a captured `RenderGraphSnapshot` has NO corresponding leaf
   anywhere in the ACTUAL, built `FrameDebuggerSnapshot` tree that same
   capture produced. This is the exact, permanent regression guard for
   Root Cause #2 (Step 2.2) — if a future change reintroduces a silent
   category-based (or any other) exclusion that drops a real survivor from
   the tree, this must fire immediately, loudly, via `GTE_LOG_ERROR`.
2. **Clause C detector** ("disabled side effect still visible") — a
   narrower, best-effort structural safeguard (a fully general version of
   this is likely infeasible — see Step 3.3's honest scoping discussion)
   that at minimum re-confirms, every capture, that every
   `RenderPassBlackboard` key this campaign's own PHASE1-3 identified as
   previously-leaky stays correctly empty/absent whenever its owning pass
   is disabled — a regression tripwire for the EXACT bugs this campaign
   fixed, not a claim of fully general coverage.

## Step 2: The Situation (Where are we now?)

`src/Editor/RenderPassHonestyChecker.h`/`.cpp` (`editor-core-separation-21`,
PHASE5) already implements Clause A: given every pass in a
`RenderGraphSnapshot::passesInExecutionOrder` and an `isEnabledLookup`
callable, it reports every NON-CULLED pass name whose OWN toggle reports
`enabled == false` — a real contradiction. `src/Editor/RenderPassHonestyGuard.h`
wires this into `FrameDebuggerPanel::TriggerCapture()` with a real
`GTE_LOG_ERROR("RenderPassHonesty", ...)`.

This detector CANNOT see Clause B (it never looks at the built
`FrameDebuggerSnapshot` tree at all, only the raw `RenderGraphSnapshot`) or
Clause C (it only ever compares a pass's OWN name against its OWN toggle
state — it has no concept of "a DIFFERENT pass's execution reproduced this
one's disabled effect"). Both gaps are architecturally real, not
implementation sloppiness — building a fully general Clause C detector
would require tracking, per blackboard key, WHICH pass published it and
WHETHER that pass's own toggle was honored before every single publish —
a much bigger undertaking than this campaign's own remaining budget
justifies for a class of bug that, once PHASE1-3 fix every KNOWN instance,
should not have any further instances to guard against except by direct
code review of future new providers. Scope Clause C's detector narrowly
and honestly — see Step 3.3.

## Step 3: The Plan (detailed strategy)

### 3.1 — Clause B: `DetectPassesMissingFromFrameDebuggerTree()`

Add a new pure function, mirroring `RenderPassHonestyChecker.h`'s own shape
exactly (same file-organization pattern: a dependency-light, ImGui-free,
ordinary-data-only pure function plus a thin Logger-aware guard on top),
e.g. `src/Editor/FrameDebuggerCoverageChecker.h`/`.cpp`:

```cpp
// Given every pass in one captured rg::RenderGraphSnapshot::passesInExecutionOrder
// and the SAME capture's own already-built FrameDebuggerSnapshot tree,
// returns every pass name that is (a) non-culled, (b) NOT
// RenderPassCategory::FrameDebuggerInternal, (c) NOT ViewScope::SceneView
// (mirroring BuildRealFrameDebuggerSnapshot()'s own documented, honest
// SceneView exclusion - not a contradiction, an intentional design choice),
// yet has NO corresponding leaf anywhere in the tree. A pass satisfying all
// of (a)/(b)/(c) with no leaf is a Clause B contradiction: it genuinely ran
// this frame, yet the Frame Debugger shows nothing for it.
std::vector<std::string> DetectPassesMissingFromFrameDebuggerTree(
    const std::vector<rg::RenderGraphPassSnapshot>& passes,
    const FrameDebuggerSnapshot& tree);
```

Implementation approach: walk the WHOLE `tree` recursively (root and every
descendant, at any depth), collecting every leaf's own real, underlying
pass-name identity into a `std::unordered_set<std::string>` (this requires
`FrameDebuggerEventNode`/its `details` payload to actually retain a
recoverable pass-name string for every REAL pass-backed leaf it wraps —
confirm this is already true by reading `FrameDebuggerEventNode`'s own
struct definition in `FrameDebuggerData.h`; if the real underlying pass
name is not currently retained anywhere reachable from a built node, this
is itself worth fixing as part of this phase, since a tree that cannot even
answer "which real pass produced this leaf" is itself a smaller instance of
the same "the tree hides information" problem this whole campaign is about
— do not skip this if it turns out to be missing). Then, for each snapshot
pass matching (a)/(b)/(c), report it if its own name is not in that set.

Wire this into the SAME chokepoint `RenderPassHonestyGuard` already uses
(`FrameDebuggerPanel::TriggerCapture()`, the exact spot PHASE5 of
`editor-core-separation-21` used) — add a SECOND, similarly-named guard
(e.g. `FrameDebuggerCoverageGuard`, mirroring `RenderPassHonestyGuard`'s own
log-once-per-new-incident shape) firing its own distinct
`GTE_LOG_ERROR("FrameDebuggerCoverage", ...)` category string, so the two
detectors' own log output is independently greppable via
`GET /get_logs?category=<X>`.

Tier-1 tests (`tests/Editor/FrameDebuggerCoverageCheckerTests.cpp`): (a) a
synthetic snapshot + a synthetic tree that DOES contain every survivor ->
empty result; (b) a synthetic snapshot with one survivor deliberately
omitted from the tree -> that one name reported; (c) a culled pass omitted
from the tree -> NOT reported (an honest, unrelated reason); (d) a
`ViewScope::SceneView` pass omitted -> NOT reported; (e) a
`FrameDebuggerInternal`-category pass omitted -> NOT reported.

### 3.2 — Prove the detector fires on a REAL, deliberately-reintroduced regression, then fully revert

Mirror `editor-core-separation-21`'s own PHASE5 proof exactly: temporarily
re-introduce the OLD, buggy exclusion (e.g. temporarily change
`FrameDebuggerData.cpp`'s check back to `== rg::RenderPassCategory::Debug`
for a moment), confirm a real, fresh `GTE_LOG_ERROR` fires against a live
`DemoRenderFeaturePlugin_Clear` capture, then fully revert the temporary
change (confirm via `git diff --stat` showing zero net change afterward) —
this is a live, mechanical proof the detector actually works end-to-end,
not just that its unit tests pass in isolation.

### 3.3 — Clause C: scope it honestly, narrowly, and say so plainly in the report

Do not attempt to build a fully general "any side-channel leak, of any
future shape" detector — that is not achievable with reasonable effort and
would risk either false positives (flagging legitimate, honest cross-
provider data hand-offs like `AtmosphereSharedLut`'s own blackboard entry)
or false confidence (a narrow heuristic that LOOKS general but silently
misses the next genuinely different shape, exactly the trap this whole
campaign exists to avoid repeating). Instead:
1. Add a small, explicit, hand-maintained list of "known-risk blackboard
   keys" (the ones PHASE2's own audit ledger identified, whether fixed in
   PHASE3 or confirmed already-honest) — e.g. a `constexpr std::array`
   pairing each key's own debug name with the toggle-registry name that
   should gate it.
2. Each capture, for each pair in that list, confirm: if the toggle
   reports disabled, the blackboard key was NOT published this frame (this
   requires `RenderPassBlackboard` to expose a way to check "was key X
   published this frame" without needing to know its value's TYPE — add a
   small, additive `bool WasPublishedThisFrame(RenderPassId key) const`
   accessor if one does not already exist, mirroring `Fetch<T>()`'s own
   existing linear-scan-by-key shape but without the `std::any_cast`).
3. This is a real, if narrow, permanent regression guard for EXACTLY the
   bug shape PHASE1/PHASE3 fixed — plainly document in both the header
   comment and this phase's own completion report that it is a curated
   allowlist-based check, not a fully general one, and that any FUTURE new
   blackboard key with the same risk shape must be manually added to this
   list by whoever adds it (call this out in `AGENTS.md` too, in PHASE7).

### 3.4 — End of phase

1. Incremental build succeeds.
2. Every new Tier-1 test passes (targeted filter), alongside a fresh,
   targeted re-run of `editor-core-separation-21`'s own existing
   `RenderPassHonestyCheckerTests.cpp` to confirm zero regression there.
3. Write `PHASE6_COMPLETION_REPORT.md`: both new detectors' own design,
   the live, deliberately-reintroduced-then-reverted proof from 3.2 (and
   an equivalent one for Clause C, reintroducing PHASE1's own original bug
   temporarily), and the honest scoping statement from 3.3.
4. `git_add` + `git_commit` covering every file changed and the report.
