# VolumetricCloud (ray-marched cloud layer)
> One sky cloud layer between two spheres, ray marched at low resolution and accumulated over frames. For anyone tuning or debugging clouds.
> Verified against code: 2026-11-11

## TL;DR
- One cloud layer (default 1500..5000 m above Y = 0) on a 6371 km planet, composited over the scene.
- Needs the baked noise in `assets/clouds/` next to the exe. The build makes it. Without it the effect draws nothing.
- **Off by default.** One call turns it on with the tuned look: `/feature_debug/set?name=VolumetricCloud&enabled=true`.
- Marches a grid about 320 texels wide (`resolutionDivisor` 0 = automatic: 1/4 per axis at 1280 wide, 1/6 at 1920 wide) and accumulates over frames. The cloud passes take about 10 to 16 ms at 1280x720 and 14 to 19 ms at 1920x1080 on an Intel Iris Xe (see Performance).
- Scene depth limits every ray, so clouds never draw over a mountain or an object in front of them.
- Off in the Scene view by default. On in the Game view once enabled.
- Sun colour, sky light and distance haze come from the Atmosphere look-up tables (`atmosphereMode` 1, the default). Without them the clouds fall back to a standalone look.

## Where in the frame
| Registration | Stage | Priority | Toggle names |
|---|---|---|---|
| `VolumetricCloud.FrameStamp` | PreOpaque | 49 | `Always` (frame clock for snapshot age) |
| `VolumetricCloud` (`Replace`) | PostComposite | -900 | `VolumetricCloud.March.Dispatch`, `...Resolve.Dispatch`, `...Debug.Dispatch`, `...CopyToPrivateTarget` |

Code: `src/Features/VolumetricCloud/`. Shaders: `VolumetricCloud.comp` (march), `VolumetricCloudResolve.comp` (upsample, history, composite), `VolumetricCloudLighting.glsl`, `VolumetricCloudSlice2D.comp`, `VolumetricCloudSlice3D.comp`. Panel: **VolumetricCloud**.
The pass runs after the aerial composite (-1000), so sky pixels are final. It runs before shadow (-500), VolumetricLight (-250), TemporalAa (-150), Bloom and ToneMapping.
The composite writes `scene * T + cloud` itself and uses `Replace`. The engine `AlphaOver` is straight alpha and does not fit premultiplied cloud light.

### Inputs
| Source | Use |
|---|---|
| Scene color and scene depth of the view | Pixel colour behind the cloud. Depth (standard Z) is the far limit of the ray |
| `Atmosphere.SunLight` channel (`SunLightModulation`) | Standalone look only: sun tint at sunset and the ambient scale. Missing channel gives neutral values |
| `Atmosphere.SharedLuts`, `Atmosphere.ViewLuts` channels | Transmittance, multi-scattering and sky-view LUTs, the planet constants and the sky intensity (see Atmosphere below) |
| First active ECS `DirectionalLight` | Sun direction and illuminance. With none, a fixed placeholder sun is used |
| `assets/clouds/*.gtvol` | Baked noise, see below |

## Atmosphere
This is the hybrid option: the clouds stay a separate feature and read the sky tables the Atmosphere publishes. The channel payloads (`AtmosphereSharedLuts`, `AtmosphereViewLuts`) and the GPU structs of the sky live in `src/Game/Lighting/AtmosphereSkyLuts.h`, and the shared GLSL math in `src/Shaders/AtmosphereCommon.glsl`. The clouds never include the Atmosphere feature.

`atmosphereMode` 1 (default) uses the tables. It falls back to 0 on its own when the Atmosphere publishes none (feature off, or its LUT passes switched off). `game.skyLight` then reads `Unavailable` and the picture is the standalone one.

| What | `atmosphereMode` 0 (standalone) | `atmosphereMode` 1 (tables) |
|---|---|---|
| Sun colour | sun light x tint channel x smooth sun visibility at the camera | sun illuminance x transmittance LUT at each cloud sample (its altitude and sun angle) x planet shadow |
| Sky light | sun luminance x tint x ambient scale | sky-view LUT: the zenith and a ring of 4 at 35 degrees, x reference sky intensity 6 x `skyAmbient`, dimmed by the cloud above each sample (`skyOcclusion`). One lookup set per marched pixel. Warm ground bounce (`groundBounce`) lights the low part |
| Distance | cloud opacity falls with `exp(-t / hazeDistanceM)` into the sky pixel behind | the air between the eye and the cloud: `final = scene*T + cloud*Tair + (1-T)*Lair`. `Lair` is 8 samples of single and multiple scattering from the LUTs along the ray up to the mean cloud distance, `Tair` its transmittance |

