# Render Pass Toggle Honesty

## The iron rule

A render pass's declared/enabled state and the Frame Debugger's own displayed
event tree must NEVER disagree. If a pass is disabled, it must not run, and it
must not appear as an executed leaf anywhere the Frame Debugger or the "Render
Graph" panel can show it. If a pass runs, the Frame Debugger MUST show it.
There is no acceptable middle ground.

## How a pass must consult `RenderPassToggleRegistry`

`src/Renderer/RenderGraph/RenderPassToggleRegistry.h` (`rg::RenderPassToggleRegistry`)
is the ONE registry every togglable pass name is recorded in. There are two
structurally different ways a pass reaches the render graph, and only one of
them is automatically honest:

- **The generic `RenderPipeline::DeclareOnePhase()` flush loop** (the majority
  of passes — `Core.cpp`'s `RegisterOffscreenRenderPipelineProviders()`/
  `RegisterPresentRenderPipelineProvider()` providers that `out.push_back(desc)`
  into their own `std::vector<rg::RenderPassDesc>&` parameter) is automatically
  gated — `DeclareOnePhase()` itself calls `NoteDeclaredAndCheckEnabled()` for
  every pushed `desc.name`, generically, with zero special-case code needed by
  the provider author.
- **Any pass declared via a DIRECT `builder.AddRenderPass()` (or
  `frame.builder.AddRenderPass()`) call inside a provider's own lambda, bypassing
  that generic flush loop entirely, gets NO automatic gating whatsoever.** This
  is exactly the shape that caused every "lie" the `editor-core-separation-21`
  campaign found and fixed (see `AGENTS.md`'s "Render Pass System" section for
  the full campaign writeup): the "Render Graph" panel's `BuildPassRow()` draws
  an identical-looking, apparently-functional "Enabled" checkbox for EVERY pass
  name that appears in a captured `RenderGraphSnapshot`, with zero knowledge of
  which declaration path produced it — toggling a direct-`AddRenderPass()`
  pass's checkbox looks exactly as functional as toggling a generic one, but
  does absolutely nothing unless that specific call site was hand-written to
  separately consult the registry.

**If you are writing a new pass that bypasses the generic flush loop (a direct
`AddRenderPass()` call inside your own provider/helper function), you MUST add
this guard yourself, as the very first statement, before any resource
creation/lookup**:

```cpp
if (toggleRegistry != nullptr
    && !toggleRegistry->NoteDeclaredAndCheckEnabled("YourPassName")) {
    return /* an empty/default-constructed result — never partially declare */;
}
```

Thread a `rg::RenderPassToggleRegistry* toggleRegistry = nullptr` parameter
(trailing, defaulted, so every existing call site keeps compiling) all the way
from the real registry instance (`Core::m_renderPassToggleRegistry`) down to
your declaration function — mirror any of `AtmosphereLutRenderer`'s five
methods, `PluginRenderPassBuilderAdapter::AddFullscreenClearPass()`,
`GBufferValidation::AddPass()`, `AddGpuSkinningPasses()`, or
`FrameDebuggerCaptureContext::AddReplayPasses()` for the exact, already-proven
pattern.

**Many dynamically-named passes behind one umbrella switch**: if your pass
declares an unbounded/session-growing set of dynamically-named leaves (e.g. one
per GPU-skinned dispatch, one per Frame Debugger replay step), do NOT register
a separate toggle-registry entry per dynamic name — that would clutter the
"Render Graph" panel's checkbox list with an unbounded, ever-growing list for
zero practical benefit. Instead, consult ONE, whole-mechanism registry entry
(e.g. `"GpuSkinning"`, `"FrameDebuggerReplay"`) once, at the very top of your
declaration function, gating every dynamically-named leaf it would otherwise
produce that frame. This exact shape is what `AddGpuSkinningPasses()`'s
direct-render-to-swapchain fallback and `AddReplayPasses()` both do.

