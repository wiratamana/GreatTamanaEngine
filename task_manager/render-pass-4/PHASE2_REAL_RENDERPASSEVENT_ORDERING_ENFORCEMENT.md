# PHASE2: Make RenderGraphCompiler Actually Use RenderPassEvent For Ordering

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first, AND read
`PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md` before starting this
one — this phase reuses and extends `DetectRenderPassEventContradictions()`,
which PHASE1 must already exist and be tested. Part of the `render-pass-4`
campaign._

## Step 1: The Goal

**This is the one phase in this campaign with a genuine, deliberate
behavior change — call this out explicitly in your own completion
report, per PHASE0's Locked Design Decision 1.** Make
`RenderGraphCompiler::Compile()` process passes, for the purpose of its
RAW/WAW dependency-edge construction AND its Kahn's-algorithm ready-set
tie-break, in an **effective order** — every pass stable-sorted by
`RenderPassEvent` first, original declaration index second (as the
tie-break for passes sharing the same `RenderPassEvent`) — instead of raw
declaration order. `RenderPassEvent` becomes real, load-bearing ordering
input for the first time, without inventing a new resource-versioning
system, without changing `RenderGraphBuilder`'s public surface at all, and
without touching `RenderGraph.cpp`'s or `RenderGraphBarrierPlanner.cpp`'s
own logic.

**Why this specific mechanism, and not something else:** a real RAW/WAW
dependency (a genuine "pass B needs pass A's output") must NEVER be
overridden — that would corrupt correctness, not just style. What CAN
safely be given real teeth is exactly the part of "order" that today falls
back to "whichever pass happened to get declared first in some C++ file,
for no reason connected to any real dependency" — a stable sort by
`RenderPassEvent`-then-declaration-index only ever changes the order
between passes with NO real dependency on each other (ties in Kahn's
ready-set) or resolves which write a read should bind to when a naive
forward-only scan would otherwise miss it entirely (the exact
`AtmosphereComposite` bug shape). This is a scoped, principled change, not
a rewrite of the compiler's actual algorithm shape.

**Important, and must be stated plainly in this phase's own doc updates
and completion report: this does NOT make the historical bug class
impossible outright.** It converts the failure mode from "hidden, spread
across which C++ file happens to call a function first" into "a single,
auditable enum value on one pass declaration is wrong" — which is exactly
what PHASE1's detector (extended in Step 3.4 below to run against this
phase's new effective order) will now catch instead of silently miscull.
The two phases are a matched pair: PHASE2 fixes the common case
structurally; PHASE1's detector (already shipped) remains the safety net
for the residual case of a genuinely mistagged `RenderPassEvent`.

## Step 2: The Situation

- `RenderGraphCompiler.cpp`'s `Compile()` today has three places that walk
  passes by raw index `i` in `[0, passCount)`: (a) the RAW/WAW edge-building
  loop (~lines 106-162), which updates `lastTextureWriter`/`lastBufferWriter`/
  `lastVolumeTextureWriter` as it goes; (b) the backward-reachability root
  scan (~lines 197-220), which is order-independent (just checks every
  pass's writes against `finalOutputs`/`finalVolumeTextureOutputs` — this
  one does NOT need to change); (c) Kahn's-algorithm's `ready` set
  (`std::set<std::int32_t>`, ~lines 260-283), which breaks ties among
  simultaneously-zero-in-degree passes by picking the SMALLEST raw index
  (`*ready.begin()`) every iteration — this is exactly this compiler's own
  documented "Step 3.3 determinism requirement" tie-break, and it is what
  must change to use effective order instead.
