# PHASE1: Core Vocabulary, `RenderPipeline`, and the Blackboard

_Child of `PHASE0_MASTER_STRATEGY.md` — read that file first (especially
its "Locked Design Decisions" section). Part of the `render-pass-3`
campaign._

## Step 1: The Goal

Add every new type `GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md` describes,
purely additively, with **zero real consumers yet** — mirrors this
codebase's own established "Phase 1 ships pure vocabulary, nothing calls
it yet" precedent (see e.g.
`RENDERGRAPH_PHASE1_CORE_DATA_MODEL_STRATEGY_v2.md`'s own "What We Will
NOT Do", and `render-pass-1`'s own `PHASE1_RENDER_PASS_CORE_ABSTRACTION.md`).
Nothing under `src/Application/` or `src/Editor/` is touched in this phase.
Nothing in this phase changes any pass's actual runtime behavior.

By the end of this phase: a brand-new header/source pair exists with a
fully working, fully unit-tested `RenderPipeline` + `RenderPassBlackboard`
that COULD be wired into `Application::Run()` today, but isn't yet (PHASE2
is the first real wiring).

## Step 2: The Situation

- `src/Renderer/RenderGraph/RenderGraphTypes.h` already defines `PassKind`,
  `ViewScope`, `RenderPassCategory`, `RenderPassDrawKind`, and `PassRecord`
  (see that file for the exact, current shape of all four enums and the
  struct — read it directly, do not rely on a paraphrase). None of these
  four enums are touched by this phase except one small, additive change
  to `PassRecord` itself (Step 3.2 below).
- `src/Renderer/RenderGraph/RenderGraphBuilder.h` already defines
  `AddPass()`/`AddComputePass()`/`AddRenderPass()` (both overloads) exactly
  as `PHASE0_MASTER_STRATEGY.md`'s Step 2 describes. This phase adds
  exactly ONE new, trailing, DEFAULTED parameter to the two
  `AddRenderPass()` overloads (Step 3.3 below) and touches NOTHING else in
  this file.
- There is currently no `RenderPassId`, `RenderPassTag`/`RenderPassTagMask`,
  `RenderViewId`, `RenderPassEvent`, `RenderPassDesc`, `RenderPassProvider`,
  `RenderPipeline`, or `RenderPassBlackboard` type anywhere in the repo —
  every one of these is a brand-new addition this phase creates.
- Per `PHASE0_MASTER_STRATEGY.md`'s Locked Design Decision 5, this new
  layer is deliberately NOT what replaces `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind` — those three keep existing, unchanged, forever.
  This phase's new types are a genuinely SEPARATE, additional, more
  general vocabulary that sits on top.

## Step 3: The Plan

### 3.1 — New file: `src/Renderer/RenderGraph/RenderPipeline.h` / `.cpp`

A brand-new header/source pair, `namespace gte::rg`, living alongside
`RenderGraphBuilder.h` (same folder — this is still "the render graph
module," just the declaration-layer half of it, matching
`GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md`'s own architecture diagram: feature
modules → `RenderPipeline` → `RenderGraphBuilder::AddPass()/AddComputePass()`).
`#include "RenderGraphBuilder.h"` (needed for `PassBuilder&`/`PassContext&`
in the `setup`/`execute` signatures) and `RenderGraphTypes.h`.

#### `RenderPassId` (design doc Section 8)

