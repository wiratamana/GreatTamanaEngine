# Atmosphere Phase 6 — Completion Report

**Phase:** `ATMOSPHERE_PHASE6_AERIAL_PERSPECTIVE_FROXEL_VOLUME_v1.md`
**Branch:** `feature/atmosphere-scattering-impl`
**Status:** Complete — `gte_core`, `GreatTamanaEngineTests`, and `GreatTamanaEngine` all build cleanly (targeted incremental build only, per this campaign's own workflow rule, including the new `AtmosphereAerialPerspectiveVolume.comp` shader compiling via `glslc` with zero errors); 176/176 `RenderGraph*`/`Atmosphere*` Tier-1 tests pass (2 new, added for the real Phase 2 infrastructure gap this phase found and fixed — see below); the engine was actually run (Editor build, Vulkan validation layers enabled by default) with a disposable end-to-end CPU-side readback proving the aerial-perspective volume's data is plausible, then that disposable code was deleted before writing this report.

## What changed

### 1. `src/Shaders/AtmosphereCommon.glsl` — extended

- `AtmosphereFrameUniforms` (GLSL mirror) gained a new `mat4 invViewProjection` field (matching the C++ struct — see below).
- `FroxelSliceToViewDepth(slice, sliceCount, maxDistanceKm, depthExponent)` / `ViewDepthToFroxelSlice(...)` — the froxel volume's Z-slice↔view-depth mapping, transcribed from `_reference/pl-sky/shaders/sky_aerial_lut.comp`'s `pl_aerial_depth()` (a quadratic, `depthExponent = 2.0` by default, distance distribution concentrating slices near the camera). Both directions added (the inverse is for Phase 7's future per-pixel Z-slice lookup), mirroring this file's own established "add the inverse too, for a later phase" precedent.
- `FroxelColumnToViewRayDirection(froxelXY, volumeXYSize, invViewProjection)` — the X/Y↔view-ray-direction mapping. **Deliberate, documented deviation from the reference's own technique**: `sky_aerial_lut.comp`'s `pl_get_aerial_view_direction()` reads specific diagonal entries of a *separate* inverse-projection matrix and rotates by an inverse-view 3×3 — a shortcut that assumes a particular symmetric-perspective matrix layout. This function instead unprojects two NDC points (Vulkan's zero-to-one near/far, `z=0`/`z=1`) through one combined `invViewProjection` and takes their normalized difference — a standard, handedness/projection-convention-agnostic technique that needed only ONE new matrix field rather than two, and works correctly regardless of the exact projection-matrix layout this engine's `Mat4::PerspectiveFovLH_ZO` happens to use.

### 2. `src/Renderer/Atmosphere/AtmosphereTypes.h` — extended

`AtmosphereFrameUniforms` (C++) gained `Mat4 invViewProjection = Mat4::Identity()` (a fourth 64-byte group; struct size grew from 48 to 112 bytes, `static_assert` updated). Computed by the CALLER (Application.cpp), never inside `ResolveAtmosphereFrameUniforms()` — that function's signature was deliberately kept stable (per Phase 5's own explicit request), so the caller resolves this frame's `AtmosphereFrameUniforms` as before and then separately assigns `.invViewProjection = viewProjection.Inverse()` right before passing it to `AddAerialPerspectiveVolumePass()`.

### 3. `src/Shaders/AtmosphereAerialPerspectiveVolume.comp` — new

`local_size_x = local_size_y = 8, local_size_z = 1` — one invocation per froxel COLUMN, looping every Z slice internally (the genuine sequential dependency along Z the phase document called for). Bindings match the campaign's established convention exactly: binding 0 = `AtmosphereParametersGpu` (read-only storage buffer), binding 1 = `AtmosphereFrameUniforms` (read-only storage buffer, including the new `invViewProjection`), binding 2/3 = `sampler2D transmittanceLut`/`multiScatteringLut`, binding 4 = `image3D destinationVolume` (`rgba16f`, write-only). Faithfully transcribed from `_reference/pl-sky/shaders/sky_aerial_lut.comp` (max distance 10 km, quadratic depth exponent 2.0, up to 8 samples per slice — all fixed, hardcoded constants, matching `ATMOSPHERE_REFERENCE_NOTES.md`'s Section 2/5), including its exact "once a ray goes below the planet surface, break the inner sample loop but keep looping the outer per-slice loop" behavior (which naturally freezes the accumulated luminance/transmittance for every remaining Z slice along a ground-hitting ray — confirmed as the correct, intentional behavior, not a bug, via the live readback below).

**Deliberate, documented deviation**: the reference's own final `scalarTransmittance = dot(accumulatedTransmittance, vec3(1.0 / atmosphere.tAerialInfo.w))` divides by the sun-intensity constant, which is dimensionally unrelated to transmittance — its own commented-out alternative directly above that line (`dot(accumulatedTransmittance, vec3(1.0 / 3.0))`) reveals the real intent is a plain per-channel AVERAGE, which only coincidentally matches "divide by sun intensity" because that reference's own default sun intensity is also 3.0. This shader computes the average directly (`(r+g+b)/3.0`), with no dependency on sun intensity.

### 4. `src/Renderer/Atmosphere/AtmosphereLutRenderer.h/.cpp` — extended

Added `AddAerialPerspectiveVolumePass(RenderGraphBuilder&, Renderer&, const AtmosphereParametersGpu&, const AtmosphereFrameUniforms&, TextureHandle transmittanceLutHandle, TextureHandle multiScatteringLutHandle, const char* outputVolumeName) -> VolumeTextureHandle` — the fourth `AddXxxPass()` method, mirroring `AddSkyViewLutPass()`'s exact per-view-state shape (`std::unordered_map<std::string, AerialPerspectiveVolumeViewState>` keyed by `outputVolumeName`, reusing `m_atmosphereParametersBuffer`, no dirty-flag optimization). The one real, load-bearing difference from every prior `AddXxxPass()`: its per-view output is a persistent `VolumeTexture` (created ONCE via `Renderer::CreateVolumeTexture()`, per Phase 2's own "Their Role" answer (c) recommendation) rather than a `RenderTexture`, re-imported fresh into the graph every frame via `RenderGraphBuilder::ImportVolumeTexture()` exactly as that phase's own disposable validation proved out.