- Nothing about `edgeExists`'s own SHAPE needs to change — it is still a
  `passCount x passCount` boolean adjacency matrix keyed by ORIGINAL pass
  index (that indexing must stay stable, since `result.executionOrder`,
  `isCulled`, and the lifetime arrays are all keyed by original index too,
  per `CompiledGraph`'s own doc comments in `RenderGraphCompiler.h`). Only
  the ORDER passes are WALKED IN while building edges/breaking ties needs
  to change.
- `PHASE1_DEPENDENCY_EVENT_CONTRADICTION_SAFETY_NET.md`'s own
  `DetectRenderPassEventContradictions()` already accepts an explicit
  `processingOrder` parameter (a permutation of `[0, passCount)`) —
  deliberately future-proofed exactly for this phase. `Compile()`'s own
  PHASE1 call site currently passes the identity permutation; this phase
  changes that ONE call site to pass the new effective order instead —
  no change needed to `DetectRenderPassEventContradictions()`'s own logic.
- A real, necessary pre-requisite before writing any code: an audit of
  every pass ACTUALLY declared in this engine today, comparing its raw
  C++ declaration/registration order against its `RenderPassEvent` tag,
  to confirm this reorder either (a) produces byte-identical execution
  order for every currently-shipping pass, or (b) if it doesn't, that any
  difference found is itself evidence of a currently-latent mistagging
  this phase should also fix as part of landing it. Real anchor points to
  start from (confirmed by direct source inspection during this
  campaign's own planning pass — re-confirm them yourself, they may have
  drifted):
  - `Application.cpp`'s `RegisterOffscreenRenderPipelineProviders()`
    registers, in this literal order: `"AtmosphereSharedLut"` (immediate,
    `BeforeDeferredPasses`, its two real passes both tagged `PreOpaques`
    inside `AtmosphereLutRenderer.cpp`), `"GpuSkinning"` (deferred,
    `PreOpaques`), `"AtmosphereViewLut"` (immediate, `BeforeDeferredPasses`,
    ALL THREE of its real passes tagged `PreOpaques` — the per-view
    `AddSkyViewLutPass()`/`AddAerialPerspectiveVolumePass()` calls (both
    inside `AddAtmosphereViewLutPasses()`, `AtmospherePassSequence.cpp`) plus
    the Game-View-only `AddAerialPerspectiveVolumeDebugSlicePass()` call
    Application.cpp makes directly right after — RE-CONFIRMED directly
    against `AtmosphereLutRenderer.cpp`'s own trailing `RenderPassEvent`
    arguments (~lines 525, 670, 1006, all three literally `PreOpaques`).
    **CORRECTION vs. an earlier draft of this audit**: `AtmosphereLutRenderer.cpp`
    ~lines 833-841 IS a real `AfterTransparents`-tagged pass, but it belongs
    to `AddAerialPerspectiveCompositePass()` — the pass
    `AddAtmosphereCompositePass()` (`AtmospherePassSequence.cpp`) wraps,
    called by the SEPARATE `"AtmosphereComposite"` provider below, NOT by
    `"AtmosphereViewLut"`. Do not attribute that `AfterTransparents` tag to
    `"AtmosphereViewLut"`'s own passes — it is already correctly accounted
    for under `"AtmosphereComposite"` at the end of this list.
    `"RenderOpaque"` (deferred, `Opaques`),
    `"DrawSkyBackground"` (deferred, `AfterOpaques`), `"RenderTransparent"`
    (deferred, `Transparents`), `"AtmosphereComposite"` (immediate,
    **`AfterDeferredPasses`**, `AfterTransparents`).
  - `RegisterPresentRenderPipelineProvider()` registers `"Present"` alone,
    on a SEPARATE `RenderPipeline` instance (`m_presentRenderPipeline`) —
    a separate `Compile()` call entirely (different `CompiledGraphInput`),
    so it is unaffected by anything the offscreen pipeline's own reorder
    does.
  - `AddFrameDebuggerReplayPasses()`/`ComputeBlurValidation.cpp` never go
    through `RenderPipeline` at all — every one of their passes defaults
    to `RenderPassEvent::Opaques` (`PassRecord`'s own default), since
    they're never given an explicit value. Confirm, by reading both
    files directly, whether any of their own real reads/writes actually
    depend on running after/before a specific OTHER real pass whose own
    tag is NOT `Opaques` — if the answer is "no, they only ever read/write
    their own dedicated debug textures/the Scene View's already-finished
    `RenderTexture`", they are safe to leave defaulted; if you find a real
    counter-example, flag it via `ask_questions` before proceeding — do
    not silently retag a pass this phase's own scope didn't originally
    anticipate touching.

## Step 3: The Plan

### 3.1 — Run the audit first

Before writing any compiler code: use `search_in_dir` across
`src/Application/`, `src/Renderer/Atmosphere/`, and `src/Editor/` for
`RenderPassEvent::` to enumerate every real, currently-tagged pass and its
value, and separately confirm every real registration/declaration
ORDER by reading `RegisterOffscreenRenderPipelineProviders()` and
`AddFrameDebuggerReplayPasses()`/`ComputeBlurValidation.cpp`'s own call
sites top-to-bottom. Build a simple table (declaration position ->
pass name -> `RenderPassEvent`) as part of your own working notes (this
does not need to become a permanent doc, but MUST appear in your
`PHASE2_COMPLETION_REPORT.md` so a future reader can see the reasoning,
per this campaign's own "focus on programming, but show your work"
spirit). Confirm every pass's position in a STABLE sort by
(`RenderPassEvent`, original declaration index) matches its current
real, working execution order. If it does not for some pass, use
`ask_questions` to confirm with the user whether to (a) fix that pass's
`RenderPassEvent` tag as part of this phase, or (b) proceed anyway and
let PHASE1's detector (Step 3.4 below) flag it going forward.

Worth deliberately checking, and calling out explicitly in your own
completion report: does `"AtmosphereComposite"`'s own
`RenderPassEvent::AfterTransparents` tag ALONE — independent of its
current `ProviderTiming::AfterDeferredPasses` registration — already
resolve correctly under the new effective order? (It should:
`AfterTransparents` sorts after `Opaques`/`AfterOpaques` regardless of
raw declaration/registration position, which is exactly this phase's
whole point.) If true, this is worth stating plainly as a genuine,
non-obvious side benefit of this phase's fix — it means a future author
could no longer reintroduce the exact historical bug shape that pass's
own `ProviderTiming` workaround was written for, even if they accidentally
left that pass on the default `BeforeDeferredPasses` timing again. This
does NOT mean the `ProviderTiming` workaround should be removed (Locked
Design Decision 4 forbids changing `RenderPipeline.h`'s behavior in this
campaign) — it is purely a confidence-building confirmation worth
recording in the audit/completion report.

### 3.2 — Implement the effective-order resort in `Compile()`

Immediately after `passCount == 0` early-return, and BEFORE PHASE1's
diagnostic block (which this step also updates — see 3.4), compute:

```cpp
    // render-pass-4 campaign, PHASE2
    // (PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md) - `effectiveOrder`
    // is a permutation of [0, passCount) - every pass's ORIGINAL
    // declaration index, reordered by a STABLE sort on (RenderPassEvent,
    // original index). This is the first time RenderPassEvent becomes
    // real, load-bearing ordering input: the RAW/WAW edge scan below, and
    // Kahn's-algorithm's own ready-set tie-break, both walk passes in THIS
    // order from now on, instead of raw declaration order. A stable sort
    // guarantees two passes sharing the same RenderPassEvent tier keep
    // their EXACT prior relative order - byte-identical behavior for
    // every pass that was already correctly ordered relative to its own
    // tier-mates (see this phase's own completion report for the full
    // audit confirming this holds for every pass shipping today).
    std::vector<std::int32_t> effectiveOrder(static_cast<std::size_t>(passCount));
    for (std::int32_t i = 0; i < passCount; ++i) {
        effectiveOrder[static_cast<std::size_t>(i)] = i;
    }
    std::stable_sort(effectiveOrder.begin(), effectiveOrder.end(), [&input](std::int32_t a, std::int32_t b) {
        return input.passes[static_cast<std::size_t>(a)].renderPassEvent
            < input.passes[static_cast<std::size_t>(b)].renderPassEvent;
    });

    // effectivePosition[originalPassIndex] = that pass's position in
    // effectiveOrder - the inverse permutation, used below wherever the
    // algorithm needs to compare/order by EFFECTIVE position rather than
    // walk effectiveOrder directly.
    std::vector<std::int32_t> effectivePosition(static_cast<std::size_t>(passCount), -1);
    for (std::size_t pos = 0; pos < effectiveOrder.size(); ++pos) {
        effectivePosition[static_cast<std::size_t>(effectiveOrder[pos])] = static_cast<std::int32_t>(pos);
    }
```

Add `#include <algorithm>` to `RenderGraphCompiler.cpp` for `std::stable_sort` —
VERIFIED this session: it is currently **NOT** included, transitively or
otherwise. `RenderGraphCompiler.cpp` today only includes `"RenderGraphCompiler.h"`,
`<set>`, and `<stdexcept>`; `RenderGraphCompiler.h` only includes
`RenderGraphBuilder.h`/`RenderGraphTypes.h`/`<cstdint>`/`<span>`/`<vector>`;
neither `RenderGraphBuilder.h` nor `RenderGraphTypes.h` includes `<algorithm>`
either. Do not assume it is already available via some other transitive
include — add it explicitly as its own new `#include` line.

### 3.3 — Rewire the RAW/WAW edge-building loop and Kahn's tie-break

**Edge-building loop** (~lines 106-162 today): change the outer
`for (std::int32_t i = 0; i < passCount; ++i)` to walk `effectiveOrder`
instead:

```cpp
    for (std::int32_t effectiveOrderPos = 0; effectiveOrderPos < passCount; ++effectiveOrderPos) {
        const std::int32_t i = effectiveOrder[static_cast<std::size_t>(effectiveOrderPos)];
        const PassRecord& pass = input.passes[static_cast<std::size_t>(i)];
        // ... body is UNCHANGED from today - every addEdge(writer, i) call,
        // every lastTextureWriter[...] = i assignment, stays exactly as it
        // is. Only WHICH ORDER `i` takes each of its values changes.
    }
```

This alone is the entire fix for the "orphan read" bug shape: a reader
tagged with an earlier `RenderPassEvent` than its real writer no longer
matters, because it is now WALKED after that writer regardless of which
one was declared first in C++ — the writer's entry in
`lastTextureWriter`/etc. is already populated by the time the reader is
processed.

**Kahn's-algorithm ready-set tie-break** (~lines 260-283 today): the
existing `std::set<std::int32_t> ready` stores and pops by SMALLEST RAW
INDEX. Change it to store/pop by SMALLEST EFFECTIVE POSITION instead,
mapping back to the real pass index only when needed:

```cpp
    std::set<std::int32_t> readyByEffectivePosition; // Stores EFFECTIVE POSITIONS, not raw pass indices.
    for (std::int32_t i = 0; i < passCount; ++i) {
        if (kept[static_cast<std::size_t>(i)] && inDegree[static_cast<std::size_t>(i)] == 0) {
            readyByEffectivePosition.insert(effectivePosition[static_cast<std::size_t>(i)]);
        }
    }

    std::vector<std::int32_t> order;
    order.reserve(static_cast<std::size_t>(passCount));

    while (!readyByEffectivePosition.empty()) {
        const std::int32_t nodePosition = *readyByEffectivePosition.begin();
        readyByEffectivePosition.erase(readyByEffectivePosition.begin());
        const std::int32_t node = effectiveOrder[static_cast<std::size_t>(nodePosition)];
        order.push_back(node);

        for (std::int32_t successor = 0; successor < passCount; ++successor) {
            if (kept[static_cast<std::size_t>(successor)] &&
                edgeExists[static_cast<std::size_t>(node)][static_cast<std::size_t>(successor)]) {
                if (--inDegree[static_cast<std::size_t>(successor)] == 0) {
                    readyByEffectivePosition.insert(effectivePosition[static_cast<std::size_t>(successor)]);
                }
            }
        }
    }
```

Everything else in `Compile()` (culling/reachability scan, lifetime
computation over `order`, the cycle-detection throw) is UNCHANGED — it
already only ever consumes `order`/`kept`/`edgeExists`, all still keyed by
original pass index, which this phase never alters the meaning of.

**Update every stale "declaration order"/"declaration index"/"declared so
far" comment this rewire invalidates — not just one sentence.** Re-grep
this file for the literal words "declaration" and "declared" once 3.2/3.3's
code changes above are in place, and fix EVERY hit that describes
ordering/dependency-resolution BEHAVIOR (a hit that's just an unrelated
struct/variable name is not in scope). Concretely, today that means at
least these four spots (line numbers approximate — re-locate by content,
not by number, they may have drifted):

- This file's own top-of-function header comment (the "Implementation
  note on cycle detection" paragraph, ~lines 37-55): BOTH (a) "the passes'
  own declaration order already being one valid topological order" ->
  "the passes' own EFFECTIVE (RenderPassEvent-then-declaration-order)
  order is one valid topological order", AND (b) the EARLIER sentence in
  the SAME paragraph — "only ever adds an edge from a STRICTLY LOWER
  declaration index to a STRICTLY HIGHER one (a pass can only depend on a
  writer that was already declared before it; there is no mechanism here
  for an earlier-declared pass to depend on a later-declared one)" — a
  SEPARATE, equally-stale claim about "declaration index"/"declared
  before" that must ALSO become "a strictly earlier EFFECTIVE POSITION to
  a strictly later one (a pass can only depend on a writer already
  WALKED, in effective order, before it...)". Do not fix only one of
  these two sentences and leave the other stale — they assert the exact
  same invariant, just in different words, and both describe the code
  3.3 above just changed.
- The RAW-edge comment immediately above the `pass.reads` loop (~lines
  109-113 today): "this pass reads whatever the most recently declared
  prior pass wrote to this resource" -> "the most recently WALKED (in
  effective order) prior pass".
- The WAW-edge comment immediately above the `pass.writes` loop (~lines
  136-139 today): "preserve that declaration order... become the new
  last writer for anything declared after this pass" -> "preserve that
  effective order... become the new last writer for anything walked
  after this pass in effective order".
- The Kahn's-algorithm section's own header comment (~lines 238-246
  today, starting "Kahn's algorithm, restricted to the kept subgraph..."):
  "Ties in the zero-in-degree \"ready\" set are broken by lowest original
  declaration index" -> "lowest effective position (RenderPassEvent-
  then-declaration-index)", and "pulling out the smallest ready index"
  -> "pulling out the smallest ready EFFECTIVE POSITION".

The cycle-freedom argument itself still holds after all of this (edges
are still only ever added from an earlier-walked pass to a later-walked
one, in whichever order is now being walked) — only WHICH order that is,
and the words used everywhere to describe it, need to change.

### 3.4 — Update PHASE1's diagnostic call site to use `effectiveOrder`

Change the one call to `DetectRenderPassEventContradictions()` PHASE1
added (Step 3.2 of that phase) to pass `effectiveOrder` instead of the
identity permutation it built locally — delete that phase's own
locally-built `declarationOrder` vector entirely and reuse this phase's
`effectiveOrder` (computed in 3.2 above, which now exists earlier in the
function). This means, from this phase onward, the detector reports
contradictions relative to the NEW effective order, not raw declaration
order — exactly matching what "before"/"after" now means for the
compiler's own real algorithm. This is the ONLY change PHASE1's own code
needs; `DetectRenderPassEventContradictions()`'s own body is untouched.

### 3.5 — Doc comments

Update `RenderGraphTypes.h`'s `RenderPassEvent` enum comment and
`PassRecord::renderPassEvent`'s field comment (both already touched once
by PHASE1's Step 3.3) with a SECOND update: the "purely descriptive...
NOT a dependency mechanism" sentence is no longer fully true and must be
corrected — replace it with something like: *"A real, load-bearing
ordering input as of the `render-pass-4` campaign's PHASE2:
`RenderGraphCompiler::Compile()` processes passes in a stable sort of
this field (ties broken by original declaration order) before building
its RAW/WAW dependency edges. It still cannot override a genuine
dependency in the opposite direction — a real data dependency always
wins — but it now determines execution order between passes that have NO
real dependency on each other, and it now determines which writer a read
resolves to when a naive declaration-order scan would otherwise miss it.
See `PHASE0_MASTER_STRATEGY.md`/`PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md`
under `task_manager/render-pass-4/` for the full history, including why
this alone still does not make an incorrectly-tagged pass safe — see
`DetectRenderPassEventContradictions()`."* Do the same update everywhere
else PHASE1's Step 3.3 already touched (`RenderPipeline.h`,
`RenderGraphBuilder.h`).

Also update `AGENTS.md`'s own "Render Pass System" section (the paragraph
describing `render-pass-3`) with one short new paragraph describing this
campaign's own existence and its one real behavior change — mirror the
"LOUD, DELIBERATE DEVIATION" callout style `render-pass-3`'s own
paragraph already uses for its Locked Design Decision 5. (If PHASE3 also
plans to touch `AGENTS.md`, coordinate — a single, correct edit is fine;
do not fight over which phase's diff includes it, just make sure it
happens exactly once, correctly, by the end of the campaign.)

### 3.6 — Tests

In `tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`, add:

- `ReaderTaggedEarlierThanItsTextuallyLaterWriterStillResolvesCorrectly` —
  **the permanent regression test for the real fix (mirrors PHASE1's own
  `OrphanReadWithLaterWriterIsDetected`, but calls the REAL `Compile()`
  end-to-end instead of just the detector)**: pass 0 (`"Composite"`,
  `RenderPassEvent::AfterTransparents`) reads a texture; pass 1
  (`"Opaque"`, `RenderPassEvent::Opaques`) writes it; `finalOutputs`
  includes Composite's own output. Before this phase, `"Opaque"` would be
  silently culled (reproduce this first, on the pre-PHASE2 code, to
  confirm the test would have failed before the fix — required by
  `AGENTS.md`'s "Fixing a bug... must add a regression test that fails
  before the fix and passes after it"). After this phase's change,
  assert `result.executionOrder` contains BOTH passes, in the order
  `"Opaque"` then `"Composite"`, and neither is culled.
- `TwoIndependentPassesWithNoSharedResourceExecuteInRenderPassEventOrderRegardlessOfDeclarationOrder` —
  declare pass 0 tagged `AfterOpaques` and pass 1 tagged `PreOpaques`,
  with NO shared resource between them at all (each writes its own,
  separate, `finalOutputs`-kept texture) — confirm `executionOrder` places
  pass 1 (`PreOpaques`) before pass 0 (`AfterOpaques`), i.e. declaration
  order (0 before 1) is overridden by `RenderPassEvent` order, exactly
  demonstrating this phase's own stated goal.
- `WriteAfterWriteAcrossDifferentRenderPassEventTiersResolvesByEffectiveOrderNotDeclarationOrder` —
  **a second, WAW-shaped regression test for this phase's own reorder** (the
  RAW test above only exercises the read-side "orphan" fix; this one
  exercises the write-side WAW edge-construction rewire directly, in 3.3,
  which the RAW test alone does not prove took effect): pass 0 (`"Late"`,
  `RenderPassEvent::AfterTransparents`, declared FIRST) writes a texture;
  pass 1 (`"Early"`, `RenderPassEvent::Opaques`, declared SECOND) writes the
  SAME texture; `finalOutputs` keeps that texture. Before this phase, raw
  declaration order processes pass 0 then pass 1 (an edge 0->1, i.e.
  `"Late"` ordered before `"Early"`) — after this phase, effective order
  processes pass 1 (`Opaques`) before pass 0 (`AfterTransparents`), so the
  edge direction REVERSES (1->0): assert `result.executionOrder` contains
  `"Early"` (pass 1) strictly before `"Late"` (pass 0), with neither culled.
- `PassesSharingTheSameRenderPassEventTierPreserveTheirOriginalDeclarationOrder` —
  confirm the stable-sort tie-break: two passes both tagged `Opaques`,
  with a real WAW dependency between them (mirrors
  `MultipleWritersToSameResourcePreserveWriteAfterWriteOrder`'s own
  existing test shape) — must still execute in their original declaration
  order, unchanged from before this phase.
- Re-run every PRE-EXISTING test in this file unmodified — every one of
  them uses `RenderPassEvent::Opaques` for every pass by default (since
  none of them ever set `.renderPassEvent` explicitly), so a stable sort
  by an all-equal key must reduce to a no-op and preserve every existing
  assertion byte-for-byte. If ANY pre-existing test's result changes,
  that is a real regression — stop and investigate before proceeding, do
  not "fix" the test's own expectation to match.
- Update PHASE1's own `EdgeContradictingDeclaredEventOrderIsDetected`/
  `OrphanReadWithLaterWriterIsDetected` tests only if their DIRECT calls
  to `DetectRenderPassEventContradictions()` need a different
  `processingOrder` argument to still exercise the intended scenario
  (they should not — those tests call the function directly with their
  own hand-built `processingOrder`, independent of `Compile()`'s own
  internal wiring — confirm this remains true, do not assume).

## Definition of Done

- `Compile()` builds and walks `effectiveOrder`/`effectivePosition` as
  described, and both the RAW/WAW edge loop and Kahn's-algorithm ready-set
  use it instead of raw declaration order.
- PHASE1's diagnostic call site now passes `effectiveOrder`, not a
  locally-built identity permutation.
- Every new test in Step 3.6 passes; every pre-existing test across
  `RenderGraphCompilerTests.cpp`, `RenderGraphSnapshotTests.cpp`,
  `RenderPipelineTests.cpp` still passes UNCHANGED.
- The Step 3.1 audit is written up in this phase's own
  `PHASE2_COMPLETION_REPORT.md`, including the declaration-order/
  `RenderPassEvent` table for every real pass in the engine today, and an
  explicit statement of whether any pass's effective position moved
  relative to today's shipping behavior (and if so, exactly which one,
  and what was done about it).