Units are the sky's: atmosphere space is km from the planet centre, the camera at `(0, R + height)` like the Atmosphere feature, world units are metres. In mode 1 the cloud shell uses the Atmosphere's planet radius, so the two can not differ; `planetRadiusKm` only applies to mode 0 (standalone) or when no tables are published.
`final = scene*T + cloud*Tair + (1-T)*Lair` leaves a sky pixel unchanged where the cloud lets everything through, because the sky already holds the air light of the same distance. The aerial perspective volume of the Atmosphere only reaches 0.5 km after its auto scale, so it is not used.
`hazeDistanceM` has no effect in mode 1. `sunIntensity` and `ambientIntensity` scale the sun term in both modes; `ambientIntensity` only in mode 0, `skyAmbient` only in mode 1. `airScale` multiplies the air's scattering and extinction between the eye and the cloud (1 is physical).

## How it works
1. **March** (compute, 1/`resolutionDivisor` of the view). Each low-res texel marches one real pixel of its block. The pixel walks a Bayer order, so 16 frames cover the full grid at divisor 4. A ray starts on the layer shell, jittered by blue noise (the jitter of a pixel moves by the golden ratio each time that pixel is marched, so a still picture converges fast), takes long steps over empty air, steps back on the first hit and then takes short steps with the detail noise and a sun march (Beer-Lambert optical depth, dual lobe Henyey-Greenstein, silver lining, 3 multiple scattering octaves, powder, sky ambient). It stores light, transmittance, the mean cloud distance and how far the ray looked.
2. **Resolve** (compute, full size). Reprojects the history of the last frame through the cloud distance (never the scene depth), blends it with the new samples and composites over the scene. A sample or a history texel is only used if it saw at least as far as the pixel's own surface and its cloud is not behind that surface. This keeps the silhouette of a mountain clean.
3. **History** is two pairs of full-size RGBA16F targets (colour; distance, end distance and the per-pixel sample count), used in ping-pong. It is dropped on a camera cut, a resize, a disabled frame, a debug view or any early exit.

**Horizon and far field.** The march range is `maxDistanceM` (250 km) and every ray grows its step length to reach the end of its segment within `maxSteps`. Beyond `farWeatherStartM` the weather map is mixed with a second sample at another scale, the coverage is lifted a little and the air in front of the cloud counts less (`farAirScale`), so the layer continues to the horizon as a flattening deck that fades into the haze instead of ending in an empty band. The resolve pass keeps its own fixed fallback distance (15 km, `kCloudHistoryFallbackDistanceM`) and does not follow `maxDistanceM`.

Rays and reprojection use `fx::View::relative`: the camera with the eye at the origin, built in double precision. An absolute float inverse is too coarse far from the origin (about 2 px at 5 km, 18 px at 54 km).

## Noise assets
Baked once by `tools/cloud_noise_bake` (the build runs it, see [Building](../../../BUILDING.md)). Format `GTV1`, read by `fx::StaticVolume` / `fx::StaticTexture` from `<working directory>/assets/clouds/`.

| File | Size | Channels |
|---|---|---|
| `cloud_shape_128.gtvol` | 128^3 RGBA8 | R Perlin-Worley, G/B/A Worley at 8/16/32 cells |
| `cloud_detail_64.gtvol` | 64^3 RGBA8 | R billowy low (inverted Worley, 4/8/16 cells), G billowy high (16/24 cells), B wispy low (ridged Perlin, 4 cells, 3 octaves, domain warped), A wispy high (12 cells, 2 octaves) |
| `cloud_curl_128.gtvol` | 128^2 RGBA8 | R/G curl of a periodic Perlin potential (0.5 = zero), B a second independent field (height shift) |
| `cloud_weather_512.gtvol` | 512^2 RGBA8 | R coverage (v3): one smooth multi-scale field (Perlin fbm at 3 / 8 / 20 cells per tile, domain warped, plus a regional busy/quiet field), so the sky holds huge masses, medium clouds and small puffs of every size joined by soft gradients; its local cap varies smoothly between 0.40 and 0.68 (no flat plateaus), and a few sparse small puffs (cap 0.55) sit in the gaps. v2 used cut-off Worley cells: flat discs of nearly one size whose straight edges showed as boxes; G high coverage (broad overcast-like regions, only used when `coverage` is above 0.5); B cloud type / peak: 0 flat low top, 1 tall cumulus (regional type field plus the big-mass field, multiplied by a ramp of R so weak thin coverage gets a low top: a cloud is about as tall as it is wide); A density 0.45..1 |
| `blue_noise_128.gtvol` | 128^2 R8 | Blue noise for the ray start jitter |

All tile on every axis and sample with REPEAT. A file is read once; restart the editor after a re-bake. A missing file logs one error and the status reads `AssetMissing`.

## Settings
Sanitized on write (`SanitizeVolumetricCloudSettings`). NaN and infinity become the default. Not saved in the scene file. Lengths are metres, angles degrees.

