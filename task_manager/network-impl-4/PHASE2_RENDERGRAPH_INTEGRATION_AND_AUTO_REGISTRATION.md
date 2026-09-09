# PHASE2 — RenderGraph Integration: Automatic Registration

> **Second-iteration audit note (this revision):** this file was re-checked
> line-by-line against the live `src/Renderer/RenderGraph/RenderGraph.h` AND
> `RenderGraph.cpp` (not just grepped) — every structural/behavioral claim
> already in this document (the exact private member names, `PhysicalTexture`'s
> exact field order, `ExecuteCompiledGraph()`'s real body/control flow, exactly
> where and under what condition `RenderGraphResourcePool::BeginFrame()` is
> called, exactly how/where `EnsureTextureResolved()` is invoked, and the
> parallel structure of `CompiledGraphInput::textureNames`/`physicalTextures`)
> was confirmed CORRECT — no design change was needed. Three narrow, precision-
> level corrections were made:
> 1. **Namespace-prefix style fix.** The original code samples in 3.1/3.2 wrote
>    `rg::DebugTextureSnapshot`/`rg::ResourceState` inside code that will
>    actually live INSIDE `namespace gte::rg` itself (`RenderGraph` is declared
>    inside `namespace gte::rg { ... }`, not `namespace gte`). This happens to
>    still compile — `rg` resolves via ordinary unqualified lookup up through
>    the enclosing `gte` scope, which has a member namespace also named `rg`
>    (itself), so `rg::X` ends up meaning exactly `X` — but it is needlessly
>    confusing, inconsistent with this exact file's own existing style
>    (`LastKnownStatsFor()`/`LastSnapshot()` already use bare
>    `PassGpuStats`/`RenderGraphSnapshot`/`ExecuteTimingMode`, never a
>    self-qualified `rg::` prefix), and inconsistent with Phase 1's own header
>    (`RenderGraphDebugTextureRegistry.h`, also inside `namespace gte::rg`,
>    already uses bare `ResourceState`/`RenderTarget`). Corrected to bare type
>    names throughout this file's own code samples below — the `rg::`-prefixed
>    spelling remains exactly correct (and required) wherever Phase 3/4/5's own
>    documents show it, since that code lives in `Application.cpp`/
>    `NetworkRoutes.cpp`, which are in `namespace gte` and genuinely need the
>    qualifier.
> 2. **Resolved a "re-read live" hedge with a confirmed answer.** The frame-
>    counter snippet in 3.2 previously left the exact `if` condition guarding
>    the existing `m_resourcePool.BeginFrame()` call as a placeholder comment.
>    Confirmed directly against the live `ExecuteCompiledGraph()` body: it is
>    exactly `if (!isPipelined)`, where `isPipelined` is that function's own
>    local `const bool isPipelined = (timingMode ==
>    ExecuteTimingMode::PipelinedDeferredReadback);`, computed at the very top
>    of the function, before this `if`. The snippet below now shows this
>    concretely (still flagged for a quick re-confirm at implementation time,
>    in case of drift between this audit and actual implementation).
> 3. **Added an explicit call-out (new 3.3a below) for a real, non-obvious
>    consequence of this phase's own design that was previously only
>    implicit:** because `m_debugTextureFrameCounter` is a SINGLE counter
>    shared by both regimes' `Upsert()` calls, but only ever INCREMENTED by the
>    synchronous regime, a texture registered ONLY by the pipelined regime
>    (today: `"Swapchain"`) has its own `frames_since_update` freshness signal
>    driven entirely by how often the OFFSCREEN regime happens to execute
>    elsewhere, not by how often the swapchain itself actually presents. This
>    was always an implicit consequence of `PHASE0_MASTER_STRATEGY.md`'s own
>    Locked Design Decision 4 discussion, but this phase is the actual
>    mechanism that locks it in, so it is called out here directly — a future
>    maintainer must not "fix" this by giving the pipelined regime its own
>    separate counter without first re-reading why this is intentional.

