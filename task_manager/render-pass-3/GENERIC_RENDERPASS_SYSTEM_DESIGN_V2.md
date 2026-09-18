# Generic `RenderPass` System — Design

_Design for the declaration layer that sits directly on top of the existing,
untouched render graph orchestrator (`RenderGraphBuilder::AddPass()`/
`AddComputePass()`, `RenderGraphCompiler`, `RenderGraphBarrierPlanner`,
`RenderGraphResourcePool`). That orchestrator is proven and correct and is
not touched by anything below; everything here is a new layer above it that
ends by calling the exact same `AddPass()`/`AddComputePass()` entry points
that exist today._

## Design goal

A render pass should be agnostic: it should not need to know anything about
the rest of the engine, yet the system built from many of them should be
able to express anything a feature needs. The core render-graph vocabulary
should carry zero enumerated opinions about what specific features exist
above it.

## Non-goals

- The render graph orchestrator itself is not rewritten — it stays exactly
  as-is. Everything below is a declaration layer sitting strictly above it.
- No polymorphic `IRenderPass` class hierarchy. A pass is data plus a
  callable, not a virtual interface, and the thing that registers a feature's
  passes is a plain function, not a subclassed object. Full genericity comes
  from a uniform data shape, not from virtual dispatch.

---

## 0. Decisions treated as settled

The following six points are foundational and should not be re-derived
casually — they shape every section below.

1. **Bridging into the render graph.** The pipeline-building entry point's
   own signature changes to accept the new opaque pass-tag/view-id types.
   This is a real, scoped change to the render-graph *builder* layer (its
   public API surface), never to the orchestrator underneath it. It rolls
   out additively: a new opaque-typed overload lives alongside the old
   concrete-enum overload first, and the old overload plus the old enums
   themselves are deleted only once every real call site has migrated. A
   permanent compatibility shim that silently translates opaque tags back
   into old concrete enums forever is explicitly rejected — that would just
   hide feature-specific enum values behind an extra layer instead of
   actually removing them from the core vocabulary.

2. **Cross-feature data hand-off.** One provider's output (for example, a
   per-model GPU-skinning output buffer) needs to reach another, otherwise
   unrelated provider (opaque/scene/present) without either one knowing the
   other exists. This is solved with a generic, opaque-keyed slot store
   carried on the shared per-frame context — not a hardcoded named field on
   that context, and not a heavier typed produce/consume declaration system
   (that would just re-implement, one layer too high, machinery the render
   graph builder already provides at the resource level).

3. **Per-view duplication.** Game-view versus scene-view duplication (and any
   future named view) is owned centrally by one loop inside the pipeline's
   own "declare everything for this frame" entry point. A feature registers
   its intent to run once, or once per active view, rather than each feature
   reimplementing its own view loop. This stays inside the existing rule
   that the graph-building callback runs exactly twice per frame — the
   per-view loop happens *inside* one of those two calls, it does not add a
   third.

4. **Tag representation.** Tags are a fixed 64-bit bitmask, not an open or
   growable tag set. The real number of distinct categories in use today is
   small (single digits), growing slowly. A fixed 64-bit mask keeps a tag
   test a free bitwise AND, matching the general expectation that this kind
   of per-pass bookkeeping should never allocate or branch expensively. If
   64 bits is ever genuinely exhausted, widening to 128 bits later is a
   small, mechanical, backward-compatible follow-up — not a reason to pay
   for a growable set today.

5. **Runtime unregistration.** Registering everything once at startup is
   sufficient; a full unregister-at-runtime facility is not a priority.
   Every real "toggle" case that exists today (a feature that only produces
   work when it actually has something to draw this frame, a panel that is
   only visible sometimes) is already a per-frame "produce zero passes this
   call" decision made inside a feature's own collection logic, not a
   registry add/remove. A light unregister escape hatch can still exist, but
   is not load-bearing.