| Key | Default | Range | Meaning |
|---|---|---|---|
| `enabled` | false | bool | Off gives status `Disabled` |
| `debugView` | `Off` | `Off`, `ShapeSlice`, `DetailSlice`, `WeatherMap`, `BlueNoise` | Show one noise texture instead of the clouds |
| `channel` | 4 | 0..4 | Channel of the debug view: 0 R, 1 G, 2 B, 3 A, 4 RGB |
| `slice` | 0.5 | 0..1 | Depth of the shown noise slice (debug views) |
| `tiling` | 2 | 0.25..16 | Repeats of the shown noise over the screen (debug views) |
| `planetRadiusKm` | 6371 | 1000..20000 | Planet radius for the layer curvature (km), standalone only. In mode 1 the Atmosphere's radius is used |
| `layerBaseM` | 1500 | 0..20000 | Layer base altitude above Y = 0 (m) |
| `layerTopM` | 5000 | 10..30000 | Layer top altitude (m). Kept at least 50 m above the base |
| `maxDistanceM` | 250000 | 1000..500000 | Longest ray (m). A camera on the ground sees the layer base 138 km away and the layer top 226 km away along the horizon (6371 km planet); a shorter range cuts the layer off near the horizon and leaves an empty band there (the old 60 km left no cloud below 1.2 degrees) |
| `fadeStart` | 0.9 | 0..0.99 | Fraction of `maxDistanceM` where the density fades to nothing. Late on purpose: in mode 1 the air hazes far cloud, so a fade over the middle distance would only make holes |
| `farWeatherStartM` | 30000 | 1000..500000 | Start of the far field (m); everything below is full at twice this distance |
| `farWeatherMix` | 0.5 | 0..1 | Far field: share of a second weather map sample (the same file at 2.8 x the tile, shifted) mixed in. Breaks the 25 km repeat of the weather and evens out the big blobs. 0 = off |
| `farCoverageLift` | 0.1 | 0..0.5 | Far field: added to `coverage`. Makes the far cloud a closed deck instead of crumbs |
| `farAirScale` | 0.6 | 0.05..1 | Far field, mode 1: share of the distance beyond `farWeatherStartM` that the air in front of a cloud counts. 1 is the physical value; below 1 the horizon deck stays visible instead of dissolving into the haze colour |
| `coverage` | 0.8 | 0..1 | Sky share under cloud. 0.4 few cumulus, 0.8 scattered, 0.9 broken, 0.97 overcast (the weather map of v3 holds less cloud per coverage value than v2) |
| `density` | 1.5 | 0..4 | Density factor after shaping |
| `extinction` | 0.05 | 0.0005..0.5 | Extinction per metre at density 1 |
| `weatherScaleM` | 25000 | 1000..500000 | World size of one weather map tile (m) |
| `shapeScaleM` | 5000 | 100..100000 | World size of one shape noise tile (m) |
| `shapeWorley` | 0.5 | 0..1.5 | Weight of the Worley cells (G, B, A of the shape volume) in the shape. The cells are what makes equal-sized popcorn balls; below 1 the soft Perlin body leads. 1 = the old look |
| `largeShapeMix` | 0.4 | 0..1 | Share of the big lobe octave: the same shape volume read at 1.75 x the tile (7 km), so every cloud has big soft lobes and the mid lobes ride on them. 0 = one scale |
| `sizeVariation` | 0.5 | 0..1 | How much the weather map's cloud type (B) moves that share: big tall clouds get more big lobes, small low clouds keep the mid lobes |
| `windShearM` | 800 | 0..10000 | The noise and weather lookups are shifted by this many metres at the layer top (0 at the base): the tops lean downwind. Static, so the reprojection is not affected |
| `bodyErosion` | 0.35 | 0..1 | Share of the fine erosion kept in the core of a cloud; the rim always gets all of it. Low = smooth soft body with detail only on the rim |
| `verticalStretch` | 0.8 | 0.25..6 | Noise lobes are this many times taller than wide. Above about 2 vertical curtains show |
| `baseVariation` | 0.5 | 0..1 | How much the big octave lowers or lifts the cloud base, so the underside is bumpy and not one plane. 0 = flat base |
| `bodyThresholdMin` | 0 | 0..0.9 | Lowest cut level of the body. A big mass with a high coverage would fill solid and look like a smooth slab; the floor keeps its lobes and holes. 0 = off |
| `bodyWidth` | 0.25 | 0..1 | Width of the body ramp in shape units above the cut level. Small values make crisp lobe surfaces with full density behind them, large values a soft fog. 0 = the whole range up to 1 |
| `baseRagged` | 0 | 0..1 | How much the mid lobes lift or lower the cloud base, so the underside shows billows and not one smooth ceiling. 0 = off |
| `farSoftening` | 0.7 | 0..1 | Extra share of the big octave at the far field start and beyond: distant clouds lose the thin stacked sheets that look like horizontal stripes at the horizon |
| `detailScaleM` | 350 | 10..20000 | World size of one detail noise tile (m) |
| `detailStrength` | 1.0 | 0..2 | Erosion by the detail noise. The erosion is cut into the linear cloud body; the density curve is applied after it |
| `detailFadeDistM` | 25000 | 100..500000 | Detail fades out by this distance (the noise has no mips) |
| `curlAmountM` | 150 | 0..2000 | How far (m) the curl noise moves the detail lookup. Gives swirled lobes and ragged edges; half as much at the top. 0 = off |
| `curlScaleM` | 3000 | 200..100000 | World size of one tile of the curl noise (m) |
| `wispiness` | 0.8 | 0..1 | Share of the wispy (fibre) detail channels in thin parts of a cloud and at its top. 0 = only billows |
| `fineDetail` | 0.15 | 0..1 | Weight of a second fetch of the detail noise at 3.1 x the frequency (small lobes). 0 = off, saves one fetch per sample |
| `windSpeed` | 4 | 0..500 | Wind speed (m/s) |
| `windDirection` | 45 | -360..360 | Direction the clouds move toward, in degrees on the ground plane: 0 = +x, 90 = +z |
| `peakMin` | 0.6 | 0.05..1 | Cloud type: top height of the lowest type (weather map B = 0) as a fraction of the layer thickness; B = 1 reaches the full top. The tops in between follow the map, so a sky holds flat low clouds and tall towers. 1 = no cloud types (every cloud as tall as the layer). Below about 0.4 flat terrace-like tops show |
| `baseRoundness` | 0.15 | 0.01..0.5 | Rounding of the height gradient at the base: how softly the underside fades in (share of the layer thickness) |
| `topRoundness` | 0.2 | 0..0.5 | Rounding of the height gradient at the top |
| `densityRamp` | 0.2 | 0.01..0.5 | Density ramp above the base (fraction of the thickness) |
| `densityTopFade` | 0.9 | 0.5..0.99 | Fraction of the thickness where density starts to fade at the top |
| `topFalloffStart` | 0.5 | 0..0.95 | Where the top rounding of a cloud starts to fall, as a fraction of its peak height. Low values give flat pancakes, high values towering tops |
| `shapeLow` | 0.4 | 0..0.95 | Shape noise value that maps to 0 (the noise is stretched between `shapeLow` and `shapeHigh`) |
| `shapeHigh` | 0.95 | 0.05..1 | Shape noise value that maps to 1 |
| `densityCurve` | 0.8 | 0.2..2 | Exponent on the eroded shape. Below 1 thickens thin cloud and keeps the edge |
| `stepBaseM` | 20 | 1..2000 | Detailed step length at the camera (m) |
| `maxStepDepth` | 0.4 | 0.1..20 | Largest optical depth one dense step may have. A longer step is shortened (not below 20 percent of its length) so the ray integrates through the cloud instead of stopping at a jittered point. 20 = off. Costs about 1 ms |
| `stepGrowth` | 0.01 | 0..0.5 | Smallest step growth per metre of distance (step = `stepBaseM` + `stepGrowth` * t). A ray that would run out of steps grows faster, see `maxSteps`. A value below 0.0001 counts as 0.0001 |
| `coarseMultiplier` | 1 | 1..16 | Coarse step = this x the detailed step |
| `maxSteps` | 96 | 8..512 | Step limit per ray. Each ray also picks its own growth so that its whole segment through the layer fits in 85 percent of this (a ray along the horizon crosses 90 km of layer: with a fixed growth of 1 percent it ran out of steps after 60 km). Rays that need no more than the minimum growth are unchanged |
| `emptyToCoarse` | 3 | 1..16 | Empty samples before the long steps come back |
| `clearSkip` | 1 | 1..32 | Stride multiplier over empty weather. Above 1 can miss small clouds |
| `minTransmittance` | 0.1 | 0..0.5 | Ray stops below this transmittance |
| `jitter` | 1 | 0..1 | Blue noise start jitter, 0..1 of a step |
| `sunIntensity` | 7 | 0..100 | Sun radiance factor on top of the engine sun |
| `ambientIntensity` | 0.28 | 0..100 | Sky light factor |
| `sunSteps` | 4 | 1..12 | Samples of the march toward the sun |
| `lightReach` | 0.7 | 0.05..2 | Length of that march as a fraction of the thickness |
| `msOctaves` | 4 | 1..6 | Multiple scattering octaves |
| `msAttenuation` | 0.55 | 0.05..1 | Per octave factor on the optical depth |
| `msContribution` | 0.65 | 0..1 | Per octave factor on the light |
| `msEccentricity` | 0.5 | 0..1 | Per octave factor on the phase eccentricity |
| `phaseModel` | 0 | 0..1 | 0 dual lobe Henyey-Greenstein, 1 Henyey-Greenstein plus Draine |
| `forwardG` | 0.8 | 0..0.99 | Forward lobe eccentricity |
| `backwardG` | -0.35 | -0.99..0 | Backward lobe eccentricity |
| `backwardMix` | 0.3 | 0..1 | Weight of the backward lobe |
| `silverIntensity` | 0.2 | 0..4 | Weight of the narrow forward lobe that lights thin edges facing the sun |
| `silverG` | 0.97 | 0.5..0.999 | Eccentricity of that lobe |
| `powder` | 0.5 | 0..1 | Dark edge effect when looking away from the sun |
| `inScatter` | 0.8 | 0..1 | Nubis in-scatter chance on the sun light: darkens cloud edges, cavities and the base, brightens the core and upper body; fades out looking into the sun so the silver lining stays. 0 = off |
| `ambientBottom` | 0.42 | 0..1 | Sky light left at the layer base, relative to the top |
| `hazeDistanceM` | 45000 | 1000..1e+07 | Mode 0 only: distance at which a cloud has faded to 1/e into the sky behind it |
| `lightDetail` | 1 | 0..1 | Share of the fine erosion the nearest sun sample uses |
| `lightStride` | 3 | 1..8 | Lit samples between two sun marches and sky occlusions; the light of the samples in between is blended from the two results, so it has no steps along the ray. 1 is the most exact (about 1 ms more than 3), 4 and above are cheaper and less exact |
| `skyOcclusion` | 0.8 | 0..4 | How strongly cloud above a sample blocks the sky light (2 density samples straight up). 0 = height ramp only |
| `skyOcclusionReachM` | 600 | 10..5000 | Height above a sample that the occlusion looks at (m) |
| `skyFloor` | 0.32 | 0..1 | Share of the sky light that always reaches a sample |
| `groundBounce` | 3 | 0..20 | Sunlight bounced up from the ground onto the low part of the cloud (mode 1). 0 = none |
| `groundWarmth` | 0.7 | 0..1 | 0 grey ground light, 1 warm sunlit-ground light |
| `msBlueShift` | 0.7 | 0..1 | Tints the deeper multiple scattering octaves toward blue |
| `ambientSaturation` | 1 | 0..1 | Colour of the sky light on the cloud (mode 1): 1 keeps the sky colour, 0 makes it grey |
| `msDiffuse` | 0.5 | 0..8 | Weight of the diffusion term `tau/(tau+2)/(1+0.15 tau)/(4 pi)` of the multiple scattering: thick cores and undersides keep light where the octave sum has died out |
| `msGlow` | 1.5 | 0..8 | Weight of the inner glow of thick cloud seen toward the sun (Nubis^3 multiple-scatter volume) |
| `sunWarmth` | 0.25 | 0..1 | Tints the sun light on the cloud toward warm |
| `resolutionDivisor` | 0 | 0..6 | March at 1/n of the view size per axis. 0 = automatic: n is the view width over 320 texels, rounded, 1..6 (1280 wide = 4, 1920 wide = 6, 501 wide = 2), so the march costs about the same at every view size. 1..6 = fixed n (others round). A pixel is marched once per n*n frames. Divisor 3 costs about 1.5x of divisor 4 at 720p |
| `temporal` | 1 | 0..1 | 1 accumulates over frames, 0 shows the current march only |
| `historyBlend` | 0 | 0..1 | Weight of new samples in a pixel that was not marched this frame. 0 holds the history, which keeps the picture sharp; any value above 0 blurs a still picture because the new samples come from the low-res taps |
| `freshBlend` | 0.1 | 0.01..1 | Same for the pixel that was marched itself. With `adaptiveBlend` it is the smallest weight a new sample gets (the first samples of a still pixel are averaged 1, 1/2, 1/3 ... down to this floor). Lower is calmer, higher is sharper and noisier |
| `clampExpand` | 0 | 0..4 | Growth of the box the history is clamped to. 0 = no clamp |
| `motionBlend` | 0.25 | 0..1 | Extra weight of new samples where the picture moves a pixel or more. Motion also pulls the per-pixel sample count back to 1 |
| `historySharp` | 1 | 0..1 | 1 resamples the reprojected history with a Catmull-Rom filter (clamped to the 4 nearest texels), 0 uses bilinear. Sharper in motion (about +3 percent edge gradient), costs about 0.6 ms in the resolve pass |
| `adaptiveBlend` | 1 | 0..1 | 1 keeps a per-pixel sample count in the history (aux z) and averages the first samples of a still pixel; 0 = the old fixed `freshBlend` |
| `subpixelJitter` | 1 | 0..2 | Size in pixels of the sub-pixel shift of each march ray (R2 sequence, blue-noise shifted per pixel). The temporal average then covers the pixel area, so the edges are antialiased. 0 = ray through the pixel centre |
| `atmosphereMode` | 1 | 0..1 | 1 lights the clouds from the Atmosphere tables (falls back to 0 when none are published), 0 is the standalone look |
| `airScale` | 1 | 0..50 | Mode 1: factor on the air's scattering and extinction between the eye and a cloud |
| `skyAmbient` | 3.5 | 0..10 | Mode 1: factor on the sky light that reaches the clouds (at the reference sky intensity 6, whatever `skyIntensity` the Atmosphere shows) |
| `shadowStrength` | 1 | 0..1 | Cloud shadows: how much of the sun a receiver under a thick cloud loses. 0 = no shadow map is made (no cost) |
| `shadowRangeKm` | 24 | 4..100 | Width of the shadow map around the camera (128 x 128 texels). Beyond its edge the sun is not shadowed |
| `shadowSoftness` | 1.5 | 0..4 | Edge blur of the cloud shadow in map texels |
| `shadowUpdate` | 3 | 1..4 | Each 8 x 8 texel tile of the map is redone once per this many frames. 1 = every frame (2.2 ms), 4 = about 1 ms |
| `shadowDensity` | 1 | 0.1..8 | Factor on the optical depth of the shadow ray; above 1 darker, harder shadows |
| `shadowSteps` | 10 | 4..24 | Samples of the shadow ray through the layer. Fewer than 10 misses thin cloud |
| `shaftSharpness` | 3 | 1..8 | Exponent on the sun transmittance of the air: higher gives crisper beams and darker gaps |
| `shaftHaze` | 0.5 | 0..4 | Extinction per km at the ground of the beam haze: a thin blue-violet haze in the aerial perspective volume (it reaches at least 24 km) that the cloud shadow map lights, so lit and shadowed columns of the air show as light curtains over the ground. Thickest for a low sun, gone above about 45 degrees. 0 = no beam haze |
| `shaftFill` | 0.6 | 0..4 | Sky light in the shadowed columns of the beam haze, relative to the multiple-scattering table (0 = black columns) |
| `shaftMist` | 0 | 0..4 | Extinction per km of an extra near-field mist in the volumetric light (stretches that volume to 4 km). 0 = none |
| `shaftHeightM` | 2500 | 200..6000 | Scale height of the beam haze and the mist |
| `shadowFill` | 0.25 | 0..1 | Share of the sun the cloud blocks that reaches shadowed receivers as diffuse light, so a shadowed ground is not black |
| `sunRays` | 1 | 0..3 | Faint screen-space sun glare: the glow around the sun seen through the gaps of the clouds (24 taps in the resolve pass, the smoothed cloud history is the occluder, the jitter changes every frame). Gone when the sun is behind the camera or well outside the picture, and for a high sun. 0 = off |
| `shaftStrength` | 1 | 0..1 | Light shafts: how much of the sun in-scatter of the air the cloud shadow map removes (aerial perspective, volumetric light, air in front of clouds). 0 = none |

