# Job System — Phase 8: Testing, Hardening & Benchmarking

Parent document: `JOBSYSTEM_PHASE0_MASTER_STRATEGY.md` (read first).
Previous phase: `JOBSYSTEM_PHASE7_EDITOR_JOBS_PANEL.md` (must be done —
the whole pipeline is visible, working, and has been visually confirmed
against the real Phase 6 scenario).
Next phase: **none — this is the campaign's final phase.** Its own
Definition of Done, once satisfied, is the campaign's own Definition of
Done (see `JOBSYSTEM_PHASE0_MASTER_STRATEGY.md`, Step 1).

**Definition of Done for this phase (closes the whole campaign):** the
Job System has survived a real stress/soak test without a single observed
crash, hang, or incorrect result; ThreadSanitizer (or an equivalent race
detector) has been run against the full test suite plus the Phase 6
production scenario with zero reported races; a repeatable, automated
benchmark exists proving the real-world win Phase 6 measured by hand; and
a final, explicit, written decision has been made and recorded about
whether `GTE_ENABLE_JOB_SYSTEM` defaults to `ON` in this engine going
forward.

---

## Step 1: The Goal (Where are we going?)

Move the Job System from **"built, tested, and demonstrated to work in a
controlled scenario"** (the honest state of things after Phase 7) to
**"trusted enough to leave running by default, in every build, indefinitely"**
— the same bar every other always-on subsystem in this engine (`Registry`,
`Renderer`, `Profiling`) has already had to clear, except this one carries
a genuinely new risk category (data races) none of them do, which is
exactly why this campaign gives it its own dedicated final hardening
phase rather than declaring victory the moment Phase 7's panel looks
correct on one developer's machine.

Concretely, this phase produces:

1. A **stress/soak test** — a long-running, high-load synthetic scenario
   (many simultaneously-animated models, deliberately adversarial
   `itemCount`/`minItemsPerBatch` combinations, deliberate queue-at-capacity
   conditions) run for an extended duration (minutes, not seconds) with
   automated result-correctness checking throughout, not just at the end.