6. **View identity growth.** Named views beyond the two that exist today
   (game/scene) are supported for free, not as extra engineering, because
   view identity is a hashed opaque name rather than a fixed enum from the
   start — the same treatment as tags, applied to views. Whether the
   application decides to actually have more than two live views at once is
   a separate, later concern this layer must simply not obstruct.

---

## 1. The core idea

A pass stops being a bespoke C++ function call and becomes pure, uniform
data. The pipeline that consumes that data is a small, generic engine that
never needs to know what any specific pass does.

```
                 +-------------------------------------------+
Feature modules  | Atmosphere / GpuSkinning / Opaque / Sky    |  each just PRODUCES
(agnostic to     | / Transparent / Present / ... (N of them)  |  RenderPassDesc values
each other)      +-----------------+-----------------+--------+
                                    | CollectPasses   |
                                    v                 v
                         +-------------------------------+
                         |   RenderPipeline                |  generic: loops views,
                         | - owns registered providers     |  sorts by order, groups
                         | - collects RenderPassDesc[]      |  by tag - zero knowledge
                         | - sorts by RenderPassEvent       |  of what any pass "is"
                         +---------------+-----------------+
                                         v
                    RenderGraphBuilder::AddPass()/AddComputePass()   <- unchanged
                                         v
                    RenderGraphCompiler / BarrierPlanner / Execute()  <- unchanged
```

---

## 2. A pass is a value, not a function call

```cpp
// The core has no enumerated knowledge of what a pass "is".
struct RenderPassDesc {
    RenderPassId id;                    // stable, typed, hashed identity - never a raw string compare
    const char* debugName = nullptr;    // display-only: ImGui, RenderDoc labels, logs - never used for lookup/identity
    PassKind kind = PassKind::Graphics; // Graphics / Compute today; see Section 6 for what widening this later actually costs
    RenderPassEvent order = RenderPassEvent::Opaques; // a SORT HINT ONLY - see Section 5
    RenderPassTagMask tags = 0;         // opaque, caller-defined bits - core never interprets them
    RenderViewId view = RenderViewId::Shared(); // opaque view identifier

    std::function<void(rg::RenderGraphBuilder::PassBuilder&)> setup;  // reuses the existing, proven PassBuilder
    std::function<void(rg::PassContext&)> execute;                    // reuses the existing PassContext
};
```

This struct is the one shared shape every pass conforms to. It is
deliberately a plain value type, not a class hierarchy, so it can be stored
in an ordinary `std::vector`, produced by any feature, and consumed
generically without the consuming code ever branching on "what kind of pass
is this."

---

## 3. Feature modules are providers, not hardcoded call sites

```cpp
// A provider is just a function - no virtual interface required.
using RenderPassProvider =
    std::function<void(const RenderPassFrameContext& frame, std::vector<RenderPassDesc>& outPasses)>;

// Once: invoked exactly one time per frame-declaration call.
// PerActiveView: invoked once per entry in frame.activeViews, with the
// current view already stamped into that specific invocation's frame context.
enum class ProviderScope { Once, PerActiveView };

class RenderPipeline {
public:
    void Register(const char* debugName, ProviderScope scope, RenderPassProvider provider);
    void Unregister(const char* debugName); // light escape hatch, not a priority feature

    // Called once per graph-build callback. Loops active views for
    // PerActiveView providers, asks every provider "what do you want to
    // contribute", sorts the combined result by `order`, then feeds each
    // one into RenderGraphBuilder - the only place that still calls
    // AddPass()/AddComputePass().
    void DeclareInto(rg::RenderGraphBuilder& builder, RenderPassFrameContext& frame);

private:
    struct Entry { const char* debugName; ProviderScope scope; RenderPassProvider provider; };
    std::vector<Entry> m_providers;

    // Owned once, reused every frame: cleared (not reconstructed) at the
    // start of each DeclareInto() call so its capacity survives across
    // frames instead of reallocating from empty every time. The same
    // discipline the render-graph compiler already applies to its own
    // per-frame vectors.
    std::vector<RenderPassDesc> m_scratchCollected;
};
```

