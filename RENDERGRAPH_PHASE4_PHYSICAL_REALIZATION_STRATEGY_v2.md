# RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md
### (Part 4 of 9 - see `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md` for the full campaign)

## V2 Revision Notes (2nd iteration review)

**This phase's own design was already correct in v1 - `AcquireTexture(const
TextureDesc& desc, const char* debugName)` already took the name as a
separate parameter from `desc`, not as a field inside it. The bug being
fixed elsewhere in this iteration (`RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md`'s
removal of `TextureDesc::debugName`) was that Phase 1's `TextureDesc`
ALSO, redundantly, carried a `debugName` field with `operator== = default`
- meaning two textures pooled by this phase's own matching logic
(`desc == entry.desc`) would have compared unequal purely because of a
differing debug-name pointer, silently defeating the ENTIRE pooling
mechanism this phase exists to build.** With Phase 1 v2 removing that
field, this phase's `AcquireTexture(desc, debugName)` now does exactly what
its v1 doc comment already claimed it did: match purely on
`desc.width/height/format/hasDepth`, with `debugName` used ONLY for the
downstream `Renderer::CreateRenderTexture()`/`GpuMemoryTracker::SetDebugName()`
call when a genuinely new pool entry has to be created - never as part of
the matching key. No code in this document needed to change as a result -
only this explanatory note, so a future reader understands exactly why
Phase 1 v2 exists and why this phase's own matching logic was at real risk
without it (the bug lived in `TextureDesc`, not in this phase's `AcquireTexture()`
signature - but this phase's whole value proposition depended on
`TextureDesc::operator==` being trustworthy, so it is very much this
phase's concern that it was fixed).

Everything else in this phase is unchanged from v1.

---

## Step 1: The Goal (Where are we going?)

Turn Phase 3's `CompiledGraph` (which still only talks about virtual
`TextureHandle`/`BufferHandle` values and their computed lifetimes) into
REAL, physical `RenderTexture`/`Buffer` objects, backed by
`GpuResourceFactory` - and, critically, do this WITHOUT allocating a brand
new `VkImage`/`VkDeviceMemory` every single frame for a resource whose
description hasn't changed since last frame. This is the first phase in the
campaign that actually touches a live `VkDevice`, and is therefore the
first phase that falls into this engine's accepted "Tier 2, no automated
GPU-backed test coverage yet" bucket (AGENTS.md, "Testability & Regression
Safety") - exactly the same bucket `Buffer`/`RenderTexture`/`Pipeline`/
`GpuResourceFactory` themselves already live in.

## Step 2: The Situation / The Problem (Where are we now?)

`FramePresenter` already contains the ONE precedent this whole phase
generalizes: its per-swapchain-image `DepthBuffer`s
(`m_depthBuffers`, `FramePresenter.h`) are deliberately allocated LAZILY,
only once `FrameRecorder::HasQueuedDraws()` proves they're actually needed,
and then kept alive and REUSED for the rest of that `FramePresenter`'s
lifetime rather than recreated every frame - see `EnsureDepthBuffersForSwapchain()`
in `FramePresenter.cpp`. That is exactly the shape a render graph's
transient-resource pool needs, generalized from "one specific, hardcoded
depth buffer" to "any resource, of any description, that any pass declares
via `CreateTexture()`/`CreateBuffer()`."

Without pooling, a render graph that faithfully allocated a fresh
`RenderTexture`/`Buffer` every single frame for every declared transient
resource would be a severe regression versus today's engine, which
allocates its Game/Scene `RenderTexture`s exactly ONCE (at construction, or
on resize - see `ImGuiEditorLayer`'s ownership of `m_gameView`/
`m_sceneView`) and reuses them for the graph's entire session. A render
graph's transient resources must match that same "allocate once, reuse
across frames, only reallocate when the DESCRIPTION actually changes (e.g.
a resize)" behavior, or it would trade correctness/flexibility for a
real, measurable performance/memory-churn regression - unacceptable per
this campaign's own goals. This is precisely why `TextureDesc::operator==`
being purely structural (see this document's own Revision Notes above, and
`RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md`) is load-bearing, not
academic - a wrong `operator==` here would have made this whole phase
silently do nothing useful while still looking, superficially, like it
worked (every `AcquireTexture()` call would just keep creating "new"
entries that never matched anything, one per call, forever).

## Step 3: The Plan (How will we get there?)

### 3.1 - New file: `src/Renderer/RenderGraph/RenderGraphResourcePool.h/.cpp`

```cpp
class RenderGraphResourcePool {
public:
    explicit RenderGraphResourcePool(Renderer& renderer); // needs CreateRenderTexture()/CreateBuffer()