```cpp
struct RenderPassId {
    std::uint64_t hash = 0;
    constexpr bool operator==(const RenderPassId&) const noexcept = default;
};

// FNV-1a (or any other fixed, deterministic, compile-time-evaluable hash -
// pick whichever this codebase's own existing string-hashing utility
// already uses, if one exists; otherwise a small hand-rolled consteval
// FNV-1a is fine) - MUST be consteval so `"Foo"_passId` never costs
// anything at runtime, matching the design doc's own "a tag test is a free
// bitwise AND" performance discipline extended to identity hashing too.
// The closest existing precedent is `HashJobName()`
// (`src/Editor/JobsPanelData.cpp`) - an FNV-1a-style hash (offset basis
// `2166136261u`, prime `16777619u`) already used elsewhere in this
// codebase for a deterministic, stable-across-runs string hash; it is a
// plain RUNTIME function today (a `const char*` loop, not `consteval`), so
// it cannot be called directly from a `consteval` operator, but reusing
// its exact same constants/algorithm (just rewritten as a small
// `consteval`-friendly loop over `s`/`n`) keeps this campaign's new hash
// consistent with that existing precedent rather than inventing a second,
// differently-parameterized FNV-1a variant for no reason.
consteval RenderPassId operator""_passId(const char* s, std::size_t n) noexcept;
```

In debug builds only (`#ifndef NDEBUG` — this codebase's real, confirmed
debug-only gate; see `src/Game/Physics/PhysicsSystem.cpp`'s own existing
`#ifndef NDEBUG` block for the established pattern — `GpuMemoryTracker.h`
itself does NOT use this convention, so do not use it as the reference),
every `_passId` construction ALSO registers its hash against the original
source string in a small global table (a plain function-local
`static std::unordered_map<std::uint64_t, const char*>` is fine — this is
debug-only, never touched in release, never used for runtime identity
comparisons per the design doc's own Section 8 rule). Expose a small
`const char* DebugNameForPassId(RenderPassId id) noexcept` helper (returns
`"<unknown>"` if not found) purely for assertion messages/future tooling —
nothing calls it yet.

#### `RenderPassTag` / `RenderPassTagMask` (design doc Section 7)

```cpp
struct RenderPassTag { std::uint64_t bit = 0; };
using RenderPassTagMask = std::uint64_t;
```

No feature-specific tag values live in this file — per the design doc's
own Section 7, those belong in each FEATURE's own header (e.g. a future
`AtmosphereTags::Lut` living in an Atmosphere-owned file), never here.

#### `RenderViewId` (design doc Section 7)

```cpp
class RenderViewId {
public:
    static RenderViewId Shared() noexcept;                 // the only built-in value this layer defines
    static RenderViewId Named(const char* name) noexcept;  // hashed, opaque - same consteval-friendly hash as RenderPassId
    bool operator==(const RenderViewId&) const noexcept;
private:
    std::uint64_t m_hash = 0;
};
```

`Shared()` must return a fixed, reserved hash value (e.g. `0`) distinct
from anything `Named(...)` could ever hash a real string to (reserve `0`
as "never a real hashed name" the same way `rg::kInvalidIndex` reserves
`0xFFFFFFFF`).

#### `RenderPassEvent` (design doc Section 6)

```cpp
enum class RenderPassEvent : std::uint32_t {
    BeforeEverything   = 0,
    PreOpaques         = 1000,
    Opaques            = 2000,
    AfterOpaques       = 2500,
    Transparents       = 3000,
    AfterTransparents  = 4000,
    AfterEverything    = 9000,
};
```

Put THIS enum in `RenderGraphTypes.h` instead of `RenderPipeline.h` — it is
also threaded onto `PassRecord`/`RenderGraphPassSnapshot` (Step 3.2/3.4
below), so it needs to live where those two types already are, matching
where `PassKind`/`RenderPassCategory`/`RenderPassDrawKind` already live for
the exact same reason. `RenderPipeline.h` simply `#include`s
`RenderGraphTypes.h` and reuses it.

#### `RenderPassDesc` (design doc Section 2 — WITH a deliberate, documented
extension beyond the design doc's own shown shape)