Resolve detail: a pixel that is not marched this frame reads its history texel exactly (positions within 0.004 texel of a texel centre are snapped). Without the snap the 1e-4 texel matrix noise made the rgba16f store round the history down one half-float step on every unmarched frame, which darkened the converged picture the more the lower `freshBlend` was.

## Cloud shadows
- The cloud pass also writes a map of the sun light the layer lets through (`VolumetricCloud.Shadow.Dispatch`, a PreOpaque pass; the same compute shader as the march, in a map mode). The map is a 128 x 128 RGBA16F texture (r = sun transmittance, g = optical depth) over the layer plane, centred on the camera and snapped to its texel lattice, so it does not shimmer when the camera moves.
- Each texel marches the sun ray through the whole layer with the smooth density (no fine erosion), 10 jittered steps, and blends with the history of the last map (shifted by the wind and the grid move). The map is shown to the materials as scene slot 4 and described by the `VolumetricCloud.ShadowMap` channel (`Game/Lighting/CloudShadowMap.h`); the packed numbers sit in the scene uniform block (`cloudShadowGrid`, `cloudShadowLayer`).
- A lit material reads it in `SunShadowAmount()` (`DirectionalLighting.glsl`, function `CloudSunTransmittance()` in `CloudShadowLookup.glsl`): the receiver's sun ray is followed to the middle of the layer, the map is read there with a cubic B-spline, and the result multiplies the sun term together with the cascade shadow. Ambient light stays. A receiver inside the layer is shaded only by the part above it; above the layer there is no shadow.
- No map (clouds off, `shadowStrength` 0, sun below 2.4 degrees, pass toggled off) = the slot is the white dummy = no change to any picture. The shadow fades in between sun heights 0.04 and 0.10 (sine of the elevation).
- Cost on the Iris Xe at 1280x720: about 0.8 ms for the dispatch at the defaults (`shadowUpdate` 3).

