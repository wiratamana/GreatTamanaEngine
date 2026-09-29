# PHASE3 — Fix every Confirmed-Lie finding from PHASE2's audit

Parent: `PHASE0_MASTER_STRATEGY.md` (read it fully first).
Campaign folder: `task_manager/editor-core-separation-22/`
Previous phase report to read first: `PHASE2_COMPLETION_REPORT.md` — this
phase's ENTIRE backlog is that report's own ledger. Do not re-derive it.

## Step 1: The Goal (Where are we going?)

Every row in PHASE2's ledger marked **Confirmed-Lie** must be fixed, live-
verified, and turned into **Already-Honest** by the end of this phase —
with zero exceptions silently deferred (mirrors
`editor-core-separation-21`'s own PHASE4, which fixed all six of its own
findings with zero deferred, after resolving genuine ambiguity via
`ask_questions` first).

## Step 2: The Situation (Where are we now?)

This phase has no independent root-cause work of its own — it purely
executes PHASE2's own backlog. Re-read `PHASE2_COMPLETION_REPORT.md`'s
ledger in full before writing any code.

## Step 3: The Plan (detailed strategy)

### 3.1 — The default fix shape for every finding

For the overwhelming majority of findings, the fix is: add
`if (!rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "<Name>")) { return; }`
(or, for a provider that legitimately has NO single umbrella name of its
own — e.g. a `ProviderScope::Once` provider pushing many dynamically-named
per-request passes, mirroring `"GpuSkinning"`'s own existing precedent — an
equivalent single, whole-mechanism check under whatever umbrella name that
mechanism already uses, or a freshly `ask_questions`-confirmed new one) at
the VERY TOP of the provider lambda body, BEFORE the first line that
produces any observable side effect — reusing PHASE1's own
`RenderPassToggleGuard.h` helper. This is a mechanical, low-risk change for
any finding whose fix is exactly this shape — apply it directly, no
`ask_questions` needed for these.

### 3.2 — Findings that need a genuinely different fix shape

Some findings from PHASE2 may not fit the "early return at the top of one
provider lambda" shape cleanly — for example: a side effect that spans
MULTIPLE providers (one publishes, a different, EARLIER-in-declaration-
order provider already consumed a stale value from a PRIOR frame before
this frame's own toggle state was even known), or a cached value that must
be explicitly CLEARED (not merely "not re-published") when a pass toggles
from enabled to disabled between two frames, so a stale value from an
EARLIER, still-enabled frame does not linger and get misread as "fresh"
this frame. For any such finding:
1. Do not force it into the default shape if it does not fit — describe
   the actual, correct fix for that specific shape in this phase's own
   completion report, with the same rigor `PHASE1`'s own Step 3 used.
2. If the correct fix requires a genuinely new decision (e.g. "should a
   toggled-off cached value be cleared to a sentinel immediately, or simply
   left stale but ignored by every consumer via its own validity check?"),
   use `ask_questions` before implementing it.

### 3.3 — Test discipline

For each finding whose fix introduces or reuses a pure, testable decision
(the common case, given 3.1's shape), add or extend a Tier-1 test exactly
as `RenderPassToggleGuardTests.cpp` (PHASE1) already established the
pattern for. For a finding whose fix is inherently integration-shaped (e.g.
clearing a cached value across frames, which needs a real
`RenderPassBlackboard`/provider sequence to exercise meaningfully), prefer
extending an existing integration-level test file over inventing a new
one, unless PHASE2's ledger shows enough findings of that shape to justify
a dedicated new test file.

### 3.4 — Live, HTTP-driven verification per finding (mandatory)

For EVERY finding fixed in this phase: toggle the relevant pass off via
`GET /render_graph/set_pass_enabled`, confirm via the SAME mechanism the
ledger's own "who consumes it" column identified (a `GET /get_game_view`
pixel check, a `GET /get_texture` on a specific cached preview, a
`GET /render_graph/passes` JSON check, or a `GET /get_logs` check) that the
side effect genuinely no longer manifests — then toggle back on and confirm
it returns. This mirrors PHASE1's own Step 3.4 exactly, once per finding.

### 3.5 — End of phase

1. Incremental build (`cmake --build build`) succeeds.
2. All new/extended Tier-1 tests pass (a targeted `ctest` filter covering
   just this phase's own new test names — not the full suite).
3. Write `PHASE3_COMPLETION_REPORT.md`: one entry per PHASE2 finding, its
   exact fix (file/line), and its exact live-verification evidence.
   Explicitly confirm the ledger has ZERO remaining Confirmed-Lie rows, or
   list any that were re-classified as an accepted non-goal via a fresh
   `ask_questions` confirmation (per PHASE0's Locked Decision #5) — never
   silently dropped.
4. `git_add` + `git_commit` covering every fix, every test, and the report.
