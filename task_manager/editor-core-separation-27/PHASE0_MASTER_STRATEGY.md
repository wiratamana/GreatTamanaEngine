# PHASE0 — MASTER STRATEGY: "Persistent Resource Cache: Named, Cross-Frame Textures with Honest Layout Tracking"

Campaign folder: `task_manager/editor-core-separation-27/`
Branch: `feature/editor-core-separation` (stay on it, do not create a new branch)
Project root: `C:\Users\F5954\Documents\TAMANA\GreatTamanaEngine`

Source design document (read-only, in a SEPARATE repo/folder — never modify it):
`C:\Users\F5954\Documents\TAMANA\GreatTamanaEngin-Ideas\project-assembly-impl-5\BIG_STEP_3_PERSISTENT_RESOURCE_CACHE_HONEST_LAYOUT_HISTORY_REV2_2026-09-30.txt`
— this is BIG STEP 3 OF 4 of a larger series. BIG STEP 1 (Core/Editor
Separation: PassRecord Debug Metadata Sink) shipped as
`task_manager/editor-core-separation-25/`. BIG STEP 2 (Buffer Roots +
Blit/Copy Passes) shipped as `task_manager/editor-core-separation-26/` —
read that campaign's own `CAMPAIGN_COMPLETION_REPORT.md` once for
background/style precedent, but nothing in THIS campaign functionally
depends on it (BIG STEP 3 has no dependency on BIG STEP 2's own gaps —
confirmed by direct code read, see Step 2 below). BIG STEP 4 (GPU Memory
Aliasing) is NOT part of this campaign either — no dependency in either
direction (transient vs. persistent resources are disjoint subsystems).

Read the source document IN FULL before starting ANY phase below — this
file and its children summarize/sequence/correct it, they do not replace
it. **Every phase must re-verify every citation (file, line number,
function name) against the ACTUAL current file before relying on it** —
line numbers drift as earlier phases land real edits.

This file is the ORCHESTRATOR. Every child phase file (`PHASE1_*.md` ..
`PHASE9_*.md`) must be read together with this file. Every child phase
must, at its own start, re-read this file plus the completion report of
the phase immediately before it (`PHASEn-1_COMPLETION_REPORT.md`) — there
might be a clue for continuation, a discovered root cause, or a locked
decision that changes how the next phase must be carried out.

---

## Step 1: The Goal (Where are we going?)

Today, `RenderGraphBuilder` is a deliberately one-frame, throwaway object
(rebuilt from scratch every single `RenderGraph::Execute()` call). There is
no mechanism anywhere in `src/Renderer/RenderGraph/` for "give me back the
exact same physical texture I used last frame, by name, with its real
content intact." The only thing resembling reuse today
(`RenderGraphResourcePool`) matches by `TextureDesc` equality alone — it
returns the FIRST unclaimed pooled entry whose *shape* matches, never a
*specific, named* one, and it always leaves a resource's contents
undefined between frames. This is exactly why `RenderFeatureCompositor`
has to hand-roll its own private, per-(plugin,view) texture map
(`m_privateTargetStates`), its own name-interning pool
(`RenderFeatureNamePool`), and a manual `ImportTexture(..., VK_IMAGE_LAYOUT_UNDEFINED)`
call every frame — safe ONLY because every one of its own private textures
is fully overwritten before anything reads it. Every future feature that
needs "a texture that persists, addressed by name, with real content
surviving frame-to-frame" (TAA history, SSR history, motion vectors,
volumetric fog history) would otherwise have to reinvent this exact
machinery by hand, again, with no shared correctness guarantees.

When this campaign is done:

1. A new class, `RenderGraphPersistentResourceCache`, owned as a sibling
   member of `RenderGraph` (mirrors `m_resourcePool`'s ownership shape
   exactly), gives the render graph ONE reusable primitive: a persistent,
   named GPU color texture whose PHYSICAL identity is stable across many
   real frames, addressed by a mandatory, mechanically-collision-safe
   `(owner, name)` pair.