**A pass with its own separate, real, bespoke feature toggle (e.g.
`ctx.showBlurredSceneOutput`) still needs its own registry consult on top of
that bespoke toggle** — the two are independent, additional layers of
granularity, not substitutes for each other. `ComputeBlurValidation`'s/
`GBufferValidation`'s own real toggle decides whether the FEATURE runs at all;
the registry consult is what makes that SAME pass's row in the "Render Graph"
panel's checkbox list actually mean something, rather than being purely
cosmetic.

## The permanent, automatic mismatch detector

Even with every known call site fixed, a FUTURE change (by a human or another
LLM agent) could reintroduce this exact class of bug — a new direct
`AddRenderPass()` call site that forgets the guard above. This is why the iron
rule is also enforced automatically, in code, not just by convention:

`src/Editor/RenderPassHonestyChecker.h/.cpp`'s
`DetectRenderPassHonestyMismatches()` is a pure, dependency-free function
(mirrors `ImGuiIdConflictTracker.h`'s own precedent) that takes this frame's
real `rg::RenderGraphSnapshot::passesInExecutionOrder` plus an
`isEnabledLookup` callable, and reports every pass name that is BOTH:

- present, **non-culled** (a real, legitimate reason a pass didn't run —
  `RenderGraphCompiler::Compile()`'s own dead-code elimination — is never a
  mismatch), AND
- reports `enabled == false` via the lookup.

`src/Editor/RenderPassHonestyGuard.h/.cpp`'s `RenderPassHonestyGuard` singleton
wraps this with a Logger-aware, log-once-per-new-incident policy (mirroring
`ImGuiIdConflictGuard`'s own "erase when no longer conflicting, so a future
recurrence is treated as fresh" shape) and is wired into
`FrameDebuggerPanel::TriggerCapture()` — the natural per-capture chokepoint
every real Frame Debugger capture already flows through, comparing that
capture's own `RenderGraphSnapshot` against the SAME `RenderPassToggleRegistry`
instance the "Render Graph" panel's own checkboxes read/write.

**If this detector ever fires**: a real, running engine will show a
`GTE_LOG_ERROR("RenderPassHonesty", ...)` entry, retrievable via
`GET /get_logs?category=RenderPassHonesty`, reading:

> `Pass '<PassName>' is marked DISABLED in RenderPassToggleRegistry but still
> executed and appears in this frame's captured Render Graph snapshot - the
> render pass and the Frame Debugger disagree.`

This means some pass named `<PassName>` is being declared via a direct
`AddRenderPass()` call that either never consults the registry at all, or
consults it under a DIFFERENT name than the one shown in the "Render Graph"
panel's checkbox. Fix it by applying the guard pattern above at that pass's own
declaration site, using the EXACT SAME name string the panel/HTTP toggle uses.

## Deny-listed passes are a separate, unrelated mechanism

`"Present"`/`"ClearViewTarget"` are permanently deny-listed
(`RenderPassToggleRegistry::IsDenyListed()`) — `GET /render_graph/set_pass_enabled`
against either returns `HTTP 409`. This is intentional: these two passes are
load-bearing for every other pass's own execution (the guaranteed view-target
clear, the final swapchain present) and were never meant to be toggled off at
all. Do not confuse a deny-listed pass with a dishonest one — a deny-list
rejection is the registry correctly refusing a mutation, not a pass silently
ignoring one.

## What this convention does NOT cover

`RenderFeatureCompositor`'s own per-feature `enabledOverride` filter
(`GET /render_graph/set_feature_enabled`) is a genuinely different, bespoke
mechanism: it filters an entire Plugin Render Feature (every one of its own
passes, for every view) out of the combined pass list BEFORE any of its own
`AddRenderPass()` calls ever run, so none of its passes need their own
`RenderPassToggleRegistry` consult to be honest. This is intentional,
already-confirmed-honest design, not an oversight — do not add a redundant
registry consult inside a `_v2`/`_v3` plugin's own operation dispatch.