Today's large, hand-wired, ever-growing frame-building function shrinks to
roughly:

```cpp
m_renderGraph.Execute(cmd, mode, [&](rg::RenderGraphBuilder& b) {
    RenderPassFrameContext frame{ /* activeViews, camera(s), targets, dt, blackboard - plain data */ };
    m_renderPipeline.DeclareInto(b, frame);
    return m_renderPipeline.CollectFinalOutputs(frame);
});
```

Adding a new pass means calling `Register()` once at startup — never editing
this function again.

---

## 4. Cross-provider data hand-off — the blackboard

```cpp
// Lives on RenderPassFrameContext - a generic key/value slot store, not a
// hardcoded named field. Any provider can publish/fetch any handle type
// under any caller-chosen key; the mechanism is generic, only the keys a
// given feature happens to use are feature-specific, and those keys live in
// that feature's own header, never in core.
class RenderPassBlackboard {
public:
    template <typename T> void Publish(RenderPassId key, T value);
    template <typename T> std::optional<T> Fetch(RenderPassId key) const;

    // Called once at the start of each frame's declaration. Clears entries
    // but keeps whatever backing storage was already reserved from the
    // previous frame's high-water mark - avoids rebuilding the container
    // from nothing every single frame.
    void BeginFrame();

    // Debug builds only: after every provider has run, any key that was
    // Publish()'d but never Fetch()'d this frame is reported once (through
    // the same channel the Frame Debugger already uses for other
    // structural warnings), so a dangling hand-off is visible instead of
    // silently doing nothing. Compiles to nothing in release.
    void ReportUnusedPublishesIfAny() const;

private:
    // A flat, linearly-scanned key/value list rather than a hash map. The
    // realistic number of live keys in a frame is small (single digits to
    // low tens), where a flat vector is both faster and lighter than a
    // hashed container, and it is reused (BeginFrame() clears, does not
    // reallocate) rather than rebuilt from scratch every frame.
    std::vector<std::pair<std::uint64_t, std::any>> m_slots;
};
```

Example usage — GPU skinning publishing, opaque consuming, two otherwise
unrelated providers wired with zero mutual knowledge:

```cpp
// Inside the GPU-skinning provider's collection step:
frame.blackboard.Publish<rg::BufferHandle>("GpuSkinning.Output.Model42"_passId, outputBufferHandle);

// Inside the opaque provider's collection step, completely independently:
if (auto handle = frame.blackboard.Fetch<rg::BufferHandle>("GpuSkinning.Output.Model42"_passId)) {
    pass.setup = [handle](rg::RenderGraphBuilder::PassBuilder& b) {
        b.ReadBuffer(*handle, rg::ResourceAccess::VertexBufferRead);
    };
}
```

---

## 5. Per-view iteration

```cpp
struct RenderPassFrameContext {
    std::vector<RenderViewId> activeViews; // e.g. {Shared} always; {Game, Scene} whichever panels are visible
    RenderViewId currentView;              // stamped before invoking a PerActiveView provider
    RenderPassBlackboard& blackboard;      // owned by the pipeline, passed by reference - see Section 4
    // ... camera/target/dt data, all plain, all opaque to the pipeline itself ...
};
```

`DeclareInto()`'s loop, in spirit:

```cpp
for (const Entry& entry : m_providers) {
    if (entry.scope == ProviderScope::Once) {
        entry.provider(frame, m_scratchCollected);
    } else { // PerActiveView
        for (RenderViewId view : frame.activeViews) {
            frame.currentView = view;
            entry.provider(frame, m_scratchCollected);
        }
    }
}
std::stable_sort(m_scratchCollected.begin(), m_scratchCollected.end(),
    [](const RenderPassDesc& a, const RenderPassDesc& b) { return a.order < b.order; });
for (RenderPassDesc& desc : m_scratchCollected) {
    builder.AddRenderPass(desc.debugName, desc.kind, desc.view, desc.tags, desc.setup, desc.execute);
}
```