2. `RenderGraphBuilder::GetOrCreatePersistentTexture()` (two overloads —
   a plain one, and a fast, token-based one for a caller's own steady-state
   hot path) is the one, official, always-safe way any pass author reaches
   this primitive — usable exactly like an `ImportTexture()`-minted handle
   in `ReadTexture()`/`WriteTexture()`/`WriteColorAttachment()`.
3. A persistent texture's real color image layout survives HONESTLY across
   frames (never guessed, never hardcoded to `VK_IMAGE_LAYOUT_UNDEFINED`
   the way `RenderFeatureCompositor` does today) — a caller may treat it as
   a genuine history buffer.
4. A pass whose ONLY write is a persistent-cache texture handle is
   GUARANTEED to survive `RenderGraphCompiler::Compile()`'s culling pass,
   every frame, with zero action needed from the pass author beyond calling
   `GetOrCreatePersistentTexture()` itself.
5. Two unrelated `_v2`/`_v3` plugins or Project Assembly features can NEVER
   silently share one physical texture merely by picking the same
   human-readable name — collision safety is fully structural for that
   population (their `owner` is mechanically their own already-unique
   `descriptor.name`). Two built-in engine features choosing the same
   `owner` is reduced (not eliminated — this is stated honestly, not
   silently smoothed over) to a one-line-diff, single-shared-header,
   code-review-catchable mistake.
6. An unused entry is evicted automatically after a generous (300-frame
   default), named, tunable idle threshold — via a mechanism that keeps
   advancing regardless of which `ExecuteTimingMode` regime a build
   actually exercises, so this never becomes a permanent GPU-memory leak in
   a build that never runs the offscreen regime.
7. Any number of entries needing a resize within the same real frame are
   resized behind exactly ONE combined `vkDeviceWaitIdle()`, never one
   stall per entry.
8. A caller that (incorrectly) requests the same identity from both
   `ExecuteTimingMode` regimes within one real frame is genuinely REFUSED
   (invalid handle, logged once) at the point of the second request —
   never merely a same-frame log message issued after both regimes have
   already recorded real, racing GPU work.
9. A persistent entry with `desc.hasDepth == false` (required for v1 — see
   Non-Goals) carries EXACTLY ONE tracked GPU allocation (the color image),
   never an unused depth companion — a true GPU-memory statement, not just
   a render-graph bookkeeping one.
10. `RenderGraphBarrierPlanner.cpp` requires ZERO changes.
    `RenderGraphCompiler.cpp` requires exactly ONE small, additive
    condition in its existing root-marking scan. `RenderTexture`/
    `GpuResourceFactory::CreateRenderTexture()` gain exactly one small,
    trailing, defaulted parameter. Every pre-existing call site anywhere in
    the engine (production AND test) compiles and behaves byte-for-byte
    unmodified.
11. This whole campaign ships with real, AUTOMATED, GPU-backed regression
    coverage (a brand-new headless test fixture — see Locked Decision 3
    below) proving every one of the above live, on real hardware, every
    `ctest` run — not merely a one-off manual screenshot session.

### Explicit Non-Goals (v1 — do not build these, even if it looks easy)

- Persistent Buffer or VolumeTexture counterparts — texture-only for v1.
- A depth companion for a persistent texture — color-only for v1
  (`desc.hasDepth == false` is enforced, not just documented).
- Any change to `RenderGraphResourcePool` (the pre-existing, unbounded,
  never-evicted transient-resource pool) — it stays exactly as it is
  today; the new stale-check free function is written reusably, but
  applying it there is explicitly out of scope.
- Merging `ComputeShaderRead`+`ComputeShaderWrite` into one combined
  access kind to save one barrier on the canonical read-modify-write
  history-buffer shape — evaluated and explicitly deferred by the source
  document (Section 5.4); this campaign does not revisit that call.
- A Player Build Pipeline of any kind — out of scope for this whole
  repository right now (see `AGENTS.md`), unaffected either way by this
  campaign.

## Step 2: The Situation (Where are we now?)

Every file this campaign touches was re-read fresh, directly, immediately
before these phase files were written — confirmed, not assumed. Three
**material corrections** to the source document were found this way; every
phase file below is written against the CORRECTED understanding, not the
document's original wording:

