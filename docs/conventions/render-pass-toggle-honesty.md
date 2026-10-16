# Render Pass Toggle Honesty

## The iron rule

A render pass's declared/enabled state and the Frame Debugger's displayed event
tree must NEVER disagree. If a pass is disabled, it must not run, and it must not
appear as an executed leaf anywhere the Frame Debugger or the "Render Graph"
panel can show it. If a pass runs, the Frame Debugger MUST show it.
There is no acceptable middle ground.

## How pass ownership and registration work

`src/Renderer/RenderGraph/RenderPassToggleRegistry.h` (`rg::RenderPassToggleRegistry`)
is the ONE registry every togglable pass name is recorded in. Registration is
automatic. A pass author does not call the registry to register a pass.

- **`rg::RenderFeatureScope`** (`src/Renderer/RenderGraph/RenderFeatureScope.h`)
  is an RAII marker. While one is alive, every pass declared through
  `RenderGraphBuilder::AddRenderPass()` / `AddBlitPass()` is tagged with that
  feature name and registered through `RenderPassToggleRegistry::NoteDeclaredWithOwner()`.
  The feature kit opens one scope around every feature callback
  (`src/Core/FeatureKit/RegistrationRunner.cpp`). Engine code that declares passes
  directly opens its own, for example `const rg::RenderFeatureScope scope(builder, "Frame Debugger");`.
- **A pass that skips its own work when disabled** reads the registry back,
  read-only, with `rg::ShouldDeclareBuiltInPassThisFrame(registry, name)`
  (`RenderPassToggleGuard.h`). It must do this before any resource creation,
  lookup or side effect:

```cpp
if (!rg::ShouldDeclareBuiltInPassThisFrame(toggleRegistry, "YourPassName")) {
    return; // never partially declare
}
const rg::RenderFeatureScope scope(builder, "Your Feature Name");
```

## Feature kit passes

Kit verbs do the check for you. `f.Compute(...)`, `f.Fullscreen(...)`,
`f.ViewBackground(...)`, `f.SceneDraw(...)` and `f.Blit(...)` ask the registry
(`src/Core/FeatureKit/VerbSupport.h`, `PassEnabled`) and declare nothing while
their pass is toggled off.

Work that happens BEFORE a verb is not covered by the verb. Creating or resizing
a texture (`ViewOutput::Acquire`, `PersistentTarget::Acquire`, `PersistentVolume::Acquire`)
is a side effect. Ask `f.PassEnabled("PassName")` first, and return early when it is false:

```cpp
if (!f.PassEnabled("AtmosphereAerialPerspectiveCompositePass")) {
    return;
}
const fx::Texture output = m_composited.Acquire(f, f.Extent(), PixelFormat::RGBA8_UNorm);
```

A callback with several separately toggled passes lists all their names in its
`fx::Gate`. The callback runs when at least one of them is enabled. Each pass
inside still needs its own `PassEnabled` check before its own side effects.

## Many dynamically named passes behind one switch

If a provider declares an unbounded set of dynamically named leaves (one per
GPU-skinned dispatch, one per Frame Debugger replay step), do not register one
toggle entry per name. That would fill the "Render Graph" panel with an
ever-growing checkbox list. Consult ONE umbrella entry (`"GpuSkinning"`,
`"FrameDebuggerReplay"`) once, at the top of the declaration function.

## A pass with its own feature toggle

A pass with a real, separate feature toggle still needs its own registry consult.
The two are independent layers. The feature toggle decides whether the feature
runs at all. The registry consult makes that pass's row in the "Render Graph"
panel mean something.

## The automatic mismatch detector

The iron rule is also enforced in code. A new direct `AddRenderPass()` call that
forgets the guard would break it.

`src/Editor/RenderPassHonestyChecker.h/.cpp` has `DetectRenderPassHonestyMismatches()`.
It is a pure function. It takes this frame's `rg::RenderGraphSnapshot::passesInExecutionOrder`
and an `isEnabledLookup` callable. It reports every pass that is BOTH:

- present and **non-culled** (a pass removed by dead-code elimination is never a mismatch), AND
- reported `enabled == false` by the lookup.

`RenderPassHonestyGuard` (`src/Editor/RenderPassHonestyGuard.h/.cpp`) wraps it with
a log-once-per-new-incident policy. `FrameDebuggerPanel::TriggerCapture()` calls it
for every Frame Debugger capture, against the same `RenderPassToggleRegistry` the
"Render Graph" panel reads and writes.

When it fires, `GET /get_logs?category=RenderPassHonesty` shows:

> `Pass '<PassName>' is marked DISABLED in RenderPassToggleRegistry but still
> executed and appears in this frame's captured Render Graph snapshot - the
> render pass and the Frame Debugger disagree.`

This means `<PassName>` is declared by a call that never consults the registry,
or consults it under a DIFFERENT name than the one in the panel. Fix it at the
declaration site with the exact name the panel and the HTTP toggle use.

## Deny-listed passes

`"Present"` and `"ClearViewTarget"` are permanently deny-listed
(`RenderPassToggleRegistry::IsDenyListed()`). `GET /render_graph/set_pass_enabled`
on either returns `HTTP 409`. Every other pass depends on them (the guaranteed
view-target clear, the final swapchain present). A deny-list rejection is the
registry refusing a change. It is not a dishonest pass.

## What this convention does NOT cover

`RenderFeatureCompositor`'s per-feature `enabledOverride` filter
(`GET /render_graph/set_feature_enabled`) is a different mechanism. It removes a
whole plugin render feature (all its passes, all views) from the pass list BEFORE
any of its `AddRenderPass()` calls run. None of its passes need their own registry
consult. Do not add one inside a plugin's operation dispatch.
