# Atmosphere

> Physically based sky, aerial perspective and sun light tint. For anyone tuning or debugging the sky.
> Verified against code: 2026-10-30

## TL;DR
- Feature `Atmosphere` in `src/Features/Atmosphere/`. Always compiled in. No CMake switch.
- The math follows the `pl-sky` sample (a local reference clone, `_reference/pl-sky`, not part of the build). The CPU code in `AtmosphereMath.*` is the reference.
- It draws the sky behind the scene and adds haze and light shafts on top of the scene.
- It also publishes a sun light tint. SceneLighting and VolumetricLight use it.
- All output is linear HDR. [Tone mapping](tone-mapping.md) runs after it.

## Where in the frame

| Registration | Stage, priority | Work | Toggle names |
|---|---|---|---|
| `AtmosphereSharedLut` | PreOpaqueShared, -10 | Transmittance LUT 256x256 RGBA8. Multi-scattering LUT 64x64 RGBA16F. Shared by all views | `AtmosphereTransmittanceLutPass`, `AtmosphereMultiScatteringLutPass` |
| `AtmosphereViewLut` | PreOpaque, 0 | Sky-view LUT 200x100 RGBA16F per view | `AtmosphereSkyViewLutPass` |
| `Atmosphere.SunLight` | PreOpaque, 5 | Publishes `SunLightModulation` per view | none (`Always`) |
| `AtmosphereAerialVolume` | PostOpaque, 20 | Aerial volume 128x128x32 RGBA16F. Shaft delta image 128x128 RGBA16F when cascades exist | `AtmosphereAerialPerspectiveVolumePass` |
| `DrawSkyBackground` | PostOpaque, 100 | Fullscreen sky where depth is still the clear value | `DrawSkyBackground` |
| `AtmosphereComposite` | PostComposite, -1000, `AlphaOver` | Applies the volume to the scene. Blits to the private target | `AtmosphereAerialPerspectiveCompositePass`, `AtmosphereComposite.CopyToPrivateTarget` |

- Channels: `SharedLuts`, `ViewLuts`, `AerialVolume`, `SunLight`. It reads `Shadow.Receiver`.
- `SharedLuts` and `ViewLuts` carry the types of `src/Game/Lighting/AtmosphereSkyLuts.h` (`AtmosphereSharedLuts`, `AtmosphereViewLuts` with the sky intensity), so another feature can read the transmittance, multi-scattering and sky-view tables. The VolumetricCloud feature does. The GPU structs (`AtmosphereParametersGpu`, `AtmosphereFrameUniforms`) live in the same header, the shared GLSL math in `src/Shaders/AtmosphereCommon.glsl`.
- With the volume pass off, nothing is published. The composite then does nothing.
- In the Scene view, the aerial volume and composite start **off**. The LUTs, sun light and sky stay on.
- Output texture: `GameViewComposited` / `SceneViewComposited` (linear HDR).

## Settings

| Key | Default | Range | Meaning |
|---|---|---|---|
| `aerialPerspectiveStrength` | 1 | 0..1e30 | 1 = physical. 0 = scene color unchanged |
| `skyIntensity` | 6 | 0..50 | Linear multiplier of the sky picture. The deep blue sky look uses 3 (see volumetric-cloud.md) |
| `sunDiskIntensity` | 8 | 0..100 | Sun disk drawn into the sky background, times the physical disk radiance. 0 = no disk. The bloom makes it glow |
| `sunDiskRadiusDeg` | 0.5 | 0.05..10 | Angular radius of the sun disk (degrees) |
| `autoScale` | true | bool | Picks haze range from the visible depth range |
| `aerialPerspectiveMaxDistanceKm` | 0.5 | 0.01..50 | Haze range. Fallback when `autoScale` is on |
| `aerialPerspectiveDepthExponent` | 2 | 1..4 | Slice distribution along depth |
| `aerialPerspectiveSamplesPerSlice` | 8 | 1..16 | Ray-march samples per slice |
| `aerialPerspectiveScatteringExaggeration` | 8 | 0.1..50 | Boosts haze for small scenes. Fallback with `autoScale` |
| `sceneLightFollowsAtmosphere` | true | bool | Sun tint and ambient follow the sky |

- Physical settings (panel only): planet radius 1..50000 km, thickness 1..500 km, multi-scattering 0..10, scale heights 0.01..100 km, Mie g -0.99..0.99, ozone, colors 0..1.
- Defaults are Earth-like: planet 6371 km, atmosphere 100 km, Rayleigh scale height 8 km, Mie 1.2 km.
- Values go through `SanitizeAtmosphereSettings()`. A non-finite value becomes the default.
- Defaults of distance and exaggeration fit small scenes. Big scenes rely on `autoScale`.

## Auto scale

| Step | Rule |
|---|---|
| Input | `visibleFarKm` = last cascade split / 1000, from a depth-fitted shadow receiver |
| Max distance | `clamp(0.5 * exp2(ceil(log2(x / 0.5) * 4 - 1e-4) / 4), 0.5, 30)` with `x = visibleFarKm * 1.25` |
| Exaggeration | `clamp(4 / maxDistanceKm, 1, 8)` |
| Shadow blend | `smoothstep(0.5, 4, maxDistanceKm)` |
| No fitted receiver | Last scale is held for 120 frames. Then the settings values apply with blend 0 |
| `autoScale` off | The sliders apply. Shadow blend is 1 |

- The grid has 4 steps per octave, so the value does not flicker inside one step.
- A small scene gets 0.5 km, 8x and blend 0. The volume pass then does no shadow taps.