### Correction 1 — the integration call site is `Core::BuildFrame()`, not `Application::Run()`

The source document repeatedly says to add the new "start of real frame"
call to `Application::Run()`. That class was deleted outright
(`editor-core-separation-1` campaign, PHASE17 — "Application Retirement and
Executable Rename") and replaced by `EditorHost` (composition
root/window/SDL/network glue) + `Core` (pure engine facade, owns
`m_renderGraph`). Confirmed by direct read:

- `Core::BuildFrame()` (`src/Core/Core.cpp`, ~line 1148) is the ONE place
  that calls `m_renderGraph.Execute(offscreenCmd,
  ExecuteTimingMode::SynchronousImmediateReadback, ...)` (~line 1172).
- `Core::Present()` (`src/Core/Core.cpp`, ~line 1557) is a genuinely
  SEPARATE method that calls `m_renderer.PresentViaRenderGraph(m_renderGraph, ...)`
  (~line 1566), which internally calls `graph.Execute(cmd,
  ExecuteTimingMode::PipelinedDeferredReadback, ...)`
  (`src/Renderer/FramePresenter.cpp`, ~line 436).
- `EditorHost::Run()`'s own per-frame loop (`src/Editor/EditorHost.cpp`)
  calls `m_core.BuildFrame();` at ~line 851, THEN `m_core.Present();` at
  ~line 948 — same loop iteration, `BuildFrame()` always strictly first.

Therefore: `renderGraph.BeginPersistentResourceFrame();` is added as the
literal FIRST statement inside `Core::BuildFrame()` — this is the one and
only call site, satisfies "strictly before either regime's `Execute()`
call runs", and needs zero `EditorHost.cpp` change at all. See PHASE7.

### Correction 2 — the cache is wired into `RenderGraphBuilder` via a SETTER, not a constructor parameter

The source document's Section 4 sketches
`explicit RenderGraphBuilder(RenderGraphPersistentResourceCache* cache = nullptr)`.
But its own prose, one paragraph later, says this pointer must follow "the
exact same shape as Step 1's `m_debugMetadataSink`" — and
`m_debugMetadataSink` is wired via a plain setter
(`RenderGraphBuilder::SetDebugMetadataSink()`, called by
`RenderGraph::Execute()`'s template body right after constructing a bare
`RenderGraphBuilder builder;` — confirmed at `RenderGraph.h` ~line 178-190).
`RenderGraphBuilder` has NO explicit constructor at all today (fully
implicit default). **Locked (confirmed with the user via `ask_questions`):
use a setter, `RenderGraphBuilder::SetPersistentResourceCache(cache,
timingMode)`, mirroring `SetDebugMetadataSink()` exactly — do NOT add any
new constructor to `RenderGraphBuilder`.** See PHASE7/PHASE8 for the exact
shape (the setter also carries `ExecuteTimingMode`, needed for FR4's
regime-aware resize-refusal — see Locked Decision 6 below, Step 3.1).

### Correction 3 — `RenderTexture::Create()` unconditionally builds a depth companion; TR5's new parameter is real, not incremental

Confirmed by direct read of `src/Renderer/RenderTexture.cpp` (~line 191):
`m_depthBuffer = std::make_unique<DepthBuffer>(...)` runs unconditionally,
every single `Create()` call, regardless of any render-graph-level
`TextureDesc::hasDepth` value. `RenderTexture`'s constructor
(`RenderTexture.h` ~line 86) and `GpuResourceFactory::CreateRenderTexture()`
(`GpuResourceFactory.h` ~line 70) both need a genuinely NEW, trailing,
defaulted `bool createDepthCompanion = true` parameter — confirmed nothing
like this exists yet. See PHASE1.

### Additional confirmed facts (not corrections — just load-bearing evidence)

- `RenderGraphResourcePool` (`RenderGraphResourcePool.h`) is the exact
  ownership-shape precedent (`RenderGraph::m_resourcePool`,
  `RenderGraph.h` ~line 489) this new cache mirrors — `std::deque` is used
  there specifically for pointer/reference stability across a same-frame
  `emplace_back()`; this new cache needs the equivalent guarantee but for a
  container keyed by `std::string`, so it uses
  `std::unordered_map<std::string, Entry>` instead (node-based — the
  standard guarantees a rehash never invalidates a reference/pointer to an
  existing key or value).
