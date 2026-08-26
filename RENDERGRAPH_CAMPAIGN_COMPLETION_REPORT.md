# RENDERGRAPH_CAMPAIGN_COMPLETION_REPORT.md

Campaign-level summary tying together all eight phases of the Render Graph
campaign described in `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`, now that
Phase 8 (`RENDERGRAPH_PHASE8_EDITOR_DEBUG_TOOLING_STRATEGY_v1.md`) has
landed — mirroring `PROFILER_IMPLEMENTATION_STATUS_v7.md`'s own role for the
Profiler campaign. Every phase below has its own detailed, standalone
`RENDERGRAPH_PHASEn_COMPLETION_REPORT.md` — this document is a map of the
whole journey, not a replacement for any of them.

## Why this campaign existed

The engine used to render its three fixed passes (Game View, Scene View,
Present) through a hand-wired, single-attachment, hardcoded-barrier pipeline
(`FrameRecorder::RecordFrame()`) that could not grow past exactly those
three passes without becoming more fragile with every new feature (shadow
maps, deferred shading, post-processing, ...). See
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own "Step 1: The Goal"/"Step 2:
The Situation" for the full diagnosis. This campaign replaced that hand-wired
pipeline with a genuine, declarative Render Graph: pure vocabulary and a
builder API (Phases 1–2), a dependency-resolving/culling compiler (Phase 3),
physical resource pooling (Phase 4), automatic barrier synthesis (Phase 5),
a real execution engine tying it all together (Phase 6), a full production
cut-over (Phase 7), and now, with Phase 8, an Editor debug panel making the
whole thing observable.

## The eight phases, in one line each

| # | Phase | One-line outcome |
|---|-------|-------------------|
| 1 | Teach the Engine New Words | `RenderGraphTypes.h` — handles, `ResourceAccess`, `TextureDesc`/`BufferDesc` (with the v2 "no `debugName` field" correctness fix), `PassRecord`. Pure, Vulkan-header-present-but-call-free, Tier-1-tested. |
| 2 | Let Developers Describe Drawing Jobs | `RenderGraphBuilder.h/.cpp` — `CreateTexture()`/`CreateBuffer()`/`ImportTexture()`/`AddPass()`, the declarative setup/execute authoring API. |
| 3 | The Smart Planner | `RenderGraphCompiler.h/.cpp` — dependency-graph construction, backward-reachability culling, deterministic topological sort, resource lifetimes. Pure graph algorithm, fully Tier-1-tested. |
| 4 | Make Real GPU Pictures and Reuse Them | `RenderGraphResourcePool.h/.cpp` — turns virtual handles into real, pooled/reused `RenderTexture`/`Buffer` objects. First Tier-2 (live-`VkDevice`) phase in the campaign. |
| 5 | Automatic GPU Safety Rules | `RenderGraphBarrierPlanner.h/.cpp` — `RequiredStateFor()`/`RequiresBarrier()` plus thin `VkImageMemoryBarrier2`/`VkBufferMemoryBarrier2` construction/emission, regression-matched field-for-field against `FrameRecorder.cpp`'s own hardcoded barriers. |
| 6 | Put Everything Together | `RenderGraph.h/.cpp` — `RenderGraph::Execute()`, tying Phases 1–5 together into real Vulkan recording, per-pass `DrawStats` (GPU timing deliberately deferred, see below). |
| 7 | Move the Real Engine to the Render Graph | The strangler-fig cut-over: `Application::Run()` now drives Game View/Scene View/Present entirely through two `RenderGraph::Execute()` calls per frame — the ONLY user-facing migration in the whole campaign, with zero observable behavior change by design. |
| 8 | Add a Debug Window in the Editor | The Editor's new "Render Graph" panel (`Panels/RenderGraphPanel.h/.cpp`) — a live view of which passes ran/were culled, their reads/writes/stats, and every resource's computed lifetime, backed by a new pure `RenderGraphSnapshot.h/.cpp` reshape. |

## Cumulative test coverage

Starting from 548 tests before this campaign began (Phase 1's own baseline)
to **623 tests** as of Phase 8 landing — every phase added Tier-1 coverage
for its own pure logic (Phases 1, 2, 3, 5's pure half, 8's snapshot reshape,
and 6's `RenderGraphNameSlotTable`), while Phases 4, 6 (Vulkan-call half),
and 7 remain Tier 2 (accepted, manually-verified — the same bucket
`Buffer`/`RenderTexture`/`Pipeline` themselves already occupy). **Zero
regressions were introduced at any phase boundary** — every phase's own
completion report confirms the full suite passing before and after its own
changes.

## What is genuinely proven vs. what remains open

**Proven, in shipped production code:**

- The engine's real Game View/Scene View/Present passes are declared,
  compiled, barrier-synthesized, and executed entirely through the Render
  Graph — not a parallel, unused code path (Phase 7).
- Automatic culling, deterministic ordering, and resource pooling all work
  correctly against the exact shapes this engine's own real passes need.
- Barrier synthesis produces field-for-field identical results to the old
  hand-written `FrameRecorder` barriers it replaced (Phase 5's own
  regression tests).
- The whole system is now observable — a human can open the Editor's
  "Render Graph" panel and see exactly what ran, in what order, and why a
  pass was culled (Phase 8).

**Still open, called out explicitly and repeatedly across Phases 6/7/8's own
"Known limitations"/"Handoff notes" sections — treat these as the real
backlog, not afterthoughts:**

1. **Real GPU timing is not wired up yet.** Every pass's GPU-timing sample
   is honestly `Absent` (never a fabricated value) — `GpuTimingService`'s
   fixed 3-slot `VkQueryPool` still needs to be generalized to match
   `RenderGraphNameSlotTable`'s already-exercised, arbitrary-name-keyed slot
   assignment. This is the single highest-priority follow-up.
2. **The campaign's own hardest, most novel capability — a genuine
   cross-pass texture READ, synthesized into a real barrier — has still
   never been exercised by a real, shipped pass**, exactly as
   `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own V2 Revision Note 4
   warned it might not be by the time Phases 1–8 finished. The
   already-scoped validation step (a Scene-view outline-highlight
   post-process reading the Scene view's own rendered color, from
   `TODO.md`) remains the natural, still-not-yet-attempted way to close
   this gap. **Until this lands, this campaign's investment should be
   considered "the machinery is real and correct" but not yet "proven
   end-to-end for the reason it was built."**
3. The old `Renderer::Present()`/`FramePresenter::Present()`
   (`FrameRecorder`-based) scaffold is confirmed unused by any production
   call site but was deliberately left in place rather than deleted (Phase
   7's own choice, to keep that already-large session's diff smaller) —
   cleanup remains open.
4. Phase 9's own backlog (`RENDERGRAPH_PHASE9_ADVANCED_FUTURE_SCOPE_STRATEGY_v2.md`)
   — memory aliasing, async compute, compute passes, temporal resources,
   full MRT + the matching `Pipeline` change — is explicitly NOT part of
   this MVP and was never attempted.

## Recommendation for whoever picks up the next session

Prioritize, in this order: (1) real GPU timing wiring, since it's now
directly visible as a permanent "N/A" in TWO Editor panels ("Profiler" and
"Render Graph") and is bounded, well-understood work; (2) the outline-
highlight cross-pass-read validation, since it's what actually proves this
whole investment paid for itself; (3) only then consider anything from
Phase 9's backlog, and only when a REAL, concrete need for it appears (a
G-buffer, an actual compute shader, ...) — never speculatively, per this
campaign's own repeatedly-stated discipline.