## Cloud shadows in the air
When the cloud feature publishes its `ShadowMap` channel with a shaft strength above 0, the aerial perspective volume multiplies the sun visibility of each sample by the cloud sun transmittance (`CloudShadowLookup.glsl`). The shaft delta (light removed from the sky background) then includes the cloud shadow, so columns of sky under clouds get darker and the haze over geometry is lit only where the sun passes the layer. The beams of the cloud shadow come from the beam haze of this volume (cloud setting `shaftHaze`), which reaches at least 24 km while it is on. The near mist of VolumetricLight is a separate option (`shaftMist`, default 0).

## Sun visibility of the air
Every air sample asks `AirSunVisibilityAt` (`Shaders/AirSunVisibility.glsl`, shared with VolumetricLight): the sun cascades, with the shadow fading out over the last quarter of the last cascade, then the cloud map. Terrain shadow reaches only as far as the cascades and only if the cascade depth span holds the terrain (`terrainReliefM` in [Shadow](shadow.md)). Beyond the last cascade the air is lit. The beam haze fill light (`shaftFill`) is reduced to 30 percent in terrain shadow and stays complete in cloud shadow. The beam light a view ray collects is held under a soft ceiling (`kBeamLightCap` in `AtmosphereAerialPerspectiveVolume.comp`, in sun units per channel): the glow of lit haze around the sun direction stays bounded, so a backlit mountain is veiled but never washed out, while the thin columns of the cloud shadow stay below the knee and keep their contrast.

## Debug keys

Writable (`/feature_debug/set`): `sceneLightFollowsAtmosphere`, `autoScale`, `aerialPerspectiveStrength`, `aerialPerspectiveMaxDistanceKm`, `aerialPerspectiveDepthExponent`, `aerialPerspectiveSamplesPerSlice`, `aerialPerspectiveScatteringExaggeration`, `skyIntensity`, `sunDiskIntensity`, `sunDiskRadiusDeg`, `multiScatteringStrength`, `miePhaseG`, `rayleighScaleHeightKm`, `mieScaleHeightKm`.

Read-only, per view with prefix `game.` or `scene.`:

| Key | Meaning |
|---|---|
| `active`, `sunTintR/G/B`, `ambientScale` | The sun light record published this frame |
| `aerial.maxDistanceKm`, `aerial.exaggeration`, `aerial.shadowBlend` | Values the volume used |
| `aerial.cascadeCount` | 0 = no shadow taps |
| `aerial.receiver` | `Used`, `Missing`, `Rejected`, `Mismatch`, `NotChecked` |
| `aerial.scaleHeld` | true = last scale is reused |
| `frameIndex`, `aerial.frameIndex` | Frame of the record |

- A bad key or value is HTTP 400 and nothing is applied. A read-only key gives `key is read-only`. There are no actions.

## Quick check

```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=Atmosphere"
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=Atmosphere&autoScale=false&aerialPerspectiveMaxDistanceKm=5"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
```

- More recipes: [HTTP cheat sheet](../../debugging/http-cheat-sheet.md). Settings reset after a restart.

## Rules for developers

1. **CPU math wins.** If a shader and `AtmosphereMath.*` disagree, fix the shader. Add CPU math first for a new LUT.
2. **Shader names are the C++ contract.** Blocks: `params`, `frame`, and `shadow` (384 bytes). Images: `transmittanceLutImage`, `multiScatteringLutImage`, `destinationImage`, `destinationVolume`, `destinationShaftDelta`, `transmittanceLut`, `multiScatteringLut`, `shadowDepth0..3`, `skyViewLut`, `sourceColor`, `sourceDepth`, `aerialPerspectiveVolume`, `aerialShaftDelta`.
3. **Parameter blocks are read-only storage buffers**, not uniform blocks. Binding 8 (SceneLighting) is the only uniform block.
4. **The volume is a real 3D texture** kept by `PersistentVolume`. See [Volumetric resources](../../conventions/volumetric-resources.md).
5. **Aerial perspective stays out of mesh shaders.** It is a screen-space composite only.
6. **Sky and composite do their own transmittance.** They must not use `SunLightModulation`.
7. **Sky pixels only lose shaft light.** Pixel with `rawDepth >= 0.999999` (`Shaders/SceneDepthConstants.glsl`) is `scene + shaftDelta * strength * skyIntensity`, clamped at 0.
8. **Two shader variants** per stage: shadowed or unshadowed. The pass name stays the same. Keep the unshadowed math equal to sun visibility 1.
9. **One record per pass.** `AtmosphereAerialResult` carries volume, shaft delta, max distance and exponent. The composite reads them from it, not from settings.
10. **Shadow receiver is used only if safe.** `SunShadowReceiverUsable()` is true and `dot(receiver sun, frame sun) >= 0.9999`.
11. **Sun light follows the sky.** `ComputeAtmosphereSunLight` gives `sunTint` and `ambientScale`: 1 for a sun overhead, 0 below the horizon.
12. **Composite output** is clamped: transmittance 0..1, color 0..65504. The push block is 112 bytes.

## Limits
- A sun below the horizon gives black light for materials and fog. The sky can still show twilight.
- A `DirectionalLight` that points up gives a black scene.
- Air outside all cascades counts as lit. Shadows of far mountains do not reach sky rays.
- The shaft delta stops at the aerial max distance.
- Shadow lookups lose precision beyond about 100000 m from the origin.
- First frames after start or a Shadow toggle use the settings values.
- GPU timing of these passes can read `Absent`.
- Scene files save ECS components such as `DirectionalLight`. They do not save Atmosphere settings.

## Related
- [Render features](README.md) · [Shadow](shadow.md) · [Scene lighting](scene-lighting.md) · [Volumetric light](volumetric-light.md)
- [Writing a render feature](../../guides/writing-a-render-feature.md) · [Feature kit](../feature-kit.md)
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md)