```cpp
struct RenderPassDesc {
    RenderPassId id;
    const char* debugName = nullptr;
    PassKind kind = PassKind::Graphics;
    RenderPassEvent order = RenderPassEvent::Opaques;
    RenderPassTagMask tags = 0;
    RenderViewId view = RenderViewId::Shared();

    // render-pass-3 campaign, PHASE1 - deliberate, documented EXTENSION
    // beyond GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md's own Section 2 shape.
    // The design doc's own Decision 1 assumed the OLD ViewScope/
    // RenderPassCategory/RenderPassDrawKind fields would eventually be
    // deleted once every pass migrated - PHASE0_MASTER_STRATEGY.md's own
    // Locked Design Decisions 2/4/5 explicitly keep them alive forever
    // instead (the Frame Debugger and its own Editor-only debug passes
    // still read them directly). A pass declared through THIS new layer
    // must therefore still carry a real, correct legacyCategory/drawKind
    // so RenderPipeline::DeclareInto() (below) can stamp them onto the
    // underlying PassRecord exactly as if the old AddRenderPass() overload
    // had been called directly - the Frame Debugger must never be able to
    // tell the difference between a pass declared the old way and one
    // declared through this new layer. `view` (above) is what
    // DeclareInto() translates into the legacy `ViewScope` - see PHASE3's
    // own translation table.
    RenderPassCategory legacyCategory = RenderPassCategory::General;
    RenderPassDrawKind drawKind = RenderPassDrawKind::DrawMesh;

    std::function<void(rg::RenderGraphBuilder::PassBuilder&)> setup;
    std::function<void(rg::PassContext&)> execute;
};
```

#### `RenderPassProvider` / `ProviderScope` (design doc Section 3)

```cpp
using RenderPassProvider =
    std::function<void(const RenderPassFrameContext& frame, std::vector<RenderPassDesc>& outPasses)>;

enum class ProviderScope { Once, PerActiveView };
```

#### `RenderPassBlackboard` (design doc Section 4)

```cpp
class RenderPassBlackboard {
public:
    template <typename T> void Publish(RenderPassId key, T value);
    template <typename T> std::optional<T> Fetch(RenderPassId key) const;

    void BeginFrame(); // clears m_slots but keeps its reserved capacity

#ifndef NDEBUG // or this codebase's existing debug-only convention
    void ReportUnusedPublishesIfAny() const; // logs (see Step 3.5 below for the "log vs. assert" call)
#endif

private:
    struct Slot { RenderPassId key; std::any value;
#ifndef NDEBUG
        mutable bool wasFetched = false;
#endif
    };
    std::vector<Slot> m_slots; // flat, linearly scanned - see design doc Section 11
};
```

`Publish<T>()` OVERWRITES an existing slot with the same key if one
already exists this frame (last-publish-wins) rather than pushing a
duplicate — a linear scan for a matching key before appending is correct
and cheap at the realistic single-digit-to-low-tens key count the design
doc's own Section 11 already commits to.

#### `RenderPassFrameContext` (design doc Section 5 — WITH the additional
`finalOutputs` fields this codebase's real integration needs)

```cpp
struct RenderPassFrameContext {
    std::vector<RenderViewId> activeViews;
    RenderViewId currentView = RenderViewId::Shared(); // stamped by RenderPipeline before each PerActiveView provider call
    RenderPassBlackboard& blackboard;

    // render-pass-3 campaign, PHASE1 - a real, concrete resolution of a gap
    // GENERIC_RENDERPASS_SYSTEM_DESIGN_V2.md leaves as a "Phase C
    // implementation detail": SOME final TextureHandle/VolumeTextureHandle
    // values genuinely need to be added to this Execute() call's own
    // `finalOutputs`/`KeepVolumeTextureOutput()` root set, or
    // RenderGraphCompiler::Compile()'s backward-reachability culling scan
    // silently removes their writing pass. Rather than inventing a second,
    // parallel produce/consume declaration system (explicitly rejected by
    // the design doc's own Decision 2), any PROVIDER that owns a handle
    // needing this treatment simply appends it here directly - the caller
    // (Application::Run(), PHASE3) reads both vectors back out AFTER
    // DeclareInto() returns and forwards them to the exact same
    // `outputs`/`KeepVolumeTextureOutput()` mechanism it already uses today.
    std::vector<rg::TextureHandle> finalTextureOutputs;
    std::vector<rg::VolumeTextureHandle> finalVolumeTextureOutputs;

    // ... any other plain, opaque-to-the-pipeline per-frame data
    // (camera/target/dt) is added here by whichever LATER phase first
    // needs it (PHASE2/PHASE3) - this phase does not need to guess every
    // field up front; adding a field here later is a trivial, additive,
    // zero-risk change (mirrors PassRecord's own "append at the end"
    // convention).
};
```