Every provider here is a pure `(frame) -> passes` function with no shared
mutable state assumed beyond appending to the output list. That means this
loop can be parallelized later — each provider filling its own local list,
merged and sorted once — across the job system that already exists in this
engine, with no change to `RenderPassDesc` or `RenderPassProvider`'s shape.
Not needed today; worth keeping in mind as headroom rather than a redesign
if declaration-time cost ever shows up on a profiler.

---

## 6. Ordering is a hint, not the dependency mechanism

```cpp
enum class RenderPassEvent : std::uint32_t {
    BeforeEverything   = 0,
    PreOpaques         = 1000,   // e.g. GPU skinning dispatch, shared LUTs
    Opaques            = 2000,   // the built-in opaque draw
    AfterOpaques       = 2500,   // sky background (needs opaque's depth buffer)
    Transparents       = 3000,
    AfterTransparents  = 4000,   // post composites (aerial perspective, etc.)
    AfterEverything    = 9000,
};
// NOTE: this is a SORT KEY / tie-breaker for passes with no hard data
// dependency between them. Real ordering (RAW/WAW on shared resources) is
// fully enforced by the render graph compiler's own dependency analysis,
// which this value never overrides and is never consulted by. Do not let a
// future edit start reading this field as if it were a dependency
// declaration - that would silently reintroduce exactly the "metadata that
// does nothing real" problem this design otherwise avoids.
```

This also gives any structural tooling (a frame debugger, a profiler
overlay) a principled way to answer "where does the view region start" (the
first pass with `order >= Opaques`) instead of searching for a specific pass
by name.

---

## 7. Opaque tags and opaque view ids — no feature/editor knowledge in core

```cpp
// The core namespace never defines what a tag or a view id MEANS.
struct RenderPassTag { std::uint64_t bit; };
using RenderPassTagMask = std::uint64_t; // fixed 64-bit, not growable - see Section 0, point 4

class RenderViewId {
public:
    static RenderViewId Shared() noexcept;                  // the only built-in value the core defines
    static RenderViewId Named(const char* name) noexcept;   // hashed, opaque - arbitrary names supported for free
    bool operator==(const RenderViewId&) const noexcept;
private:
    std::uint64_t m_hash;
};
```

Feature modules define their own tags outside the core vocabulary entirely:

```cpp
// Lives with the feature, e.g. under its own Atmosphere-specific header -
// never inside the shared render-graph vocabulary header.
namespace AtmosphereTags {
    constexpr rg::RenderPassTag Lut{ 1ull << 0 };
}
```

Anything downstream groups by testing `pass.tags & AtmosphereTags::Lut`,
without the render graph's own core ever having heard of that feature by
name. No new feature campaign ever needs to add a value to a shared core
enum again.

---

## 8. Stable, typed pass identity

```cpp
struct RenderPassId {
    std::uint64_t hash;
    constexpr bool operator==(const RenderPassId&) const noexcept = default;
};
consteval RenderPassId operator""_passId(const char* s, std::size_t n) noexcept;

namespace PassIds {
    constexpr rg::RenderPassId Opaques = "Opaques"_passId;
}
```

Lookups are O(1) integer compares; `debugName` remains purely for
humans/UI, fully decoupled from identity/lookup. This is also the identity
type used for blackboard keys (Section 4).

In debug builds only, every `_passId`/tag construction site also registers
its hash against its original source string in a small global lookup table,
purely so an assertion message or a frame-debugger label can print "which
name this hash came from" if two names were ever mistaken for each other or
a lookup silently misses. This table does not exist in release builds and
is never used for runtime identity comparisons — identity is always the
64-bit hash itself. A genuine collision between two distinct names is
astronomically unlikely at the number of distinct ids this engine will ever
define, so this exists purely to make the already-rare case debuggable, not
because collisions are expected.