Parent: `PHASE0_MASTER_STRATEGY.md`. Depends on: Phase 1
(`RenderGraphDebugTextureRegistry`).

## Step 1: The Goal (Where are we going?)

Make `RenderGraph` own a `RenderGraphDebugTextureRegistry` and keep it
automatically, passively up to date every single time
`ExecuteCompiledGraph()` resolves a texture — for BOTH `ExecuteTimingMode`
regimes, with zero new passes, zero culling risk, and zero behavior change
to anything `RenderGraph` already does. Also establish the one shared,
monotonically increasing "engine frame" counter Locked Design Decision 4
needs, and expose four new small public methods on `RenderGraph` for later
phases to call: `DebugTextureSnapshotFor()`, `ListDebugTextures()`,
`NotifyDebugTextureStateOverride()`, and
`CurrentDebugTextureFrameCounter()`.

## Step 2: The Situation (Where are we now?)

- `RenderGraph::ExecuteCompiledGraph()` (`RenderGraph.cpp`) is the ONE place
  every texture handle a pass declared gets resolved
  (`EnsureTextureResolved()`, called from inside the per-usage barrier
  loop, via `ApplyUsageBarrierIfNeeded()`) into a real `PhysicalTexture` —
  `{ bool resolved; bool isImported; bool hasDepth; RenderTarget target;
  VkSampler sampler; ResourceState colorState; ResourceState depthState; }`
  (`RenderGraph.h`, private — field order and shape reconfirmed live, exact
  match). This vector (confirmed live: the real local variable is named
  `physicalTextures`, declared as `std::vector<PhysicalTexture>
  physicalTextures(input.textureDescs.size());` near the top of
  `ExecuteCompiledGraph()`) is local to ONE `ExecuteCompiledGraph()` call and
  discarded when it returns. This phase's job is to walk it ONE more time,
  right before it goes out of scope, and feed every RESOLVED entry into the
  registry.
- `CompiledGraphInput::textureNames` (`RenderGraphBuilder.h`) is a
  `std::vector<const char*>`, PARALLEL (same index) to
  `CompiledGraphInput::textureDescs`/`textureImportInfo`, and — critically —
  also parallel to the `physicalTextures`/`PhysicalTexture` vector indexed
  the same way (`EnsureTextureResolved(index, input, physicalTextures)` is
  called with a SHARED `index` against both `input` and `physicalTextures`
  throughout `ExecuteCompiledGraph()`). This is exactly the join key this
  phase needs: `input.textureNames[i]` is the name for `physicalTextures[i]`.
  Confirmed live: `RenderGraphBuilder::CreateTexture()`/`ImportTexture()`
  both push onto `m_textureDescs`/`m_textureNames`/`m_textureImportInfo`
  together, in the same call, so the three (and therefore
  `physicalTextures`, sized off `textureDescs.size()`) can never drift out of
  parallel alignment.