#### `RenderPipeline`

```cpp
class RenderPipeline {
public:
    void Register(const char* debugName, ProviderScope scope, RenderPassProvider provider);
    void Unregister(const char* debugName); // light escape hatch - linear scan by debugName pointer-or-content match, not load-bearing (design doc Section 0.5)

    void DeclareInto(rg::RenderGraphBuilder& builder, RenderPassFrameContext& frame);

private:
    struct Entry { const char* debugName; ProviderScope scope; RenderPassProvider provider; };
    std::vector<Entry> m_providers;
    std::vector<RenderPassDesc> m_scratchCollected; // cleared, not reconstructed, every DeclareInto() call
};
```

`DeclareInto()`'s body is a direct implementation of design doc Section
5's pseudocode: loop `m_providers`, invoke `Once`-scope providers exactly
once, invoke `PerActiveView`-scope providers once per entry in
`frame.activeViews` (stamping `frame.currentView` before each one, and
restoring/leaving it as whatever the last iteration set — no caller reads
`currentView` after `DeclareInto()` returns, so there is nothing to restore),
`std::stable_sort` the collected list by `.order`, then for each collected
`RenderPassDesc`, call the underlying, PRE-EXISTING, byte-for-byte-
unchanged
`builder.AddRenderPass(desc.debugName, desc.kind, translatedViewScope,
desc.legacyCategory, desc.setup, desc.execute, desc.drawKind,
desc.order)` (the `translatedViewScope`/`renderPassEvent`-parameter
mechanics are PHASE3's concern for the real Game/Scene translation table —
this phase's own test double can use a trivial `Shared`-only translation,
since no real per-view translation table exists to call into yet).

### 3.2 — `RenderGraphTypes.h`: add `RenderPassEvent` + `PassRecord::renderPassEvent`

