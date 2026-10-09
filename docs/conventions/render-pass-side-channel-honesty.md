# Render Pass Side-Channel Honesty

This convention is a genuinely additional, NARROWER layer on top of
[`render-pass-toggle-honesty.md`](render-pass-toggle-honesty.md) — read that file
first. That file's own iron rule ("a render pass's declared/enabled state and
the Frame Debugger's own displayed event tree must NEVER disagree") is restated
and EXTENDED here with two clauses that campaign's own detector could not
enforce, both closed by the `editor-core-separation-22` campaign
("The Engine Is STILL Lying"):

> **Clause B**: if a pass runs (survives culling, is non-culled in the real
> `RenderGraphSnapshot`), the Frame Debugger's own tree MUST show it as a real
> leaf somewhere. There is no acceptable middle ground, and no pass is exempt
> except a small, explicitly-named, permanently-documented allowlist of the
> Frame Debugger's OWN ephemeral internal replay scaffolding
> (`RenderPassCategory::FrameDebuggerInternal`) — never a whole category shared
> with real user-facing features.
>
> **Clause C**: a pass's own toggle-off state must gate EVERY observable side
> effect its own declaration code produces — not only whether its own
> `RenderPassDesc` reaches the graph, but also any data (blackboard publish,
> cached callback, member-variable mutation) that some OTHER, independently
> toggled pass might read and reproduce that effect from, regardless of the
> first pass's own disabled state.

## The rule: gate every side effect, not just the `RenderPassDesc`

`render-pass-toggle-honesty.md` already establishes the guard pattern
(`rg::ShouldDeclareBuiltInPassThisFrame(registry, name)`, read-only, run
BEFORE any side effect) for whether a pass's own
`RenderPassDesc` reaches `builder.AddRenderPass()`. This convention closes a
DIFFERENT, subtler gap: a provider can be perfectly honest about whether ITS
OWN pass declares, while still leaking a side effect that happens BEFORE that
decision is even made.