- **Not every declared texture handle is necessarily resolved by the end of
  a call** — `EnsureTextureResolved()` is only ever invoked for a handle
  actually referenced by SOME pass's `reads`/`writes` while that pass's own
  barrier is being emitted (`ApplyUsageBarrierIfNeeded()`), and a CULLED
  pass's usages are simply never visited at all (`RenderGraph::
  ExecuteCompiledGraph()`'s own per-pass loop — `for (const PassHandle&
  passHandle : compiled.executionOrder)` — only ever iterates
  `compiled.executionOrder`, which is already the post-culling, surviving-
  passes-only list produced by `RenderGraphCompiler::Compile()`; confirmed
  live, matching this bullet's original claim exactly).
  This phase's own registration loop must therefore check
  `physicalTextures[i].resolved == true` before calling `Upsert()` for index
  `i` — an unresolved entry this call is simply skipped (its PREVIOUS
  registry entry, if any, from an earlier call where it WAS resolved,
  is deliberately left untouched — never cleared — so a texture that
  temporarily stops being declared for a few frames, e.g. a hidden Editor
  panel, keeps serving its last-known-good snapshot rather than vanishing
  from the registry entirely; this is the exact same "silently serve
  last-known value" convention `RenderGraph::LastKnownStatsFor()` already
  established for pass stats).
- `RenderGraphResourcePool::BeginFrame()` (`RenderGraphResourcePool.h/.cpp`)
  is already called from inside `ExecuteCompiledGraph()`, but — per
  `RenderGraph.h`'s own top-of-file comment — **only for the
  `SynchronousImmediateReadback` call**, since that one is documented to
  always run FIRST each engine frame ("By CONVENTION... the
  SynchronousImmediateReadback call happens FIRST each frame... and is the
  ONE call that triggers RenderGraphResourcePool::BeginFrame()"). **Confirmed
  live, exactly:** `const bool isPipelined = (timingMode ==
  ExecuteTimingMode::PipelinedDeferredReadback);` is computed at the very top
  of `ExecuteCompiledGraph()`, and the existing call is
  `if (!isPipelined) { m_resourcePool.BeginFrame(); }`. This is the exact,
  already-existing "this is a brand new REAL engine frame, not just a second
  Execute() call within the same one" signal this phase's own frame-counter
  increment should piggyback on — re-confirm this one-line condition hasn't
  drifted at implementation time (cheap to check, and this document has been
  wrong about smaller things before), but there is no ambiguity left to
  resolve here.
- `ResourceState` (`RenderGraphBarrierPlanner.h`) is exactly what
  `DebugTextureSnapshot::colorState`/`depthState` need — already imported
  transitively via Phase 1's own header.
- **Namespace note (see this file's own audit note above):** `RenderGraph`
  itself is declared inside `namespace gte::rg { ... }` — every code sample
  in this phase's own Step 3 below therefore uses BARE type names
  (`DebugTextureSnapshot`, `ResourceState`, ...), never a self-qualified
  `rg::` prefix, matching this file's own pre-existing style
  (`LastKnownStatsFor()`/`LastSnapshot()`). Do not add an `rg::` prefix back
  in when implementing this phase — it is not wrong, but it is not this
  file's convention, and Phase 1's own header already establishes the bare
  spelling as the one to match.

## Step 3: The Plan

### 3.1 — `RenderGraph.h` changes

- `#include "RenderGraphDebugTextureRegistry.h"` (new).
- New private member: `RenderGraphDebugTextureRegistry m_debugTextures;`
  (declare near `m_lastKnownStats`/`m_synchronousSnapshot` — same "per-run
  cached, queryable state" family of members).
- New private member: `std::uint64_t m_debugTextureFrameCounter = 0;`
  (declare alongside `m_pipelinedFrameCounter` — same kind of monotonic
  counter, different purpose/cadence: this one increments once per REAL
  engine frame, i.e. once per `SynchronousImmediateReadback` call, per Step
  2 above — NOT once per `ExecuteCompiledGraph()` call in general, since
  that would double-count relative to a real engine frame given both
  regimes share it).
- New public methods, declared near `LastKnownStatsFor()`/`LastSnapshot()`:

```cpp
// network-impl-4 campaign, Phase 2
// (task_manager/network-impl-4/PHASE2_RENDERGRAPH_INTEGRATION_AND_AUTO_REGISTRATION.md) -
// the query surface behind GET /get_texture and GET /list_textures (see
// src/Application/FrameCaptureBridge.h and src/Network/NetworkRoutes.h,
// Phases 4/5). `name` is compared as a plain std::string - see
// RenderGraphDebugTextureRegistry::FindByName()'s own doc comment for why
// this is safe against an arbitrary, HTTP-supplied string. Returns
// std::nullopt if this RenderGraph has never resolved a texture under this
// exact name this session.
//
// NOTE (bare names, no "rg::" prefix): this class is already declared
// inside namespace gte::rg, so DebugTextureSnapshot/ResourceState below
// refer to gte::rg::DebugTextureSnapshot/gte::rg::ResourceState directly -
// matching this header's own pre-existing style (see LastSnapshot() below).
std::optional<DebugTextureSnapshot> DebugTextureSnapshotFor(const std::string& name) const;

// Every texture name/snapshot ever registered this session - the primitive
// behind GET /list_textures.
std::vector<DebugTextureSnapshot> ListDebugTextures() const;

// PHASE0_MASTER_STRATEGY.md's Locked Design Decision 7 - called ONLY from
// the small number of call sites that perform a graph-EXTERNAL manual
// image-layout transition on an already-registered named texture right
// after this RenderGraph's own ExecuteCompiledGraph() call returns (today:
// Application::Run()'s two FinalizeRenderTextureForExternalSampling() call
// sites for "GameView"/"SceneView", FramePresenter.cpp's own "Swapchain"
// finalize, and ComputeBlurValidation::FinalizeForSampling()'s
// "BlurredSceneOutput" finalize - see Phase 3, which corrected an earlier
// revision's "only two call sites" undercount). A safe no-op if `name`
// is not yet a known entry (see RenderGraphDebugTextureRegistry::
// ApplyColorStateOverride()'s own doc comment).
void NotifyDebugTextureStateOverride(const std::string& name, const ResourceState& newColorState);

// The registry's own current monotonic "engine frame" counter value (see
// RenderGraph.h's own m_debugTextureFrameCounter member doc comment) - a
// caller (Phase 4/5) computes a snapshot's own "frames_since_update" as
// `CurrentDebugTextureFrameCounter() - snapshot.lastUpdatedFrameCounter`
// at the exact moment it services a request, never cached/stale.
//
// IMPORTANT (see this phase's own Step 3.3a below): this counter only ever
// advances during a SynchronousImmediateReadback call. A texture that is
// ONLY ever registered by the PipelinedDeferredReadback regime (today:
// "Swapchain") therefore has its own "frames_since_update" freshness signal
// driven entirely by how often the OFFSCREEN (Game/Scene) regime executes
// elsewhere - NOT by how often that texture itself actually updates. This
// is intentional (see PHASE0_MASTER_STRATEGY.md's own Locked Design
// Decision 4 caveat), not a bug to fix by adding a second counter.
std::uint64_t CurrentDebugTextureFrameCounter() const noexcept;
```

(These are plain forwarders to `m_debugTextures`'s own methods (plus a
trivial accessor for the counter itself) — no extra logic needed beyond the
forwarding itself; keep them one-liners.)