    // Returns an existing pooled RenderTexture whose TextureDesc equals
    // `desc` and that is NOT currently claimed by an earlier pass THIS
    // frame (see 3.3's "concurrent lifetime" rule) - or creates a fresh
    // one via Renderer::CreateRenderTexture() if none qualifies. Never
    // returns a null/dangling reference; the pool owns every RenderTexture
    // it ever creates for its own entire lifetime (same "resize in place,
    // never destroy-then-later-need-again" ownership model as
    // ImGuiEditorLayer's own m_gameView/m_sceneView). `debugName` is used
    // ONLY when a fresh entry actually needs to be created (forwarded to
    // Renderer::CreateRenderTexture()'s own debugName parameter) - it is
    // NEVER part of the match key; matching is purely `desc == entry.desc`
    // (see this document's own Revision Notes for why this is now
    // guaranteed true rather than merely intended).
    RenderTexture& AcquireTexture(const TextureDesc& desc, const char* debugName);
    Buffer& AcquireBuffer(const BufferDesc& desc, const char* debugName);

    // Call once per frame, BEFORE realizing this frame's compiled graph -
    // marks every pooled resource as "not yet claimed this frame," the
    // same spirit as FrameRecorder::BeginFrame() clearing last frame's
    // queue before this frame re-populates it.
    void BeginFrame();
};
```

### 3.2 - Matching key: `(TextureDesc, claimed-this-frame == false)`

A pool entry is a `{TextureDesc desc; RenderTexture texture; bool
claimedThisFrame;}` record, kept in a plain `std::vector` (small N -
expect single-digit distinct transient resources for the foreseeable
future; a linear scan matching `desc == entry.desc && !entry.claimedThisFrame`
is simpler, more debuggable, and plenty fast at this scale than a hash
map - mirroring this engine's repeated "no hashing on the hot path, plain
array scan is fine at this N" judgment call already applied to
`ComponentStorage<T>` and `RenderGraphCompiler`'s own vector-indexed
graph). The FIRST matching, unclaimed entry is claimed and returned; if
none matches, a new entry is created via `Renderer::CreateRenderTexture()`/
`CreateBuffer()` and appended. **v2: this matching now works exactly as
described, because `TextureDesc` (Phase 1 v2) no longer carries any field
that isn't a genuine determinant of physical shareability - see this
document's own Revision Notes.**

### 3.3 - Why "claimed this frame" matters: concurrent resource lifetimes

Phase 3 computes a `firstUsePassIndex`/`lastUsePassIndex` PER RESOURCE, but
never checks whether two DIFFERENT resources' lifetimes overlap. Two
transient textures with the IDENTICAL `TextureDesc` (e.g. two same-sized
scratch color targets both alive during the SAME span of passes - a ping
and a pong buffer for a blur pass, say) must never be handed the SAME pool
entry while both are simultaneously live, or writes to one would corrupt
the other. `claimedThisFrame` is a simple, correct (if conservative -
Phase 9's aliasing work is where a lifetime-aware, non-conservative version
of this belongs) guard: once a `TextureDesc`-matching entry is claimed by
ANY resource this frame, it is excluded from matching again until
`BeginFrame()` resets every entry's claim next frame - guaranteeing a pool
entry is used by AT MOST ONE virtual resource per frame, which is trivially
memory-safe even though it under-utilizes potential aliasing opportunities
(an explicitly accepted, documented trade-off for the MVP - see Phase 9).

### 3.4 - Imported resources bypass the pool entirely

An `ImportTexture()`-declared resource (Phase 2) never goes through
`AcquireTexture()` at all - Phase 6's execution engine resolves it directly
to the externally-supplied `RenderTarget` (and now-explicit `currentLayout`
- see `RENDERGRAPH_PHASE2_BUILDER_API_STRATEGY_v2.md`) stored alongside its
`PassRecord`/resource-table entry. The pool exists strictly for
GRAPH-OWNED, transient resources - resources the graph itself is
responsible for the whole lifecycle of.

### 3.5 - Debug names and `GpuMemoryTracker` integration

`AcquireTexture()`/`AcquireBuffer()`'s `debugName` parameter threads
straight into `Renderer::CreateRenderTexture()`/`CreateBuffer()`'s own
`debugName` parameter exactly as any other call site already does - no new
mechanism needed here at all; this phase's only job is to make sure a
render-graph-owned resource shows up in the Editor's "Memory" panel
identically to a hand-created one, for free, simply by routing through the
exact same `Renderer` factory API every other GPU resource in this engine
already goes through. This is a deliberate non-feature: the render graph
does NOT get its own parallel memory-tracking mechanism. **v2: since Phase
2's builder now owns the ONE canonical name for a declared resource (its
`name` parameter to `CreateTexture()`/`CreateBuffer()`/`ImportTexture()` -
see Phase 2 v2), Phase 6's execution engine is what actually threads that
stored name through into this phase's `debugName` parameter when resolving
a virtual resource for the first time each frame - this phase itself
remains unopinionated about WHERE a name comes from, only that it is
forwarded correctly.**

### 3.6 - Resize handling

When a pass's declared `TextureDesc` for a logical resource changes size
frame-to-frame (e.g. the Editor's Game/Scene panel was resized, and the
NEXT frame's graph declares a `CreateTexture()` at the new resolution), the
OLD, now-non-matching pool entry is simply never claimed again - it becomes
a stale, still-allocated entry sitting in the pool. A trim policy (evict any
entry not claimed for N consecutive frames, calling `Untrack()`/destroying
it) is EXPLICITLY DEFERRED past this phase's MVP scope (see Step 4 below) -
for the MVP, a stale entry is accepted as a small, bounded memory cost
(bounded by "at most a handful of stale entries from the last few distinct
sizes this session has ever asked for"), consistent with this being a
Tier-2, judgment-call-driven area rather than a hard correctness
requirement.

## Step 4: What We Will NOT Do (Focus)

- We will **not** implement a stale-entry EVICTION/trim policy in this
  phase - a pool entry, once created, lives for the rest of the process
  unless explicitly noted otherwise. This is the exact same judgment call
  `FramePresenter`'s own per-swapchain-image `DepthBuffer`s already make
  (created once `EnsureDepthBuffersForSwapchain()` decides they're needed,
  never individually evicted even if a frame stops needing depth) -
  consistent, not a shortcut invented just for this phase. Flagged
  explicitly in Phase 9 as a real, worthwhile, deliberately-deferred
  follow-up once actual memory pressure from this is ever observed (mirror
  `TODO.md`'s own "revisit if that ever changes" pattern).
- We will **not** implement memory ALIASING (multiple pool entries sharing
  one physical `VkDeviceMemory` allocation because their claimed lifetimes
  never overlap) - `claimedThisFrame`'s conservative "at most one virtual
  resource per pool entry per frame" rule is the WHOLE of this phase's
  memory-reuse story. See Phase 9.
- We will **not** give the resource pool its own independent
  `GpuMemoryTracker`-like bookkeeping - it relies entirely on
  `Renderer::CreateRenderTexture()`/`CreateBuffer()`'s EXISTING tracking
  registration; adding a second, parallel tracking mechanism here would
  violate AGENTS.md's own "GPU Resource Memory Tracking" rules (one
  tracker, `Renderer` owns it, every resource type registers with THAT
  ONE).
- We will **not** write automated tests against a live `VkDevice` for this
  phase (see Phase 0's own Tier-2 acknowledgment) - manual verification (a
  debug build, driven through Phase 7's real Application integration once
  it lands, checked against the "Memory" panel to confirm no unexpected
  per-frame allocation churn) is this phase's accepted verification bar,
  exactly matching how `Buffer`/`RenderTexture`/`Pipeline` were verified
  when they were first added. **v2: this manual verification is the FIRST
  real end-to-end proof that Phase 1 v2's `TextureDesc` fix actually
  matters - if the Editor's "Memory" panel GPU resource count is NOT
  stable frame-to-frame at steady state once Phase 7 lands, re-check
  `TextureDesc::operator==` and every field flowing into a `TextureDesc`
  before suspecting anything else in this pool's own logic.**
- We will **not** re-introduce a name/label field into `TextureDesc`/
  `BufferDesc` to make this phase's own debug-name plumbing "simpler" -
  see Phase 1 v2's standing rule. `debugName` stays a parameter, forever,
  never a compared-for-equality field.

## Step 5: Their Role (What does this mean for you?)

- If you are implementing this phase, your single most important manual
  verification step is: with Phase 7 wired up (later), open the Editor's
  "Memory" panel (`Panels/MemoryPanel.cpp`) and confirm the GPU resource
  COUNT is STABLE frame-to-frame at steady state (no window resize, no
  scene change) - if it climbs every frame, the pool's matching logic has
  a bug (most likely: a `TextureDesc` that includes a field that
  incidentally differs frame-to-frame, e.g. an uninitialized/garbage
  field, defeating `operator==` - re-check Phase 1's `TextureDesc` for
  every field actually being deterministically set by every call site, and
  re-confirm no field has crept back in that shouldn't participate in
  equality - see Phase 1 v2's own standing rule for exactly this class of
  regression).
- Do not conflate this phase's `debugName` plumbing with a NEW debug-name
  concept - it must be the SAME `const char*`/`GTE_ENABLE_EDITOR`-gated
  mechanism `Buffer`/`RenderTexture` already have (AGENTS.md's "Human-
  readable debug names are Editor-only" rule) - if you find yourself
  writing a `#if GTE_ENABLE_EDITOR` guard inside `RenderGraphResourcePool`
  itself, stop: that guard already lives inside `Renderer`/
  `GpuMemoryTracker`, one layer down, and does not need to be duplicated
  here.
- When Phase 9's aliasing work eventually happens, it will almost
  certainly be built as a REPLACEMENT for this phase's `claimedThisFrame`
  matching rule, not an addition alongside it - keep that rule small and
  isolated (a single boolean per pool entry, checked in one place) so it is
  easy to find and replace wholesale later, rather than letting the
  "claimed" concept leak into `RenderGraphCompiler`/`RenderGraphBuilder`'s
  own code.
