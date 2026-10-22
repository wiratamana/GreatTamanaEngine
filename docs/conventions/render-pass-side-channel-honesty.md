# Render Pass Side-Channel Honesty

This convention is a narrower layer on top of
[`render-pass-toggle-honesty.md`](render-pass-toggle-honesty.md). Read that file
first. Its iron rule (a pass's enabled state and the Frame Debugger's event tree
must never disagree) is extended here with two clauses:

> **Clause B**: if a pass runs (survives culling, is non-culled in the real
> `RenderGraphSnapshot`), the Frame Debugger's tree MUST show it as a real leaf.
> No pass is exempt, except the Frame Debugger's own replay scaffolding
> (`RenderPassCategory::FrameDebuggerInternal`). That is a small, named
> allowlist. It is never a whole category shared with user-facing features.
>
> **Clause C**: a pass's toggle-off state must gate EVERY observable side effect
> of its declaration code. This covers whether its pass reaches the graph, and
> also any data (blackboard publish, cached callback, member mutation) that
> another, independently toggled pass could read to reproduce the effect.

## Gate every side effect, not just the pass

A provider can be honest about whether ITS OWN pass is declared and still leak a
side effect that happens before that decision. Example of the bug class: a sky
provider publishes its draw callback to the blackboard unconditionally. The
generic toggle gate removes the sky pass from the graph. The published callback
stays. A different, enabled pass (the Frame Debugger replay) fetches it and
redraws the sky anyway.

Current design, which avoids it:

- The sky background is `f.ViewBackground(shader, "DrawSkyBackground")` in the
  feature kit. The verb publishes the replay callback (`rg::FullscreenReplay`,
  key `kViewBackgroundReplayCallbackKey` = `"Core.ViewBackgroundReplayCallback"`,
  `src/Core/ViewBackgroundReplayKey.h`) only when its pass was actually declared
  (`src/Core/FeatureKit/FullscreenVerb.cpp`). A disabled pass publishes nothing.
- `Core` fetches that key once per offscreen frame and passes it to the Frame
  Debugger replay passes.

Rules for any new provider or kit callback with side effects:

- Engine provider (`RenderPipeline`): the first statement of the lambda is
  `rg::ShouldDeclareBuiltInPassThisFrame(registry, "YourPassName")`. It comes
  before `FindViewData()`, before building a callback, before any
  `blackboard.Publish()`:

```cpp
if (!rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "YourPassName")) {
    return;
}
```

- Feature callback: publish to a `fx::Channel` only after the verbs that produce
  the data declared their pass. Ask `f.PassEnabled("PassName")` before creating
  textures. `AtmosphereFeature::SharedLuts` is the reference: it publishes
  `SharedLuts` only when both LUT passes were declared.

Checklist for a new provider: does it publish to the blackboard, cache a
callback, or mutate a member another system reads? If yes, does the guard run
BEFORE that, not just before `out.push_back(desc)`? If not, you wrote this bug.

## `RenderPassCategory::Debug` vs `RenderPassCategory::FrameDebuggerInternal`

The two look alike and mean opposite things.

- **`Debug`**: a real, optional or debug-flavored feature pass. It is a normal
  leaf in the Frame Debugger tree when it runs.
- **`FrameDebuggerInternal`**: Frame Debugger replay scaffolding. It is never a
  tree leaf itself. Only the `FrameDebuggerReplayStepN` passes in
  `src/Editor/FrameDebuggerReplayPasses.cpp` use it.

Ask: can the user see this pass running and want to inspect it? If yes, use
`Debug` (or `General` when it is not debug-flavored). Use `FrameDebuggerInternal`
only for scaffolding that is meaningless as a selectable leaf.

## The "Other Render Passes" fallback bucket

`BuildRealFrameDebuggerSnapshot()` (`src/Editor/FrameDebuggerData.cpp`) tracks,
per execution-order index, whether a bucket "claimed" it (the pre-GameView
compute sweep, the view-region walk, the post-GameView compute sweep). A pass is
claimed when it became a leaf, or was excluded for a documented reason (culled,
`ViewScope::SceneView`, `FrameDebuggerInternal`). A final sweep builds an
`"Other Render Passes"` group (added only when non-empty). It holds one leaf per
unclaimed, non-culled, non-`SceneView`, non-`FrameDebuggerInternal` survivor, of
any `PassKind`, at any index. The tree cannot silently drop a surviving pass.

## The automatic detectors

Both run in `FrameDebuggerPanel::TriggerCapture()`, next to the Clause A detector
(`RenderPassHonestyChecker`, `GET /get_logs?category=RenderPassHonesty`).

- **Clause B**: `src/Editor/FrameDebuggerCoverageChecker.h/.cpp`
  (`DetectPassesMissingFromFrameDebuggerTree()`) and `FrameDebuggerCoverageGuard`.
  Logs `GTE_LOG_ERROR("FrameDebuggerCoverage", ...)`, read it with
  `GET /get_logs?category=FrameDebuggerCoverage`. It names any pass that ran
  (non-culled, non-`FrameDebuggerInternal`, non-`SceneView`) but has no leaf in
  the captured tree.
- **Clause C**: `src/Editor/FrameDebuggerSideChannelChecker.h/.cpp`
  (`DetectDisabledPassBlackboardKeyLeaks()`) and `FrameDebuggerSideChannelGuard`.
  Logs `GTE_LOG_ERROR("FrameDebuggerSideChannel", ...)`, read it with
  `GET /get_logs?category=FrameDebuggerSideChannel`. It names any blackboard key
  that was published this frame while its gating toggle is disabled.

**Limit of the Clause C detector**: it is a curated list, not a general check.
It covers only the keys in `KnownRiskBlackboardKeyRules()`
(`src/Editor/FrameDebuggerSideChannelChecker.cpp`):

| Key | Gating toggle |
| --- | --- |
| `Core.ViewBackgroundReplayCallback` | `DrawSkyBackground` |
| `GpuSkinning.OutputBuffers` | `GpuSkinning` |
| Shadow `Map` channel, Game and Scene view | `Shadow.Cascade0.Draw` |
| Shadow `Mask` channel, Game and Scene view | `Shadow.Mask.Dispatch` |
| Atmosphere `SharedLuts` shared channel | `AtmosphereTransmittanceLutPass` and `AtmosphereMultiScatteringLutPass` (published only when both ran) |
| Atmosphere `AerialVolume` channel, Game and Scene view | `AtmosphereAerialPerspectiveVolumePass` (published only when the pass ran) |

Feature channel keys are built with `fx::ChannelKey(feature, channel, view)`, so
the rule list never repeats a string literal for them. Whoever adds a new
blackboard key with this risk shape (a value another, independently toggled pass
reads back to reproduce an effect) MUST add a rule here. Do not add a rule for a
channel that is published with invalid textures when its pass is off (for
example the Atmosphere per-view `ViewLuts`, which is deliberately excluded). It would report false leaks.

The detector does not catch leaks that bypass the blackboard, such as a plain
member variable read by another system.

## What this convention does NOT cover

- `RenderFeatureCompositor`'s per-feature `enabledOverride` filter is the
  different, already-honest mechanism described in
  `render-pass-toggle-honesty.md`.
- A pass's own registry consult (Clause A) and its side-effect gating (Clause C)
  are separate obligations. A pass can satisfy one and still break the other.
  The generic gate in `RenderPipeline::DeclareOnePhase()` removes a disabled
  pass from the graph, but it cannot undo a `Publish()` that already ran inside
  the provider.