**Include hygiene (minor, optional):** `std::string`/`std::optional` end up
available in `RenderGraph.h` transitively today (via `RenderGraphSnapshot.h`
and `RenderGraphTypes.h`/`RenderGraphTimestampPool.h` respectively, both
already included by this header) — so the methods above compile without any
further include changes. It is still slightly more robust to add explicit
`#include <string>`/`#include <optional>` to `RenderGraph.h` directly rather
than depending on a neighboring header's own include list staying the same
forever; do this if convenient, but it is not required for correctness.

### 3.2 — `RenderGraph.cpp` changes

**Frame-counter increment** — immediately alongside (same `if` body as) the
existing `m_resourcePool.BeginFrame();` call. Confirmed live (see Step 2
above): the real, exact condition is `!isPipelined`, where `isPipelined` is
already computed at the top of `ExecuteCompiledGraph()` — do not reintroduce
a placeholder here, but do re-glance at the live function before editing in
case it has drifted since this audit:

```cpp
if (!isPipelined) {
    m_resourcePool.BeginFrame();
    ++m_debugTextureFrameCounter; // network-impl-4, Phase 2 - see RenderGraph.h's own doc comment on this member.
}
```

**Registry population** — at the very END of `ExecuteCompiledGraph()`,
after every pass has run and every barrier emitted, but BEFORE
`physicalTextures` goes out of scope (i.e. before the function returns —
place this either right after the existing `RenderGraphSnapshot snapshot =
BuildRenderGraphSnapshot(...)` block's own `if (!isPipelined) { ... } else {
... }` assignment, or right before it; order relative to the snapshot-build
does not matter, since neither reads the other's output):

```cpp
// network-impl-4 campaign, Phase 2 - passive registration: every texture
// this call actually resolved becomes (or stays) queryable by name via
// DebugTextureSnapshotFor()/ListDebugTextures(), regardless of which
// ExecuteTimingMode this call was. An unresolved index this call is
// skipped, deliberately leaving any PREVIOUS entry for that name
// untouched - see PHASE2's own Step 2 analysis for why.
for (std::size_t i = 0; i < physicalTextures.size(); ++i) {
    const PhysicalTexture& tex = physicalTextures[i];
    if (!tex.resolved) {
        continue;
    }
    const char* name = input.textureNames[i];
    if (name == nullptr || name[0] == '\0') {
        continue; // Defensive - every real call site always supplies a real name (RenderGraphBuilder::CreateTexture()/ImportTexture() both assert a non-null/non-empty name), but never trust that blindly here (an assert compiles out entirely in a release/NDEBUG build).
    }

    DebugTextureSnapshot snapshot;
    snapshot.name = name;
    snapshot.regime = timingMode; // ExecuteCompiledGraph()'s own parameter - confirmed live, exact spelling.
    snapshot.target = tex.target;
    snapshot.hasDepth = tex.hasDepth;
    snapshot.colorState = tex.colorState;
    snapshot.depthState = tex.depthState;
    snapshot.lastUpdatedFrameCounter = m_debugTextureFrameCounter;
    m_debugTextures.Upsert(snapshot);
}
```

Also add the four thin forwarders from 3.1 (bare type names, per this
phase's own audit note above):

```cpp
std::optional<DebugTextureSnapshot> RenderGraph::DebugTextureSnapshotFor(const std::string& name) const
{
    return m_debugTextures.FindByName(name);
}