**The worked example this campaign fixed**: `Core.cpp`'s `"DrawSkyBackground"`
provider used to build a `recordSkyBackground` callback and
`frame.blackboard.Publish<...>(kGameSkyBackgroundCallbackKey, recordSkyBackground)`
it — UNCONDITIONALLY, before any toggle check of any kind. The generic,
late-stage toggle gate (`RenderPipeline::DeclareOnePhase()`) correctly,
honestly removed the `"DrawSkyBackground"` pass itself from the graph once
disabled — but it does nothing to undo a `Publish()` call that already
happened as a side effect of merely invoking the provider lambda. A completely
different, honestly-enabled pass (`FrameDebuggerReplayPasses.cpp`'s own "sky
step", gated by its own, unrelated `"FrameDebuggerReplay"` toggle) then
`Fetch()`ed that stale callback and redrew the sky anyway — the disabled
pass's own visual effect survived it, smuggled through a side channel that
never checked the first pass's own toggle at all.

**The fix, and the mandated pattern for any FUTURE provider that produces a
side effect**: use
`src/Renderer/RenderGraph/RenderPassToggleGuard.h`'s
`ShouldDeclareBuiltInPassThisFrame(registry, name)` — deliberately smaller and
more generic than `AtmospherePassToggleLogic.h`'s own
`ShouldDeclareAtmospherePassThisFrame()` (no upstream-handle-validity concept
folded in) — as the VERY FIRST statement in your provider's lambda body,
before `FindViewData()`, before building any callback, before ANY
`frame.blackboard.Publish()` call:

```cpp
if (!rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "YourPassName")) {
    return;
}
```

**Checklist for anyone adding a new `RenderPipeline` provider**: does this
provider publish to the blackboard, cache a callback, or mutate a member
variable another system reads? If yes, does an early
`ShouldDeclareBuiltInPassThisFrame()` guard run BEFORE that, not just before
`out.push_back(desc)`? If the answer to the second question is no, you have
just written this exact bug class — fix it before landing the change.

## `RenderPassCategory::Debug` vs `RenderPassCategory::FrameDebuggerInternal`

These two categories look similar but mean OPPOSITE things, and confusing them
is exactly Root Cause #2 of the `editor-core-separation-22` campaign:

- **`RenderPassCategory::Debug`** means "a real, optional/debug-flavored
  FEATURE pass — fully visible in the Frame Debugger tree like any other
  survivor, when it runs." `PluginRenderPassBuilderAdapter.cpp`'s
  `AddFullscreenClearPass()`, `GBufferValidation.cpp`, and
  `ComputeBlurValidation.cpp` all correctly use this value today.
- **`RenderPassCategory::FrameDebuggerInternal`** means "Frame-Debugger-internal
  replay scaffolding — never a real tree citizen itself, already filtered out
  or shown under its own separate heading." Only
  `src/Editor/FrameDebuggerReplayPasses.cpp`'s own ephemeral
  `FrameDebuggerReplayStepN` passes use this value — this is a small,
  explicitly-named, permanently-documented allowlist, never a whole category
  shared with real user-facing features.

**When adding a new pass, ask: is this pass something the user can see running
and might want to inspect, even if it is debug/optional?** If yes, use
`Debug` (or `General`, if it is not debug-flavored at all). Reserve
`FrameDebuggerInternal` EXCLUSIVELY for scaffolding that exists purely to feed
the Frame Debugger's own replay/preview mechanism and would be meaningless as
an independently-selectable tree leaf.

## The "Other Render Passes" fallback bucket

`src/Editor/FrameDebuggerData.cpp`'s `BuildRealFrameDebuggerSnapshot()` tracks,
per real execution-order index, whether that index has been "claimed" by any
of its existing buckets (the pre-GameView compute sweep, the view-region walk,
the post-GameView compute sweep) — claimed either because it became a real
leaf, or because it was deliberately, honestly excluded for an
already-documented reason (culled, `ViewScope::SceneView`,
`RenderPassCategory::FrameDebuggerInternal`). A final, generic sweep then
builds an `"Other Render Passes"` group (only appended if non-empty)
containing one leaf per UNCLAIMED, non-culled, non-`SceneView`,
non-`FrameDebuggerInternal` survivor, of EITHER `PassKind`, at ANY execution
index — this is what makes the tree STRUCTURALLY incapable of silently
dropping a real survivor, by construction, rather than merely "so far,
nothing has been observed to slip through."

## The permanent, automatic bidirectional detectors

Alongside `editor-core-separation-21`'s own untouched, still-passing Clause A
detector (`RenderPassHonestyChecker.h`/`RenderPassHonestyGuard.h`,
`GET /get_logs?category=RenderPassHonesty`), two new detectors close Clause B
and Clause C automatically, wired into the same
`FrameDebuggerPanel::TriggerCapture()` chokepoint:

- **Clause B** — `src/Editor/FrameDebuggerCoverageChecker.h`/`.cpp`
  (`DetectPassesMissingFromFrameDebuggerTree()`, pure/Tier-1-tested) +
  `FrameDebuggerCoverageGuard.h`/`.cpp` (the Logger-aware singleton wrapper).
  Fires `GTE_LOG_ERROR("FrameDebuggerCoverage", ...)`, retrievable via
  `GET /get_logs?category=FrameDebuggerCoverage`, naming any pass that
  genuinely ran this frame (non-culled, non-`FrameDebuggerInternal`,
  non-`SceneView`) but has no corresponding leaf anywhere in the captured
  tree.
- **Clause C** — `src/Editor/FrameDebuggerSideChannelChecker.h`/`.cpp`
  (`DetectDisabledPassBlackboardKeyLeaks()`, pure/Tier-1-tested) +
  `FrameDebuggerSideChannelGuard.h`/`.cpp`. Fires
  `GTE_LOG_ERROR("FrameDebuggerSideChannel", ...)`, retrievable via
  `GET /get_logs?category=FrameDebuggerSideChannel`, naming any
  `RenderPassBlackboard` key that was published this frame even though its own
  gating toggle is disabled.

**Honest, permanent limitation of the Clause C detector, stated plainly**:
unlike Clause A/B, Clause C is a **curated allowlist-based check, not a fully
general one** — it covers exactly the blackboard keys hand-added to
`FrameDebuggerSideChannelChecker.h`'s `KnownRiskBlackboardKeyRules()` (4
entries as of this writing: `"Atmosphere.GameSkyBackgroundCallback"` gated by
`"DrawSkyBackground"`; `"GpuSkinning.OutputBuffers"` gated by `"GpuSkinning"`;
`"Atmosphere.CompositedOutput.Game"`/`"...Scene"` gated by
`"AtmosphereComposite"`). A future new blackboard key with this exact risk
shape (a provider Publishes a value some OTHER, independently-toggled pass
reads back to reproduce a visual/behavioral effect) that nobody manually adds
to this list is a real, permanent, accepted gap — **whoever adds such a key
MUST also add a matching entry to `KnownRiskBlackboardKeyRules()`**, mirroring
the existing 4 entries' own shape exactly (a hardcoded copy of the real
`Core.cpp` string literal, with a comment noting what it must stay in sync
with — the same "duplicate a hardcoded engine constant" pattern
`FrameDebuggerData.cpp`'s own `kFrameDebuggerGameClearColor` already
established). It also does not catch a side-channel leak shaped differently —
e.g. a plain `Core`-owned member variable never routed through the blackboard
at all (the `GpuDrivenBatches` entity-exclusion bug this same campaign's
PHASE3 fixed is exactly this OTHER shape; its own regression protection is its
own dedicated `GpuDrivenBatchEntityExclusionLogicTests.cpp`, not this
detector).

## What this convention does NOT cover

- `RenderFeatureCompositor`'s own per-feature `enabledOverride` filter remains
  the same genuinely-different, already-honest mechanism
  `render-pass-toggle-honesty.md`'s own "What this convention does NOT cover"
  section describes — unaffected by anything in this file.
- A pass's OWN toggle-registry consult (Clause A, `render-pass-toggle-honesty.md`)
  is a completely separate obligation from the side-effect-gating obligation
  this file describes — a pass can satisfy one and still violate the other
  (this is exactly what made `"DrawSkyBackground"`'s own bug hard to spot: its
  own registry consult, added generically by `RenderPipeline::DeclareOnePhase()`,
  was already honest; the leak lived entirely inside the provider lambda's own
  body, executed BEFORE that generic consult ever ran).
