# PHASE9 — Full Acceptance Criteria, Regression, and Campaign Closeout

Campaign folder: `task_manager/editor-core-separation-27/`

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md` in full. Also re-read EVERY prior phase's
`PHASEn_COMPLETION_REPORT.md` (1 through 8) — this phase's whole job is to
re-confirm every one of them adds up to a coherent, fully-working whole,
with fresh evidence, not to trust each report's own "done" claim blindly.

## Step 1: The Goal (Where are we going?)

Every acceptance-criteria checkbox from the source design document's own
Section 12 ("ACCEPTANCE CRITERIA") re-confirmed, once, here, at the end —
with FRESH evidence (re-run, not merely cited from an earlier phase's own
report) — followed by a full clean build, a full `ctest` regression pass,
and `CAMPAIGN_COMPLETION_REPORT.md`. When this phase is green,
`editor-core-separation-27` (BIG STEP 3 of 4) is closed for good, and BIG
STEP 4 (GPU Memory Aliasing) may begin as its own, later, separate
campaign.

## Step 2: The Situation (Where are we now?)

By this point: `RenderTexture`/`GpuResourceFactory` can skip the depth
companion (PHASE1); the vocabulary, compiler fix, and the full
`RenderGraphPersistentResourceCache` class (construction, age, eviction,
resize) all exist and are individually Tier-2-tested via the new headless
fixture (PHASE2-6); `RenderGraph`/`Core::BuildFrame()` drive the cache's
own per-frame lifecycle (PHASE7); and
`RenderGraphBuilder::GetOrCreatePersistentTexture()` (both overloads) is
the real, producible, first-class entry point, with honest layout
tracking and batched resize flushing fully wired end-to-end (PHASE8).

This phase does NOT introduce new production behavior — it is
verification, consolidation, and closeout only.

## Step 3: The Plan

### 3.1 — Re-confirm the source document's own Section 12 checklist, item by item, with fresh evidence

For each checkbox below, re-run (do not merely re-read) the relevant test
from the phase named, and record the fresh result in
`CAMPAIGN_COMPLETION_REPORT.md`:

1. Same physical `VkImage` reused across 3 consecutive real `Execute()`
   calls — PHASE8, test 1.
2. Token-based overload: identical reuse result AND confirmed fast path
   on calls 2/3 — PHASE8, test 2.
3. Write on frame N, read back correctly on frame N+1, no intervening
   discard — PHASE8, test 3.
4. A write-only, no-in-frame-reader, not-in-`finalOutputs` pass still
   runs every frame (not culled), content survives to frame N+1 — PHASE3
   (compiler-level) + PHASE8 test 4 (full builder-level).
5. Two DIFFERENT `_v2`/`_v3`-style owners, identical name, identical
   desc → two DISTINCT `VkImage`s — PHASE4 test 2 (cache-level) + PHASE8
   test 5 (builder-level) — **the single most important regression test
   in this whole campaign, per the source document's own words**; confirm
   BOTH levels pass.
6. Forced `RenderTexture` construction failure leaves NO entry behind;
   an immediate retry succeeds — PHASE4 test 3. If this test was unable to
   reliably force a real allocation failure on this machine (a known,
   honestly-flagged possibility — see PHASE4's own Step 4, item 3),
   re-attempt it here with fresh eyes; if it still cannot be forced
   reliably, state this plainly in the completion report as an accepted,
   documented verification gap — do not fabricate a pass.
7. Eviction after the configured threshold, confirmed via
   `FramesUntilEviction()` AND a GPU-memory-total drop, in BOTH (a) a
   session exercising both `ExecuteTimingMode` regimes every frame and (b)
   a session that NEVER exercises `SynchronousImmediateReadback` at all —
   PHASE5 tests 1/2. Re-confirm scenario (b) specifically now, at the
   full-integration level (through `Core`/a real session if practical, or
   explicitly re-argue why the cache-level headless test already covers
   this claim in full — the cache's own `BeginFrame()` has no dependency
   on which regime ran, only on being called at all).
8. Resize: current frame's handle stays at old extent; layout resets to
   `VK_IMAGE_LAYOUT_UNDEFINED` by end of that frame; NEXT frame observes
   new extent + `UNDEFINED` — PHASE6 test 1 (cache-level) + PHASE8's own
   resize-adjacent coverage if any additional builder-level nuance was
   found there.
9. THREE OR MORE entries resized the same frame → exactly ONE
   `vkDeviceWaitIdle()` — PHASE6 test 2. Re-state, explicitly, in this
   phase's own report, the code-level reasoning that makes "exactly one"
   true (a direct quote/citation of `FlushPendingResizes()`'s own final
   body, confirming a single, unconditional `vkDeviceWaitIdle()` call
   outside any loop) — this claim was never mechanically instrumented
   (PHASE6's own honest caveat), so restate the proof by code inspection
   here, fresh, rather than merely re-citing PHASE6's report.
10. A dozen distinct identities force container growth; every earlier
    entry's `RenderTexture` and every earlier token still work afterward —
    PHASE4 test 7.
11. Debug-build asserts (or release log-and-refuse) fire for:
    `desc.hasDepth == true`; null/empty owner or name; a resize from
    `PipelinedDeferredReadback`; a token reused across two different
    identities — PHASE4 tests 4/5, PHASE6 test 4, PHASE5 test 8 — re-run
    ALL FOUR in one pass here to confirm none regressed relative to each
    other (they all touch the SAME `Resolve()`/`ResolveAgainstEntry()`
    body after PHASE8's own refactor — this is exactly the kind of
    cross-cutting regression a single combined re-run is meant to catch).
12. `desc.hasDepth == false` entry carries EXACTLY ONE tracked GPU
    allocation — PHASE4 test 6.
13. Same-frame double-request from both regimes: logged exactly once,
    tested BOTH call orders, first caller's own handle/work fully
    unaffected — PHASE5 tests 4/6. Re-confirm the "both call orders"
    claim explicitly: since `Resolve()` itself has no notion of "which
    regime" for the double-request check (only `currentFrame` matters —
    see PHASE5's own Step 4, item 5 reasoning), confirm this in the report
    as a structural/order-independence argument, not just a single
    empirical test run.
14. Every pre-existing `RenderGraphBuilder`-constructing call site
    (production AND test) and every pre-existing
    `RenderTexture`/`GpuResourceFactory::CreateRenderTexture()` call site
    compiles unmodified and still creates its depth companion exactly as
    before — confirmed by the full clean build (Step 3.3) compiling with
    zero unexpected changes outside this campaign's own diffs, and by a
    full `ctest` regression showing zero unexplained delta.
15. `Core::BuildFrame()` calls `BeginPersistentResourceFrame()` exactly
    once per real engine frame, strictly before either regime's
    `Execute()` call — confirmed by direct code read (PHASE7) plus a
    live-Editor smoke check (Step 3.2 below) showing no double-eviction/
    double-log anomaly across several real frames.
16. Full `ctest` regression pass, zero unexplained delta — Step 3.3.

### 3.2 — Live-Editor smoke check (secondary, cheap sanity pass — NOT the primary proof, per Locked Decision 3)

1. `cmake --build build` (incremental — full clean build is Step 3.3,
   separately).
2. `run_app_background` the built Editor executable.
3. `gte_send_request "/get_logs"` — confirm no new warning/error appeared
   that wasn't present before this campaign started.
4. `gte_send_request "/list_textures"` — confirm the response is
   well-formed (this campaign adds no NEW production caller of
   `GetOrCreatePersistentTexture()` yet — TAA/SSR do not exist — so no
   persistent-cache entry is expected to appear here yet; this call is
   purely a "did I break the existing debug-texture registry" smoke
   check, not a feature-specific one).
5. `gte_send_request "/render_graph"` — same purpose, confirm well-formed,
   no crash, no new unexplained warning.
6. `stop_app_background` the Editor process.

### 3.3 — Full clean build + full regression

1. Full clean build (see `BUILDING.md`/`TESTING.md` for the exact
   commands this repository uses — likely `cmake --build build` from a
   freshly configured `build` directory, or the project's own documented
   "full clean build" recipe; confirm before running, do not guess).
2. `cd /d C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine\build && ctest -C Debug --output-on-failure`
   — full regression. Confirm the total executed/skipped/passed counts
   against the LAST known-good baseline (search prior campaigns'
   completion reports, e.g. `editor-core-separation-1`'s own
   "1773 tests... up from render-pass-7's own 1753 baseline" convention,
   for the most recent recorded baseline) — the new count must be that
   baseline PLUS every new test this campaign added (PHASE2 through
   PHASE8's own additions), with zero unexplained regressions and no new
   unexplained skips beyond the expected `VK_EXT_headless_surface`-gated
   `GTEST_SKIP()`s this campaign's own new tests may add on a machine that
   lacks that extension (if this development machine DOES support it, as
   PHASE4 already established, these should show as PASSED, not SKIPPED —
   investigate immediately if any of THIS campaign's own new tests show as
   skipped here when they didn't in their own originating phase).
3. If anything fails: diagnose the root cause directly; if the fix is
   small and confidently understood, fix it directly and re-run; if it
   requires meaningfully re-opening an earlier phase's own design (not
   just a typo/build-config fix), use `dispatch_sub_agent` to investigate
   and propose a fix, following `PHASE0_MASTER_STRATEGY.md`'s Rule 4 (never
   `delegate_task` from within an implementation phase).

### 3.4 — `CAMPAIGN_COMPLETION_REPORT.md`

Write one, in this same folder, covering: a one-paragraph summary of what
shipped; the full Section 12 checklist (Step 3.1 above) with fresh
evidence for each line; the full clean build + `ctest` result; any
honestly-flagged, still-open gaps (e.g. if the exception-safety test
(item 6) or the ASan-availability check (PHASE5's stale-token test) could
not be fully exercised on this machine); and an explicit statement that
BIG STEP 4 (GPU Memory Aliasing) may now begin as its own, later, separate
campaign.

## Step 4: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. This is the ONE
phase permitted to run a full clean build and a full `ctest` regression
(Rule 3's own stated exception). No new production files. Git commit
covering the completion report (and any small fixes made along the way).