std::vector<DebugTextureSnapshot> RenderGraph::ListDebugTextures() const
{
    return m_debugTextures.ListAll();
}

void RenderGraph::NotifyDebugTextureStateOverride(const std::string& name, const ResourceState& newColorState)
{
    m_debugTextures.ApplyColorStateOverride(name, newColorState);
}

std::uint64_t RenderGraph::CurrentDebugTextureFrameCounter() const noexcept
{
    return m_debugTextureFrameCounter;
}
```

### 3.3 — A note on `PipelinedDeferredReadback`'s own registered snapshot

The "Present" pass's own swapchain image handle (imported fresh, every
frame, under the TEXTURE name `"Swapchain"` — confirmed live in
`FramePresenter.cpp`: `builder.ImportTexture("Swapchain", target,
VK_IMAGE_LAYOUT_UNDEFINED);` — NOT `"Present"`, which is only ever a PASS
name; see `PHASE0_MASTER_STRATEGY.md`'s own audit note for the full
correction history on this point) will register into this SAME registry
automatically, with `regime == PipelinedDeferredReadback`. This is fine and
expected — `/get_texture` (Phase 4) can capture it too, exactly like any
other name, via the `vkDeviceWaitIdle()` stall Locked Design Decision 1
accepts. No special-casing is needed here; do not add any "skip Present/skip
pipelined regime" filter to this phase's population loop.

### 3.3a — Consequence worth understanding: the frame counter is SHARED across regimes, but only advanced by ONE of them

Every `Upsert()` call in this phase's population loop — for BOTH regimes —
stamps `lastUpdatedFrameCounter` with whatever `m_debugTextureFrameCounter`
currently holds. But that counter is only ever incremented inside the
`if (!isPipelined)` block above, i.e. **only** during a
`SynchronousImmediateReadback` call. A direct, concrete consequence:

- `"GameView"`/`"SceneView"` (synchronous-regime textures) get a
  `frames_since_update` that behaves exactly as `PHASE0_MASTER_STRATEGY.md`'s
  Locked Design Decision 4 describes — "how many renders since this was last
  produced".
- `"Swapchain"` (a pipelined-regime texture) gets `lastUpdatedFrameCounter`
  re-stamped with the counter's CURRENT value every single time it is
  re-imported/re-registered (i.e. on every real Present), but that value
  itself only ever changes when the SYNCHRONOUS regime happens to run. In a
  session where the offscreen regime runs every real engine frame (the
  common Editor case, both "Game"/"Scene" panels visible), this is
  unobservable — the counter advances just as often as Swapchain updates,
  so its `frames_since_update` still reads close to 0 right after a request.
  But in a session where the offscreen regime runs rarely or never (both
  panels hidden, or any `-DGTE_ENABLE_EDITOR=OFF` build, which has no
  Game/Scene targets at all and therefore never triggers a
  `SynchronousImmediateReadback` call), `m_debugTextureFrameCounter` stays
  frozen (at 0, in the `GTE_ENABLE_EDITOR=OFF` case, for the entire process
  lifetime) — so `"Swapchain"`'s own `frames_since_update` will report a
  constant, always-fresh-looking `0` (or whatever the frozen value implies)
  regardless of how many real frames have actually presented.

This is an accepted, intentional consequence of Locked Design Decision 4's
own "renders since the offscreen regime last executed" definition — it is
NOT a bug this phase needs to fix, and it must NOT be "fixed" later by
giving the pipelined regime its own separate counter (that would silently
change what `frames_since_update` means for `"GameView"`/`"SceneView"` too,
since Phase 4/5 read it via the SAME shared
`CurrentDebugTextureFrameCounter()` accessor for every texture regardless of
regime). It is called out explicitly here, rather than left only as a
general aside in `PHASE0_MASTER_STRATEGY.md`, because this phase is the
exact place that mechanism gets implemented — Phase 6's documentation
updates (`AGENTS.md`) should mention this specific "Swapchain in a headless/
hidden-panel session always reads as maximally fresh" caveat explicitly, not
just the generic "measures renders, not wall-clock" one.

### 3.4 — What this phase deliberately does NOT do

- Does not add the `NotifyDebugTextureStateOverride()` CALL SITES for
  "GameView"/"SceneView"/"Swapchain"/"BlurredSceneOutput" yet — that is
  Phase 3 (it also needs Phase 3's own understanding of exactly what
  `ResourceState` value `ShaderRead`/`PRESENT_SRC_KHR` resolves to, via
  `RequiredStateFor()`).
- Does not touch `Renderer`/`FrameCaptureBridge`/`Application`/`Network` at
  all — Phases 3/4/5.
- Does not change `ExecuteCompiledGraph()`'s existing behavior, return
  value, or performance characteristics in any observable way — the new
  loop is O(declared textures this call), same order of magnitude as the
  barrier-emission loop it sits right next to, and does zero Vulkan calls
  of its own (pure CPU bookkeeping).

### 3.5 — Tests

- Extend Phase 1's own `RenderGraphDebugTextureRegistryTests.cpp` only if
  this phase's `RenderGraph.cpp` changes expose any NEW pure logic worth
  isolating (in practice, the population loop is thin enough to stay
  inline and untested in isolation — it is exercised indirectly by any
  existing/future `RenderGraph`-level test, which remains Tier 2 per
  `AGENTS.md`'s existing "no live-VkDevice automated coverage yet" bucket,
  since `RenderGraph.cpp` — unlike Phase 1's own pure registry class —
  genuinely touches live Vulkan handles).
- **Fast compile check**: `cmake --build build` (or the project's existing
  incremental build command) must succeed with zero new warnings before
  moving to Phase 3 — this phase touches a widely-included header
  (`RenderGraph.h`), so a full rebuild of every translation unit that
  includes it is expected; do not skip this check.
- Manual verification is deferred to Phase 4 (nothing is wired to actually
  QUERY the registry from outside `RenderGraph` itself until then) —
  this phase's own correctness is confirmed purely by compiling cleanly
  and passing Phase 1's existing tests unchanged.