Add the `RenderPassEvent` enum (Step 3.1's code block above) directly in
`RenderGraphTypes.h`, next to `RenderPassDrawKind`. Add a new field,
appended at the END of `PassRecord` (never inserted in the middle — this
codebase's own explicit, documented convention):

```cpp
// render-pass-3 campaign, PHASE1 - a purely descriptive SORT HINT (see
// RenderPassEvent's own doc comment above) - read by NOTHING in
// RenderGraph.cpp/RenderGraphCompiler.cpp/RenderGraphBarrierPlanner.cpp,
// exactly like every other purely-descriptive PassRecord field before it.
// Defaults to Opaques so every pre-existing AddRenderPass() call site
// (which never mentions this at all) is completely unaffected - the
// default is irrelevant for any pass the Frame Debugger already excludes
// via category == Debug (PHASE4 never reads this field for those).
RenderPassEvent renderPassEvent = RenderPassEvent::Opaques;
```

Also add a `const char* ToString(RenderPassEvent) noexcept;` declaration
(`RenderGraphTypes.h`) + definition (`RenderGraphTypes.cpp`), with the same
deliberately-`default:`-free exhaustive switch every other enum in this same
file already uses (`ToString(PassKind)`/`ToString(RenderPassCategory)`/
`ToString(RenderPassDrawKind)` — see `RenderGraphTypes.cpp`'s own existing
pattern) — this is a small, genuinely worthwhile consistency fix: every
other enum this file defines already has one "from day one" per this file's
own header comment on `ToString(ResourceAccess)`, and a future assertion
message/tooling label (e.g. a Frame Debugger tooltip) will want this exactly
the same way it already wants `ToString(RenderPassDrawKind)` today. Add one
matching test case to `RenderGraphTypesTests.cpp` alongside its
`ToString(RenderPassDrawKind)` sibling tests.

### 3.3 — `RenderGraphBuilder.h`: one new trailing, defaulted parameter

Add `RenderPassEvent renderPassEvent = RenderPassEvent::Opaques` as the
LAST parameter on BOTH existing `AddRenderPass()` overloads (mirroring
`RenderPassDrawKind drawKind`'s own identical precedent from
`render-pass-2`'s own PHASE1 — a trailing defaulted plain-type parameter
after the two template-deduced lambda parameters never touches template
argument deduction, so EVERY pre-existing call site across the whole repo
compiles completely unmodified with zero other changes). Stamp it exactly
where `drawKind` is already stamped:
`m_passes.back().renderPassEvent = renderPassEvent;`.

### 3.4 — `RenderGraphSnapshot.h` / `.cpp`: thread `renderPassEvent` through

`RenderGraphPassSnapshot` gains its own
`RenderPassEvent renderPassEvent = RenderPassEvent::Opaques;` field, copied
straight through for BOTH a surviving and a culled pass in
`BuildRenderGraphSnapshot()` (`RenderGraphSnapshot.cpp`) — mirror
`category`/`drawKind`'s own exact copy-through precedent. This is what lets
PHASE4's Frame Debugger fix read it later; nothing reads it yet in this
phase.

### 3.5 — Tests (new file:
`tests/Renderer/RenderGraph/RenderPipelineTests.cpp`)

All of the following are genuinely Tier-1 (pure data/logic, zero live
`VkDevice` needed) — register the new file in `tests/CMakeLists.txt` next
to its siblings:

- `RenderPassId`/`RenderViewId` hashing: two identical literal strings
  produce equal ids/views; two different strings produce different ones
  (a real collision is not something to test for — just confirm the
  mechanism itself is deterministic and content-based, not pointer-based).
- `RenderPassBlackboard`: `Publish<int>`/`Fetch<int>` round-trips a value;
  `Fetch` for a never-published key returns `std::nullopt`; `Fetch` with
  the WRONG type (e.g. published as `int`, fetched as `float`) returns
  `std::nullopt` (relies on `std::any_cast`'s own type-mismatch behavior —
  confirm this explicitly with a test, do not assume it); `BeginFrame()`
  clears a previously-published key (a `Fetch` after `BeginFrame()` for a
  key published before it returns `std::nullopt`) while NOT shrinking
  `m_slots`'s own `.capacity()` below its prior high-water mark (assert
  `capacity()` is unchanged or only grows across a `BeginFrame()` call,
  never shrinks — this is the "reused, not reallocated" contract design
  doc Section 4/11 both call out explicitly).
- `RenderPipeline::DeclareInto()`: register one `Once`-scope fake provider
  and one `PerActiveView`-scope fake provider (each just appends a
  `RenderPassDesc` with a distinguishable `id`/`debugName` and no real
  `setup`/`execute` body beyond a no-op lambda), call `DeclareInto()` with
  `frame.activeViews` containing 0/1/2 entries, and assert: the `Once`
  provider's contribution appears exactly once regardless of
  `activeViews.size()`; the `PerActiveView` provider's contribution
  appears exactly `activeViews.size()` times, each with the correct
  `frame.currentView` value visible from inside the provider callback
  (capture a local vector of "which view was I called with" and assert its
  contents match `activeViews` in order); the final, sorted order matches
  ascending `.order` values regardless of registration order or provider
  scope, using `RenderGraphBuilder::Finish()`'s own resulting
  `CompiledGraphInput::passes` to assert on (this needs a REAL
  `RenderGraphBuilder` instance — no live device needed, matching
  `RenderGraphBuilderTests.cpp`'s own existing precedent for testing this
  same builder with zero Vulkan device).