## Cloud coverage knobs
| Want | Set |
|---|---|
| Fewer or more clouds | `coverage` (0.4 sparse, 0.8 default, 0.97 overcast), `peakMin` (cloud type mix: 0.6 default, 1 = all clouds tall) |
| Thicker, darker clouds | `density`, `extinction` |
| Bigger cloud fields or smaller | `weatherScaleM` |
| Puffier or smoother edges | `detailStrength`, `detailScaleM` |
| Lower or higher sky | `layerBaseM`, `layerTopM` (a mountain scene wants 250..1600) |
| Brighter or darker | `sunIntensity`, `ambientIntensity` |
| Deeper shade under and between clouds | `skyOcclusion`, `skyFloor`, `skyAmbient` |
| Warmer cloud bases | `groundBounce`, `groundWarmth` |

### Deep blue sky look
The sky background belongs to the Atmosphere and the exposure to ToneMapping. The default sky is pale next to a game-engine reference. This recipe gives a deeper blue sky with bright clouds and does not change the cloud shading, because the clouds' sky light is tied to the reference sky intensity 6, not to the live `skyIntensity`:
```
/feature_debug/set?name=Atmosphere&skyIntensity=3
/feature_debug/set?name=ToneMapping&exposure=0.5&saturation=1.4
```
The sun disk is on by default (`sunDiskIntensity` 8, `sunDiskRadiusDeg` 0.5 on the Atmosphere); the Bloom feature turns it into a glow. Set `sunDiskIntensity=0` for no disk.

