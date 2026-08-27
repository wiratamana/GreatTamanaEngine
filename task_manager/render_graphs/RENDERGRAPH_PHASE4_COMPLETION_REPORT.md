# RENDERGRAPH_PHASE4_COMPLETION_REPORT.md

Session report for **Phase 4 — Make Real GPU Pictures and Reuse Them**, the
fourth implementation chunk of the Render Graph campaign described in
`RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`. Scope was taken directly from
`RENDERGRAPH_PHASE4_PHYSICAL_REALIZATION_STRATEGY_v2.md` — nothing beyond
that document's own "Step 3: The Plan" was implemented, per its own "Step 4:
What We Will NOT Do".

## What shipped

Two new, additively-compiled files — nothing else in the engine was
touched, and nothing outside `src/Renderer/RenderGraph/` includes any of it
yet:

- **`src/Renderer/RenderGraph/RenderGraphResourcePool.h`/`.cpp`** — the
  class that turns Phase 3's still-virtual `gte::rg::TextureHandle`/
  `gte::rg::BufferHandle` (each backed only by a `TextureDesc`/`BufferDesc`)
  into a REAL, physical `gte::RenderTexture`/`gte::Buffer`, backed by
  `Renderer::CreateRenderTexture()`/`CreateBuffer()`:
  - **`RenderGraphResourcePool(Renderer& renderer)`** — takes the one
    `Renderer&` it needs to actually create GPU resources through; stores
    a raw, non-owning pointer (the pool's own lifetime is expected to be
    tied to something that already outlives/equals `Renderer`'s own, same
    convention as every other `Renderer&`-taking collaborator in this
    engine).
  - **`RenderTexture& AcquireTexture(const TextureDesc& desc, const char*
    debugName)`** — a linear scan over every pooled entry for the FIRST one
    that is both unclaimed this frame AND has a desc that equals `desc`
    (structural `TextureDesc::operator==`, never comparing `debugName` —
    see the "Verification against Phase 1 v2's regression concern" section
    below). If none matches, a fresh entry is created via
    `Renderer::CreateRenderTexture()` (forwarding `desc.width`/
    `desc.height`/`desc.format`/`debugName` straight through — `desc.format
    == VK_FORMAT_UNDEFINED` is passed through unresolved, since
    `CreateRenderTexture()` itself already treats that as "match
    `Renderer::ColorFormat()` exactly", keeping every "default format"
    request comparing equal to every other one) and appended.
  - **`Buffer& AcquireBuffer(const BufferDesc& desc, const char*
    debugName)`** — the symmetric counterpart. `BufferDesc` (Phase 1)
    carries no `BufferMemoryUsage` field (unlike
    `Renderer::CreateBuffer()`'s own required parameter), so a freshly
    created entry always uses `BufferMemoryUsage::GpuOnly` — the natural
    default for a render-graph-owned transient buffer with no CPU-access
    pattern declared in its desc. No real Phases 1-8 pass exercises this
    path yet (mirroring `RenderGraphBuilder::PassBuilder::ReadBuffer()`/
    `WriteBuffer()`'s own "for a future compute pass" comment) — this
    exists purely so `CreateBuffer()`'s `BufferHandle` has a real Phase 4
    counterpart, completing the same API surface `AcquireTexture()`
    already provides.
  - **`void BeginFrame()`** — marks every pooled entry (texture AND buffer)
    as "not yet claimed this frame", mirroring
    `FrameRecorder::BeginFrame()`'s own "clear last frame's queue before
    this frame re-populates it" spirit, applied to per-entry claim flags.
  - **Storage: `std::deque<TextureEntry>`/`std::deque<BufferEntry>`, not
    `std::vector`.** This is the one implementation detail worth calling
    out explicitly: `AcquireTexture()`/`AcquireBuffer()` return a
    REFERENCE into a pool entry, and a render graph pass author declaring
    several transient resources in the same frame must be able to hold
    every previously-returned reference without any of them dangling the
    moment a LATER, first-time `Acquire*()` call in the SAME frame appends
    a brand-new entry. `std::vector::push_back()`/`emplace_back()` may
    reallocate its entire backing store on growth, invalidating every
    existing reference into it — `std::deque` never does this for
    insertion at either end (only iterators are invalidated, never
    references/pointers to already-existing elements), so it was the
    correct container here, not `std::vector` with a pre-reserved
    capacity (which would just be trading one hazard for a silent,
    easy-to-violate assumption about an upper bound that was never
    actually enforced).
  - No stale-entry eviction/trim, no memory aliasing, no independent
    `GpuMemoryTracker`-like bookkeeping, and no name/label field
    reintroduced onto `TextureDesc`/`BufferDesc` — all four explicitly
    deferred/forbidden per the strategy document's own "Step 4: What We
    Will NOT Do" (see below).

## Build system changes

- Root `CMakeLists.txt`: added
  `src/Renderer/RenderGraph/RenderGraphResourcePool.h`/`.cpp` to
  `gte_core`'s source list, right after the existing
  `RenderGraphCompiler.h`/`.cpp` entry.
- `tests/CMakeLists.txt`: added an explanatory note to the file's existing
  "Tier 2 (GPU-dependent) tests: intentionally not implemented yet" comment
  block, naming `RenderGraphResourcePool.h`/`.cpp` and explaining why it
  joins that bucket (see "Verification performed" below) — **no new test
  file was added**, per this phase's own explicit "Step 4" scope fence (see
  below).

## Verification performed

This is the FIRST phase in the whole Render Graph campaign that actually
touches a live `VkDevice` (through `Renderer`), and therefore the first
phase that falls into this engine's already-accepted "Tier 2, no automated
GPU-backed test coverage yet" bucket (see `AGENTS.md`, "Testability &
Regression Safety") — exactly the same bucket `Buffer`/`RenderTexture`/
`Pipeline`/`GpuResourceFactory` themselves already live in. The strategy
document's own Step 4 explicitly rules out writing automated tests against
a live `VkDevice` for this phase, so no new test file was added — this was
a deliberate decision, not an oversight, and matches how `Buffer`/
`RenderTexture`/`Pipeline`/`GpuResourceFactory` were verified when they
were first added.

What WAS done:

- Reconfigured with CMake (reusing the existing `build/` Ninja
  configuration) — no network access needed, everything was already
  fetched.
- Built `GreatTamanaEngineTests` from the existing incremental build —
  compiled with zero warnings/errors introduced by the new files.
- Ran the **entire** test suite — **586 tests total**, **585 passed**, **1
  skipped** (`PmxLoaderRealModelSmokeTest.LoadsAnMmdModelIfPresentOnThisMachine`,
  the same pre-existing machine-gated smoke test noted in every prior
  phase's report, unrelated to this change). **Zero regressions.**
- Built the real `GreatTamanaEngine.exe` target too — succeeded cleanly,
  confirming the new files don't break the shipping executable's build
  (this also proves the new `.cpp` compiles correctly against a real
  `Renderer.h` include, since `GreatTamanaEngine.exe`'s link step pulls in
  every translation unit in `gte_core`, including a genuinely unreferenced
  one like `RenderGraphResourcePool.cpp` at this stage of the campaign).
- Manually reviewed `RenderGraphResourcePool.cpp`'s own matching logic (a
  linear scan for `desc == entry.desc && !claimedThisFrame`) against the
  strategy document's own reasoning — simple enough to verify by
  inspection, and this phase's accepted verification bar defers the REAL
  end-to-end proof (steady-state GPU resource count staying stable
  frame-to-frame in the Editor's "Memory" panel) to Phase 7, once
  `Application::Run()` actually drives this pool every frame — there is
  nothing to observe in the "Memory" panel yet, since nothing calls
  `AcquireTexture()`/`AcquireBuffer()` from production code at this stage
  of the campaign (see "What was deliberately NOT done" below).

## Acceptance criteria check (against the strategy document's own Step 3/Step 5)

- ✅ `AcquireTexture()`/`AcquireBuffer()` match purely on `desc ==
  entry.desc` — `debugName` is used ONLY when a brand-new entry is
  created, forwarded straight to `Renderer::CreateRenderTexture()`/
  `CreateBuffer()`'s own `debugName` parameter, and never participates in
  matching (verified by direct code inspection of both functions' own
  matching loop, which only ever reads `entry.desc`/`entry.claimedThisFrame`
  — `debugName` never appears inside either loop body at all).
- ✅ `BeginFrame()` resets every pooled entry's `claimedThisFrame` to
  `false`, for both textures and buffers independently.
- ✅ A pool entry, once claimed by any resource this frame, is excluded
  from matching again until the next `BeginFrame()` — guaranteed by the
  `!entry.claimedThisFrame` check inside both `Acquire*()` loops, and by
  never resetting the flag anywhere except `BeginFrame()`.
- ✅ An imported resource (Phase 2's `ImportTexture()`) never goes through
  this pool at all — `RenderGraphResourcePool` has no knowledge of
  `TextureImportInfo`/`RenderGraphBuilder` whatsoever; Phase 6's future
  execution engine is what will decide to skip this pool entirely for an
  imported handle, exactly as the strategy document specifies.
- ✅ `debugName`/GpuMemoryTracker integration is a genuine non-feature
  here — `debugName` threads straight into
  `Renderer::CreateRenderTexture()`/`CreateBuffer()`'s own parameter, with
  no separate/parallel tracking mechanism added anywhere in this file.
- ✅ No name/label field was reintroduced onto `TextureDesc`/`BufferDesc` —
  confirmed by re-reading `RenderGraphTypes.h`'s own "standing rule"
  comment before considering this phase done, per this phase's own Step 5
  guidance for whoever implements it.

## What was deliberately NOT done (per the strategy document's own Step 4)

- **No stale-entry eviction/trim policy.** A pool entry, once created,
  lives for the rest of this pool's lifetime unless a future phase
  explicitly adds this — mirroring `FramePresenter`'s own per-swapchain-
  image `DepthBuffer`s, which make the exact same judgment call. Flagged
  in Phase 9's backlog per the strategy document.
- **No memory aliasing.** `claimedThisFrame`'s conservative "at most one
  virtual resource per pool entry per frame" rule is the whole of this
  phase's memory-reuse story — no multiple pool entries ever share one
  physical `VkDeviceMemory` allocation based on non-overlapping lifetimes.
  Deferred to Phase 9.
- **No independent `GpuMemoryTracker`-like bookkeeping.** This pool relies
  entirely on `Renderer::CreateRenderTexture()`/`CreateBuffer()`'s
  EXISTING tracking registration — adding a second, parallel tracking
  mechanism here would violate `AGENTS.md`'s "GPU Resource Memory
  Tracking" rules.
- **No automated tests against a live `VkDevice`.** See "Verification
  performed" above — this phase's own strategy document explicitly rules
  this out; manual verification (deferred to Phase 7, once this pool is
  actually wired into production) is the accepted bar, exactly like
  `Buffer`/`RenderTexture`/`Pipeline`/`GpuResourceFactory` themselves.
- **No name/label field re-added to `TextureDesc`/`BufferDesc`.** See Phase
  1 v2's own standing rule, still fully in force — a resource's name stays
  a parameter, forever, never a compared-for-equality struct field.
- **No wiring into `Renderer`/`Application::Run()`/any production call
  site.** Nothing outside `src/Renderer/RenderGraph/` includes this header
  yet, and nothing calls `RenderGraphResourcePool::AcquireTexture()`/
  `AcquireBuffer()`/`BeginFrame()` from production code — Phase 6/7 are the
  first real consumers, exactly as planned.

## Handoff notes for whoever picks up Phase 5

- Phase 5 (`RENDERGRAPH_PHASE5_BARRIER_SYNTHESIS_STRATEGY_v2.md`) is the
  next unit of work in this campaign — automatic per-resource, per-pass
  barrier/layout-transition generation, replacing `FrameRecorder`'s
  hardcoded barriers. Per `RENDERGRAPH_PHASE0_MASTER_STRATEGY_v2.md`'s own
  V2 Revision Note 3, MRT (multi-color-attachment support) is explicitly
  OUT of Phase 5's MVP scope — narrowed to a single color attachment,
  matching literally every real pass in this campaign — and moved to
  Phase 9, to be built together with the matching `Pipeline`
  multi-format-attachment change it actually requires.
- Phase 5 will need to know each resource's CURRENT tracked layout to
  synthesize a correct `oldLayout`/`srcAccessMask` for its next barrier —
  this is not something `RenderGraphResourcePool` tracks today (it only
  tracks `desc`/`claimedThisFrame` per entry) — whoever implements Phase 5
  should decide whether that state belongs on a `TextureEntry` here, on a
  new per-resource tracking structure of Phase 5's own, or is derived
  fresh each frame from `CompiledGraph`'s already-known execution order —
  this phase deliberately left that decision open, since it wasn't needed
  to satisfy this phase's own acceptance criteria.
- `RenderGraphResourcePool`'s `AcquireTexture()`/`AcquireBuffer()` are only
  ever meant to be called once per virtual resource, per frame, by
  whichever future code (Phase 6's execution engine) resolves a
  `TextureHandle`/`BufferHandle` into its physical resource for the first
  time that frame — repeatedly calling `AcquireTexture()` for the SAME
  logical resource within the same frame (rather than caching the returned
  reference) would incorrectly claim a SECOND, different pool entry
  instead of returning the same one — Phase 6 must resolve each handle
  exactly once per frame and reuse the resulting reference for every pass
  that touches it that frame, never re-`Acquire*()` per pass.
- The `std::deque` storage choice (see "What shipped" above) is a real
  correctness requirement, not an implementation detail to casually
  "simplify" back to `std::vector` later — re-read this file's own header
  comment before ever touching `m_textureEntries`/`m_bufferEntries`'s
  container type.