---

## 9. Why not a polymorphic `IRenderPass` hierarchy

Data plus `std::function` is used instead of virtual dispatch:
`RenderPassDesc` is a value, `RenderPassProvider` is a plain callable. Full
genericity is achieved without requiring every pass author to subclass a
heavyweight base. Stateful providers (persisted per-feature settings, for
example) are covered by ordinary lambda captures — no virtual interface
needed.

The one real reason to reconsider this later would be a genuine need for
third-party/plugin-style providers loaded without recompiling the engine —
that would justify a narrow virtual interface at the provider level (not the
per-pass level). Nothing today needs that.

---

## 10. What extending `PassKind` actually costs later

`PassKind` covers `Graphics`/`Compute` today, and is expected to grow a
`Transfer` and/or `RayTrace` value eventually. That growth is mechanical,
not free: it means visiting a small, fixed number of known spots — the place
that special-cases compute dispatch inside the builder, the human-readable
`ToString()` switch, and whichever part of the executor eventually needs to
record a transfer or ray-trace command differently from a graphics or
compute one. None of those sites are hidden or numerous, but a switch
statement without a `default:` case will not by itself force a compile
error if one is missed, so this only stays truly safe if unhandled-enum
warnings are treated as build errors project-wide. Worth confirming once,
rather than assuming the compiler will always catch it.

---

## 11. Performance discipline this design commits to

- No per-frame container is rebuilt from empty when it can instead be
  cleared and reused with retained capacity: the pipeline's own collected-
  pass list (Section 3) and the blackboard's slot storage (Section 4) both
  follow this rule from day one, matching the `reserve()`-before-work
  discipline the render graph compiler already applies to its own vectors.
- The blackboard is a flat, linearly scanned list rather than a hash map,
  because the realistic number of live keys per frame is small; this is
  both faster and allocates less than a hashed container at that scale.
- `setup`/`execute` as `std::function` carries the same, already-accepted
  cost the render graph's own per-pass execute callback already pays today
  — this design does not introduce a new category of cost, only reuses an
  existing one.
- Sorting the collected pass list is `O(n log n)` over a per-frame pass
  count in the tens, not the thousands — not a place worth hand-optimizing.
- None of the above touches the actual hot path: resource barrier synthesis,
  culling, and command recording remain entirely inside the existing,
  unmodified compiler/barrier-planner/executor. This layer only decides
  *what* gets declared and *in what order it is proposed*, never how it is
  synchronized or submitted.

---

## 12. Rough migration shape (non-breaking, additive-first)

1. **Phase A** — introduce the new identity/tag/order/view types additively,
   alongside the existing enums. Nothing breaks yet.
2. **Phase B** — introduce `RenderPassDesc`/`RenderPassProvider`/
   `RenderPipeline`/`RenderPassBlackboard` (including the reused-storage and
   unused-publish-warning behavior from Sections 3–4 from the start, not as
   a later pass); migrate one self-contained feature through it end-to-end
   as a proof, including a concrete cross-provider blackboard hand-off.
3. **Phase C** — migrate every remaining call site in the current hand-wired
   frame-building function; change the builder's pass-adding entry point to
   the new opaque types; delete the old feature-specific enum values from
   the core vocabulary once nothing references them; rewrite any
   name-string-based structural pivot (e.g. a tool that currently finds "the
   opaque pass" by literal name) to use the new ordering/tag data instead.
4. **Phase D** — full build, full regression test pass, and live tooling
   verification, per this project's own established convention.

## Open questions (not pre-decided)

- Exact final parameter order/shape of the new opaque-typed pass-adding
  overload — the mechanism is settled; its precise call signature is a
  Phase C implementation detail.
- Whether the blackboard's unused-publish warning should be a hard assert in
  debug builds or a soft log line — a small usability decision better made
  once the Phase B proof-of-concept feature exists to test it against.