### Bloom halo
Sunlit cloud is far brighter than white, so the default Bloom (`threshold` 1) spreads a pale glow onto the sky around every small bright cloud: they look like cut-outs with a halo. For a cloud scene use `/feature_debug/set?name=Bloom&threshold=2`: the halo goes, the cloud shading and the glow around the sun stay. A value of 4 removes the halo and most of the sun glow. Bloom also hides some grain: with Bloom off the grain metric rises from 0.66 to 1.03.

### Tone mapping and sunsets
ToneMapping with `saturation` 1.3 and the `Aces` mode clips red and green together on a low sun and turns the clouds neon yellow. The engine default 1.1 gives natural gold; `mode=PbrNeutral` gives a pink-orange sunset.

### Presets
Presets (action, no arguments, keep everything else and keep `enabled`): `preset_clear` (coverage 0.4, density 1.3), `preset_scattered` (the defaults), `preset_broken` (coverage 0.9), `preset_overcast` (coverage 0.97, density 1.5). The panel has one button per preset.

## Debug keys
Name for `/feature_debug`: `VolumetricCloud`. Actions: `reset_defaults`, `preset_clear`, `preset_scattered`, `preset_broken`, `preset_overcast`. Every setting above is writable. Read-only keys:

| Key | Meaning |
|---|---|
| `game.status`, `scene.status` | `NotRendered`, `Ok`, `Disabled`, `AssetMissing`, `ShadersNotReady`, `TargetFailed`, `PassDisabled`, `NoValidCamera` |
| `game.historyUsed` | The resolve pass blended the history of the last frame |
| `game.divisor` | March divisor of the last frame |
| `game.skyLight`, `scene.skyLight` | `Standalone` (mode 0), `Atmosphere` (mode 1 in use) or `Unavailable` (mode 1 asked for, no tables, standalone look used) |
| `game.assets.shape/detail/weather/blueNoise/curl` | Loaded size, or `not loaded` |

