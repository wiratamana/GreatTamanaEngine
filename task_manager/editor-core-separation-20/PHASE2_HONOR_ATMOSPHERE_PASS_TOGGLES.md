# PHASE2 — Honor the Atmosphere Pass Toggles (fix the inert checkbox bug)

Parent: `PHASE0_MASTER_STRATEGY.md` — **read it first** (Root Cause #2, section 2.3, and Locked Design Decisions 4/5/7).
Also read `PHASE1_GUARANTEED_VIEW_TARGET_CLEAR.md`'s own completion report (if present) before starting — PHASE1 must land first; this phase builds on top of it.

This is an **implementation task**. Do not use `delegate_task`. If you hit a genuine design ambiguity, use `ask_questions` to ask the user directly, in plain, easy English, before proceeding.

---

## Step 1: The Goal

Make the "Enabled" checkbox (and the equivalent `GET /render_graph/set_pass_enabled` HTTP call) for these 5 real render-graph passes **genuinely take effect**, exactly like it already does for `RenderOpaque`/`DrawSkyBackground`/`RenderTransparent`:

1. `AtmosphereTransmittanceLutPass`
2. `AtmosphereMultiScatteringLutPass`
3. `AtmosphereSkyViewLutPass` (declared once per active view)
4. `AtmosphereAerialPerspectiveVolumePass` (declared once per active view)
5. `AtmosphereAerialPerspectiveCompositePass` (declared once per active view)

Disabling any one of them must genuinely remove it (and, correctly, everything downstream of it that depends on its output THIS frame) from the render graph — never a no-op, and never a crash.

## Step 2: The Situation (recap — full detail lives in PHASE0 section 2.3)

- `RenderPassToggleRegistry::NoteDeclaredAndCheckEnabled()` is the ONLY function that makes a pass's disabled state take effect, and it is called from exactly 3 places today — none of which cover these 5 passes.
- All 5 are declared via direct, immediate `builder.AddRenderPass()` calls inside `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp`, reached through `Core.cpp`'s `"AtmosphereSharedLut"`/`"AtmosphereViewLut"` providers (which call `frame.builder` directly — a deliberate `RenderPipeline.h` mechanism for passes with real inter-call data dependencies, NOT a bug in itself — only the missing toggle-consult is the bug).
- These 5 passes form a real dependency chain: `Transmittance` → `MultiScattering` (reads Transmittance) → `SkyView` + `AerialPerspectiveVolume` (both read Transmittance + MultiScattering) → `AerialPerspectiveComposite` (reads AerialPerspectiveVolume) → (separately) `DrawSkyBackground`'s own draw-time sampling of `SkyView`'s output.
- Every `TextureHandle`/`VolumeTextureHandle` already has a real, working `IsValid()` sentinel (`index != kInvalidIndex`, `RenderGraphTypes.h`), and the codebase already has a live precedent for "return an invalid handle as the disabled/degrade-gracefully signal" at `AtmosphereLutRenderer::AddAerialPerspectiveVolumeDebugSlicePass()` (lines ~945-963) — **reuse that exact pattern**, do not invent `std::optional<TextureHandle>` wrapping (LDD-4).
- **CRITICAL, engine-crashing hazard confirmed by reading the real render-graph executor** (`RenderGraph.cpp`): `RenderGraph::EnsureTextureResolved()`/`EnsureBufferResolved()`/`EnsureVolumeTextureResolved()` all index straight into a `std::vector` (`physicalTextures[index]`/`physicalVolumeTextures[index]`/etc.) with **NO bounds check at all**. `TextureHandle::IsValid()`'s sentinel, `kInvalidIndex`, is `0xFFFFFFFFu` — a default-constructed `rg::TextureHandle{}`/`rg::VolumeTextureHandle{}` reaching ANY of `PassBuilder::ReadTexture()`/`ReadVolumeTexture()`/`ReadBuffer()`/`WriteTexture()`/`WriteVolumeTexture()`/`WriteBuffer()` for a pass that is not culled is therefore a **guaranteed out-of-bounds vector access the instant that pass actually executes** (inside `RenderGraph::ApplyUsageBarrierIfNeeded()`, called once per declared read/write, every frame, for every non-culled pass) — undefined behavior, in practice a hard crash. This is not hypothetical: **section 3.7 below identifies one real, pre-existing call site this exact campaign's own fix would newly expose to this crash if left unguarded.** Every cascading `IsValid()` check this phase adds exists specifically to make sure NO invalid handle is EVER threaded into a `builder.AddRenderPass()` call's `setup` lambda, for any pass that is not itself culled — verify this property for every single call site you touch, not just the 5 methods explicitly named above.

## Step 3: The Plan

### 3.1 — Files you will touch

- `src/Renderer/Atmosphere/AtmosphereLutRenderer.h` (5 method signatures gain one new trailing parameter)
- `src/Renderer/Atmosphere/AtmosphereLutRenderer.cpp` (5 method bodies gain a guard at the top; body already returns `rg::TextureHandle{}`/`rg::VolumeTextureHandle{}` style values that just need an earlier bail-out added)
- `src/Renderer/Atmosphere/AtmospherePassToggleLogic.h` (**NEW FILE** — the small, pure, Tier-1-testable decision helper factored out of the 5 guard clauses above — see section 3.4a)
- `tests/Renderer/Atmosphere/AtmospherePassToggleLogicTests.cpp` (**NEW FILE** — the Tier-1 test for the helper above — see section 3.8), plus one new line in `tests/CMakeLists.txt`'s test source list
- `src/Application/AtmospherePassSequence.h` and `.cpp` (the two wrapper functions `AddAtmosphereSharedLutPasses()`/`AddAtmosphereViewLutPasses()` gain the same trailing parameter and cascading `IsValid()` checks; `AddAtmosphereCompositePass()` too)
- `src/Core/Core.cpp` (the 4 real call sites: `"AtmosphereSharedLut"` provider, `"AtmosphereViewLut"` provider, `"DrawSkyBackground"` provider, `"AtmosphereComposite"` provider — thread `&m_renderPassToggleRegistry` through, add `.IsValid()` guards before using a possibly-invalid upstream handle — **plus one additional, easily-missed guard inside the `"AtmosphereViewLut"` provider's own body, on its inline call to the SEPARATE, sixth function `AddAerialPerspectiveVolumeDebugSlicePass()` — see section 3.7, item 2, this is the confirmed crash fix**)

All 7 real call sites of the 5 functions above (searched exhaustively across the whole `src/` tree, including every test file) are: the two wrapper definitions in `AtmospherePassSequence.cpp` (which themselves become the only callers `AtmosphereLutRenderer.cpp`'s 5 methods have), and the 4 `Core.cpp` provider bodies named above. No test file calls any of the 5 methods directly (confirmed via `search_in_dir` for each method name across `tests/` — zero matches; these methods remain genuinely Tier-2/GPU-only, exactly as PHASE0 states). `AddAerialPerspectiveVolumeDebugSlicePass()` itself has exactly one real call site in `Core.cpp` (inside the `"AtmosphereViewLut"` provider, `if (isGameView) { ... }` block) — this is the 6th function/7th-not-otherwise-counted call site that needs a defensive guard even though the function itself is untouched (LDD-5 still fully honored — see 3.7).

### 3.2 — Add the new parameter to `RenderPassToggleRegistry` forward declaration

`AtmosphereLutRenderer.h` needs to know about `rg::RenderPassToggleRegistry` as a pointer type only — add a forward declaration near its existing `namespace gte { class AtmosphereLutRenderer { ... } }` wrapper:

```cpp
namespace gte::rg {
class RenderPassToggleRegistry;
} // namespace gte::rg
```

(Do this instead of a full `#include` of `RenderPassToggleRegistry.h` in the header, to avoid growing this header's own include graph — the `.cpp` file needs the full include, the header only ever holds a pointer.)

### 3.3 — Change each of the 5 method signatures (header + source)

Add ONE new, trailing, **defaulted** parameter to each — mirrors `RenderPipeline::SetPassToggleRegistry()`'s own established "defaults to nullptr, unset = old/always-enabled behavior" discipline (PHASE0 section 2.3, and `RenderPipeline.h`'s own doc comment on `SetPassToggleRegistry()`). **Every signature below has been read and verified, byte-for-byte, against the real, current `AtmosphereLutRenderer.h`/`.cpp` — only the trailing `toggleRegistry` parameter is new; every other parameter's name/type/order is unchanged:**

```cpp
rg::TextureHandle AddTransmittanceLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

rg::TextureHandle AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle,
    rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

rg::TextureHandle AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
    const char* outputTextureName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

rg::VolumeTextureHandle AddAerialPerspectiveVolumePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle,
    const char* outputVolumeName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);

rg::TextureHandle AddAerialPerspectiveCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    rg::TextureHandle sourceColorHandle, VkSampler sourceColorSampler, VkImageView sourceDepthView,
    VkSampler sourceDepthSampler, rg::VolumeTextureHandle aerialPerspectiveVolumeHandle,
    const char* aerialPerspectiveVolumeName, const Mat4& invViewProjection, Vec3 cameraWorldPosition,
    float aerialPerspectiveStrength, float maxDistanceKm, float depthExponent, VkExtent2D extent,
    const char* outputTextureName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry = nullptr);
```

The `.cpp` definitions repeat the exact same parameter list, WITHOUT the `= nullptr` default (C++ only allows a default argument at the first declaration — the header above is that first declaration).

`AddAerialPerspectiveVolumeDebugSlicePass()` itself (the 6th, Frame-Debugger-only method) keeps its EXACT current signature, unchanged — LDD-5 (do not touch it) still applies in full. Only its ONE call site in `Core.cpp` gets a defensive guard — see 3.7.

### 3.4a — NEW: extract the pure "should this pass run" decision into its own, Tier-1-testable helper

Every one of the 5 guards in 3.4 below boils down to the exact same two-input boolean decision: *"is this pass individually enabled AND are all of its upstream handles valid?"* This decision touches no GPU, no `Renderer`, no `RenderGraphBuilder` — it is pure boolean logic operating on plain values already resolved by the caller. Per `AGENTS.md`'s "Testability & Regression Safety" section ("before wiring new logic directly into a GPU/SDL-owning class, ask whether it can instead be extracted as a small pure function... if it can, do that"), extract it, rather than leaving 5 separate, untested copies of the same `if` inline inside a Tier-2 class's `.cpp` file. This does NOT change the "these 5 `AddXxxPass()` methods stay Tier-2, no direct unit test possible" claim (PHASE0/PHASE2) — the methods themselves still need a live `VkDevice`/`Renderer` to do anything useful — it only extracts the one small slice of their own logic that never needed a GPU in the first place.

Create `src/Renderer/Atmosphere/AtmospherePassToggleLogic.h` (header-only, no `.cpp` needed — a single trivial `inline` function):

```cpp
#pragma once

// editor-core-separation-20 campaign, PHASE2
// (task_manager/editor-core-separation-20/PHASE2_HONOR_ATMOSPHERE_PASS_TOGGLES.md)
// - the ONE pure decision every one of AtmosphereLutRenderer's 5 toggle-aware
// AddXxxPass() methods needs before it is allowed to call
// builder.AddRenderPass() at all: "is this pass individually enabled THIS
// FRAME, and are every one of its upstream TextureHandle/VolumeTextureHandle
// dependencies already valid THIS FRAME?" Extracted out of those methods'
// own guard clauses specifically so this exact branching is Tier-1-testable
// with ZERO live VkDevice/Renderer/RenderGraphBuilder involved (see
// AGENTS.md's "Testability & Regression Safety" section) - the 5 methods
// themselves remain genuinely Tier-2 (they still need a live GPU to do
// anything useful once this check passes), but the SKIP DECISION itself
// never touched the GPU in the first place, so it does not need to stay
// bundled inline with the GPU-owning class's own .cpp file.
//
// This header deliberately does NOT include RenderPassToggleRegistry.h or
// RenderGraphTypes.h - every caller resolves its own two booleans FIRST
// (`toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled(name)`
// for the first; a logical AND of every relevant TextureHandle::IsValid()/
// VolumeTextureHandle::IsValid() call for the second) and passes them in as
// plain bool values - this keeps this function usable from a Tier-1 test
// with no dependency on either type at all.
//
// A caller with NO upstream handle to check at all (AddTransmittanceLutPass,
// the very first pass in the chain) passes `allUpstreamHandlesValid = true`
// unconditionally - vacuously true, matching how an empty AND is true.
namespace gte {

inline bool ShouldDeclareAtmospherePassThisFrame(bool passEnabledThisFrame, bool allUpstreamHandlesValid) noexcept
{
    return passEnabledThisFrame && allUpstreamHandlesValid;
}

} // namespace gte
```

`#include "AtmospherePassToggleLogic.h"` from `AtmosphereLutRenderer.cpp` (NOT from `AtmosphereLutRenderer.h` — this header is an implementation detail of the 5 method bodies, not part of the class's own public interface).

### 3.4 — Add the guard at the top of each of the 5 `.cpp` method bodies

Place the guard as the **very first real statement** of the function body (before any `EnsureXInitialized()` lazy-init call, before any `builder.ImportTexture()` call — the pre-existing `(void)params; // ...` no-op comment-only cast some of these bodies already start with may stay exactly where it is, immediately after the guard; its relative position does not matter). Use the pass's own exact literal debugName string as the argument to `NoteDeclaredAndCheckEnabled()` — it MUST byte-for-byte match the string literal already passed to `builder.AddRenderPass(...)` a few lines below in that same function (copy it, don't retype it, to avoid a silent typo that would make the toggle check a permanent no-op again — verified below against the real, current `.cpp` file for all 5).

**`AddTransmittanceLutPass`** (real `builder.AddRenderPass()` name: `"AtmosphereTransmittanceLutPass"`; no upstream handle to check):

```cpp
rg::TextureHandle AtmosphereLutRenderer::AddTransmittanceLutPass(
    rg::RenderGraphBuilder& builder, Renderer& renderer, const AtmosphereParametersGpu& params,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame =
        toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereTransmittanceLutPass");
    if (!ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, /*allUpstreamHandlesValid=*/true)) {
        return rg::TextureHandle{};
    }

    EnsureTransmittanceLutInitialized(renderer, params);
    // ... rest of the method's existing body, completely unchanged ...
```

**`AddMultiScatteringLutPass`** (real name: `"AtmosphereMultiScatteringLutPass"`; checks `transmittanceLutHandle` only):

```cpp
rg::TextureHandle AtmosphereLutRenderer::AddMultiScatteringLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, rg::TextureHandle transmittanceLutHandle,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame =
        toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereMultiScatteringLutPass");
    if (!ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, transmittanceLutHandle.IsValid())) {
        return rg::TextureHandle{};
    }

    (void)params; // Already uploaded into m_atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.
    EnsureMultiScatteringLutInitialized(renderer);
    // ... rest of the method's existing body, completely unchanged ...
```

**`AddSkyViewLutPass`** (real name: `"AtmosphereSkyViewLutPass"`; checks BOTH `transmittanceLutHandle` AND `multiScatteringLutHandle`):

```cpp
rg::TextureHandle AtmosphereLutRenderer::AddSkyViewLutPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle, const char* outputTextureName,
    rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame =
        toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereSkyViewLutPass");
    if (!ShouldDeclareAtmospherePassThisFrame(
            passEnabledThisFrame, transmittanceLutHandle.IsValid() && multiScatteringLutHandle.IsValid())) {
        return rg::TextureHandle{};
    }

    (void)params; // Already uploaded into m_atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.
    EnsureSkyViewLutInitialized(renderer);
    // ... rest of the method's existing body, completely unchanged ...
```

**`AddAerialPerspectiveVolumePass`** (real name: `"AtmosphereAerialPerspectiveVolumePass"`; checks BOTH; returns `rg::VolumeTextureHandle{}`, NOT `rg::TextureHandle{}`):

```cpp
rg::VolumeTextureHandle AtmosphereLutRenderer::AddAerialPerspectiveVolumePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, const AtmosphereParametersGpu& params, const AtmosphereFrameUniforms& frameUniforms,
    rg::TextureHandle transmittanceLutHandle, rg::TextureHandle multiScatteringLutHandle, const char* outputVolumeName,
    rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame = toggleRegistry == nullptr
        || toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveVolumePass");
    if (!ShouldDeclareAtmospherePassThisFrame(
            passEnabledThisFrame, transmittanceLutHandle.IsValid() && multiScatteringLutHandle.IsValid())) {
        return rg::VolumeTextureHandle{};
    }

    (void)params; // Already uploaded into m_atmosphereParametersBuffer by AddTransmittanceLutPass() this same frame.
    EnsureAerialPerspectiveVolumeInitialized(renderer);
    // ... rest of the method's existing body, completely unchanged ...
```

**`AddAerialPerspectiveCompositePass`** (real name: `"AtmosphereAerialPerspectiveCompositePass"`; checks `aerialPerspectiveVolumeHandle` only — `sourceColorHandle` always comes from `viewData->colorTarget`, which is always valid by construction, so it needs no check):

```cpp
rg::TextureHandle AtmosphereLutRenderer::AddAerialPerspectiveCompositePass(rg::RenderGraphBuilder& builder,
    Renderer& renderer, rg::TextureHandle sourceColorHandle, VkSampler sourceColorSampler,
    VkImageView sourceDepthView, VkSampler sourceDepthSampler, rg::VolumeTextureHandle aerialPerspectiveVolumeHandle,
    const char* aerialPerspectiveVolumeName, const Mat4& invViewProjection, Vec3 cameraWorldPosition,
    float aerialPerspectiveStrength, float maxDistanceKm, float depthExponent, VkExtent2D extent,
    const char* outputTextureName, rg::ViewScope viewScope, rg::RenderPassToggleRegistry* toggleRegistry)
{
    const bool passEnabledThisFrame = toggleRegistry == nullptr
        || toggleRegistry->NoteDeclaredAndCheckEnabled("AtmosphereAerialPerspectiveCompositePass");
    if (!ShouldDeclareAtmospherePassThisFrame(passEnabledThisFrame, aerialPerspectiveVolumeHandle.IsValid())) {
        return rg::TextureHandle{};
    }

    EnsureAerialPerspectiveCompositeInitialized(renderer);
    // ... rest of the method's existing body, completely unchanged ...
```

### 3.5 — Update the two wrapper functions in `AtmospherePassSequence.h`/`.cpp`, and the composite wrapper

`AddAtmosphereSharedLutPasses()` gains the same trailing `rg::RenderPassToggleRegistry* toggleRegistry = nullptr` parameter (header) / `rg::RenderPassToggleRegistry* toggleRegistry` (source, no default), forwards it to both calls, and short-circuits if Transmittance came back invalid:

```cpp
AtmosphereSharedLutHandles AddAtmosphereSharedLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, const AtmosphereParametersGpu& atmosphereParameters,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    AtmosphereSharedLutHandles handles;
    handles.transmittanceLutHandle =
        atmosphereLutRenderer.AddTransmittanceLutPass(builder, renderer, atmosphereParameters, toggleRegistry);
    if (!handles.transmittanceLutHandle.IsValid()) {
        return handles; // multiScatteringLutHandle stays default-constructed (invalid) too - nothing to feed it.
    }
    handles.multiScatteringLutHandle = atmosphereLutRenderer.AddMultiScatteringLutPass(
        builder, renderer, atmosphereParameters, handles.transmittanceLutHandle, toggleRegistry);
    return handles;
}
```

`AddAtmosphereViewLutPasses()` gains the same trailing parameter, forwards it to both calls, and skips BOTH calls if either shared LUT handle is invalid:

```cpp
AtmosphereViewLutHandles AddAtmosphereViewLutPasses(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, Registry& registry,
    const AtmosphereParametersGpu& atmosphereParameters, const AtmosphereSettings& atmosphereSettings,
    const AtmosphereSharedLutHandles& sharedLuts, Vec3 eyeWorldPosition, const Mat4& viewProjection,
    const char* skyViewLutName, const char* aerialPerspectiveVolumeName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    AtmosphereViewLutHandles result;
    result.frameUniforms = ResolveAtmosphereFrameUniforms(registry, eyeWorldPosition);
    result.frameUniforms.invViewProjection = viewProjection.Inverse();
    result.frameUniforms.aerialPerspectiveMaxDistanceKm = atmosphereSettings.aerialPerspectiveMaxDistanceKm;
    result.frameUniforms.aerialPerspectiveDepthExponent = atmosphereSettings.aerialPerspectiveDepthExponent;
    result.frameUniforms.aerialPerspectiveSamplesPerSliceAsFloat =
        static_cast<float>(atmosphereSettings.aerialPerspectiveSamplesPerSlice);
    result.frameUniforms.aerialPerspectiveScatteringExaggeration =
        atmosphereSettings.aerialPerspectiveScatteringExaggeration;

    if (!sharedLuts.transmittanceLutHandle.IsValid() || !sharedLuts.multiScatteringLutHandle.IsValid()) {
        return result; // skyViewLutHandle/aerialPerspectiveVolumeHandle stay invalid - nothing valid to feed them.
    }

    result.skyViewLutHandle = atmosphereLutRenderer.AddSkyViewLutPass(builder, renderer, atmosphereParameters,
        result.frameUniforms, sharedLuts.transmittanceLutHandle, sharedLuts.multiScatteringLutHandle, skyViewLutName,
        viewScope, toggleRegistry);

    result.aerialPerspectiveVolumeHandle = atmosphereLutRenderer.AddAerialPerspectiveVolumePass(builder, renderer,
        atmosphereParameters, result.frameUniforms, sharedLuts.transmittanceLutHandle,
        sharedLuts.multiScatteringLutHandle, aerialPerspectiveVolumeName, viewScope, toggleRegistry);

    return result;
}
```

`AddAtmosphereCompositePass()`'s real, current signature (verified against `AtmospherePassSequence.h`/`.cpp`) is a thin, reshaping wrapper around `AtmosphereLutRenderer::AddAerialPerspectiveCompositePass()` — it takes a `RenderTexture& viewRenderTexture` (not raw sampler/view handles) and resolves `viewRenderTexture.Sampler()`/`.Target().depthImageView`/`.DepthSampler()` itself before forwarding. It gains the same trailing parameter, forwarded straight through — no new internal branch needed (its own callee, `AddAerialPerspectiveCompositePass()`, already does the one relevant `IsValid()` check internally per 3.4 above):

```cpp
// AtmospherePassSequence.h
rg::TextureHandle AddAtmosphereCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, RenderTexture& viewRenderTexture, rg::TextureHandle sourceColorHandle,
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    const AtmosphereFrameUniforms& frameUniforms, Vec3 eyeWorldPosition, float aerialPerspectiveStrength,
    float maxDistanceKm, float depthExponent, VkExtent2D extent, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry = nullptr);
```

```cpp
// AtmospherePassSequence.cpp
rg::TextureHandle AddAtmosphereCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    AtmosphereLutRenderer& atmosphereLutRenderer, RenderTexture& viewRenderTexture, rg::TextureHandle sourceColorHandle,
    rg::VolumeTextureHandle aerialPerspectiveVolumeHandle, const char* aerialPerspectiveVolumeName,
    const AtmosphereFrameUniforms& frameUniforms, Vec3 eyeWorldPosition, float aerialPerspectiveStrength,
    float maxDistanceKm, float depthExponent, VkExtent2D extent, const char* outputTextureName, rg::ViewScope viewScope,
    rg::RenderPassToggleRegistry* toggleRegistry)
{
    return atmosphereLutRenderer.AddAerialPerspectiveCompositePass(builder, renderer, sourceColorHandle,
        viewRenderTexture.Sampler(), viewRenderTexture.Target().depthImageView, viewRenderTexture.DepthSampler(),
        aerialPerspectiveVolumeHandle, aerialPerspectiveVolumeName, frameUniforms.invViewProjection, eyeWorldPosition,
        aerialPerspectiveStrength, maxDistanceKm, depthExponent, extent, outputTextureName, viewScope, toggleRegistry);
}
```

Add a forward declaration of `rg::RenderPassToggleRegistry` inside `AtmospherePassSequence.h`'s own `namespace gte::rg { ... }` block (it currently only forward-declares `RenderGraphBuilder` there — add `class RenderPassToggleRegistry;` alongside it).

### 3.6 — `DrawSkyBackground()` itself needs NO change (verified, not just assumed)

`AtmosphereLutRenderer::DrawSkyBackground()` (the actual draw-time function — distinct from the 5 pass-DECLARING methods above) already has its own, pre-existing degrade-gracefully guard:

```cpp
const auto it = m_skyViewLutViewStates.find(skyViewLutName);
if (it == m_skyViewLutViewStates.end() || !it->second.output.has_value()) {
    return; // draws nothing
}
```

Trace the two ways this phase's fix can reach it:
- **Very first frame ever, pass pre-disabled before it ever ran once**: `m_skyViewLutViewStates` has no entry for `skyViewLutName` at all (`EnsureSkyViewLutViewInitialized()` was never called) → `it == end()` → safe, degrades to "draw nothing." No dangling access — there is nothing to dangle; the `RenderTexture` was simply never constructed.
- **A later frame, pass disabled after having run successfully before**: the map entry (and its `RenderTexture`) persist for the whole session (never erased) → `it->second.output.has_value()` is `true` → without any other guard, this would sample **STALE** (last-successfully-computed-frame's) data — visually wrong but not a crash, not a dangling pointer.

Both paths are already made unreachable in practice by section 3.7's own `"DrawSkyBackground"` provider fix below (`!viewLuts->skyViewLutHandle.IsValid()` short-circuits BEFORE `MakeRecordSkyBackgroundCallback()`/`DrawSkyBackground()` is ever invoked at all, in either case). `DrawSkyBackground()`'s own internal map-lookup guard is therefore genuinely redundant defense-in-depth today — correct to leave in place, unnecessary to add anything new to it. **Do not modify `DrawSkyBackground()`'s signature or body in this phase.**

### 3.7 — Update the Core.cpp call sites (4 real provider bodies, PLUS one confirmed crash fix inside one of them)

1. **`"AtmosphereSharedLut"` provider** (near the top of `RegisterOffscreenRenderPipelineProviders()`, ~line 449): change

   `entry.handles = AddAtmosphereSharedLutPasses(frame.builder, m_renderer, m_atmosphereLutRenderer, entry.parameters);`

   to

   `entry.handles = AddAtmosphereSharedLutPasses(frame.builder, m_renderer, m_atmosphereLutRenderer, entry.parameters, &m_renderPassToggleRegistry);`

2. **`"AtmosphereViewLut"` provider** (~line 511) — TWO changes here, not one:

   **2a.** Add `, &m_renderPassToggleRegistry` as the new trailing argument to the existing `AddAtmosphereViewLutPasses(...)` call (~line 530).

   **2b. — THE CONFIRMED CRASH FIX.** This SAME provider body, a few lines below, ALSO contains a direct, unconditional call to the SEPARATE, 6th function `AddAerialPerspectiveVolumeDebugSlicePass()` (Frame-Debugger-only volume-slice-preview tooling, LDD-5 — this function's own signature/body is NOT touched by this phase):

   ```cpp
   if (isGameView) {
       const rg::TextureHandle debugSlice = m_atmosphereLutRenderer.AddAerialPerspectiveVolumeDebugSlicePass(
           frame.builder, m_renderer, viewLuts.aerialPerspectiveVolumeHandle, aerialVolumeName,
           static_cast<std::uint32_t>(m_atmosphereSettings.aerialPerspectiveDebugSliceIndex),
           "AtmosphereAerialPerspectiveVolumeDebugSlice", rg::ViewScope::GameView);
       frame.finalTextureOutputs.push_back(debugSlice);
   }
   ```

   `AddAerialPerspectiveVolumeDebugSlicePass()` itself does NOT check `aerialPerspectiveVolumeHandle.IsValid()` anywhere in its own body (it only checks whether `m_aerialPerspectiveVolumeViewStates` has an entry for `aerialVolumeName` — a MAP-EXISTENCE check, not a THIS-FRAME-HANDLE-VALIDITY check; that map entry persists for the whole session once created). Once this phase's own fix ships, `viewLuts.aerialPerspectiveVolumeHandle` CAN legitimately be invalid this frame (e.g. the user disabled `AtmosphereTransmittanceLutPass`, cascading all the way down) while the map entry from an earlier, successful frame still exists. In that exact case, this call proceeds unconditionally, declares `pass.ReadVolumeTexture(aerialPerspectiveVolumeHandle, ...)` against the invalid handle inside its own `setup` lambda, and — because its own output is unconditionally pushed onto `frame.finalTextureOutputs` (making it a non-culled root) — this pass WILL execute this frame. When `RenderGraph::Execute()` applies this pass's barriers, `ApplyUsageBarrierIfNeeded()` calls `EnsureVolumeTextureResolved(volumeHandle.index, ...)`, which indexes `physicalVolumeTextures[0xFFFFFFFF]` with **no bounds check** — this is the exact, general out-of-bounds hazard documented in Step 2 above, and it is REAL and REACHABLE the very first time anyone runs this phase's own section 3.9 smoke test (disabling `AtmosphereTransmittanceLutPass` while the Game View is active, which it always is during that test).

   **The fix — guard this call site with the same handle, using the SAME cascading `IsValid()` idiom this whole phase already establishes (LDD-5 is still fully honored: `AddAerialPerspectiveVolumeDebugSlicePass()`'s own signature/behavior is completely untouched; only ITS CALLER gains a defensive check)**:

   ```cpp
   if (isGameView && viewLuts.aerialPerspectiveVolumeHandle.IsValid()) {
       const rg::TextureHandle debugSlice = m_atmosphereLutRenderer.AddAerialPerspectiveVolumeDebugSlicePass(
           frame.builder, m_renderer, viewLuts.aerialPerspectiveVolumeHandle, aerialVolumeName,
           static_cast<std::uint32_t>(m_atmosphereSettings.aerialPerspectiveDebugSliceIndex),
           "AtmosphereAerialPerspectiveVolumeDebugSlice", rg::ViewScope::GameView);
       frame.finalTextureOutputs.push_back(debugSlice);
   }
   ```

   (This debug-slice pass is Game-View-only by design, already excluded from Scene View before this phase — nothing about Scene View symmetry changes here.)

3. **`"DrawSkyBackground"` provider** (~line 733): currently has
   ```cpp
   if (!viewLuts.has_value() || !sharedLuts.has_value()) {
       return;
   }
   ```
   Add one more condition so a disabled/invalid SkyView LUT this frame also correctly skips drawing the sky (rather than sampling last frame's stale LUT texture — see 3.6 above for why this is the "stale, not dangling" case this closes):
   ```cpp
   if (!viewLuts.has_value() || !sharedLuts.has_value() || !viewLuts->skyViewLutHandle.IsValid()) {
       return;
   }
   ```
   This provider makes no direct call to any of the 5 toggle-aware methods itself (it only calls `MakeRecordSkyBackgroundCallback()`, which wraps `DrawSkyBackground()` — a plain draw, not a pass-declaring call) — no `toggleRegistry` argument needs to be threaded through here.

4. **`"AtmosphereComposite"` provider** (~line 829) — TWO changes here, not one:

   **4a.** currently has
   ```cpp
   if (!viewLuts.has_value()) {
       return;
   }
   ```
   Add the same kind of check for the aerial perspective volume:
   ```cpp
   if (!viewLuts.has_value() || !viewLuts->aerialPerspectiveVolumeHandle.IsValid()) {
       return;
   }
   ```
   and update the `AddAtmosphereCompositePass(...)` call a few lines below to pass `&m_renderPassToggleRegistry` as its new trailing argument.

   **4b. — a second, related gap in this SAME provider, closed at the same time.** Even after 4a's guard passes (upstream volume valid), `AddAtmosphereCompositePass()` can STILL legitimately return an invalid handle — specifically when `"AtmosphereAerialPerspectiveCompositePass"` itself (not any of its upstream passes) is individually disabled via the toggle registry (per 3.4's own guard on that exact method). Today, the provider unconditionally does:
   ```cpp
   frame.finalTextureOutputs.push_back(composited);
   frame.blackboard.Publish<rg::TextureHandle>(
       isGameView ? kGameCompositedOutputKey : kSceneCompositedOutputKey, composited);
   ```
   Pushing an invalid handle onto `finalTextureOutputs` is harmless (nothing ever writes a literally-invalid handle, so `RenderGraphCompiler::Compile()`'s root-matching scan simply never matches it) — but **publishing it onto the blackboard is not harmless**: `Core::FindPluginRenderFeatureTarget()` (the ONE real consumer of `kGameCompositedOutputKey`/`kSceneCompositedOutputKey`, feeding the `"PluginRenderFeatures"` provider) does `info.target = composited.value_or(viewData->colorTarget);` — since `Fetch<rg::TextureHandle>()` returns an `std::optional` that legitimately HAS A VALUE here (just an invalid `TextureHandle`), the existing, correct `.value_or(viewData->colorTarget)` fallback is silently bypassed, and an invalid handle can reach a real `ReadTexture()`/`WriteTexture()` declaration on the `"PluginRenderFeatures"` pass — the exact same class of hazard as 2b above (LDD-6 correctly keeps the Plugin Render Feature system itself out of scope for this campaign — this fix does NOT touch that system at all; it only makes sure THIS provider never publishes a value that system was never designed to receive). Fix by only publishing/keeping `composited` when it is actually valid:
   ```cpp
   const rg::TextureHandle composited = AddAtmosphereCompositePass(frame.builder, m_renderer,
       m_atmosphereLutRenderer, *viewData->renderTexture, viewData->colorTarget,
       viewLuts->aerialPerspectiveVolumeHandle, aerialVolumeName, viewLuts->frameUniforms,
       viewData->eyeWorldPosition, m_atmosphereSettings.aerialPerspectiveStrength,
       m_atmosphereSettings.aerialPerspectiveMaxDistanceKm, m_atmosphereSettings.aerialPerspectiveDepthExponent,
       viewData->renderTexture->Extent(), outputTextureName, legacyViewScope, &m_renderPassToggleRegistry);

   if (!composited.IsValid()) {
       // "AtmosphereAerialPerspectiveCompositePass" was individually toggled off this
       // frame even though its own upstream volume is valid this frame - degrade
       // gracefully: publish NOTHING under kGameCompositedOutputKey/kSceneCompositedOutputKey
       // this frame, so FindPluginRenderFeatureTarget()'s own composited.value_or(...)
       // correctly falls back to viewData->colorTarget (always valid by construction)
       // instead of an invalid handle silently reaching a real ReadTexture()/WriteTexture()
       // declaration on the "PluginRenderFeatures" pass.
       return;
   }

   frame.finalTextureOutputs.push_back(composited);

   frame.blackboard.Publish<rg::TextureHandle>(
       isGameView ? kGameCompositedOutputKey : kSceneCompositedOutputKey, composited);
   ```

### 3.8 — New Tier-1 test for the extracted helper (3.4a)

Create `tests/Renderer/Atmosphere/AtmospherePassToggleLogicTests.cpp` (mirrors this repository's existing `tests/Renderer/Atmosphere/*.cpp` files — Vulkan-header-free, zero `Renderer`/`VkDevice` involved, same precedent as `AtmosphereMathTests.cpp`). The include path below is the confirmed, real convention this test suite already uses (verified directly against `tests/Renderer/Atmosphere/AtmosphereMathTests.cpp`'s own `#include "Renderer/Atmosphere/AtmosphereMath.h"` line — a path relative to `src/`, not a relative-dot-dot path to the physical file, and not a bare filename) — copy it exactly, do not substitute a `../../../src/...`-style relative path:

```cpp
#include "Renderer/Atmosphere/AtmospherePassToggleLogic.h"

#include <gtest/gtest.h>

namespace {

TEST(AtmospherePassToggleLogicTest, ReturnsFalseWhenDisabledRegardlessOfUpstreamValidity)
{
    EXPECT_FALSE(gte::ShouldDeclareAtmospherePassThisFrame(/*passEnabledThisFrame=*/false,
        /*allUpstreamHandlesValid=*/true));
    EXPECT_FALSE(gte::ShouldDeclareAtmospherePassThisFrame(false, false));
}

TEST(AtmospherePassToggleLogicTest, ReturnsFalseWhenAnyUpstreamHandleIsInvalidRegardlessOfEnabledState)
{
    EXPECT_FALSE(gte::ShouldDeclareAtmospherePassThisFrame(/*passEnabledThisFrame=*/true,
        /*allUpstreamHandlesValid=*/false));
}

TEST(AtmospherePassToggleLogicTest, ReturnsTrueOnlyWhenEnabledAndEveryUpstreamHandleIsValid)
{
    EXPECT_TRUE(gte::ShouldDeclareAtmospherePassThisFrame(/*passEnabledThisFrame=*/true,
        /*allUpstreamHandlesValid=*/true));
}

} // namespace
```

Add one new line to `tests/CMakeLists.txt`'s test source list, immediately after the existing `Renderer/Atmosphere/DirectionalLightResolverTests.cpp` entry:
```
Renderer/Atmosphere/AtmospherePassToggleLogicTests.cpp
```

This test file, plus its own new source file, may be compiled/run right now, incrementally, to confirm it passes — LDD-8 only defers the FULL `ctest` regression suite to PHASE3, it does not forbid adding and locally verifying one new, self-contained test file in the phase that introduces the logic it covers.

### 3.9 — Build (incremental only)

```
cmake --build build
```
Fix all compile errors (expect some parameter-count mismatches on first try if you skipped re-reading the actual current file before editing — re-read with `read_file` and adjust; every signature in this document has already been verified against the real, current files, so a mismatch at this point most likely means a transcription slip while editing, not a wrong reconstruction in this document). No full `ctest` yet.

### 3.10 — Live verification via `run_app_background` + `gte_send_request`

1. Launch the engine, confirm liveness via `/get_logs`.
2. **Baseline**: `GET /get_game_view` — confirm unchanged from PHASE1's own baseline (no regression from this refactor alone, with every pass still at its default enabled state).
3. **The mechanical proof for Root Cause #2, AND the confirmed-crash regression test (3.7, item 2b)**: `GET /render_graph/set_pass_enabled?name=AtmosphereTransmittanceLutPass&enabled=false`, then `GET /render_graph` — confirm `AtmosphereTransmittanceLutPass` (and, cascading, `AtmosphereMultiScatteringLutPass`/`AtmosphereSkyViewLutPass`/`AtmosphereAerialPerspectiveVolumePass`/`AtmosphereAerialPerspectiveCompositePass`) are now genuinely **absent** from the pass list (or show zero draws/GPU time) — this is the fix confirmed. Also confirm `GET /get_game_view` now shows the `kGameClearColor` fallback (since PHASE1's `ClearViewTarget` still runs, and nothing draws the sky anymore) rather than a stale/incorrect-looking sky. **Critically: confirm the engine process is STILL RUNNING and responsive after this exact call** (re-confirm via `/get_logs`) — before section 3.7's item 2b fix, this exact step is the one that would have crashed the process (Game View is always the active view here, so the unguarded debug-slice call would have fired with an invalid volume handle). If the process is unresponsive/gone at this step, the 2b fix was not applied correctly — stop and re-check it before continuing.
4. **The second gap's fix (3.7, item 4b), verified narrowly without needing a live plugin** (loading a demo plugin is out of scope, LDD-6): `GET /render_graph/set_pass_enabled?name=AtmosphereAerialPerspectiveCompositePass&enabled=false` (with everything else re-enabled first) — confirm via `GET /render_graph` that only this one pass (not its upstream LUTs) disappears, `GET /get_game_view` still shows a normal rendered scene (RenderOpaque + sky, just without the aerial-perspective composite blended on top — expected, correct degrade), and `GET /get_logs?level=error` shows nothing new. This confirms the provider's own early-return-before-publish path is exercised and harmless even with zero plugins loaded (`kGameCompositedOutputKey` simply never gets published this frame, and nothing reads it without a plugin loaded anyway — this step only proves no crash/error, not the plugin fallback path itself, which remains provably safe by code inspection given LDD-6's own boundary).
5. Re-enable it (`enabled=true`), confirm `GET /get_game_view` returns to a correct, full atmosphere render.
6. Re-run the user's exact "atmosphere only" acceptance test one more time end-to-end (disable `RenderOpaque`/`RenderTransparent`, keep everything Atmosphere-related + `DrawSkyBackground` enabled) — confirm a real sky renders, matching PHASE0's Success Criterion #2.
7. Check `GET /get_logs?level=warning`/`?level=error` for anything new/unexpected.
8. Restore every pass to enabled, `stop_app_background`.

### 3.11 — Report

Write `PHASE2_COMPLETION_REPORT.md` in this folder describing the diff, compile result, the new Tier-1 test's result, and live verification results — call out explicitly that step 3.10.3's crash-regression check was performed and passed (this is the single most important line of that report). Commit (`git_add` + `git_commit`), message e.g. `"editor-core-separation-20 PHASE2: thread RenderPassToggleRegistry through the Atmosphere LUT/composite pass chain with cascading IsValid() skips, fix a confirmed out-of-bounds crash this change would otherwise expose in the debug-slice/plugin-composited-output call sites"`.