- `RenderPassDesc`'s `legacyCategory`/`drawKind` fields survive unchanged
  through a `DeclareInto()` call into the resulting `PassRecord`
  (`category`/`drawKind` on the produced `CompiledGraphInput::passes`
  entry match exactly what the test's fake provider set) — this is the
  ONE test in this file that directly protects PHASE0's Locked Design
  Decision 5 (the old fields must never silently stop being stamped).

## Definition of Done

- `RenderPassId`/`RenderPassTag`/`RenderPassTagMask`/`RenderViewId`/
  `RenderPassDesc`/`RenderPassProvider`/`ProviderScope`/
  `RenderPassBlackboard`/`RenderPassFrameContext`/`RenderPipeline` all
  exist in a new `src/Renderer/RenderGraph/RenderPipeline.h`/`.cpp` pair.
- `RenderPassEvent` exists in `RenderGraphTypes.h`; `PassRecord`/
  `RenderGraphPassSnapshot` both carry the new `renderPassEvent` field,
  defaulted to `Opaques`, copied through for both surviving and culled
  passes. `ToString(RenderPassEvent)` exists in `RenderGraphTypes.cpp`,
  following the same exhaustive-switch-with-no-`default:` convention every
  sibling enum in that file already follows, with a matching test in
  `RenderGraphTypesTests.cpp`.
- Both `RenderGraphBuilder::AddRenderPass()` overloads compile with the new
  trailing, defaulted `renderPassEvent` parameter; every pre-existing call
  site in the entire repo (grep for `AddRenderPass(` across `src/`)
  compiles completely unmodified — confirm this explicitly via an
  incremental compile of `gte_core`, not just a visual code read.
- `tests/Renderer/RenderGraph/RenderPipelineTests.cpp` exists, is
  registered in `tests/CMakeLists.txt`, and every test in it passes.
- Zero files under `src/Application/` or `src/Editor/` are touched by this
  phase. Zero real pass declaration anywhere in the engine goes through
  `RenderPipeline` yet — `grep`/`search_in_dir` for `RenderPipeline` outside
  `src/Renderer/RenderGraph/` and `tests/` should return zero hits.
- An incremental compile of `gte_core` AND `GreatTamanaEngineTests`
  succeeds, and the new test binary, run directly, shows every new test
  passing (do not wait for PHASE5's full `ctest` run to first confirm
  these — run them now).

## What We Will NOT Do

- Do NOT wire `RenderPipeline` into `Application::Run()` yet — that starts
  in PHASE2.
- Do NOT touch `RenderGraphCompiler.cpp`/`RenderGraphBarrierPlanner.cpp`/
  `RenderGraph.cpp` at all — `RenderPassEvent` is read by nothing there, by
  design, exactly like `category`/`drawKind`/`viewScope` before it.
- Do NOT delete or modify `ViewScope`/`RenderPassCategory`/
  `RenderPassDrawKind` in any way — see `PHASE0_MASTER_STRATEGY.md`'s
  Locked Design Decision 5. This phase only ADDS a new, separate,
  additional field (`renderPassEvent`) and a new, separate layer above the
  builder.
- Do NOT invent real per-feature tag values (no `AtmosphereTags::Lut` yet)
  — those belong to whichever phase first migrates that specific feature
  (PHASE2/PHASE3), living in that feature's own header, never in this
  phase's new core files.
- Do NOT attempt to resolve the design doc's own still-open "hard assert
  vs. soft log" question for `ReportUnusedPublishesIfAny()` with anything
  more than a simple, safe default: implement it as a soft, non-fatal log
  line (this codebase's existing logging facility - check for one used
  elsewhere in `Renderer`/`Editor`, e.g. `std::fprintf(stderr, ...)` if no
  richer logger exists) rather than an `assert()`/crash — a debug-time
  usability nuisance is an acceptable default; a hard crash the first time
  a legitimately-unused-this-frame publish happens (e.g. GPU Skinning
  publishes but nothing is animating and Opaque never actually fetches
  this particular frame) is not. Document this choice plainly in this
  phase's own completion report so a later phase can revisit it once real
  mileage exists, exactly as the design doc's own "Open questions" section
  anticipated.