Texture for `/get_texture`: `GameViewVolumetricCloud` (also `SceneViewVolumetricCloud`).

## Quick check
```cmd
curl.exe "http://127.0.0.1:8080/feature_debug/set?name=VolumetricCloud&enabled=true"
curl.exe "http://127.0.0.1:8080/feature_debug/invoke?name=VolumetricCloud&action=preset_overcast"
curl.exe "http://127.0.0.1:8080/feature_debug/state?name=VolumetricCloud"
curl.exe -o game.png "http://127.0.0.1:8080/get_game_view"
curl.exe "http://127.0.0.1:8080/render_graph/set_feature_enabled?name=VolumetricCloud&enabled=true&view=secondary"
```
Status `Ok` and `game.historyUsed=true` mean the temporal path runs. Add a `DirectionalLight` (`POST /instantiate_light`) to steer the sun. The origin area of the weather map is a clear patch: test with the camera around (2000, 2, -5000).

## Performance
Measured on an Intel Iris Xe iGPU (shared memory, power limited), clouds on minus clouds off, `gpu_span`, medians of 3 interleaved rounds. Noise between runs is +-10..25 percent. A discrete GPU was not tested.

Cloud passes = March + Resolve + Copy rows of the GPU profiler, defaults, medians of 3 interleaved rounds in one session (scene S2 = camera 2 m up, looking 8 degrees up; S8 = a big cloud mass overhead; S10 = low sun in front):

| Resolution | Divisor | S2 | S8 | S10 |
|---|---|---|---|---|
| 1280x720 | 4 (automatic) | 10.5 ms | 15.6 ms | 14.2 ms |
| 1280x720 | 3 | 15.9 ms | 23.1 ms | 22.0 ms |
| 1280x720 | 2 | 26.4 ms | 40.7 ms | 37.2 ms |
| 1920x1080 | 6 (automatic) | 13.7..14.1 ms | 18.9..19.2 ms | not measured |
| 1920x1080 | 4 | 22.6 ms | 32.0 ms | 28.7 ms |
| 1920x1080 | 3 | 32.6 ms | 47.5 ms | 42.0 ms |