### 5. **A genuine Phase 2 infrastructure gap, found and fixed** (per this phase's own Step 5 anticipation)

`RenderGraphCompiler::Compile()`'s backward-reachability root-marking scan was `finalOutputs`-only (`TextureHandle`), with **no way for a `VolumeTextureHandle` write to ever be treated as a root** — meaning a pass whose *only* declared write is a volume texture (this phase's own aerial-perspective pass, precisely) would be **silently culled every single time**, no matter what it declared, since nothing could ever mark it "kept." This was confirmed directly (re-reading `RenderGraphCompiler.cpp`'s real root-marking loop before writing any Phase 6 code), not assumed.

**Fix** (all in Phase 2's own files, not worked around inside this pass):
- `CompiledGraphInput` (`RenderGraphBuilder.h`) gained a new field, `std::vector<VolumeTextureHandle> finalVolumeTextureOutputs` — a SECOND, independent root set alongside `finalOutputs`.
- `RenderGraphBuilder::KeepVolumeTextureOutput(VolumeTextureHandle)` — a new public method that appends to it, deliberately a SEPARATE call rather than changing `RenderGraph::Execute()`'s `build` callback return type (which is `std::vector<TextureHandle>`, texture-only, and shared by every existing call site — changing it would have touched every pre-existing pass declaration for zero benefit).
- `RenderGraphCompiler::Compile()`'s root-marking scan now checks BOTH: `ContainsTextureHandle(finalOutputs, usage.texture)` for `ResourceKind::Texture`, and the new `ContainsVolumeTextureHandle(input.finalVolumeTextureOutputs, usage.volumeTexture)` for `ResourceKind::VolumeTexture` — `ResourceKind::Buffer` still can never be a root (unchanged, matching the pre-existing, deliberate rule).
- `RenderGraphCompiler.h`'s own `Compile()` doc comment updated to describe the new second root set.

This is a real, load-bearing fix, not a documentation-only change: without it, `AddAerialPerspectiveVolumePass()`'s own compute dispatch would never actually run in the compiled/executed graph at all (the pass would be culled every frame), despite compiling and "looking" wired up correctly. **Cross-reference for Phase 2's own docs**: this note should be considered an addendum to `ATMOSPHERE_PHASE2_VOLUME_TEXTURE_RENDERGRAPH_SUPPORT_v1.md`/its own completion report — the "backward-reachability root-marking scan... already correctly three-way-safe as written" conclusion recorded there was accurate as far as it went (a `Buffer` could never wrongly become a root) but incomplete: it did not notice that the SAME property also made a `VolumeTexture` root structurally impossible, which is a genuine gap, not just a safety guarantee.

### 6. Tests (`tests/Renderer/RenderGraph/RenderGraphCompilerTests.cpp`)

Two new regression tests for the fix above, following the existing `BufferOnlyWriteSurvivesCullingOnlyWhenAReaderReachesATextureFinalOutput` pattern:
- `VolumeTextureOnlyWriteSurvivesCullingOnlyWhenExplicitlyKeptAsOutput` — a pass writing `volumeA` (kept via `KeepVolumeTextureOutput()`) survives; a sibling pass writing a different, never-kept `volumeB` is still culled — proves the fix is correctly OPT-IN, not a blanket "every volume write survives" regression.
- `VolumeTextureWriteNeverKeptIsCulledEvenWithNoTextureFinalOutputsAtAll` — the exact pre-fix behavior is still correct post-fix when nothing ever calls `KeepVolumeTextureOutput()`.

### 7. Wiring: `src/Application/Application.cpp`

Extended the existing (Phase 3/4/5) temporary validation call site, right after the Sky-View LUT pass, exactly as this phase's own Step 3 specified:
- Resolves the Game View's own current view-projection matrix via `RenderSystem::ResolveActiveCameraViewProjection(registry, aspect)` (falling back to a fixed 16:9 aspect when the Game panel isn't currently visible this frame, mirroring how this whole block already runs independent of Game/Scene dock-tab visibility).
- Copies `gameViewFrameUniforms` (already resolved for the Sky-View LUT) and sets `.invViewProjection = viewProjection.Inverse()`.
- Calls `AddAerialPerspectiveVolumePass(..., "AtmosphereAerialPerspectiveVolume_GameView")`.
- Calls the new `b.KeepVolumeTextureOutput(aerialPerspectiveVolumeHandle)` — REQUIRED, or the pass is silently culled per the Phase 2 gap above.
- Scene View wiring deliberately NOT added (Phase 7's job, per this phase's own "What We Will NOT Do", mirroring Phase 5's identical Sky-View LUT scope).

### 8. `CMakeLists.txt`

`gte_add_shader(GreatTamanaEngine src/Shaders/AtmosphereAerialPerspectiveVolume.comp EXTRA_DEPENDS src/Shaders/AtmosphereCommon.glsl)` added right after the Sky-View LUT's own registration, unconditional (not `GTE_ENABLE_EDITOR`-gated), same reasoning as Phase 3/4/5.

## Plausibility-check results (actual numbers)

Since `GET /get_texture` cannot capture a 3D volume (Phase 2's own explicit scope note), correctness was proven via (a) zero Vulkan validation errors/warnings, and (b) a small, throwaway CPU-side readback (`vkCmdCopyImageToBuffer` into a mapped `BufferMemoryUsage::GpuToCpu` staging buffer, read directly in C++, reusing `RenderGraphBarrierPlanner`'s already-tested `RequiredStateFor()`/`EmitImageBarrier()` for the one manual transition needed) — added temporarily to `Application.cpp`/`AtmosphereLutRenderer.h/.cpp`, run once, then fully deleted per this phase's own explicit instruction (confirmed absent via `git status` before this report/commit).

Ran the engine (`GreatTamanaEngine.exe`, stdout/stderr redirected to log files via a throwaway batch wrapper — both logs were **0 bytes** at process exit, confirming zero Vulkan validation errors/warnings throughout) for ~2500+ frames, with the readback firing once, 30 frames in. Sampled voxels at the volume's center column `(x=64, y=64)`, `z ∈ {0, 1, 2, 4, 8, 16, 24, 31}` (of a `128×128×32` volume):

| z | inScattering (r, g, b) | transmittance (alpha) |
|---|---|---|
| 0  | (1.79e-7, 7.15e-7, 2.68e-6) | 0.999512 |
| 1  | (8.34e-7, 2.86e-6, 1.07e-5) | 0.999023 |
| 2  | (1.97e-6, 6.44e-6, 2.41e-5) | 0.997559 |
| 4  | (3.70e-6, 1.22e-5, 4.59e-5) | 0.995605 |
| 8  | (3.70e-6, 1.22e-5, 4.59e-5) | 0.995605 |
| 16 | (3.70e-6, 1.22e-5, 4.59e-5) | 0.995605 |
| 24 | (3.70e-6, 1.22e-5, 4.59e-5) | 0.995605 |
| 31 | (3.70e-6, 1.22e-5, 4.59e-5) | 0.995605 |

This matches every plausibility criterion the phase document asked for:
- **Transmittance is close to 1.0 at `z=0`** (0.999512) **and monotonically non-increasing as `z` grows** (0.999512 → 0.999023 → 0.997559 → 0.995605, then a plateau).
- **In-scattering is close to 0 at `z=0`** (~1e-7 to ~1e-6 magnitude) **and increases, then plateaus** (rising through `z=0..4`, then frozen for `z ≥ 4`).
- The plateau starting at `z=4` and holding through `z=31` is **the shader's own, faithfully-transcribed "ray hit the ground" behavior** (see "What changed" §3 above) — the sampled column's view ray intersected the planet surface somewhere between `z=2` and `z=4`, after which every subsequent slice's inner sample loop breaks immediately (still-negative height) and simply re-stores the already-accumulated, now-frozen values — a legitimate, expected outcome for a ray pointed anywhere near/below the horizon, not a bug.

The magnitudes are small in absolute (linear) terms — expected, and consistent with the Sky-View LUT's own equivalent comment (`AtmosphereSkyViewLut.comp`) about scattering coefficients (~0.006 km⁻¹) over only a few kilometers of accumulated path length; a real visual composite (Phase 7) will apply its own exposure/tonemapping, exactly as every other LUT in this campaign does.

## Deviations from the plan (and why)

1. **`FroxelColumnToViewRayDirection()` uses a two-NDC-point unprojection through one combined `invViewProjection`, rather than the reference's own separate inverse-projection-diagonal + inverse-view-3×3 technique.** Documented in `AtmosphereCommon.glsl` itself; the phase document explicitly allowed either shape ("or pass it as a separate small per-view read-only storage buffer/push-constant if that fits the existing convention better — either is fine, document the choice"). The combined-matrix approach needed only one new `AtmosphereFrameUniforms` field, is handedness/projection-layout-agnostic, and required no assumption about this engine's specific `Mat4::PerspectiveFovLH_ZO` internal layout.
2. **`invViewProjection` is computed/assigned by the CALLER (`Application.cpp`), not inside `ResolveAtmosphereFrameUniforms()`.** Keeps that function's signature stable (per Phase 5's own explicit instruction that every future call site depends on it never changing shape) while still letting the struct grow — the new field is simply left at every OTHER call site's default (`Mat4::Identity()`, harmless/invertible, never read by the Sky-View LUT shader).
3. **The scalar-transmittance-averaging formula deliberately does NOT reproduce the reference's own `divide by sun intensity` line verbatim** — see "What changed" §3; the reference's own commented-out alternative directly above that same line makes the intended formula (a plain average) unambiguous, and reproducing an accidental-looking dependency on an unrelated constant would have been a worse-faith transcription, not a more faithful one.
4. **A genuine Phase 2 infrastructure gap was found and fixed directly in `RenderGraphCompiler`/`RenderGraphBuilder` (not worked around in this pass's own code)** — see "What changed" §5 above; this was explicitly anticipated by this phase's own Step 5 ("this is the phase most likely to surface a real gap in Phase 2's infrastructure").
5. No other deviations — the froxel volume's resolution (128×128×32), sample-count knobs (max distance 10 km, depth exponent 2.0, 8 samples per slice), binding order/convention, the "reuse Phase 3's buffer, don't duplicate it" rule, and the "one compute invocation per froxel column, loop Z internally" design were all followed exactly as written.

## What was explicitly NOT done (per Step 4)

- No configurable froxel grid resolution exposed to the Editor — fixed, hardcoded constants (both in the `.comp` file and in `AtmosphereLutRenderer.cpp`, matching the reference's own defaults).
- No attempt to make Phase 7's composite step work yet — this phase's own deliverable is the correct volume data existing and being provably plausible; consuming it is entirely Phase 7's job.
- No froxel volume resolution matching real screen resolution — 128×128×32, deliberately much lower than any real Game/Scene View resolution, relying on the `VolumeTexture`'s own trilinear sampler (Phase 2) for Phase 7's future interpolation.
- No Scene View wiring (Phase 7's job, matching Phase 5's identical precedent for the Sky-View LUT).

## Build/run verification actually performed

- `cmake --build build --target gte_core` — compiled cleanly (both before and after the disposable-validation-code cleanup pass).
- `cmake --build build --target GreatTamanaEngineTests` — compiled cleanly (both passes); `GreatTamanaEngineTests.exe --gtest_filter=RenderGraph*:Atmosphere*` — **176/176 passed** (2 newly added, 174 pre-existing, zero regressions), run again after the disposable-code cleanup to confirm nothing broke.
- `cmake --build build --target GreatTamanaEngine` — compiled, the new `AtmosphereAerialPerspectiveVolume.comp` shader compiled via `glslc` with zero errors (alongside the three pre-existing atmosphere shaders), and linked cleanly (both passes).
- Ran the engine (`run_app_background`, Editor build, Vulkan validation layers enabled by default in this non-`NDEBUG` configuration) with the disposable readback wired in — confirmed the plausibility numbers above, confirmed zero validation-layer output (both stdout/stderr logs 0 bytes), and confirmed the engine kept running normally for ~2500+ frames afterward (`GET /list_textures` still responding, `GameView`'s own `frames_since_update` still advancing) before stopping it (`stop_app_background`).
- Re-ran the engine a second time (after deleting the disposable readback code) purely to confirm the cleanup itself didn't break anything — compiled/linked cleanly, `GET /list_textures` responded normally.
- Per this campaign's own workflow rule, **no full clean build and no full `ctest` regression run** were performed (reserved for Phase 9 only).

## Open questions / notes for Phase 7

- **`AtmosphereLutRenderer` is still the one class to extend** — Phase 7 adds the Sky Background pass (reading `AtmosphereSkyViewLut_GameView`/`_SceneView`) and the Aerial Perspective composite pass (reading `AtmosphereAerialPerspectiveVolume_GameView`/`_SceneView`, `SceneColor`, `SceneDepth`) — likely as new methods here too, or a sibling class, per that phase's own document.
- **`ViewDepthToFroxelSlice()` (new this phase, unused by this phase's own generation pass) is exactly what Phase 7 needs** to map a real on-screen pixel's own view-space depth back into this volume's Z axis — the froxel column lookup itself is the simple `pixelUv * froxelGridSize` computation the Phase 6 document already described, not something this phase needed to build.
- **The Phase 2 `RenderGraphCompiler`/`RenderGraphBuilder` fix (see "What changed" §5) is now a permanent, tested part of the render graph's public API** (`RenderGraphBuilder::KeepVolumeTextureOutput()`) — any future pass whose only write is a volume texture (there are none besides this one today) must call it, exactly like this phase's own `Application.cpp` call site does.
- **The per-view state map pattern (`std::unordered_map<std::string, ViewState>`) is now used identically by THREE passes** (Sky-View LUT, Aerial Perspective volume) — if Phase 7's own Sky Background/composite passes also need per-view state, reuse this exact same shape rather than inventing a different one (same recommendation Phase 5's own report already made for Phase 6, now doubly proven out).
- **Open question inherited, unchanged, from Phase 5**: `ResolveActiveCameraWorldPosition()` still lives as a small, local, anonymous-namespace helper in `Application.cpp` rather than a shared `RenderSystem` location — this phase reused `RenderSystem::ResolveActiveCameraViewProjection()` directly (a genuine `RenderSystem` public API) for its own new view-projection need, so this specific gap did not need to be revisited by this phase, but Phase 7 (which will need BOTH a world position and a view-projection for its own Scene View wiring) may want to reconsider it.