2. A **ThreadSanitizer (TSan) verification pass** — the empirical
   backstop for every design-time safety claim Phase 4's classification
   table made, run against both the synthetic Job System test suite
   (Phases 1–3's own tests) AND the real Phase 6 production scenario.
3. A **repeatable, automated benchmark** (not just Phase 6's one manual
   measurement) integrated into whatever this engine's future
   "benchmark mode" ends up being (see `TODO.md`'s own Phase 6
   cross-reference inside `PROFILER_STRATEGY_v2.md`'s campaign — this Job
   System benchmark should plug into that SAME future infrastructure, not
   invent a second, parallel one).
4. A **final go/no-go decision**, written down, on `GTE_ENABLE_JOB_SYSTEM`'s
   default value, with the reasoning behind it.

---

## Step 2: The Situation / The Problem (Where are we now?)

Every phase from 1 through 7 has been built with real discipline —
Tier-1 tests for pure logic, stress-repeated ordering tests in Phase 3, a
bit-for-bit parity test in Phase 6, a real visual confirmation in Phase 7
— but every single one of those tests, by necessity, ran for a SHORT
duration, under WHATEVER thread-scheduling behavior happened to occur on
one CI/developer machine, during one test session. This is a real,
well-understood limitation of testing concurrent code: **the absence of an
observed race in N short test runs is evidence of safety, not proof of
it** — a genuine race condition can easily have a vanishingly small window
of actual misbehavior, one that a handful of quick test runs on a
lightly-loaded machine may simply never happen to hit, while a real
end-user's machine, running a real, much longer, much more heavily loaded
session, eventually does.

Two industry-standard techniques exist specifically to close this gap, and
neither has been used anywhere in this campaign yet:

1. **Soak testing** — running the SAME correctness checks that already
   exist (Phase 6's parity test, Phase 3's ordering tests) not once or a
   few times, but continuously, for an extended real-world duration, to
   dramatically increase the chance of ever hitting a rare scheduling
   window that would expose a latent bug.
2. **Dynamic race detection (ThreadSanitizer)** — a compiler/runtime
   instrumentation tool that does not merely HOPE to observe a race by
   luck, but actively tracks every memory access's "happens-before"
   relationship across threads and reports a race the INSTANT one occurs,
   even if the resulting values happen to look correct by chance that
   particular run. This is qualitatively stronger evidence than "the test
   passed" — it is the difference between "we didn't see a problem" and
   "a tool specifically built to find this class of problem looked, hard,
   and found nothing".

Additionally, every phase so far has deliberately measured correctness,
never institutionalized the PERFORMANCE measurement Phase 6 did by hand —
without a repeatable, automated benchmark, there is no way to catch a
FUTURE regression (someone innocently changing `JobQueue`'s locking
strategy, say, six months from now) that quietly makes the whole system
slower than the single-threaded baseline it was built to beat, short of
someone remembering to re-run Phase 6's manual steps by hand again.

---

## Step 3: The Plan (How will we get there?)

### 3.1 — The stress/soak test

New, explicitly-opt-in (NOT part of the default `ctest` run — this is a
long-running test, matching this engine's own precedent of gating
expensive/machine-dependent tests, e.g. the PMX/VMD real-model smoke tests
that `GTEST_SKIP()` when their real asset isn't present) test target:

```
tests/Jobs/
    JobSystemSoakTest.cpp   - gated behind a new GTE_BUILD_SOAK_TESTS
                              CMake option (default OFF - mirrors
                              GTE_BUILD_TESTS' own "zero-touch when off"
                              philosophy), or an environment-variable/
                              command-line-flag gate if a separate CMake
                              option is judged unnecessary overhead for
                              one test file
```

Scenario: repeatedly (in a tight loop, for a configurable duration —
default several minutes, overridable via a command-line argument/env var
for a quick local smoke run vs. a longer CI/nightly run) —
1. Constructs several synthetic "model" datasets of varying vertex counts
   (some deliberately below `kMinVerticesToParallelize`, most above it,
   one deliberately using a prime-number vertex count to maximize the
   chance of an uneven, edge-case batch split per Phase 2's own
   `ComputeBatchRanges()` math).
2. Runs the FULL real pipeline: `Dispatch()` across all of them
   concurrently (simulating several simultaneously-animating models, not
   just one, deliberately exceeding what Phase 6's own parity test ever
   tried), `WaitForJobs()`, and a bit-for-bit correctness check against a
   freshly-computed serial reference EVERY SINGLE ITERATION (not just once
   at the end) — any single incorrect result anywhere aborts the test
   immediately with full diagnostic context (which iteration, which
   dataset, which vertex index first differed).
3. Deliberately varies timing by inserting small, randomized
   `std::this_thread::yield()`/short sleeps at points designed to
   perturb scheduling (before `Dispatch()`, between `Dispatch()` calls for
   different models, right before `WaitForJobs()`) — a well-known
   technique for widening a race's effective window during testing,
   making an otherwise-rare interleaving far more likely to actually occur
   during the test's run, rather than relying purely on natural OS
   scheduling noise.

### 3.2 — ThreadSanitizer verification

A documented, one-time (repeated at meaningful future milestones, not
continuously in every CI run — TSan instrumentation carries real runtime
overhead) verification pass:
1. Build `GreatTamanaEngineTests` (and, separately, a small standalone
   harness driving Phase 6's real Furina-model production scenario
   end-to-end) with `-fsanitize=thread` (or the MSVC-toolchain equivalent
   available at the time — note in this phase's own findings whichever
   concrete toolchain/flag combination was actually used, since Windows/
   MSVC's own TSan support has historically lagged behind Clang/GCC's; using
   a Clang-on-Windows or WSL/Linux cross-check build specifically for this
   verification pass is an acceptable, explicitly-noted deviation from this
   engine's own Windows-only `CMakeLists.txt` platform gate, since this is a
   verification-only build, never a shipped configuration).
2. Run the full test suite (Phases 1–7's own tests) plus the soak test
   (3.1, a SHORT duration is enough under TSan, since TSan's own
   instrumentation already dramatically increases the chance of exposing a
   real race without needing minutes of wall-clock time) under this
   instrumented build.
3. **Any single reported race is a hard blocker** — this phase does not
   close, and `GTE_ENABLE_JOB_SYSTEM` does not get recommended as a safe
   default, until TSan reports zero races across this entire run. Any race
   found must be root-caused, fixed, and RE-VERIFIED under TSan again
   (not just "fixed and assumed correct") — treating a TSan-caught race as
   optional or informational, rather than a build-blocking correctness
   bug, would defeat the entire purpose of running the tool.
4. Record the exact verification performed (toolchain, flags, test scope,
   duration, and the "zero races reported" result) in this phase's own
   completion notes — mirroring this codebase's own existing, established
   discipline of writing concrete, reproducible "Verification performed"
   sections into its strategy/completion documents (see any
   `PHASE4x_COMPLETION_REPORT.md`, or `TESTING.md`'s own description of
   Tier 2 GPU verification "against a real Vulkan device with validation
   layers enabled").

### 3.3 — The automated benchmark

A small, dedicated benchmark entry point (mirroring the ALREADY-noted
future "Phase 6: benchmark mode" cross-reference inside this engine's own
`PROFILER_STRATEGY_v2.md`/`AGENTS.md`'s "Profiling" section — this Job
System benchmark should be written to plug into that SAME eventual
benchmark-mode CLI, e.g. a future `--benchmark job-system-skinning` flag on
`main.cpp`, rather than becoming its own bespoke, one-off measurement
tool): runs Phase 6's real Furina-model skinning workload, BOTH with
`GTE_ENABLE_JOB_SYSTEM` effectively disabled (forcing the inline/serial
fallback path — see Phase 1's own `GTE_ENABLE_JOB_SYSTEM=OFF` behavior, or
a runtime-only equivalent switch if a build-time-only comparison proves too
inconvenient for a single benchmark binary to offer both sides of) and with
it genuinely parallel, over a fixed number of frames, reporting the mean/
min/max per-frame `AnimationSystem::Update()` cost for each configuration
side by side. This is what turns Phase 6's one-time, manual, "trust me, I
measured it" result into a standing, re-runnable regression check a future
contributor can re-verify in seconds after touching anything under
`src/Jobs/`, `src/Animation/`, or `src/Game/Animation/`.

### 3.4 — The final go/no-go decision on the default

Once 3.1–3.3 are all complete and clean, this phase makes and records an
explicit decision — not left implicit:

- **If** the soak test ran clean, TSan reported zero races, and the
  benchmark shows a genuine, meaningful win with no observed regression
  risk → `GTE_ENABLE_JOB_SYSTEM` stays `ON` by default (as it already was
  from Phase 1), and this phase's own notes state this plainly, with the
  supporting evidence summarized.
- **If** any concern remains (e.g. TSan support on the actual target
  toolchain was incomplete, or the soak test surfaced something fixed but
  not yet re-verified for long enough to be confident) → this phase
  explicitly recommends flipping the DEFAULT to `OFF` (mirroring how
  `GTE_ENABLE_PROJECT_PANEL`/`GTE_ENABLE_EDITOR` themselves are each
  independently toggleable specifically so a developer can disable a
  still-maturing feature without losing the rest of the engine), while
  leaving the fully-working, opt-in `ON` path available for continued
  hardening — an honest, documented "not quite there yet" is a strictly
  better outcome for this codebase's own culture (see this whole
  campaign's Phase 0 "bias toward NOT parallelizing something when in
  doubt") than a confident-sounding default that turns out to hide a real
  problem.

---

## Step 4: What We Will NOT Do (Focus)

- **We will NOT treat a clean soak-test run as sufficient ON ITS OWN,
  without also running ThreadSanitizer.** Per Step 2, these are
  complementary, not redundant — a soak test that never happens to
  trigger a race is not the same evidence as a race detector that actively
  looked for one and found none. Both are required before this phase
  closes.
- **We will NOT expand this phase into a general "make the whole engine
  provably thread-safe" audit beyond what Phase 4 already scoped and what
  Phase 6 actually shipped.** This phase hardens and verifies EXACTLY the
  surface area this campaign actually built and shipped (Phases 1–7) — it
  does not retroactively re-open or re-scope the campaign's own boundaries.
- **We will NOT add new job-system FEATURES in this phase** (no new
  scheduling capability, no new integration point beyond Phase 6's
  animation-skinning migration). This is a hardening and verification
  phase, full stop — any new capability discovered to be genuinely useful
  during this work becomes its OWN, separately-scoped future strategy
  document, not scope creep folded into this one.
- **We will NOT permanently keep TSan-instrumented builds/soak tests as
  part of the default, every-commit CI/`ctest` run.** Both carry real
  runtime cost (TSan instrumentation overhead; the soak test's own
  multi-minute duration) that is appropriate for a periodic/milestone
  verification pass, not for every ordinary build — matching this engine's
  own existing precedent of keeping expensive/machine-dependent
  verification (real-GPU Tier 2 checks, real-model smoke tests) explicitly
  separate from the fast, always-run Tier 1 suite (see `TESTING.md`).
- **We will NOT ship a default-`ON` recommendation without the honest
  written justification described in 3.4.** "It seems fine" is not an
  acceptable substitute for the actual soak-test/TSan/benchmark evidence
  this phase exists to produce.

---

## Step 5: Their Role (What does this mean for you?)

- Build the soak test (3.1) to actually PERTURB scheduling (the randomized
  yield/sleep insertion) rather than just looping the existing tests
  faster — a soak test that merely repeats a fast-passing test many times
  under otherwise-identical, lightly-loaded scheduling conditions gains far
  less confidence than one deliberately designed to widen a race's
  effective window.
- Actually run ThreadSanitizer, on a real toolchain, against the real test
  suite AND the real Phase 6 production scenario — do not skip this step
  because Windows/MSVC TSan support is inconvenient; use a Clang or
  WSL/Linux cross-check build specifically for this verification pass if
  needed, and say so plainly in this phase's own recorded findings (per
  3.2, point 1).
- Treat ANY TSan-reported race, no matter how minor-looking, as a hard
  blocker requiring root-cause, fix, and re-verification — never as a
  "probably fine, ship anyway" judgment call.
- Wire the benchmark (3.3) into whatever this engine's eventual
  benchmark-mode infrastructure turns out to be, rather than building a
  second, disconnected measurement tool — check `PROFILER_STRATEGY_v2.md`'s
  own Phase 6 status before duplicating effort here.
- Make the explicit go/no-go call (3.4) and write it down, with its actual
  supporting evidence (soak-test duration and result, TSan toolchain/flags/
  result, benchmark before/after numbers) — this record is what a future
  contributor, months or years from now, will read to understand not just
  THAT the Job System is considered safe, but WHY, and with what level of
  scrutiny that confidence was earned.
- Once this phase's Definition of Done is satisfied, the entire Job System
  campaign (`JOBSYSTEM_PHASE0_MASTER_STRATEGY.md` through this file) is
  complete — update the master strategy document's own status to reflect
  this, mirroring how `RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md` closes
  out that campaign's own master strategy document today.