- `RenderGraphNameSlotTable.h`'s own file-header comment documents, in
  full, a REAL, ALREADY-FIXED `AccessViolation` crash from exactly the
  hazard Section 6 of the source document warns about (a raw `const char*`
  name outliving a Project Assembly's `FreeLibrary()`'d `.dll` image) — the
  fix there (own a `std::string` copy) is the exact discipline this new
  cache's own map key must follow from day one.
- `RenderTexture::m_debugName` (`RenderTexture.h` ~line 152) is a bare,
  NON-OWNED `const char*`, captured once in the constructor and re-attached
  unchanged on every `Resize()` — confirmed by direct read of both the
  constructor and `Resize()`/`Create()` in `RenderTexture.cpp`. This is
  why the cache's own key string must live at a permanently STABLE address
  (an `std::unordered_map`'s node, never a `std::vector` element) — see
  PHASE4.
- `RenderGraphCompiler.cpp`'s Step 2 root-marking scan (~line 515-544)
  already has the EXACT precedent this campaign's own compiler fix copies
  verbatim: `ContainsVolumeTextureHandle()`/`input.finalVolumeTextureOutputs`
  (Atmosphere Scattering Phase 6) and `ContainsBufferHandle()`/
  `input.finalBufferOutputs` (`editor-core-separation-26`, PHASE1) are two
  already-shipped, byte-for-byte identical instances of "add one more
  opt-in root-set vector, checked inside the existing `DispatchByKind()`
  call". See PHASE3.
- `RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp` ~line 495-879)
  already has the EXACT call-site precedent this campaign's own
  honest-layout-recording loop and batched-resize flush attach to:
  `RegisterDebugTextureSnapshots(timingMode, input, physicalTextures);`
  (~line 832) runs unconditionally, every call, right after the per-pass
  execution loop and right before this call's `RenderGraphSnapshot` is
  built. `m_resourcePool.BeginFrame()` (~line 506) already shows the
  established `if (!isPipelined) { ... }` gating this campaign's own
  `FlushPendingResizes()` call reuses verbatim. See PHASE8.
- `RenderGraphTypes.h`'s `TextureDesc` (~line 278) already has `hasDepth`
  and a purely-structural `operator==` with NO `debugName` field — nothing
  to change here; the persistent cache's own combined `(owner, name)` key
  is threaded as a wholly separate parameter, exactly matching this file's
  own "standing rule" comment.
- `PassKind::Blit`/`BlitSpec` (`editor-core-separation-26`) already
  shipped and are COMPLETELY UNRELATED to this campaign — no interaction,
  confirmed by direct read of `RenderGraphTypes.h`/`RenderGraph.cpp`.
