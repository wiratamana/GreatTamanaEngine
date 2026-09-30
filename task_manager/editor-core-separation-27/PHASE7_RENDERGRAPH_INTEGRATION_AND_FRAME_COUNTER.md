# PHASE7 — `RenderGraph` Integration: Sibling Cache Member, Frame Counter, `Core::BuildFrame()` Wiring

Campaign folder: `task_manager/editor-core-separation-27/`

## Parent — MUST READ
`PHASE0_MASTER_STRATEGY.md`, Corrections 1 and 2, Locked Decisions 1, 2, 7.
Also re-read `PHASE6_COMPLETION_REPORT.md` — `RenderGraphPersistentResourceCache`
is now fully functional (construction, age, eviction, resize) but is NOT
yet reachable from `RenderGraphBuilder`/production code at all. This phase
makes it reachable; PHASE8 makes it USABLE by a real pass author.

## Step 1: The Goal (Where are we going?)

1. `RenderGraph` gains a sibling `m_persistentResourceCache` member
   (mirrors `m_resourcePool`'s exact ownership shape) and a new, dedicated,
   regime-agnostic `m_persistentResourceFrameCounter`.
2. `RenderGraph::BeginPersistentResourceFrame()` — a new public method,
   called EXACTLY once per real engine frame — advances that counter and
   drives the cache's own eviction sweep (`BeginFrame()`, PHASE5).
3. `RenderGraph::CurrentPersistentResourceFrameCounter()` — a new public
   getter, the counterpart of `CurrentDebugTextureFrameCounter()`.
4. `RenderGraphBuilder::SetPersistentResourceCache(cache, timingMode,
   currentFrame)` — a new setter (Locked Decision 1), wired into
   `RenderGraph::Execute()`'s template body right alongside the existing
   `SetDebugMetadataSink()` call. `currentFrame` is threaded through here
   (rather than the builder reaching back into `RenderGraph` some other
   way) because `RenderGraph::Execute()` already has BOTH
   `m_persistentResourceFrameCounter` and `timingMode` on hand at exactly
   the point it makes this one call — this is the single, natural
   hand-off point, and `RenderGraphPersistentResourceCache::Resolve()`
   (PHASE5) already takes `currentFrame` as a plain, explicit, per-call
   argument (deliberately NOT cached as hidden internal state on the
   cache itself — see PHASE5's own tests, which call `Resolve(..., frame)`
   directly without first calling `BeginFrame()`, proving this value must
   stay an explicit, per-call argument, never implicit cache state).
5. `Core::BuildFrame()` gains the ONE new call,
   `m_renderGraph.BeginPersistentResourceFrame();`, as its literal FIRST
   statement (Locked Decision 2/Correction 1) — the one, single, correct
   integration point outside `src/Renderer/RenderGraph/` this whole
   campaign needs.

When this phase is done: the cache is fully wired into every real engine
frame's lifecycle (evicting stale entries automatically) and every
`RenderGraphBuilder` a real pass author receives has a live, non-null
`m_persistentCache` pointer PLUS the current frame number available — but
`GetOrCreatePersistentTexture()` itself still does not exist (PHASE8).
This phase is pure plumbing, with NO new pass-author-facing capability
yet — deliberately kept separate from PHASE8 so its own "did I wire the
frame counter correctly" claim is trivially verifiable in isolation (see
`PHASE0_MASTER_STRATEGY.md`, Section 3.2's own reasoning for this split).

## Step 2: The Situation (Where are we now?)

- `RenderGraph::RenderGraph(Renderer& renderer)` (`RenderGraph.cpp`
  ~line 26-35), confirmed EXACT current body:
  ```cpp
  RenderGraph::RenderGraph(Renderer& renderer)
      : m_resourcePool(renderer)
      , m_timestampPool(QueryVulkanContextInfo(renderer).device, QueryVulkanContextInfo(renderer).graphicsQueue,
            QueryVulkanContextInfo(renderer).graphicsQueueFamily, QueryVulkanContextInfo(renderer).timestampCapability,
            kSynchronousTimingSlotBudget, kPipelinedTimingSlotBudget, kGpuTimingFramesInFlight)
  {
      m_renderer = &renderer;
  }
  ```
  `m_persistentResourceCache(renderer)` is added to this SAME
  member-initializer list, mirroring `m_resourcePool(renderer)` exactly
  (both take a plain `Renderer&`) — add it immediately after
  `m_resourcePool(renderer)`.
- `RenderGraph::Execute()`'s template body (`RenderGraph.h` ~line 175-193),
  confirmed EXACT current body:
  ```cpp
  template <typename BuildFn>
  void Execute(VkCommandBuffer cmd, ExecuteTimingMode timingMode, BuildFn&& build)
  {
      RenderGraphBuilder builder;
      builder.SetDebugMetadataSink(m_debugMetadataSink);
      if (m_debugMetadataSink != nullptr) {
          m_debugMetadataSink->BeginFrame();
      }
      const std::vector<TextureHandle> finalOutputs = build(builder);
      ExecuteCompiledGraph(cmd, timingMode, builder.Finish(), finalOutputs);
  }
  ```
  This is the ONE production call site that constructs a `RenderGraphBuilder`
  — every Tier-1 test's own bare `RenderGraphBuilder builder;` is
  completely unaffected either way (this phase does not touch
  `RenderGraphBuilder`'s constructor at all — only its NEW setter).
- `RenderGraphDebugTextureRegistry.h` (~line 31) already forward-declares
  `enum class ExecuteTimingMode : std::uint8_t;` specifically to avoid a
  circular include with `RenderGraph.h` — **this is the EXACT precedent
  `RenderGraphBuilder.h` copies** to gain its own new
  `SetPersistentResourceCache()` setter without `#include`-ing
  `RenderGraph.h` back (which already includes `RenderGraphBuilder.h`
  itself, so the reverse include would be circular).
- `RenderGraphBuilder.h` does NOT yet `#include
  "RenderGraphPersistentResourceCache.h"` (PHASE2 deliberately deferred
  this exact include to this phase) — `RenderGraphPersistentResourceCache.h`
  itself only depends on `RenderGraphTypes.h`/`RenderTexture.h`, so this
  new include introduces NO circularity.
- `Core::BuildFrame()` (`src/Core/Core.cpp` ~line 1148) — confirmed its
  current first real statements (~line 1158-1165) are the
  `gameTarget`/`sceneTarget`/`frameDebuggerCapture` lookups, BEFORE the
  `if (gameTarget != nullptr || sceneTarget != nullptr)` guard that wraps
  the actual offscreen `Execute()` call. The new call must land BEFORE
  even those lookups — it must run EVERY real frame, unconditionally,
  regardless of whether `gameTarget`/`sceneTarget` end up null this frame
  (source document's own requirement: "regardless of whether the offscreen
  `SynchronousImmediateReadback` regime runs at all that frame").
- `Core`'s own member, `rg::RenderGraph m_renderGraph;` (confirm exact name
  via `search_in_dir "RenderGraph m_renderGraph" Core.h` before editing) is
  what `BuildFrame()` already calls `.Execute(...)` on — the new call is
  `m_renderGraph.BeginPersistentResourceFrame();`.
- `RenderGraphResourcePool::BeginFrame()`'s own placement (`RenderGraph.cpp`
  ~line 505-508, inside `ExecuteCompiledGraph()`'s own `if (!isPipelined)`
  guard) is a DIFFERENT, unrelated frame-boundary concept — it fires once
  per real frame too, but from a completely different call site
  (`ExecuteCompiledGraph()`, gated on regime) than this phase's own
  `BeginPersistentResourceFrame()` (called from `Core::BuildFrame()`,
  BEFORE either regime's `Execute()` call runs at all). Do not confuse the
  two or try to merge them — the source document is explicit that this new
  counter must NOT be built on top of `m_debugTextureFrameCounter`'s
  regime-gated advancement (see `RenderGraph.h`'s own
  `CurrentDebugTextureFrameCounter()` doc comment for that field's accepted
  "Swapchain" freshness caveat, which this new counter must NOT inherit).

## Step 3: The Plan

### 3.1 — `RenderGraph.h`/`.cpp`

Add, as a new sibling member right after `m_resourcePool` (~line 489):

```cpp
// editor-core-separation-27 campaign, PHASE7 (BIG STEP 3 of 4) - mirrors
// m_resourcePool's exact ownership shape.
RenderGraphPersistentResourceCache m_persistentResourceCache;
```

Add `#include "RenderGraphPersistentResourceCache.h"` to `RenderGraph.h`'s
existing include block (alongside `RenderGraphResourcePool.h`).

Add, as a new sibling member right after `m_debugTextureFrameCounter`
(~line 595):

```cpp
// editor-core-separation-27 campaign, PHASE7 - a NEW, dedicated,
// regime-agnostic counter, deliberately SEPARATE from
// m_debugTextureFrameCounter (see this class's own CurrentDebugTextureFrameCounter()
// doc comment for why that one's regime-gated advancement would be wrong
// to reuse here - see BIG_STEP_3 Section 5.3/Section 8). Starts at 0;
// BeginPersistentResourceFrame() pre-increments, so its first-ever real
// value is 1 - matching PersistentResourceCacheEntry::lastRequestedFrame's
// own "0 is never a real frame" sentinel convention.
std::uint64_t m_persistentResourceFrameCounter = 0;
```

Add, as new public methods (near `CurrentDebugTextureFrameCounter()`):

```cpp
// editor-core-separation-27 campaign, PHASE7 - called EXACTLY once per
// real engine frame, by Core::BuildFrame(), as the very first thing it
// does, strictly before either ExecuteTimingMode regime's Execute() call
// runs that frame - see BIG_STEP_3 Section 8.
void BeginPersistentResourceFrame() noexcept;

// The counterpart of CurrentDebugTextureFrameCounter(), for THIS cache's
// own dedicated, regime-agnostic counter - a caller must use THIS value,
// never CurrentDebugTextureFrameCounter(), when computing
// RenderGraphPersistentResourceCache::FramesUntilEviction()'s own
// `currentFrame` argument.
std::uint64_t CurrentPersistentResourceFrameCounter() const noexcept { return m_persistentResourceFrameCounter; }
```

`RenderGraph.cpp` — add the new member-initializer (constructor body
itself needs no new statement — the default member initializer `= 0` is
enough), and implement:

```cpp
void RenderGraph::BeginPersistentResourceFrame() noexcept
{
    ++m_persistentResourceFrameCounter;
    m_persistentResourceCache.BeginFrame(
        m_persistentResourceFrameCounter, kPersistentResourceStaleThresholdFrames);
}
```

(`kPersistentResourceStaleThresholdFrames` is a NAMESPACE-scope
`inline constexpr` (deliberately NOT a class `static constexpr` member —
PHASE5's own corrected wording) declared in
`RenderGraphPersistentResourceCache.h` — already usable bare/unqualified
here via this file's own new include, exactly like `kNoNameSlot` already is
from `RenderGraph.cpp`/`RenderGraphTimestampPool.cpp`.)

Update `Execute()`'s template body to add the new setter call, right after
the existing `SetDebugMetadataSink()` line:

```cpp
builder.SetDebugMetadataSink(m_debugMetadataSink);
builder.SetPersistentResourceCache(&m_persistentResourceCache, timingMode, m_persistentResourceFrameCounter);
```

### 3.2 — `RenderGraphBuilder.h`/`.cpp`

Add, near the top of the file, alongside any other forward declarations
(mirrors `RenderGraphDebugTextureRegistry.h`'s own identical precedent —
cite it directly in the comment):

```cpp
namespace gte::rg {
enum class ExecuteTimingMode : std::uint8_t; // see RenderGraph.h - forward-declared to avoid a circular include (mirrors RenderGraphDebugTextureRegistry.h's own identical precedent).
}
```

Add `#include "RenderGraphPersistentResourceCache.h"` to this file's
existing include block (this header is NOT circular — confirmed in Step
2).

Add, as new private members, alongside `m_debugMetadataSink`:

```cpp
// editor-core-separation-27 campaign, PHASE7 - optional, nullable,
// zero-cost-when-absent, mirroring m_debugMetadataSink's exact shape (see
// SetPersistentResourceCache() below).
RenderGraphPersistentResourceCache* m_persistentCache = nullptr;
ExecuteTimingMode m_persistentCacheTimingMode = ExecuteTimingMode::SynchronousImmediateReadback;
std::uint64_t m_persistentCacheCurrentFrame = 0;
```

Add, as a new public method, alongside `SetDebugMetadataSink()`:

```cpp
// editor-core-separation-27 campaign, PHASE7 - forwarded into this
// builder by RenderGraph::Execute()'s own template body, immediately
// after constructing a fresh RenderGraphBuilder. `timingMode` is needed
// by GetOrCreatePersistentTexture()'s own regime-aware resize-refusal
// logic (PHASE8/FR4); `currentFrame` is needed by
// RenderGraphPersistentResourceCache::Resolve()'s own same-frame
// double-request guard and age-stamping (PHASE5/PHASE8) - bundled here
// (never a separate setter for either) since RenderGraph::Execute()
// already has all three values on hand at exactly the point it makes
// this one call.
void SetPersistentResourceCache(
    RenderGraphPersistentResourceCache* cache, ExecuteTimingMode timingMode, std::uint64_t currentFrame) noexcept
{
    m_persistentCache = cache;
    m_persistentCacheTimingMode = timingMode;
    m_persistentCacheCurrentFrame = currentFrame;
}
```

Do NOT add `GetOrCreatePersistentTexture()` itself in this phase — that is
PHASE8's own job, once this plumbing exists to build on.

### 3.3 — `Core::BuildFrame()`

Re-confirm `Core.h`'s exact `RenderGraph` member name (expected
`m_renderGraph`, confirm via `search_in_dir`), then add, as the LITERAL
FIRST statement inside `Core::BuildFrame()` (`Core.cpp` ~line 1148),
BEFORE the `gameTarget`/`sceneTarget` lookups:

```cpp
void Core::BuildFrame()
{
    // editor-core-separation-27 campaign, PHASE7 (BIG STEP 3 of 4) -
    // must run EXACTLY once per real engine frame, unconditionally,
    // strictly before either ExecuteTimingMode regime's Execute() call
    // this frame - see BIG_STEP_3 Section 8. Core::Present() (a SEPARATE
    // method, called AFTER this one returns by EditorHost::Run()'s own
    // per-frame loop) issues the OTHER regime's Execute() call - this
    // call must precede BOTH.
    m_renderGraph.BeginPersistentResourceFrame();

    RenderTexture* gameTarget = ...
    ...
}
```

## Step 4: Required Tests

1. **Tier-1** (no GPU): none of this phase's own new logic is pure/
   GPU-free in a meaningful new way — `BeginPersistentResourceFrame()`
   itself just forwards to already-tested (PHASE5) cache logic.
2. **Tier-2, headless fixture** (extend
   `RenderGraphPersistentResourceCacheTests.cpp`, or add a focused new
   block to it): using `HeadlessRenderGraphFixture`, construct a bare
   `rg::RenderGraph` directly (already how the fixture works) and confirm:
   - `CurrentPersistentResourceFrameCounter()` starts at 0 and becomes 1
     after exactly one `BeginPersistentResourceFrame()` call, 2 after a
     second, etc.
   - `RenderGraph::Execute()`'s own template body, called via
     `fixture.RunSynchronousFrame(...)`, does not crash/misbehave now that
     it also calls `SetPersistentResourceCache()` every time (a basic
     smoke check — an empty `build` lambda returning `{}` must still work
     exactly as before).
   - Full end-to-end confirmation that `SetPersistentResourceCache()`'s
     THREE values genuinely reach `GetOrCreatePersistentTexture()`
     correctly is deferred to PHASE8's own tests (that method does not
     exist until then) — note this deferral explicitly in the completion
     report; do not add a test-only public getter on `RenderGraphBuilder`
     purely to inspect these private fields in isolation.
3. **Live sanity** (optional, quick): run the Editor
   (`run_app_background`), confirm via `GET /get_logs` that no new
   warning/error appeared across a few real frames, then
   `stop_app_background`.

## Step 5: Rules recap

See `PHASE0_MASTER_STRATEGY.md`, Section 3.3, in full. No new files.
Incremental build + `ctest -R RenderGraphPersistentResourceCacheTest`. End
with `PHASE7_COMPLETION_REPORT.md` + git commit.
