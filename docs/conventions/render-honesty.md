# Render honesty
> Rules that keep pass toggles, the Frame Debugger tree and hidden side effects in agreement. For anyone who adds or changes a render pass.
> Verified against code: 2026-10-30

## TL;DR
- Iron rule: a pass's enabled state and the Frame Debugger tree must never disagree.
- Clause A: a disabled pass is not declared and not shown as executed.
- Clause B: a pass that runs is a leaf in the Frame Debugger tree.
- Clause C: a disabled pass leaves no side effect. No blackboard publish, no cached callback, no member change.
- Three detectors check this on every Frame Debugger capture. They log under their own categories.

## Rules
1. A disabled pass must not run. It must not appear as an executed leaf in the Frame Debugger or the Render Graph panel.
2. A pass that runs must be a leaf in the Frame Debugger tree. Only `RenderPassCategory::FrameDebuggerInternal` is exempt.
3. Toggle-off must gate every side effect of the declaration code. This includes `Publish()`, cached callbacks and member writes.
4. Engine provider: the first statement is `rg::ShouldDeclareBuiltInPassThisFrame(registry, "Name")`. It comes before `FindViewData()` and before any `Publish()`.
5. Kit callback: the verbs (`Compute`, `Fullscreen`, `ViewBackground`, `SceneDraw`, `Blit`) check the toggle for you.
6. Work before a verb is a side effect. This means `Acquire`, resize and member writes. Ask `f.PassEnabled("Name")` first and return early.
7. Publish to a channel only after the verbs that make the data declared their pass.
8. A callback with several toggled passes lists all names in its `fx::Gate`. Each pass still needs its own `PassEnabled` check.
9. Use `fx::Gate::Always()` only for work that is not a render pass.
10. A dynamic set of pass names (GPU skinning, replay steps) uses one umbrella toggle: `GpuSkinning` or `FrameDebuggerReplay`.
11. A pass with its own feature toggle still consults the registry. The feature toggle and the registry are two separate layers.
12. A new blackboard key that another toggled pass could read to rebuild an effect needs a rule in `KnownRiskBlackboardKeyRules()`.

## Do and Don't
| Do | Don't |
|---|---|
| Check the registry first, then do any work | Publish to the blackboard, then check the toggle |
| `if (!f.PassEnabled("X")) return;` before `Acquire` | Create or resize a texture for a pass that is off |
| Give a pass a stable literal name that matches its toggle | Declare a pass under a name the panel does not show |
| Use `Debug` category for a real optional pass | Use `FrameDebuggerInternal` for a pass users want to inspect |
| Mark replay scaffolding `FrameDebuggerInternal` | Hide a feature pass behind that category |
| Add a keep list when a channel's pass output has no reader | Rely on the compiler to keep an unread output |

## How to
Engine provider:
```cpp
if (!rg::ShouldDeclareBuiltInPassThisFrame(&m_renderPassToggleRegistry, "YourPassName")) {
    return; // never partially declare
}
const rg::RenderFeatureScope scope(builder, "Your Feature Name");
```

Kit callback with a side effect before the verb:
```cpp
if (!f.PassEnabled("AtmosphereAerialPerspectiveCompositePass")) {
    return;
}
const fx::Texture output = m_composited.Acquire(f, f.Extent(), PixelFormat::RGBA16_Float);
```

Registration is automatic. While an `rg::RenderFeatureScope` is alive, `AddRenderPass()` and `AddBlitPass()` register each pass name with its owner in `RenderPassToggleRegistry`. The kit opens one scope around every feature callback.

Reference for rule 7: `AtmosphereFeature::SharedLuts` publishes `SharedLuts` only when both LUT passes were declared. `fx::ViewBackground` publishes its replay callback only when its pass was declared.

Pick the pass category:
1. Can a user see this pass running and want to inspect it? Use `General`, or `Debug` if it is debug-flavored.
2. Is it replay scaffolding that is meaningless as a leaf? Use `FrameDebuggerInternal`. Only the `FrameDebuggerReplayStep<N>` passes do.

## Checks
Each capture runs three detectors. A finding is logged once per new incident. Read them with `GET /get_logs?category=<name>`.

| Clause | Log category | Function | Finds |
|---|---|---|---|
| A | `RenderPassHonesty` | `DetectRenderPassHonestyMismatches()` | A non-culled pass that the registry reports disabled |
| B | `FrameDebuggerCoverage` | `DetectPassesMissingFromFrameDebuggerTree()` | A non-culled pass with no leaf in the tree |
| C | `FrameDebuggerSideChannel` | `DetectDisabledPassBlackboardKeyLeaks()` | A listed blackboard key published while its gating toggle is off |

- Code: `src/Editor/RenderPassHonestyChecker.*`, `FrameDebuggerCoverageChecker.*`, `FrameDebuggerSideChannelChecker.*`, plus a `*Guard.*` logger for each.
- Culled passes, Scene view passes and `FrameDebuggerInternal` passes are never reported by Clause B.
- A Clause A message means the pass is declared by code that never consults the registry. Or it consults it under a different name than the panel. Fix the name at the declaration site.
- Checks that need the registry or blackboard do nothing without them.

Clause C is a curated list, not a general check. The list is `KnownRiskBlackboardKeyRules()` in `src/Editor/FrameDebuggerSideChannelChecker.cpp`. Read the code for the full list. Three examples:

| Key | Gating toggle |
|---|---|
| `Core.ViewBackgroundReplayCallback` | `DrawSkyBackground` |
| Shadow `Map`, `Receiver` channels (Game and Scene) | `Shadow.Cascade0.Draw` |
| Atmosphere `SharedLuts` (shared) | `AtmosphereTransmittanceLutPass` and `AtmosphereMultiScatteringLutPass` |

Channel keys are built with `fx::ChannelKey(feature, channel, view)`, so rules never repeat a string literal.

## Gotchas
- The `Other Render Passes` group in the tree is a safety net. It holds any surviving pass no other tree rule claimed. The tree never drops a pass silently.
- Do not add a rule for a channel that is published with invalid textures while its pass is off. An example is Atmosphere `ViewLuts`. It would report false leaks.
- The Clause C detector does not see leaks outside the blackboard, such as a plain member read by another system.
- `Present` and `ClearViewTarget` are deny-listed. `GET /render_graph/set_pass_enabled` on them returns HTTP 409. This is the registry refusing a change, not a dishonest pass.
- `GET /render_graph/set_feature_enabled` is a different layer. It removes a whole feature (all passes, all views) before any of its code runs. Its passes need no registry consult for that.
- A typo in `set_pass_enabled` creates a ghost entry. The registry keeps at most 256 ghosts.
- Clause A and Clause C are separate duties. A pass can satisfy one and still break the other. The generic gate cannot undo a `Publish()` that already ran.

## Related
- [Frame Debugger](../debugging/frame-debugger.md) · [Render graph](../architecture/render-graph.md)
- [Feature kit](../reference/feature-kit.md) · [Writing a render feature](../guides/writing-a-render-feature.md)
- [HTTP cheat sheet](../debugging/http-cheat-sheet.md)