At 1920x1080 the resolve pass costs 4.6 ms and the copy 0.9 ms; the rest is the march. A whole editor frame (S2, 1280x720) is 15.7 ms without clouds and 28.1 ms with; at 1920x1080 it is 29.6 ms and 45.3 ms. The editor scene without clouds already costs that much (volumetric light, temporal anti-aliasing, aerial perspective). The resolve pass is bandwidth bound. The march pass is latency bound.
Cheap levers at 1920x1080 and divisor 4 (S2 / S8): `maxSteps` 64 saves 3.6 / 5.3 ms, 48 saves 5.9 / 7.9 ms (but the far horizon gets thinner); `lightDetail` 0, `fineDetail` 0, `sunSteps` 3, `lightStride` 4 and `msOctaves` 3 save 0..2 ms each, inside the noise of the GPU clock.
Memory per view: 4 full-size RGBA16F history targets (about 12 MB at 884x432), small march targets, and about 9.6 MB of noise shared by all views.

## Limits
- Cloud shadows reach the lit materials, the aerial perspective (light shafts over the haze and in the sky), the volumetric light and the air in front of clouds. The map is centred on the point where the camera's sun ray crosses the layer and covers `shadowRangeKm` around it; air or ground farther from the camera than about half of that range is not shadowed. No cloud shadow on other clouds from outside the layer.
- The noise has no mips. Detail fades out with distance instead.
- Divisor 2 (crisper edges) costs 26..41 ms on the Iris Xe at 720p; the automatic divisor keeps the march grid at about 320 texels wide instead.
- A coarser divisor is a little softer (mean error against a full-res reference 0.8 at divisor 4 against 0.5 at divisor 2, out of 255, still camera). A still picture at divisor 6 converges to nearly the same picture as divisor 4 but needs 36 frames per sample of a pixel (about 10 samples = 12 s at 30 fps). Moving clouds are soft: wind of 40 m/s keeps 86 percent of the still sharpness, 200 m/s about 75 percent. A newly exposed screen edge and the first frames after a resize or a camera cut show the raw block pattern for up to n*n frames.
- TemporalAa (-150) sees the clouds as normal pixels. Its jitter is not fed into the march pattern. TemporalAa, Gtao and Ssgi use the same eye-relative camera, so they stay on far from the origin.
- Mode 1 reads the sky of the camera's height only: one sky-view lookup set for the whole picture and the camera's position for the air. Clouds that are far away and high see the same sky light as the ones overhead.
- Clouds do not shadow the aerial perspective shafts or the sun glow of the sky. They only dim what is behind them. A sun glow behind a cloud is dimmed by the cloud transmittance, like any other pixel.
- Mode 1 follows the planet's shadow: at a sun angle just below the horizon only the high far clouds stay lit and the rest go dark, as the sky does. Mode 0 keeps lighting them with a fixed twilight tint.
- The layer is a sphere shell around a planet centred below the camera. Horizontal camera position does not bend it.
- Perspective cameras only. A view with no valid eye-relative camera (`View::relative`) gives `NoValidCamera`.
- The Game camera far plane is 1000 m. The scene depth limit works inside that range; beyond it the sky shows through.
- A noise file is loaded once per process. A file larger than the device's 2D or 3D image limit is refused with one log line.
- Transparent objects and particles are not in the scene depth, and the cloud pass runs after the transparent stage. Where a transparent object is seen in front of the sky, cloud in that pixel is added on top of it: the object looks dimmed by the cloud, as if it were behind it. Opaque objects are never affected. Workaround: keep the clouds off the view that shows such objects close to the camera, or lower `coverage`. The proper fix is to run the cloud pass before the transparent stage, or to give it a depth input that includes the transparent objects.
- A march that stops at `maxSteps` before the end of its segment still reports that it looked all the way. This can only drop far cloud, never add cloud in front of a surface. It is left as is on purpose: reporting the short reach makes the resolve pass reject the pixel's own march and punches holes in the far clouds.
- The wind offset is kept in double precision per view and wraps at the common repeat length of the three noise tiles (350000 m with the default scales), so long sessions keep sub-metre precision. When the tile sizes have no small common length the offset does not wrap.

## Related
- [Render features](README.md) · [Atmosphere](atmosphere.md) · [Temporal anti-aliasing](temporal-aa.md) · [Volumetric light](volumetric-light.md)
- [Render feature kit](../feature-kit.md) · [Building](../../../BUILDING.md) · `tools/cloud_noise_bake/README.md`
- [HTTP cheat sheet](../../debugging/http-cheat-sheet.md) · [HTTP debug routes](../../debugging/http-routes-debug.md)