- A real, already-working HEADLESS GPU test fixture exists:
  `tests/Fakes/HeadlessSurfaceProvider.h` (`editor-core-separation-1`
  campaign, PHASE18) provides a real `VkSurfaceKHR` via
  `VK_EXT_headless_surface`, with no window/SDL/ImGui involved. Five
  existing test files already use it to construct a real, headless
  `gte::Core` and `GTEST_SKIP()` cleanly if the local Vulkan
  driver/loader lacks the extension — but NONE of them drive a real
  `RenderGraph::Execute()` frame through it yet (they only exercise
  bookkeeping methods like `RegisterProjectFeature()`). Critically,
  `Renderer`'s own constructor is `explicit Renderer(ISurfaceProvider&
  surfaceProvider);` — ONE parameter — meaning a test can build a real,
  live `Renderer` (and therefore a real `RenderGraph`) directly, with NO
  `Core`/`Game`/`EditorLayer` involved at all. **Locked (confirmed with the
  user via `ask_questions`): this campaign builds a new, small, reusable
  headless test fixture on top of this precedent
  (`tests/Fakes/HeadlessRenderGraphFixture.h`) and uses it as the PRIMARY,
  AUTOMATED proof mechanism for every live-GPU acceptance criterion in the
  source document — a genuine upgrade over every prior Vulkan-touching
  RenderGraph campaign, which relied solely on a human running the live
  Editor and taking a screenshot.** See PHASE4.
- New test files are NOT auto-discovered — they must be added explicitly
  to the root `CMakeLists.txt`'s test-source list (confirmed: every
  existing test file, e.g. `Core/Plugins/RenderFeatureCompositorProjectFeatureTests.cpp`
  at line 2231, `Renderer/RenderGraph/RenderGraphNameSlotTableTests.cpp` at
  line 2313, is listed there explicitly). Every phase below that adds a new
  test `.cpp` file must also add its one line to this list.
- No naming collision: `search_in_dir` for `PersistentResourceCache`
  across `src/` returns zero hits — every new type name this campaign
  introduces is genuinely unused today.

## Step 3: The Plan (detailed strategy)

This campaign is split into 9 implementation phases, each its own `.md`
file in this same folder, plus this PHASE0 orchestrator.

| Phase | File | One-line summary |
|---|---|---|
| 1 | `PHASE1_RENDERTEXTURE_CREATEDEPTHCOMPANION_PARAMETER.md` | TR5: `RenderTexture`/`GpuResourceFactory::CreateRenderTexture()` gain a trailing, defaulted `createDepthCompanion` bool. Fully isolated, zero interaction with anything else this campaign does. |
| 2 | `PHASE2_TOKEN_OWNERS_HEADER_AND_BUILDER_SIDE_LIST.md` | New vocabulary: `RenderGraphPersistentResourceOwners.h`, `PersistentTextureCacheToken`, the pure `IsStaleCacheEntry()` free function (+ Tier-1 test), and the additive `CompiledGraphInput::persistentCacheTextures` / builder-side private vector (data-only, mirrors `finalVolumeTextureOutputs` shape — no cache logic yet). |
| 3 | `PHASE3_COMPILER_ROOT_MARKING_FIX.md` | `RenderGraphCompiler::Compile()`'s Step 2 gains the one-line `input.persistentCacheTextures` root check — the keep-alive guarantee (Section 5.2). Fully independent of the cache class itself; Tier-1 tested with a hand-fabricated `CompiledGraphInput`. |
| 4 | `PHASE4_HEADLESS_TEST_FIXTURE_AND_CACHE_CONSTRUCTION.md` | The new `HeadlessRenderGraphFixture` test harness, PLUS `RenderGraphPersistentResourceCache`'s own construction/ownership/exception-safety core (Section 6, 6.1, 6.2, 7) — the biggest, riskiest phase in this campaign. |
| 5 | `PHASE5_CACHE_AGE_TRACKING_DOUBLE_REQUEST_GUARD_AND_EVICTION.md` | Adds age-stamping, the same-real-frame double-request refusal (Section 5.3), the debug-only token-identity misuse guard (Section 4), eviction (Section 8), and `FramesUntilEviction()` (Section 9) onto the PHASE4 cache. |
| 6 | `PHASE6_CACHE_BATCHED_RESIZE.md` | Bounded, batched resize — exactly one `vkDeviceWaitIdle()` per real frame no matter how many entries need resizing (Section 5.5, FR4/FR8), plus the regime-aware refuse-if-pipelined rule. |
| 7 | `PHASE7_RENDERGRAPH_INTEGRATION_AND_FRAME_COUNTER.md` | `RenderGraph` gains the sibling `m_persistentResourceCache` member, `m_persistentResourceFrameCounter`, `BeginPersistentResourceFrame()`, `CurrentPersistentResourceFrameCounter()`; `RenderGraphBuilder::SetPersistentResourceCache()` setter wired into `RenderGraph::Execute()`; `Core::BuildFrame()` gains the one new call (Correction 1). |
| 8 | `PHASE8_BUILDER_GETORCREATEPERSISTENTTEXTURE_AND_HONEST_LAYOUT_WIRING.md` | The real, producible `RenderGraphBuilder::GetOrCreatePersistentTexture()` (both overloads) + `RenderGraph::ExecuteCompiledGraph()`'s tail hook (`RecordFinalLayout()` loop + `FlushPendingResizes()` call) — this is where the whole feature becomes observably real for the first time. |
| 9 | `PHASE9_FULL_ACCEPTANCE_AND_CLOSEOUT.md` | Every acceptance-criteria checkbox from the source document re-confirmed with fresh, live, automated evidence (via the PHASE4 fixture); full clean build; full `ctest` regression; `CAMPAIGN_COMPLETION_REPORT.md`. |

### 3.1 — Locked Decisions (apply to every phase, do not re-litigate)

Resolved with the user, via `ask_questions`, BEFORE any phase file below
was written:

1. **`RenderGraphBuilder::SetPersistentResourceCache(RenderGraphPersistentResourceCache* cache, ExecuteTimingMode timingMode)`
   is a plain setter, mirroring `SetDebugMetadataSink()` exactly — NOT a
   constructor parameter.** See Correction 2 above. `timingMode` is
   threaded through alongside the cache pointer (not a separate setter)
   because the ONLY thing that ever needs it is
   `GetOrCreatePersistentTexture()`'s own regime-aware resize-refusal logic
   (FR4) — bundling it here means `RenderGraph::Execute()`'s template body
   (which already has `timingMode` as its own parameter) sets both in one
   call, right next to where it already sets `SetDebugMetadataSink()`.
2. **`Core::BuildFrame()` gets the new `renderGraph.BeginPersistentResourceFrame();`
   call, as its literal first statement — NOT `Application::Run()`
   (deleted) and NOT `EditorHost::Run()`.** See Correction 1 above.
3. **This campaign builds a brand-new, reusable, headless (no window/SDL/
   ImGui) automated GPU test fixture,
   `tests/Fakes/HeadlessRenderGraphFixture.h`, and uses it as the PRIMARY
   proof mechanism for every live-GPU acceptance criterion** — confirmed
   with the user as worth the extra upfront investment specifically
   because it is reusable by every future history-buffer feature (TAA,
   SSR, BIG STEP 4's aliasing work) and turns this campaign's own
   correctness guarantees into real, permanent, automatically-re-checked
   `ctest` entries instead of one-off manual screenshots. A short, final
   live-Editor smoke check (PHASE9 only) still happens, but only as a
   cheap sanity pass on top of the automated suite, never as the primary
   proof.
4. **Default stale-eviction threshold is exactly 300 frames**
   (`kPersistentResourceStaleThresholdFrames`, PHASE5), matching the source
   document's own explicit recommendation (confirmed with the user) — a
   false-positive eviction costs a real GPU stall + a history reset, which
   is strictly worse than a few extra MB idling a little longer.
5. **`RenderGraphPersistentResourceCache`'s internal storage is
   `std::unordered_map<std::string, Entry>`** (TR4) — never a
   `std::vector<Entry>` (would relocate every existing element on growth,
   silently dangling every `RenderTexture::m_debugName` pointer already
   handed out) and never a `std::deque` (the map itself IS the stable-key
   container here; `RenderGraphResourcePool`'s `std::deque` solves a
   different problem — value stability for an UNKEYED sequence — that
   does not apply to a class whose entries are looked up BY the very key
   whose address must stay stable).
6. **The combined identity key is built exactly once, as
   `std::string(owner) + "::" + name`, entirely inside
   `RenderGraphPersistentResourceCache::Resolve()`** (never inside
   `RenderGraphBuilder`) — every validation (null cache aside, which stays
   a `RenderGraphBuilder`-level check since it is about builder wiring, not
   cache internals), including empty owner/name, `desc.hasDepth == true`,
   the same-frame double-request refusal, and the regime-aware resize
   refusal, also lives inside `Resolve()` — ONE place, shared by both
   `GetOrCreatePersistentTexture()` overloads, so the token-based fast path
   can never accidentally skip a check the slow path enforces.
7. **`RenderGraphPersistentResourceCache::BeginFrame(std::uint64_t
   currentFrame, std::uint64_t staleThresholdFrames)` is the ONE method
   `RenderGraph::BeginPersistentResourceFrame()` calls** — it both records
   `currentFrame` for this frame's double-request detection (Section 5.3)
   AND runs the eviction sweep (Section 8) in the same call, mirroring
   `RenderGraphResourcePool::BeginFrame()`'s identical naming/placement
   convention.

### 3.2 — Why this shape (nine phases, not fewer/more)

- PHASE1 is fully self-contained and lands first — nothing else in this
  campaign can be correctly tested end-to-end (Section 9's "exactly one
  tracked allocation" acceptance check) without it, but it has zero
  dependency on anything else here.
- PHASE2 and PHASE3 are both pure-data/pure-function additions with **zero
  live-GPU involvement** — deliberately front-loaded, before the risky
  PHASE4, so the compiler fix (the single most safety-critical change in
  this whole campaign, per the source document's own Section 5.2 framing)
  gets its own dedicated, low-risk, Tier-1-tested phase, fully decoupled
  from whether the cache class itself works yet. This mirrors
  `editor-core-separation-26`'s own PHASE1 (Buffer Roots) being deliberately
  first and self-contained for the identical reason.
- PHASE4 is, by a wide margin, the single highest-risk phase in this
  campaign (mirrors PHASE6's role in `editor-core-separation-26`) — the
  first phase that touches a live `VkDevice`, the first phase that needs
  the new headless test fixture to exist at all, and the phase that
  implements the most novel, most exception-safety-sensitive piece (Section
  6.1's two-phase construction recipe). Kept as its own phase, deliberately
  SEPARATE from age-tracking/eviction (PHASE5) and resize (PHASE6), so a
  build failure or a wrong-exception-safety bug is easy to bisect to
  exactly one phase.
- PHASE5 and PHASE6 are split from PHASE4 (and from each other) because
  they are genuinely different correctness concerns — age/liveness
  bookkeeping vs. bounded-cost mutation — each with its OWN dedicated
  acceptance tests (order-independence for PHASE5; the "N entries, one
  stall" proof for PHASE6) that must not be diluted by being folded into
  the already-large PHASE4.
- PHASE7 and PHASE8 are split because PHASE7 is pure `RenderGraph`-level
  plumbing (frame counter, eviction driver, the one new `Core::BuildFrame()`
  call) with NO new pass-author-facing capability yet, while PHASE8 is
  where the feature becomes REAL and observable (the actual entry point a
  pass author calls, and the honest-layout-recording/batched-resize-flush
  tail hook). Keeping them separate means PHASE7's own "did I wire the
  frame counter correctly" claim is trivially verifiable in isolation
  before PHASE8's much larger, feature-complete integration test runs.
- PHASE9 is always last, matching every prior campaign in this codebase's
  own history — full regression + the literal acceptance-criteria
  checklist tick-through, once, at the end, never spread across phases.

### 3.3 — Rules (apply to every phase, no exception)

1. **Every phase must, at its start, use `ask_questions` for any genuine
   ambiguity it personally discovers** beyond what this file and its own
   phase file already resolve. **Every task an implementation phase itself
   delegates must ALSO be instructed to use `ask_questions`** for its own
   ambiguities — this applies recursively, with no exception, to every
   `dispatch_sub_agent` call made anywhere in this campaign (implementation
   phases may ONLY use `dispatch_sub_agent`, never `delegate_task` — see
   Rule 4).
2. **Never use `printf`/`std::cout`/`fprintf`/`OutputDebugString` for any
   NEW diagnostic or permanent code in this campaign.** Always
   `GTE_LOG_DEBUG`/`_INFO`/`_WARNING`/`_ERROR` (`src/Core/Logging.h`),
   retrieved via `GET /get_logs` (`gte_send_request`) — a hard, repo-wide
   rule (`AGENTS.md`), not new to this campaign. (The one pre-existing
   `std::fprintf(stderr, ...)` inside `RenderGraphCompiler.cpp`'s
   `DetectRenderPassEventContradictions()`/`Compile()` machinery is
   untouched, pre-existing code — do not copy its style for anything NEW,
   and do not "fix" it either; it is out of scope.)
3. **Never run a full clean build or full `ctest` regression pass except in
   PHASE9.** Every other phase uses an INCREMENTAL build (`cmake --build
   build`) as its compile-check gate, plus a TARGETED `ctest -R <filter>`
   for whatever new/changed test(s) that phase itself owns. Use
   `run_app_background`/`gte_send_request`/`stop_app_background` for any
   live, HTTP-driven debugging a phase genuinely needs (`GET /get_logs`,
   `GET /render_graph`, `GET /get_texture`, `GET /list_textures`) — never a
   blind guess. Always `stop_app_background` whatever you
   `run_app_background`'d, every time, before ending the phase.
4. **Implementation-phase agents (i.e. whoever is actually executing
   PHASE1-9) may ONLY use `dispatch_sub_agent` to delegate any piece of
   their own work — NEVER `delegate_task`.** `delegate_task` is reserved
   exclusively for the orchestration layer sequencing PHASE1 → ... → PHASE9
   as whole, separate task steps. Use `dispatch_sub_agent` freely for a
   self-contained side-quest (diagnosing a build failure, a targeted
   grep-and-report, a git operation) OR to independently double-check this
   phase's OWN just-finished, large piece of work before writing its
   completion report (PHASE4 is this campaign's own flagged
   heaviest/highest-risk phase — the most likely candidate for this, per
   Note 2 of this campaign's own governing instructions). Such a
   `dispatch_sub_agent` double-check must NEVER create its own separate
   report file — its final report comes back inline, and the ORIGINAL
   phase still writes the one `PHASEn_COMPLETION_REPORT.md`.
5. **Every phase that changes `gte_core`-tier logic must add or extend a
   test wherever the underlying problem allows it** (`AGENTS.md`'s
   "Testability & Regression Safety" section) — every phase file below
   spells out its own exact required test cases, and states plainly
   whether that coverage is Tier-1 (pure, no GPU) or the new
   fixture-backed Tier-2 (real GPU, headless, automated).
6. **Every phase must end with**: an incremental compile check succeeding,
   a targeted `ctest` pass for that phase's own new/changed tests, a `.md`
   completion report (`PHASEn_COMPLETION_REPORT.md`) written into this same
   folder, and a git commit (`git_add` + `git_commit`) covering both the
   code change and the report.
7. **A new test `.cpp` file must be added to the root `CMakeLists.txt`'s
   explicit test-source list** (see Step 2's own confirmed fact above) — if
   a phase adds a new test file and forgets this, the incremental build
   will silently not run it; always confirm the new test actually executes
   (via a targeted `ctest -R` filter) before ending the phase, never just
   that it compiled.
8. **`TR2`/`TR3` ("every existing call site compiles and behaves
   unmodified") is the one hard constraint that makes this whole campaign
   safe.** If any phase discovers a pre-existing call site
   (`RenderGraphBuilder builder;`, `renderer.CreateRenderTexture(...)`,
   `RenderGraphResourcePool::AcquireTexture()`, etc.) that would need to
   change its own behavior, STOP and use `ask_questions` before proceeding.
9. **Every phase must re-read this file's Step 2 "Situation" section and
   its own phase file's exact citations, then re-confirm each one against
   the ACTUAL current file before relying on it.** Re-search
   (`search_in_dir`/`read_line`), never assume.
10. **No source file outside this campaign's own new files
    (`RenderGraphPersistentResourceOwners.h`,
    `RenderGraphPersistentResourceCache.h/.cpp`,
    `tests/Fakes/HeadlessRenderGraphFixture.h`, and one new test `.cpp`) is
    expected to be newly created.** Every other change is either a new
    member/method added to an EXISTING file, or a new test case added to an
    EXISTING test file. If any phase discovers it genuinely needs another
    new file, STOP and use `ask_questions` before proceeding.

### 3.4 — Definition of Done for the whole campaign

Identical to the source design document's own Section 12 ("ACCEPTANCE
CRITERIA") checklist, re-stated and re-confirmed with FRESH, LIVE evidence
by PHASE9 — using the PHASE4 headless fixture as the primary proof for
every item that needs a real `VkDevice`, per Locked Decision 3. When this
is green, `editor-core-separation-27` is closed for good, and BIG STEP 4 of
4 (GPU Memory Aliasing) may begin as its own, later, separate campaign.