- Doc comments from Step 3.5 are updated, `AGENTS.md` reflects this
  campaign's one real behavior change.
- An incremental compile of `gte_core` and `GreatTamanaEngineTests`
  succeeds. A quick, targeted live smoke check (per PHASE0's cross-cutting
  rules: `run_app_background` + `gte_send_request /get_game_view` and
  `/get_swapchain`) confirms the real Game View/Scene View still render
  correctly (atmosphere, opaque geometry, sky, present all still visibly
  correct) — this is NOT the full build/regression pass (that is PHASE3),
  just a targeted sanity check that this phase's real behavior change
  didn't visibly break the one thing it touches.

## What We Will NOT Do

- Do NOT introduce resource "versions"/a multi-writer-per-resource
  tracking system, or any other structural rewrite of the compiler's
  RAW/WAW model — the stable-sort-by-effective-order approach is
  deliberately the smallest change that gives `RenderPassEvent` real
  teeth without that complexity.
- Do NOT change `RenderGraphBuilder.h`'s public surface, `RenderGraph.cpp`,
  or `RenderGraphBarrierPlanner.cpp` at all.
- Do NOT retag any real pass's `RenderPassEvent` value unless the Step 3.1
  audit finds a genuine, confirmed mismatch AND `ask_questions` confirms
  the user wants it fixed here rather than left for the detector to catch
  later.
- Do NOT run a full build or the full `ctest` suite in this phase — that
  is PHASE3's job specifically.
- Do NOT claim in any doc comment or completion report that this phase
  makes the historical bug class "impossible" — it converts it to "a
  wrong enum value on one pass, now detected", which is what actually
  happened; say that precisely.
